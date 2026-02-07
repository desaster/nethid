#!/usr/bin/env python3
#
# CLI tool for sending HID events to NetHID device via HTTP API
#
# Usage: ./keycli.py [options] <commands...>
#
# Options:
#   -v, --verbose        Show debug output
#   -h, --help           Show this help
#   --ip HOST            Override target host (default: $NETHID_IP or 192.168.1.10)
#   --password PASSWORD  Device password (default: $NETHID_PASSWORD)
#
# Commands:
#   KEY down|up|press|release   Press or release a key
#   KEY tap                     Press and release a key
#   mouse1|mouse2|mouse3 down|up   Press or release mouse button
#   move X Y                    Move mouse by X,Y pixels
#   media NAME                  Press and release a media key
#   media NAME down|up          Press or release a media key
#   system NAME                 Press and release a system key (POWER, SLEEP, WAKE)
#   system NAME down|up         Press or release a system key
#   sleep N                     Sleep for N seconds
#
# Key names are resolved server-side (case-insensitive):
#   Letters:   A, B, C, ...
#   Numbers:   1, 2, 3, ...
#   Special:   ENTER, ESCAPE, TAB, SPACE, BACKSPACE, DELETE, ...
#   Modifiers: CTRL, SHIFT, ALT, GUI, WIN, ...
#   Arrows:    UP, DOWN, LEFT, RIGHT
#   Function:  F1, F2, ..., F12
#   Consumer:  VOLUME_UP, VOLUME_DOWN, MUTE, PLAY_PAUSE, ...
#   System:    POWER, SLEEP, WAKE
#   Raw hex:   0x04, 0xE0
#
# Examples:
#   ./keycli.py A down sleep 0.1 A up
#   ./keycli.py mouse1 down move 10 0 mouse1 up
#   ./keycli.py media volume_up
#   NETHID_IP=mydevice.lan NETHID_PASSWORD=secret ./keycli.py ENTER tap

import os
import sys
import json
from time import sleep
from urllib.request import Request, urlopen
from urllib.error import URLError, HTTPError

HOST = os.environ.get('NETHID_IP', '192.168.1.10')
PASSWORD = os.environ.get('NETHID_PASSWORD')
VERBOSE = False
TOKEN = None

def api_request(method, path, body=None):
    url = f'http://{HOST}' + path
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode()
        headers['Content-Type'] = 'application/json'
    if TOKEN:
        headers['Authorization'] = f'Bearer {TOKEN}'
    if VERBOSE:
        print(f'  {method} {url}', file=sys.stderr)
        if body:
            print(f'  body: {body}', file=sys.stderr)
    req = Request(url, data=data, headers=headers, method=method)
    try:
        resp = urlopen(req, timeout=5)
        resp_body = resp.read().decode()
        if resp_body:
            return json.loads(resp_body)
        return None
    except HTTPError as e:
        resp_body = e.read().decode()
        try:
            err = json.loads(resp_body)
            msg = err.get('error', resp_body)
        except (json.JSONDecodeError, ValueError):
            msg = resp_body
        print(f'Error: {e.code} {msg}', file=sys.stderr)
        sys.exit(1)
    except URLError as e:
        print(f'Error: {e.reason}', file=sys.stderr)
        sys.exit(1)

def api_get(path):
    return api_request('GET', path)

def api_post(path, body=None):
    return api_request('POST', path, body)

def authenticate():
    global TOKEN
    status = api_get('/api/auth/status')
    if not status or not status.get('required'):
        if VERBOSE:
            print('  Auth not required', file=sys.stderr)
        return
    if not PASSWORD:
        print('Error: device requires authentication. '
              'Use --password or set NETHID_PASSWORD.', file=sys.stderr)
        sys.exit(1)
    resp = api_post('/api/login', {'password': PASSWORD})
    TOKEN = resp['token']
    if VERBOSE:
        print(f'  Authenticated', file=sys.stderr)

def map_action(action):
    ACTION_MAP = {'down': 'press', 'up': 'release', 'press': 'press', 'release': 'release', 'tap': 'tap'}
    if action not in ACTION_MAP:
        raise Exception(f'Invalid action: {action}')
    return ACTION_MAP[action]

def print_help():
    with open(__file__) as f:
        for line in f:
            if line.startswith('#!'):
                continue
            if not line.startswith('#'):
                break
            print(line[2:].rstrip() if len(line) > 1 else '')
    sys.exit(0)

if __name__ == '__main__':
    args = sys.argv[1:]

    # Parse options
    while args and args[0].startswith('-'):
        opt = args.pop(0)
        if opt in ('-v', '--verbose'):
            VERBOSE = True
        elif opt in ('-h', '--help'):
            print_help()
        elif opt == '--ip':
            HOST = args.pop(0)
        elif opt == '--password':
            PASSWORD = args.pop(0)
        else:
            print(f'Unknown option: {opt}', file=sys.stderr)
            sys.exit(1)

    if not args:
        print_help()

    authenticate()

    while len(args):
        arg = args.pop(0)
        if arg in ('mouse1', 'mouse2', 'mouse3'):
            action = map_action(args.pop(0))
            button = {'mouse1': 1, 'mouse2': 2, 'mouse3': 4}[arg]
            print(f'Sending {arg} {action}')
            api_post('/api/hid/mouse/button', {'button': button, 'action': action})
        elif arg == 'move':
            dx = int(args.pop(0))
            dy = int(args.pop(0))
            print(f'Sending move {dx} {dy}')
            api_post('/api/hid/mouse/move', {'dx': dx, 'dy': dy})
        elif arg == 'sleep':
            sleep(float(args.pop(0)))
        elif arg in ('media', 'system'):
            name = args.pop(0).upper()
            if args and args[0] in ('down', 'up', 'press', 'release'):
                action = map_action(args.pop(0))
            else:
                action = 'tap'
            print(f'Sending {arg} {name.lower()} {action}')
            key_type = {'media': 'consumer', 'system': 'system'}[arg]
            api_post('/api/hid/key', {'key': name, 'action': action, 'type': key_type})
        else:
            action = map_action(args.pop(0))
            print(f'Sending {arg} {action}')
            api_post('/api/hid/key', {'key': arg, 'action': action})
