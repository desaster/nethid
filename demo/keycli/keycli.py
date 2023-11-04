#!/usr/bin/env python3

import sys
import socket
from struct import pack
from time import sleep

import usb_hid as hid

UDP_IP = '192.168.1.10'
UDP_PORT = 4444

def send_scancode(scancode, pressed):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    packet = pack('BBBBB', 0x01, 0x01, pressed, 0x00, scancode)
    sock.sendto(packet, (UDP_IP, UDP_PORT))

def send_move(buttons, x, y, vertical, horizontal):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    packet = pack('BBBbbbb', 0x02, 0x01, buttons, x, y, vertical, horizontal)
    sock.sendto(packet, (UDP_IP, UDP_PORT))

if __name__ == '__main__':
    args = sys.argv[1:]

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
