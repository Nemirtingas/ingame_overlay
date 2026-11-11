#!/usr/bin/env python3
# Copyright (C) Nemirtingas
# This file is part of the ingame overlay project
#
# The ingame overlay project is free software; you can redistribute it
# and/or modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# The ingame overlay project is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with the ingame overlay project; if not, see
# <http://www.gnu.org/licenses/>.

"""Runs macos_metal_app in each test scenario (see the METAL_TEST_* variables in tests/macos_metal/main.mm)
with overlay_example, and checks the Metal hook through the overlay's MCP server.

Needs the MCP server (INGAMEOVERLAY_BUILD_MCP, from the MCP server feature), a build with
INGAMEOVERLAY_BUILD_TESTS=ON and a logged in console session.
Python 3 standard library only.

    python3 tests/macos_metal/check_scenarios.py <build dir> [--only name,...] [--validation] [--soak 20]

Checks of each scenario:
    ready       the hook gets Ready and runs frames;
    drawn       the overlay is in the presented image: the screenshots taken before and after the overlay
                differ inside the overlay's window;
    size        the screenshot has the drawable's size (scenarios with a fixed drawable size);
    soak        the application still renders after --soak seconds (render thread and event scenarios);
    leak        the process footprint grows by less than 8 MB in --soak seconds with the overlay visible.

--validation runs every scenario with the Metal API validation layer (MTL_DEBUG_LAYER=1), which aborts the
process on any misuse of the API, by the application or by the overlay.

The application can't be closed by a script (it needs accessibility rights): it is killed after each scenario.
"""

import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import time
import zlib

APP = 'macos_metal_app'

# name: (environment, expected drawable size or None, extra checks)
SCENARIOS = {
    'default':               ({}, None, []),
    'present_at_time':       ({'METAL_TEST_PRESENT': 'drawable_at_time'}, None, []),
    'present_min_duration':  ({'METAL_TEST_PRESENT': 'drawable_min_duration'}, None, []),
    'scheduled_present':     ({'METAL_TEST_PRESENT': 'scheduled'}, None, []),
    'scheduled_present_at':  ({'METAL_TEST_PRESENT': 'scheduled_at_time'}, None, []),
    'after_scheduled':       ({'METAL_TEST_PRESENT': 'after_scheduled'}, None, []),
    'offscreen_first':       ({'METAL_TEST_OFFSCREEN_FIRST': '1'}, None, []),
    'drawable_size':         ({'METAL_TEST_DRAWABLE_SIZE': '600x400'}, (600, 400), []),
    'msaa':                  ({'METAL_TEST_SAMPLE_COUNT': '4'}, None, []),
    'copyable_drawable':     ({'METAL_TEST_FRAMEBUFFER_ONLY': '0'}, None, []),
    'unretained':            ({'METAL_TEST_UNRETAINED': '1'}, None, []),
    'render_thread':         ({'METAL_TEST_RENDER_THREAD': '1'}, None, ['soak']),
    'render_thread_events':  ({'METAL_TEST_RENDER_THREAD': '1', 'METAL_TEST_EVENT_STORM': '1'}, None, ['soak']),
    'scheduled_thread':      ({'METAL_TEST_RENDER_THREAD': '1', 'METAL_TEST_PRESENT': 'scheduled'}, None, ['soak']),
    'leak':                  ({}, None, ['leak']),
}


class Bridge_t:
    def __init__(self, bridge_path, name, pid):
        self.process = subprocess.Popen([bridge_path, '--wait', '30', '--name', name, '--pid', str(pid)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8')
        self.next_id = 1
        self.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'check_scenarios', 'version': '1'}})

    def request(self, method, params):
        self.process.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': self.next_id, 'method': method, 'params': params}) + '\n')
        self.process.stdin.flush()
        self.next_id += 1
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('the MCP bridge closed (did the application crash?)')
        return json.loads(line)

    def tool(self, name, arguments=None):
        result = self.request('tools/call', {'name': name, 'arguments': arguments or {}})['result']
        texts = [c['text'] for c in result['content'] if c['type'] == 'text']
        images = [c['data'] for c in result['content'] if c['type'] == 'image']
        data = None
        if texts:
            try:
                data = json.loads(texts[0])
            except ValueError:
                data = texts[0]
        return result.get('isError', False), data, images

    def close(self):
        try:
            self.process.stdin.close()
            self.process.wait(10)
        except Exception:
            self.process.kill()


def decode_png(data):
    """Returns (width, height, channels, pixels) of an 8-bit RGB or RGBA PNG."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    offset, idat, header = 8, b'', None
    while offset < len(data):
        length, kind = struct.unpack('>I4s', data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            idat += chunk
        offset += 12 + length
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace != 0:
        raise ValueError('unsupported PNG')
    channels = 4 if color == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 0xff
            elif kind == 2:
                line[x] = (line[x] + up) & 0xff
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 0xff
            elif kind == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else upper_left)
                line[x] = (line[x] + predictor) & 0xff
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def changed_fraction(before, after, rect):
    """Fraction of the pixels of rect (x0, y0, x1, y1 in pixels) that differ noticeably between two images."""
    width, height, channels, a = before
    _, _, _, b = after
    x0, y0, x1, y1 = (max(0, int(rect[0])), max(0, int(rect[1])), min(width, int(rect[2])), min(height, int(rect[3])))
    total = changed = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = (y * width + x) * channels
            total += 1
            if abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2]) > 24:
                changed += 1
    return changed / total if total else 0.0


def footprint_mb(pid):
    output = subprocess.run(['footprint', '-p', str(pid)], capture_output=True, text=True).stdout
    for line in output.splitlines():
        if 'phys_footprint:' in line:
            value, unit = line.split(':', 1)[1].split()[:2]
            return float(value) * {'KB': 1 / 1024, 'MB': 1, 'GB': 1024}.get(unit, 1)
    return None


def run_scenario(build_dir, name, environment, expected_size, extra, validation, soak, out_dir):
    checks = []

    def check(label, ok, detail=''):
        checks.append((label, bool(ok), detail))
        return ok

    env = dict(os.environ, INGAMEOVERLAY_MCP='1', INGAMEOVERLAY_MCP_NAME=name.replace('_', '-')[:32], **environment)
    if validation:
        env['MTL_DEBUG_LAYER'] = '1'
    log = open(os.path.join(out_dir, name + '.log'), 'w')
    process = subprocess.Popen([os.path.join(build_dir, APP)], cwd=build_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
    bridge = None
    try:
        bridge = Bridge_t(os.path.join(build_dir, 'ingame_overlay_mcp_bridge'), env['INGAMEOVERLAY_MCP_NAME'], process.pid)

        start = time.time()
        status = {}
        while time.time() - start < 20:
            error, status, _ = bridge.tool('overlay_status')
            if not error and status.get('hook_state') == 'Ready' and status.get('frame_running'):
                break
            time.sleep(0.5)
        if not check('ready', status.get('hook_state') == 'Ready' and status.get('frame_running'),
                     'after %.1fs' % (time.time() - start) if status.get('hook_state') == 'Ready' else 'hook state %s' % status.get('hook_state')):
            return checks

        bridge.tool('overlay_set_visible', {'visible': True})
        bridge.tool('overlay_wait_frames', {'count': 10})
        error, status, _ = bridge.tool('overlay_status')

        shots = []
        for include_overlay in (False, True):
            error, info, images = bridge.tool('overlay_screenshot', {'include_overlay': include_overlay, 'max_dimension': 512})
            if error or not images:
                check('screenshot', False, str(info)[:120])
                return checks
            png = base64.b64decode(images[0])
            open(os.path.join(out_dir, '%s_%s.png' % (name, 'after' if include_overlay else 'before')), 'wb').write(png)
            shots.append((info, decode_png(png)))

        # The overlay's window, from ImGui's coordinates to the screenshot's pixels.
        error, tree, _ = bridge.tool('imgui_tree', {'window': 'Overlay'})
        window = next((w for w in (tree or {}).get('windows', []) if w.get('name') == 'Overlay'), None) if not error else None
        info, after = shots[1]
        if window is None or not status.get('display_width'):
            check('drawn', False, 'no Overlay window')
        else:
            sx = after[0] / status['display_width']
            sy = after[1] / status['display_height']
            x, y, w, h = window['rect']
            fraction = changed_fraction(shots[0][1], after, (x * sx, y * sy, (x + w) * sx, (y + h) * sy))
            check('drawn', fraction > 0.25, '%.0f%% of the overlay window changed' % (fraction * 100))

        if expected_size is not None:
            check('size', (info.get('source_width'), info.get('source_height')) == expected_size,
                  '%sx%s, expected %dx%d' % (info.get('source_width'), info.get('source_height'), expected_size[0], expected_size[1]))

        if 'soak' in extra or 'leak' in extra:
            error, before, _ = bridge.tool('overlay_status')
            memory_before = footprint_mb(process.pid)
            time.sleep(soak)
            error, later, _ = bridge.tool('overlay_status')
            frames = (later or {}).get('frame_count', 0) - (before or {}).get('frame_count', 0)
            if 'soak' in extra:
                check('soak', not error and process.poll() is None and frames > soak * 10, '%d frames in %ds' % (frames, soak))
            if 'leak' in extra:
                memory_after = footprint_mb(process.pid)
                if memory_before is None or memory_after is None:
                    check('leak', False, 'footprint unavailable')
                else:
                    check('leak', memory_after - memory_before < 8, '%.0f MB -> %.0f MB in %ds' % (memory_before, memory_after, soak))
    except Exception as e:
        check('exception', False, repr(e)[:160])
    finally:
        if bridge is not None:
            bridge.close()
        crashed = process.poll()
        if crashed is not None:
            check('alive', False, 'exited with %s, see %s.log' % (crashed, name))
        process.kill()
        process.wait(10)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build_dir')
    parser.add_argument('--only', help='comma separated scenario names: ' + ', '.join(SCENARIOS))
    parser.add_argument('--validation', action='store_true', help='enable the Metal API validation layer')
    parser.add_argument('--soak', type=int, default=20, help='seconds of the soak and leak checks')
    arguments = parser.parse_args()

    build_dir = os.path.abspath(arguments.build_dir)
    names = arguments.only.split(',') if arguments.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        parser.error('unknown scenario: ' + ', '.join(unknown))

    out_dir = os.path.join(build_dir, 'check_scenarios')
    os.makedirs(out_dir, exist_ok=True)

    failures = 0
    for name in names:
        environment, expected_size, extra = SCENARIOS[name]
        checks = run_scenario(build_dir, name, environment, expected_size, extra, arguments.validation, arguments.soak, out_dir)
        bad = [c for c in checks if not c[1]]
        failures += 1 if bad else 0
        print('%-22s %s' % (name, 'OK' if not bad else 'FAILED'))
        for label, ok, detail in checks:
            print('    %-10s %-5s %s' % (label, 'ok' if ok else 'FAIL', detail))
        sys.stdout.flush()

    print('%d/%d scenarios passed, screenshots and logs in %s' % (len(names) - failures, len(names), out_dir))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
