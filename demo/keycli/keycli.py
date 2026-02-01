#!/usr/bin/env python3
#
# CLI tool for sending HID events to NetHID device
#
# Usage: ./keycli.py [options] <commands...>
#
# Options:
#   -v, --verbose    Show debug output
#   -h, --help       Show this help
#   --ip IP          Override target IP (default: $NETHID_IP or 192.168.1.10)
#
# Commands:
#   HID_KEY_X down|up   Press or release a key (see usb_hid.py for key names)
#   mouse1|mouse2 down|up   Press or release mouse button
#   move X Y            Move mouse by X,Y pixels
#   sleep N             Sleep for N seconds
#
# Examples:
#   ./keycli.py HID_KEY_A down sleep 0.1 HID_KEY_A up
#   ./keycli.py mouse1 down move 10 0 mouse1 up
#   NETHID_IP=192.168.1.100 ./keycli.py HID_KEY_ENTER down HID_KEY_ENTER up

import os
import sys
import socket
from struct import pack
from time import sleep

import usb_hid as hid

UDP_IP = os.environ.get('NETHID_IP', '192.168.1.10')
UDP_PORT = 4444
VERBOSE = False

def send_scancode(scancode, pressed):
    if VERBOSE:
        print(f'send_scancode: {scancode}, {pressed}')
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    packet = pack('BBBBB', 0x01, 0x01, pressed, 0x00, scancode)
    sock.sendto(packet, (UDP_IP, UDP_PORT))

def send_move(buttons, x, y, vertical, horizontal):
    if VERBOSE:
        print(f'send_move: {buttons}, {x}, {y}, {vertical}, {horizontal}')
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    packet = pack('BBBbbbb', 0x02, 0x01, buttons, x, y, vertical, horizontal)
    sock.sendto(packet, (UDP_IP, UDP_PORT))

def print_help():
    # Print the header comment block from this file
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
            UDP_IP = args.pop(0)
        else:
            print(f'Unknown option: {opt}', file=sys.stderr)
            sys.exit(1)

    if not args:
        print_help()

    mouse_button1 = False
    mouse_button2 = False

    def mouse_buttons(mouse_button1, mouse_button2):
        return (mouse_button2 << 1) | mouse_button1

    while len(args):
        arg = args.pop(0)
        if hasattr(hid, arg):
            key = getattr(hid, arg)
            action = args.pop(0)
            if not action in ('down', 'up'):
                raise Exception('Invalid action: {}'.format(action))
            print('Sending {} {}'.format(arg, action))
            send_scancode(key, action == 'down');
        elif arg in ('mouse1', 'mouse2'):
            action = args.pop(0)
            if not action in ('down', 'up'):
                raise Exception('Invalid action: {}'.format(action))
            if arg == 'mouse1':
                mouse_button1 = action == 'down'
            else:
                mouse_button2 = action == 'down'
            print('Sending mouse {} {}'.format(arg, action))
            send_move(mouse_buttons(mouse_button1, mouse_button2), 0, 0, 0, 0)
        elif arg in ('move'):
            delta_x = int(args.pop(0))
            delta_y = int(args.pop(0))
            print('Sending move {} {}'.format(delta_x, delta_y))
            send_move(mouse_buttons(mouse_button1, mouse_button2), delta_x, delta_y, 0, 0)
        elif arg == 'sleep':
            sleep(float(args.pop(0)))
        else:
            raise Exception('Invalid argument: {}'.format(arg))
