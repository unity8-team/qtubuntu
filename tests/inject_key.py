#!/usr/bin/python

import sys, time, evdev

usage = 'Usage: inject_keypress.py count delay'

def main(args):
    if args[0] == '--help' or args[0] == '-help' or args[0] == '-h':
        print usage
        sys.exit(0)
    elif len(args) != 2:
        print usage
        sys.exit(1)
    count = max(1, min(int(args[0]), 9))
    delay = max(0.0, min(float(args[1]), 2.0))
    print count, delay

    uinput = evdev.UInput()
    for i in range(count):
        uinput.write(evdev.ecodes.EV_KEY, i+2, 1)
        uinput.syn()
        uinput.write(evdev.ecodes.EV_KEY, i+2, 0)
        uinput.syn()
        time.sleep(delay)
    uinput.close()

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
