"""Drive a beeros QEMU session: REPL lines on the serial port, commands on
the monitor (sendkey, mouse_move, ...) and QMP (input-send-event, ...).

usage: python3 scripts/qemu-drive.py script.txt log.txt -- qemu-system-riscv64 \
           -M virt -m 256M -display none -bios none -serial stdio \
           -global virtio-mmio.force-legacy=false -kernel beeros.elf [devices...]
script lines: "r <beerlang>" (REPL line + CR), "k <bytes>" (raw serial bytes,
Python escapes: \x1b[D is Left, \r is Enter), "m <monitor cmd>", "q <QMP json>",
"s <seconds>", "t <text>" (typed on the virtio keyboard with sendkey, then
Enter; needs -device virtio-keyboard-device)
The serial console must be "-serial stdio" (not mon:stdio); the monitor and
QMP go to localhost TCP ports 45454/45455.
"""
import socket, subprocess, sys, time

script, log = sys.argv[1], sys.argv[2]
qemu = sys.argv[sys.argv.index("--") + 1:]
port = 45454
out = open(log, "wb")
p = subprocess.Popen(qemu + ["-monitor", f"tcp:127.0.0.1:{port},server,nowait",
                             "-qmp", f"tcp:127.0.0.1:{port+1},server,nowait"],
                     stdin=subprocess.PIPE, stdout=out, stderr=subprocess.STDOUT)
time.sleep(3)
mon = socket.create_connection(("127.0.0.1", port))
mon.settimeout(0.2)
qmp = socket.create_connection(("127.0.0.1", port + 1))
qmp.settimeout(0.2)
qmp.sendall(b'{"execute":"qmp_capabilities"}\n')

def drain(sock=None):
    try:
        while (sock or mon).recv(4096):
            pass
    except OSError:
        pass

KEYS = {' ': 'spc', '(': 'shift-9', ')': 'shift-0', '[': 'bracket_left',
        ']': 'bracket_right', '{': 'shift-bracket_left', '}': 'shift-bracket_right',
        '"': 'shift-apostrophe', "'": 'apostrophe', '-': 'minus', '_': 'shift-minus',
        '=': 'equal', '+': 'shift-equal', '*': 'shift-8', ';': 'semicolon',
        ':': 'shift-semicolon', '.': 'dot', ',': 'comma', '/': 'slash', '?': 'shift-slash',
        '!': 'shift-1', '@': 'shift-2', '#': 'shift-3', '%': 'shift-5', '&': 'shift-7',
        '<': 'shift-comma', '>': 'shift-dot', '\\': 'backslash', '|': 'shift-backslash'}

def sendkey(c):
    k = KEYS.get(c) or (("shift-" + c.lower()) if c.isupper() else c)
    mon.sendall(f"sendkey {k}\n".encode()); time.sleep(0.05); drain()

for line in open(script):
    line = line.rstrip("\n")
    if not line:
        continue
    kind, arg = line[0], line[2:]
    if kind == "r":
        p.stdin.write(arg.encode() + b"\r"); p.stdin.flush(); time.sleep(0.8)
    elif kind == "k":
        for ch in arg.encode().decode("unicode_escape").encode("latin-1"):
            p.stdin.write(bytes([ch])); p.stdin.flush(); time.sleep(0.02)
        time.sleep(0.6)
    elif kind == "m":
        mon.sendall(arg.encode() + b"\n"); time.sleep(0.15); drain()
    elif kind == "q":
        qmp.sendall(arg.encode() + b"\n"); time.sleep(0.15); drain(qmp)
    elif kind == "t":
        for c in arg:
            sendkey(c)
        mon.sendall(b"sendkey ret\n"); time.sleep(0.6); drain()
    elif kind == "s":
        time.sleep(float(arg))
time.sleep(1.5)
p.kill(); p.wait()
