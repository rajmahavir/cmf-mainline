"""Mac version of ser.py without pyserial: send one line, print output until idle."""
import os, sys, time, termios, select

port = '/dev/cu.usbmodem01234567891'
cmd = sys.argv[1]
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 10
idle = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIFLUSH)
os.write(fd, cmd.encode() + b'\n')
out, start, last = b'', time.time(), time.time()
while time.time() - start < wait:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        b = os.read(fd, 4096)
        if b:
            out += b; last = time.time(); continue
    if out and time.time() - last > idle:
        break
os.close(fd)
sys.stdout.write(out.decode(errors='replace'))
