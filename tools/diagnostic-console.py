import base64
import hashlib
import os
from pathlib import Path
import re
import secrets
import select
import sys
import termios
import time
import tty

port = os.environ["CMF_SERIAL_PORT"]
action = sys.argv[1]
deadline = time.monotonic() + (int(sys.argv[3]) if len(sys.argv) > 3 else 60)
while not Path(port).exists() and time.monotonic() < deadline:
    time.sleep(.25)
fd = os.open(port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
tty.setraw(fd)
token = secrets.token_hex(5)

def send(data):
    data = data.replace(b"\n", b"\r")
    while data:
        wait = deadline - time.monotonic()
        if wait <= 0 or not select.select([], [fd], [], wait)[1]:
            raise TimeoutError("USB serial write")
        try:
            size = os.write(fd, data[:16384])
        except BlockingIOError:
            continue
        if size <= 0:
            raise OSError("USB serial write closed")
        data = data[size:]

def receive(pattern):
    seen = b""
    while time.monotonic() < deadline:
        if not select.select([fd], [], [], max(0, deadline - time.monotonic()))[0]:
            break
        try:
            data = os.read(fd, 8192)
        except BlockingIOError:
            continue
        if not data:
            break
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        seen = (seen + data)[-65536:]
        if match := pattern.search(seen):
            return match
    raise TimeoutError(f"USB serial marker {pattern!r}")

if action == "bootcheck":
    receive(re.compile(rb" # "))
    send((f"uname -r; printf '\\nDONE_%s=0\\n' {token}\n").encode())
    receive(re.compile(fr"\nDONE_{token}=0\r?\n".encode()))
elif action == "upload":
    path = Path(sys.argv[2])
    contents = path.read_bytes()
    payload = base64.b64encode(contents)
    name = path.name
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
        sys.exit("Unsafe remote filename")
    digest = hashlib.sha256(contents).hexdigest()
    command = (
        f"mkdir -p /tmp/cmf-v13; stty -icanon -echo min 1 time 0; "
        f"echo READY_{token}; head -c {len(payload)} | base64 -d > /tmp/cmf-v13/{name}; "
        f"stty sane; echo '{digest}  /tmp/cmf-v13/{name}' | sha256sum -c; "
        f"printf '\\nDONE_%s=%s\\n' {token} \"$?\""
    )
    send(("\n" + command + "\n").encode())
    receive(re.compile(fr"\nREADY_{token}\r?\n".encode()))
    send(payload)
    result = receive(re.compile(fr"\nDONE_{token}=(\d+)\r?\n".encode()))
    if result[1] != b"0":
        sys.exit("Upload hash mismatch")
elif action == "run":
    send((f"\n({sys.argv[2]}); printf '\\nDONE_%s=%s\\n' {token} \"$?\"\n").encode())
    result = receive(re.compile(fr"\nDONE_{token}=(\d+)\r?\n".encode()))
    if result[1] != b"0":
        sys.exit(1)
elif action == "reboot":
    send(b"\x03")
    time.sleep(.3)
    send(b"\n sync; /tmp/cmf-v13/reboot-bootloader\n")
    termios.tcdrain(fd)
    time.sleep(1)
else:
    sys.exit("Unknown action")
