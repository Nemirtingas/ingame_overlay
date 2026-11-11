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

"""Starts each test host application with overlay_example and its MCP server, and checks the renderer hook
through the MCP bridge: the hook gets ready, the overlay shows, the ImGui tree is readable, a text can be set,
the MCP and application screenshots work. Then closes the application.

    python3 tests/mcp_check_apps.py <build dir> <overlay library> <app> [<app>...]

The Linux and macOS test applications load the overlay library relative to their own directory, the Windows
ones take it as given. Needs a build with the MCP server (INGAMEOVERLAY_BUILD_MCP, on with the tests) and a
desktop session. Closing: WM_CLOSE on Windows; with CLOSEWIN=<closewin program> on Linux, WM_DELETE_WINDOW;
with QUIT_SCRIPT=<AppleScript with %d for the pid> on macOS; SIGTERM otherwise.
Logs and screenshots go to <build dir>/mcp_check. Python 3 standard library only.
"""

import base64
import json
import os
import subprocess
import sys
import time

build_dir = os.path.abspath(sys.argv[1])
# As given: the Linux and macOS test apps load it relative to their own directory.
overlay = sys.argv[2]
apps = sys.argv[3:]
windows = os.name == 'nt'
exe = '.exe' if windows else ''
bridge_path = os.path.join(build_dir, 'ingame_overlay_mcp_bridge' + exe)
out_dir = os.path.join(build_dir, 'mcp_check')
os.makedirs(out_dir, exist_ok=True)


class Bridge_t:
    def __init__(self, name, pid):
        arguments = [bridge_path, '--wait', '30', '--name', name]
        # The bootstrap services can't be listed on macOS.
        if sys.platform == 'darwin':
            arguments += ['--pid', str(pid)]
        self.process = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8')
        self.next_id = 1
        self.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'check', 'version': '1'}})

    def request(self, method, params):
        message = {'jsonrpc': '2.0', 'id': self.next_id, 'method': method, 'params': params}
        self.next_id += 1
        self.process.stdin.write(json.dumps(message) + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('bridge closed')
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
        self.process.stdin.close()
        self.process.wait(10)


def find_window(tree, name):
    return next((w for w in tree['windows'] if w['name'] == name), None)


def check_app(app):
    name = app.replace('_app', '').replace('_', '-')
    env = dict(os.environ, INGAMEOVERLAY_MCP='1', INGAMEOVERLAY_MCP_NAME=name)
    log = open(os.path.join(out_dir, app + '.log'), 'w')
    process = subprocess.Popen([os.path.join(build_dir, app + exe), overlay], cwd=build_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
    checks = []

    def check(label, ok, detail=''):
        checks.append((label, bool(ok), detail))

    try:
        bridge = Bridge_t(name, process.pid)

        # The server starts with the overlay, before the renderer detection: wait for the hook.
        start = time.time()
        while True:
            error, status, _ = bridge.tool('overlay_status')
            ready = not error and status['hook_state'] == 'Ready' and status['frame_running']
            if ready or time.time() - start > 60:
                break
            time.sleep(1)
        check('ready', ready, 'after %.1fs' % (time.time() - start))
        check('status', not error and status['hook_started'] and status['hook_state'] == 'Ready' and status['frame_running'],
              '%s %s %s' % (status.get('renderer'), status.get('backend_renderer'), status.get('hook_state')))
        renderer = status.get('renderer')

        error, _, _ = bridge.tool('overlay_set_visible', {'visible': True})
        bridge.tool('overlay_wait_frames', {'count': 5})
        error, status, _ = bridge.tool('overlay_status')
        check('set_visible', not error and status.get('overlay_visible') is True)

        error, tree, _ = bridge.tool('imgui_tree')
        overlay_window = find_window(tree, 'Overlay') if not error else None
        items = overlay_window['items'] if overlay_window else []
        check('imgui_tree', overlay_window is not None and any(i.get('label') == 'Take screenshot after overlay' for i in items),
              '%d items' % len(items))

        error, result, _ = bridge.tool('imgui_item', {'label': 'Test input text', 'action': 'set_text', 'text': 'MCP ok'})
        bridge.tool('overlay_wait_frames', {'count': 2})
        error2, found, _ = bridge.tool('imgui_find', {'query': 'Test input text', 'type': 'InputText'})
        value = found['items'][0].get('value') if not error2 and found['items'] else None
        check('set_text', not error and value == 'MCP ok', repr(value))

        error, size, images = bridge.tool('overlay_screenshot', {'max_dimension': 640})
        if images:
            open(os.path.join(out_dir, app + '.png'), 'wb').write(base64.b64decode(images[0]))
        check('mcp_screenshot', not error and images and size['source_width'] > 0, '%sx%s' % (size.get('source_width'), size.get('source_height')) if isinstance(size, dict) else str(size))

        # The application's own screenshot still reaches its callback: overlay_example then opens its window.
        error, _, _ = bridge.tool('imgui_item', {'label': 'Take screenshot after overlay', 'action': 'click'})
        window = None
        for _ in range(20):
            bridge.tool('overlay_wait_frames', {'count': 5})
            _, tree, _ = bridge.tool('imgui_tree')
            window = find_window(tree, 'Screenshot window')
            if window is not None:
                break
        check('app_screenshot', not error and window is not None)
        bridge.close()
    except Exception as e:
        check('exception', False, repr(e))
        renderer = None

    # Graceful close: WM_CLOSE on Windows, SIGTERM elsewhere. The overlay unloads and Release stops the server.
    if windows:
        subprocess.run(['taskkill', '/PID', str(process.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif os.environ.get('CLOSEWIN'):
        # WM_DELETE_WINDOW: the app returns from main and the overlay's library destructor runs.
        titles = {'linux_opengl_app': 'GL 3.0 Window'}
        subprocess.run([os.environ['CLOSEWIN'], str(process.pid), titles.get(app, '-')], stdout=subprocess.DEVNULL)
    elif os.environ.get('QUIT_SCRIPT'):
        subprocess.run(['osascript', '-e', os.environ['QUIT_SCRIPT'] % process.pid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        process.terminate()
    start = time.time()
    try:
        process.wait(15)
        check('exit', True, '%.1fs code %s' % (time.time() - start, process.returncode))
    except subprocess.TimeoutExpired:
        process.kill()
        check('exit', False, 'still running after 15s')
    return checks


failures = 0
for app in apps:
    checks = check_app(app)
    bad = [c for c in checks if not c[1]]
    failures += len(bad)
    print('%-22s %s' % (app, 'OK' if not bad else 'FAILED'))
    for label, ok, detail in checks:
        print('    %-15s %-6s %s' % (label, 'ok' if ok else 'FAIL', detail))
    sys.stdout.flush()
sys.exit(1 if failures else 0)
