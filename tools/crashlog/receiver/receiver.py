#!/usr/bin/env python3
"""CMF phone log receiver: UDP lines from the phone, stamped with receive time.
Port 5514 = /dev/kmsg, 5515 = journal. One file per day in ~/cmf-phone-log."""
import os, selectors, socket, time
base = os.path.expanduser("~/cmf-phone-log")
sel = selectors.DefaultSelector()
for port, tag in ((5514, "kmsg"), (5515, "jrnl")):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", port))
    sel.register(s, selectors.EVENT_READ, tag)
cur, fh = None, None
while True:
    for key, _ in sel.select():
        data, addr = key.fileobj.recvfrom(65535)
        day = time.strftime("%Y%m%d")
        if day != cur:
            if fh: fh.close()
            fh = open(f"{base}/phone-{day}.log", "a", buffering=1)
            cur = day
        stamp = time.strftime("%H:%M:%S") + f".{int(time.time()*1000)%1000:03d}"
        for line in data.decode(errors="replace").splitlines():
            fh.write(f"{stamp} {key.data} {addr[0]} {line}\n")
