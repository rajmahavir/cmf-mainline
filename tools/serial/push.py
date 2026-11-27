"""Push a local file to the phone over the USB serial root shell as base64 chunks.

usage: python push.py LOCAL REMOTE
"""
import base64, sys, time, serial

local, remote = sys.argv[1], sys.argv[2]
data = base64.b64encode(open(local, 'rb').read()).decode()
s = serial.Serial('COM12', 115200, timeout=0.2)


def run(line, settle=0.15):
    s.write(line.encode() + b'\n')
    time.sleep(settle)
    return s.read(65536)


run('stty -echo')
run(f'rm -f {remote}.b64')
for i in range(0, len(data), 768):
    run(f"printf %s '{data[i:i + 768]}' >> {remote}.b64", 0.08)
time.sleep(0.5)
s.reset_input_buffer()
s.write(f'base64 -d {remote}.b64 > {remote} && rm {remote}.b64; sha256sum {remote}; stty echo\n'.encode())
time.sleep(2)
print(s.read(65536).decode(errors='replace'))
s.close()
