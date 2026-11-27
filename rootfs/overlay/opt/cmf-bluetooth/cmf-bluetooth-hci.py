#!/usr/bin/env python3
"""Expose the CMF's MediaTek stpbt H4 transport to Linux Bluetooth."""

import argparse
from contextlib import ExitStack
import errno
import os
import select
import signal


# H4 packet type: (header size including packet type, length offset, mask)
PACKET_LENGTHS = {
    0x02: (5, 3, 0xFFFF),  # ACL
    0x03: (4, 3, 0xFF),    # SCO
    0x04: (3, 2, 0xFF),    # Event
    0x05: (5, 3, 0x3FFF),  # ISO
}


def controller_frames(buffer):
    while buffer:
        try:
            header, offset, mask = PACKET_LENGTHS[buffer[0]]
        except KeyError:
            raise ValueError(f"Invalid H4 packet type 0x{buffer[0]:02x}") from None
        if len(buffer) < header:
            return
        size = header + (int.from_bytes(buffer[offset:header], "little") & mask)
        if len(buffer) < size:
            return
        frame = bytes(buffer[:size])
        del buffer[:size]
        yield frame


def controller_features(frame):
    # The CMF firmware advertises Synchronization Train on feature page 2,
    # but rejects Read Synchronization Train Parameters during Linux setup.
    if len(frame) == 17 and frame[:3] == b"\x04\x0e\x0e" and frame[4:8] == b"\x04\x10\x00\x02":
        features = bytearray(frame)
        features[9] &= ~0x04
        return features
    return frame


def send_frame(fd, frame):
    # Both character devices consume one complete HCI frame per write.
    if os.write(fd, frame) != len(frame):
        raise OSError(errno.EIO, "Short write to HCI transport")


def forward(stpbt, vhci):
    pending = bytearray()
    send_frame(vhci, b"\xff\x00")  # Create a primary HCI controller.
    while True:
        ready, _, failed = select.select([vhci, stpbt], [], [vhci, stpbt])
        if failed:
            raise OSError(errno.EIO, "HCI transport reported an error")
        for fd in ready:
            try:
                packet = os.read(fd, 65540)
            except BlockingIOError:
                continue
            if not packet:
                raise OSError(errno.EIO, "HCI transport closed")
            if fd == stpbt:
                pending.extend(packet)
                for frame in controller_frames(pending):
                    send_frame(vhci, controller_features(frame))
            elif len(packet) == 4 and packet[:2] == b"\xff\x00":
                print(f"Registered hci{int.from_bytes(packet[2:], 'little')}", flush=True)
            else:
                send_frame(stpbt, packet)


def stop(_signal, _frame):
    raise SystemExit(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stpbt", default="/dev/stpbt")
    parser.add_argument("--vhci", default="/dev/vhci")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, stop)
    with ExitStack() as devices:
        stpbt = os.open(args.stpbt, os.O_RDWR | os.O_NONBLOCK)
        devices.callback(os.close, stpbt)
        vhci = os.open(args.vhci, os.O_RDWR | os.O_NONBLOCK)
        devices.callback(os.close, vhci)
        forward(stpbt, vhci)


if __name__ == "__main__":
    main()
