"""Send one shell command line over the phone's USB serial root shell and print output.

usage: python ser.py COMMAND [wait_seconds] [idle_seconds]
Reads until the output has been idle for idle_seconds or wait_seconds elapse.
"""
import sys, time, serial

cmd = sys.argv[1]
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 10
idle = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
s = serial.Serial('COM12', 115200, timeout=0.2)
s.reset_input_buffer()
s.write(cmd.encode() + b'\n')
out, start, last = b'', time.time(), time.time()
while time.time() - start < wait:
    b = s.read(4096)
    if b:
        out += b
        last = time.time()
    elif out and time.time() - last > idle:
        break
s.close()
sys.stdout.write(out.decode(errors='replace'))
