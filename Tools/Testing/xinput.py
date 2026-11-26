#!/usr/bin/env python3
"""Synthesizes real X11 keyboard/mouse input (via the XTEST extension) so the
Linux build of the engine can be play-tested automatically, exactly like a
person pressing keys in the game window.

    python3 xinput.py key W 0.5           # hold W for 0.5 s
    python3 xinput.py tap Space
    python3 xinput.py click 1             # 1=left 2=middle 3=right
    python3 xinput.py drag 3 200 0 0.5    # hold right button, move mouse by (200,0) over 0.5 s
    python3 xinput.py move 640 360        # absolute pointer position
    python3 xinput.py sleep 1.0
Several commands can be chained:  xinput.py tap B , sleep 1 , key W 0.5
"""
import ctypes, ctypes.util, sys, time

xlib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("X11") or "libX11.so.6")
xtst = ctypes.cdll.LoadLibrary(ctypes.util.find_library("Xtst") or "libXtst.so.6")
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XStringToKeysym.restype = ctypes.c_ulong
xlib.XStringToKeysym.argtypes = [ctypes.c_char_p]
xlib.XKeysymToKeycode.restype = ctypes.c_ubyte
xlib.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
xlib.XFlush.argtypes = [ctypes.c_void_p]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeRelativeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]

dpy = xlib.XOpenDisplay(None)
if not dpy:
    sys.exit("xinput.py: cannot open X display")

NAMES = {"Space": "space", "Escape": "Escape", "Enter": "Return", "Shift": "Shift_L",
         "Control": "Control_L", "Tab": "Tab", "Home": "Home", "End": "End", "Backspace": "BackSpace"}

CHARS = {" ": ("space", False), "-": ("minus", False), "_": ("minus", True), "=": ("equal", False),
         "(": ("9", True), ")": ("0", True), ".": ("period", False), ",": ("comma", False), "\"": ("apostrophe", True),
         "'": ("apostrophe", False), ":": ("semicolon", True), ";": ("semicolon", False), "!": ("1", True)}

def keycode(name):
    sym = xlib.XStringToKeysym(NAMES.get(name, name.lower() if len(name) == 1 else name).encode())
    code = xlib.XKeysymToKeycode(dpy, sym)
    if not code:
        sys.exit("xinput.py: unknown key " + name)
    return code

def keycode_raw(sym_name):
    code = xlib.XKeysymToKeycode(dpy, xlib.XStringToKeysym(sym_name.encode()))
    if not code:
        sys.exit("xinput.py: unknown keysym " + sym_name)
    return code

def key(name, down):
    xtst.XTestFakeKeyEvent(dpy, keycode(name), 1 if down else 0, 0)
    xlib.XFlush(dpy)

def button(b, down):
    xtst.XTestFakeButtonEvent(dpy, b, 1 if down else 0, 0)
    xlib.XFlush(dpy)

def run(cmd):
    op, a = cmd[0], cmd[1:]
    if op == "key":
        key(a[0], True); time.sleep(float(a[1]) if len(a) > 1 else 0.1); key(a[0], False)
    elif op == "down":
        key(a[0], True)
    elif op == "up":
        key(a[0], False)
    elif op == "type":
        for ch in " ".join(a):
            name = {" ": "space", "(": "parenleft", ")": "parenright", "'": "apostrophe", ".": "period",
                    "=": "equal", ",": "comma", "-": "minus", "\"": "quotedbl", ":": "colon"}.get(ch, ch)
            shifted = ch.isupper() or ch in '()":'
            if shifted: key("Shift", True)
            xtst.XTestFakeKeyEvent(dpy, keycode_raw(name), 1, 0); xlib.XFlush(dpy)
            xtst.XTestFakeKeyEvent(dpy, keycode_raw(name), 0, 0); xlib.XFlush(dpy)
            if shifted: key("Shift", False)
            time.sleep(0.03)
    elif op == "tap":
        key(a[0], True); time.sleep(0.12); key(a[0], False)
    elif op == "click":
        button(int(a[0]), True); time.sleep(0.12); button(int(a[0]), False)
    elif op == "move":
        xtst.XTestFakeMotionEvent(dpy, -1, int(a[0]), int(a[1]), 0); xlib.XFlush(dpy)
    elif op == "drag":
        b, dx, dy, dur = int(a[0]), int(a[1]), int(a[2]), float(a[3])
        button(b, True)
        steps = max(int(dur / 0.02), 1)
        for _ in range(steps):
            xtst.XTestFakeRelativeMotionEvent(dpy, dx // steps, dy // steps, 0); xlib.XFlush(dpy)
            time.sleep(dur / steps)
        button(b, False)
    elif op == "type":                    # type "some text" (words joined with spaces)
        text = " ".join(a)
        for ch in text:
            name, shift = CHARS.get(ch, (ch, ch.isupper()))
            if shift: key("Shift", True)
            key(name if len(name) > 1 else name.lower(), True); time.sleep(0.02); key(name if len(name) > 1 else name.lower(), False)
            if shift: key("Shift", False)
            time.sleep(0.02)
    elif op == "combo":                   # combo Control s
        for k in a[:-1]: key(k, True)
        key(a[-1], True); time.sleep(0.1); key(a[-1], False)
        for k in reversed(a[:-1]): key(k, False)
    elif op == "sleep":
        time.sleep(float(a[0]))
    else:
        sys.exit("xinput.py: unknown command " + op)

cmds, cur = [], []
for tok in sys.argv[1:]:
    if tok == ",":
        cmds.append(cur); cur = []
    else:
        cur.append(tok)
if cur:
    cmds.append(cur)
for c in cmds:
    run(c)
