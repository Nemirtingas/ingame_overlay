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

"""Checks the Windows window and OpenGL hooks with windows_hook_tests_app and overlay_example.

The script sends real keyboard and mouse input (SendInput) to the application, drives the overlay through its
MCP server and reads what the application itself saw from its log (see tests/windows_hook_tests/main.cpp).
It needs the MCP server (INGAMEOVERLAY_BUILD_MCP, from the MCP server feature). The interactive scenarios need
the desktop to themselves: the application takes the foreground, the keyboard and the cursor, so don't use the
computer while they run (another window taking the foreground makes them inconclusive). The other scenarios run
in the background. Python 3 standard library only.

    python tests/windows_hook_tests/check_scenarios.py <directory of the executables> [--only name,...] [--skip-interactive]

Scenarios:
    toggle_keys             Shift+F2 opens then closes the overlay: F2's message never reaches the application, and
                            nothing of the key combination does when closing (messages, raw input, GetAsyncKeyState,
                            GetKeyState, GetKeyboardState). Raw input of the opening press is read before the key
                            message that opens the overlay: it can't be hidden.
    toggle_keys_raw_buffer  Same with an application reading raw input with GetRawInputBuffer.
    keyboard_state_hidden   While the application's inputs are hidden, GetKeyboardState fills the buffer.
    mouse_look_key          A mouse look application doesn't turn while the overlay is open (opened by the keys).
    mouse_look_api          Same with the inputs hidden through the API (HideAppInputs).
    clip_key                The application's cursor clipping is restored when the overlay closes (keys).
    clip_api                Same through the API.
    gl_pixel_state          OpenGL application leaving pixel buffers bound and a hostile pixel store state: the
                            overlay's font and images are right, the screenshot has the window's size.
    gl_error                OpenGL application leaving a GL error: the overlay's images still load.
    mouse_hidden            While the overlay is open, the application gets no mouse message, in its client area or
                            outside it (title bar), and the overlay still follows the cursor.
    pointer_mouse_hidden    Same with an application that called EnableMouseInPointer(TRUE): no WM_POINTER* message
                            either, and the overlay still follows the cursor.
"""

import argparse
import base64
import ctypes
import ctypes.wintypes as wintypes
import json
import os
import struct
import subprocess
import sys
import time
import zlib

APP = 'windows_hook_tests_app.exe'
OVERLAY = 'overlay_example.dll'
VK_SHIFT, VK_F2, VK_MENU = 0x10, 0x71, 0x12
WATCHED = {VK_SHIFT, VK_F2}

user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32')
kernel32.GetTickCount64.restype = ctypes.c_ulonglong


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('mouseData', wintypes.DWORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _Union(ctypes.Union):
        _fields_ = [('mi', MOUSEINPUT), ('ki', KEYBDINPUT)]
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _Union)]


def key_input(vk, up):
    i = INPUT(type=1)
    i.ki = KEYBDINPUT(vk, user32.MapVirtualKeyW(vk, 0), 2 if up else 0, 0, 0)
    return i


def mouse_move_input(dx, dy):
    i = INPUT(type=0)
    i.mi = MOUSEINPUT(dx, dy, 0, 0x0001, 0, 0)
    return i


def mouse_absolute_input(x, y):
    """A move to the screen pixel (x, y): unlike SetCursorPos, it makes WM_POINTERUPDATE messages."""
    left, top = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    width, height = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    i = INPUT(type=0)
    # MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_ABSOLUTE
    i.mi = MOUSEINPUT((x - left) * 65535 // (width - 1), (y - top) * 65535 // (height - 1), 0, 0x0001 | 0x4000 | 0x8000, 0, 0)
    return i


def mouse_button_input(up):
    i = INPUT(type=0)
    i.mi = MOUSEINPUT(0, 0, 0, 0x0004 if up else 0x0002, 0, 0)  # MOUSEEVENTF_LEFTUP / MOUSEEVENTF_LEFTDOWN
    return i


def send(*inputs):
    array = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))


def tick():
    return kernel32.GetTickCount64()


def press_toggle(hold=0.6, mouse_offset=None):
    """Shift+F2, F2 held long enough for the application to poll it. A mouse move can be sent in the same batch,
    so that the cursor has moved when the key combination is handled."""
    first = [mouse_move_input(*mouse_offset)] if mouse_offset else []
    if CURRENT_APP is not None:
        CURRENT_APP.check_foreground()
    send(*first, key_input(VK_SHIFT, False), key_input(VK_F2, False))
    time.sleep(hold)
    send(key_input(VK_F2, True), key_input(VK_SHIFT, True))
    time.sleep(0.4)
    if CURRENT_APP is not None:
        CURRENT_APP.check_foreground()


# The application of the running scenario, checked for the foreground around each key press.
CURRENT_APP = None


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


class Inconclusive(Exception):
    pass


class App_t:
    def __init__(self, directory, name, options, out_dir, interactive):
        self.log_path = os.path.join(out_dir, name + '.log')
        if os.path.exists(self.log_path):
            os.remove(self.log_path)
        mcp_name = name.replace('_', '-')[:32]
        env = dict(os.environ, INGAMEOVERLAY_MCP='1', INGAMEOVERLAY_MCP_NAME=mcp_name, INGAMEOVERLAY_TEST_LOG=self.log_path)
        env.update(options)
        if not interactive:
            env['INGAMEOVERLAY_TEST_BACKGROUND'] = '1'
        self.interactive = interactive
        self.process = subprocess.Popen([os.path.join(directory, APP), OVERLAY], cwd=directory, env=env,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.bridge = None
        try:
            self._start(directory, mcp_name, interactive)
        except BaseException:
            # The scenario never gets the application: don't leave it running.
            self.close()
            raise

    def _start(self, directory, mcp_name, interactive):
        self.bridge = Bridge_t(os.path.join(directory, 'ingame_overlay_mcp_bridge.exe'), mcp_name, self.process.pid)
        start = time.time()
        while time.time() - start < 30:
            error, status, _ = self.bridge.tool('overlay_status')
            if not error and status.get('hook_state') == 'Ready' and status.get('frame_running'):
                break
            time.sleep(0.5)
        else:
            raise RuntimeError('the hook never got ready')
        if interactive:
            self.foreground()

    def window(self):
        found = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def enum(hwnd, _):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == self.process.pid and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
                return False
            return True
        user32.EnumWindows(enum, 0)
        return found[0] if found else None

    def foreground(self):
        hwnd = self.window()
        # The process that sent the last input may set the foreground window.
        send(key_input(VK_MENU, False), key_input(VK_MENU, True))
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.5)
        self.check_foreground()

    def check_foreground(self):
        """The input goes to the foreground window: the scenario means nothing once another window has it."""
        if user32.GetForegroundWindow() != self.window():
            raise Inconclusive('another window has the foreground (is the computer in use?)')

    def visible(self):
        return self.bridge.tool('overlay_status')[1].get('overlay_visible')

    def events(self, since=0):
        """Log lines as (tick, [fields]) at or after since."""
        result = []
        for line in open(self.log_path, encoding='utf-8', errors='replace'):
            fields = line.split()
            if len(fields) >= 2 and int(fields[0]) >= since:
                result.append((int(fields[0]), fields[1:]))
        return result

    def close(self):
        if self.bridge is not None:
            self.bridge.close()
        self.process.terminate()
        try:
            self.process.wait(10)
        except subprocess.TimeoutExpired:
            self.process.kill()


def decode_png(data):
    """Returns (width, height, channels, pixels) of an 8-bit RGB or RGBA PNG."""
    offset, idat, header = 8, b'', None
    while offset < len(data):
        length, kind = struct.unpack('>I4s', data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            idat += chunk
        offset += 12 + length
    width, height, depth, color = header[:4]
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
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else (up if pb <= pc else upper_left))) & 0xff
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def crop(image, rect):
    width, height, channels, pixels = image
    x0, y0, x1, y1 = (max(0, int(rect[0])), max(0, int(rect[1])), min(width, int(rect[2])), min(height, int(rect[3])))
    return [bytes(pixels[(y * width + x0) * channels:(y * width + x1) * channels]) for y in range(y0, y1)], channels


def changed_fraction(a, b):
    (rows_a, channels), (rows_b, _) = a, b
    total = changed = 0
    for row_a, row_b in zip(rows_a, rows_b):
        for i in range(0, min(len(row_a), len(row_b)), channels):
            total += 1
            if abs(row_a[i] - row_b[i]) + abs(row_a[i + 1] - row_b[i + 1]) + abs(row_a[i + 2] - row_b[i + 2]) > 24:
                changed += 1
    return changed / total if total else 1.0


# Scenarios: each returns a list of (label, ok, detail).

def watched_events(events):
    """Events where the application saw one of the key combination's keys."""
    result = []
    for t, fields in events:
        kind = fields[0]
        if kind in ('msg', 'raw') and int(fields[2]) in WATCHED:
            result.append(' '.join(fields))
        elif kind in ('async', 'state') and int(fields[1]) in WATCHED:
            result.append(' '.join(fields))
        elif kind == 'kbstate' and any(int(k) in WATCHED for k in fields[3:]):
            result.append(' '.join(fields))
    return result


def scenario_toggle_keys(app):
    checks = []
    opened = tick()
    press_toggle()
    checks.append(('open', app.visible() is True, ''))
    leaked = [e for e in watched_events(app.events(opened)) if e.startswith('msg down 113')]
    checks.append(('open_hidden', not leaked, '; '.join(leaked[:4])))
    time.sleep(0.5)
    closed = tick()
    press_toggle()
    time.sleep(0.5)
    checks.append(('close', app.visible() is False, ''))
    leaked = watched_events(app.events(closed))
    checks.append(('close_hidden', not leaked, '; '.join(leaked[:6]) or 'nothing reached the application'))
    return checks


def scenario_keyboard_state_hidden(app):
    app.bridge.tool('overlay_set_visible', {'visible': True})
    time.sleep(0.5)
    states = [' '.join(f[1:]) for t, f in app.events() if f[0] == 'kbstate']
    app.bridge.tool('overlay_set_visible', {'visible': False})
    last = states[-1] if states else 'none'
    return [('kbstate', last.startswith('TRUE written'), 'GetKeyboardState while hidden: ' + last)]


def count_looks(app, since, until):
    return sum(1 for t, f in app.events(since) if t <= until and f[0] == 'look')


def scenario_mouse_look_key(app):
    time.sleep(0.5)
    opened = tick()
    press_toggle(hold=0.2, mouse_offset=(60, 40))
    time.sleep(1.5)
    closed = tick()
    turns = count_looks(app, opened + 300, closed)
    app.check_foreground()
    press_toggle(hold=0.2)
    return [('open', True, ''), ('no_turning', turns <= 2, '%d frames read a cursor offset while the overlay was open' % turns)]


def scenario_mouse_look_api(app):
    time.sleep(0.5)
    hidden = tick()
    app.bridge.tool('overlay_input_policy', {'hide_from_app': True})
    time.sleep(1.5)
    shown = tick()
    app.bridge.tool('overlay_input_policy', {'hide_from_app': False})
    turns = count_looks(app, hidden + 300, shown)
    app.check_foreground()
    return [('no_turning', turns <= 2, '%d frames read a cursor offset while the inputs were hidden' % turns)]


def clip_scenario(app, hide, show):
    events = app.events()
    clips = [f[1:] for t, f in events if f[0] == 'appclip']
    app_clip = clips[-1] if clips else None
    start = tick()
    before = [f[1:] for t, f in events if f[0] == 'clip']
    if not before or before[-1] != app_clip:
        return [('clip', True, 'skipped: the clipping was already reset before the test (%s)' % (before[-1] if before else 'none'))]
    hide()
    time.sleep(0.8)
    show()
    time.sleep(0.8)
    app.check_foreground()
    after = [f[1:] for t, f in app.events(start) if f[0] == 'clip']
    last = after[-1] if after else before[-1]
    return [('clip_restored', last == app_clip, 'after closing: %s, application: %s' % (' '.join(last), ' '.join(app_clip)))]


def scenario_clip_key(app):
    return clip_scenario(app, press_toggle, press_toggle)


def scenario_clip_api(app):
    return clip_scenario(app, lambda: app.bridge.tool('overlay_input_policy', {'hide_from_app': True}),
                         lambda: app.bridge.tool('overlay_input_policy', {'hide_from_app': False}))


def images_loaded(app, seconds=3.0):
    """The overlay swaps its images every second: each must be loaded in at least one sample."""
    seen = [False, False]
    end = time.time() + seconds
    while time.time() < end:
        error, tree, _ = app.bridge.tool('imgui_tree', {'window': 'Overlay'})
        for window in (tree or {}).get('windows', []) if not error else []:
            for item in window.get('items', []):
                text = item.get('text', '')
                if text.startswith('Images loaded:'):
                    values = text.split(':')[1].split()
                    seen = [seen[i] or values[i] == '1' for i in range(2)]
        time.sleep(0.2)
    return seen


def overlay_title_bar(app):
    """The Overlay window's title bar in a screenshot after the overlay, and the screenshot's size."""
    error, status, _ = app.bridge.tool('overlay_status')
    error, tree, _ = app.bridge.tool('imgui_tree', {'window': 'Overlay'})
    window = next((w for w in tree.get('windows', []) if w.get('name') == 'Overlay'), None)
    error, info, images = app.bridge.tool('overlay_screenshot', {'include_overlay': True, 'max_dimension': 4096})
    if error or not images or window is None:
        return None, info
    image = decode_png(base64.b64decode(images[0]))
    sx, sy = image[0] / status['display_width'], image[1] / status['display_height']
    x, y, w, _ = window['rect']
    return crop(image, (x * sx, y * sy, (x + w) * sx, (y + 18) * sy)), info


def scenario_gl_pixel_state(app, reference):
    app.bridge.tool('overlay_set_visible', {'visible': True})
    app.bridge.tool('overlay_wait_frames', {'count': 10})
    checks = []
    title, info = overlay_title_bar(app)
    size = None
    for t, f in app.events():
        if f[0] == 'start':
            size = (int(f[1]), int(f[2]))
    checks.append(('screenshot_size', isinstance(info, dict) and (info.get('source_width'), info.get('source_height')) == size,
                   '%s, window %s' % ((info.get('source_width'), info.get('source_height')) if isinstance(info, dict) else info, size)))
    if reference is not None and title is not None:
        fraction = changed_fraction(reference, title)
        checks.append(('font', fraction < 0.05, '%.0f%% of the title bar differs from a run without the hostile state' % (fraction * 100)))
    seen = images_loaded(app)
    checks.append(('images', all(seen), 'images loaded: %s' % seen))
    return checks


MOUSE_EVENTS = ('mouse', 'ncmouse', 'pointer')


def mouse_events(app, since, until):
    """The mouse messages the application saw, from its window procedure or its message queue."""
    result = []
    for t, f in app.events(since):
        if t > until:
            break
        if f[0] in MOUSE_EVENTS:
            result.append(' '.join(f[:2]))
        elif f[0] == 'queued' and len(f) >= 3 and f[1] in MOUSE_EVENTS:
            result.append(' '.join(f[1:3]))
    return result


def client_rect(app):
    """The window's rectangle and its client area's, in screen pixels."""
    hwnd = app.window()
    window, client = wintypes.RECT(), wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    user32.GetClientRect(hwnd, ctypes.byref(client))
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return window, (origin.x, origin.y, origin.x + client.right, origin.y + client.bottom)


def move_and_click(app, points):
    """Glides the cursor through points, then clicks at the last one."""
    app.check_foreground()
    for x, y in points:
        send(mouse_absolute_input(x, y))
        time.sleep(0.05)
    send(mouse_button_input(False))
    time.sleep(0.05)
    send(mouse_button_input(True))
    time.sleep(0.3)
    app.check_foreground()


def mouse_hidden_scenario(app, pointer):
    checks = []
    if pointer and any(f[0] == 'pointer_enable_failed' for t, f in app.events()):
        return [('pointer', False, 'EnableMouseInPointer failed')]
    window, (left, top, right, bottom) = client_rect(app)
    # Away from the overlay's windows, at the top left, and from the title bar's buttons and icon.
    client_points = [(left + (right - left) * k // 10, top + (bottom - top) * 7 // 10) for k in (6, 7, 8)]
    title_y = (window.top + top) // 2
    title_points = [(window.left + (window.right - window.left) * k // 10, title_y) for k in (3, 4, 5)]

    def gestures():
        move_and_click(app, client_points)
        move_and_click(app, title_points)
        # Back to the client area: the overlay follows the cursor there.
        for x, y in reversed(client_points):
            send(mouse_absolute_input(x, y))
            time.sleep(0.05)
        time.sleep(0.3)

    # The application sees the mouse while the overlay is closed: the test sees what it hides.
    start = tick()
    gestures()
    seen = set(mouse_events(app, start, tick()))
    expected = {'mouse move', 'mouse ldown', 'ncmouse move', 'ncmouse ldown'}
    if pointer:
        expected |= {'pointer update', 'pointer down', 'pointer ncdown'}
    missing = sorted(expected - seen)
    checks.append(('seen_closed', not missing, 'missing: ' + ', '.join(missing) if missing else 'seen: ' + ', '.join(sorted(seen))))

    app.bridge.tool('overlay_set_visible', {'visible': True})
    time.sleep(0.5)
    hidden = tick()
    gestures()
    leaked = mouse_events(app, hidden, tick())
    error, state, _ = app.bridge.tool('imgui_state')
    app.bridge.tool('overlay_set_visible', {'visible': False})
    checks.append(('hidden', not leaked, '; '.join(sorted(set(leaked))) or 'nothing reached the application'))

    x, y = client_points[0]
    position = (state or {}).get('mouse', {}).get('pos') if not error else None
    expected_position = (x - left, y - top)
    followed = position is not None and abs(position[0] - expected_position[0]) <= 2 and abs(position[1] - expected_position[1]) <= 2
    checks.append(('overlay_mouse', followed, 'ImGui mouse %s, cursor %s' % (position, expected_position)))
    return checks


def scenario_mouse_hidden(app):
    return mouse_hidden_scenario(app, False)


def scenario_pointer_mouse_hidden(app):
    return mouse_hidden_scenario(app, True)


def scenario_gl_error(app):
    app.bridge.tool('overlay_set_visible', {'visible': True})
    seen = images_loaded(app)
    return [('images', all(seen), 'images loaded: %s' % seen)]


# name: (application options, scenario, interactive)
SCENARIOS = {
    'toggle_keys':            ({'INGAMEOVERLAY_TEST_RAW_KEYBOARD': '1'}, scenario_toggle_keys, True),
    'toggle_keys_raw_buffer': ({'INGAMEOVERLAY_TEST_RAW_KEYBOARD': 'buffer'}, scenario_toggle_keys, True),
    'keyboard_state_hidden':  ({}, scenario_keyboard_state_hidden, False),
    'mouse_look_key':         ({'INGAMEOVERLAY_TEST_MOUSE_LOOK': '1'}, scenario_mouse_look_key, True),
    'mouse_look_api':         ({'INGAMEOVERLAY_TEST_MOUSE_LOOK': '1'}, scenario_mouse_look_api, True),
    'clip_key':               ({'INGAMEOVERLAY_TEST_CLIP_CURSOR': '1'}, scenario_clip_key, True),
    'clip_api':               ({'INGAMEOVERLAY_TEST_CLIP_CURSOR': '1'}, scenario_clip_api, True),
    'gl_pixel_state':         ({'INGAMEOVERLAY_TEST_GL_PIXEL_STATE': '1'}, None, False),
    'gl_error':               ({'INGAMEOVERLAY_TEST_GL_ERROR': '1'}, scenario_gl_error, False),
    'mouse_hidden':           ({}, scenario_mouse_hidden, True),
    'pointer_mouse_hidden':   ({'INGAMEOVERLAY_TEST_POINTER': '1'}, scenario_pointer_mouse_hidden, True),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('directory', help='directory of windows_hook_tests_app.exe, overlay_example.dll and ingame_overlay_mcp_bridge.exe')
    parser.add_argument('--only', help='comma separated scenario names: ' + ', '.join(SCENARIOS))
    parser.add_argument('--skip-interactive', action='store_true', help='only run the scenarios without keyboard and mouse input')
    arguments = parser.parse_args()

    # Screen coordinates in pixels, like the application (SetProcessDPIAware).
    user32.SetProcessDPIAware()
    directory = os.path.abspath(arguments.directory)
    names = arguments.only.split(',') if arguments.only else list(SCENARIOS)
    if arguments.skip_interactive:
        names = [n for n in names if n in SCENARIOS and not SCENARIOS[n][2]]
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        parser.error('unknown scenario: ' + ', '.join(unknown))
    out_dir = os.path.join(directory, 'windows_hook_tests')
    os.makedirs(out_dir, exist_ok=True)

    global CURRENT_APP
    failures = inconclusive = 0
    for name in names:
        options, run, interactive = SCENARIOS[name]
        checks = []
        result = None
        try:
            if name == 'gl_pixel_state':
                # The reference title bar comes from a run without the hostile state.
                reference_app = App_t(directory, name + '_reference', {}, out_dir, False)
                try:
                    reference_app.bridge.tool('overlay_set_visible', {'visible': True})
                    reference_app.bridge.tool('overlay_wait_frames', {'count': 10})
                    reference, _ = overlay_title_bar(reference_app)
                finally:
                    reference_app.close()
                app = App_t(directory, name, options, out_dir, False)
                try:
                    checks = scenario_gl_pixel_state(app, reference)
                finally:
                    app.close()
            else:
                app = App_t(directory, name, options, out_dir, interactive)
                CURRENT_APP = app
                try:
                    checks = run(app)
                finally:
                    CURRENT_APP = None
                    app.close()
        except Inconclusive as e:
            result = 'INCONCLUSIVE'
            checks = [('foreground', False, str(e))]
        except Exception as e:
            checks.append(('exception', False, repr(e)[:200]))
        bad = [c for c in checks if not c[1]]
        if result is None:
            result = 'OK' if not bad else 'FAILED'
        failures += 1 if result == 'FAILED' else 0
        inconclusive += 1 if result == 'INCONCLUSIVE' else 0
        print('%-24s %s' % (name, result))
        for label, ok, detail in checks:
            print('    %-16s %-5s %s' % (label, 'ok' if ok else 'FAIL', detail))
        sys.stdout.flush()

    print('%d/%d scenarios passed, %d inconclusive, logs in %s' % (len(names) - failures - inconclusive, len(names), inconclusive, out_dir))
    return 1 if failures or inconclusive else 0


if __name__ == '__main__':
    sys.exit(main())
