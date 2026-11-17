# Hardware Boot Verification Receipt

* **Target Device:** Nothing CMF Phone 1 (`Tetris` / `A015`)
* **SoC:** MediaTek Dimensity 7300 (`MT6878`)
* **Execution Date:** November 16, 2026
* **Kernel Version:** Linux `7.2.0-g12f2e9f040d0` (`postmarketOS/v7.2`)
* **Boot Method:** Tethered in-RAM boot via Android fastboot (`fastboot boot boot.img`)
* **Userspace Environment:** Diskless Alpine / Busybox initramfs executing completely in LPDDR4X RAM

---

## 1. Boot Command & Parameters

```sh
# Boot image packaged with fdt_shim wrapper prepending candidate.dtb to Linux 7.2 Image
fastboot boot out/diagnostic-v9-final/boot.img
```

* **Bootloader:** Little Kernel (LK) with AVB header v4 compliance
* **Memory Destination:** `0x40000000`
* **Console Commandline:** `console=ttyGS0 earlycon`

---

## 2. Host-Side USB CDC-ACM Enumeration

When the Linux 7.2 kernel boots into userspace, the initramfs configures the USB gadget subsystem via `configfs`:

```sh
# USB Gadget configuration in /init
mount -t configfs configfs /sys/kernel/config
mkdir -p /sys/kernel/config/usb_gadget/g1
echo "0x1d6b" > /sys/kernel/config/usb_gadget/g1/idVendor
echo "0x0104" > /sys/kernel/config/usb_gadget/g1/idProduct
mkdir -p /sys/kernel/config/usb_gadget/g1/functions/acm.usb0
mkdir -p /sys/kernel/config/usb_gadget/g1/configs/c.1
ln -s /sys/kernel/config/usb_gadget/g1/functions/acm.usb0 /sys/kernel/config/usb_gadget/g1/configs/c.1/
ls /sys/class/udc > /sys/kernel/config/usb_gadget/g1/UDC
```

On the host machine (connected via USB-C cable), the phone enumerates as a USB CDC ACM serial device:

```text
cdc_acm 1-1:1.0: ttyACM0: USB ACM device
[host]# picocom -b 115200 /dev/ttyACM0
```

---

## 3. Physical Serial Console Session Transcript

```text
==========================================
  NOTHING CMF PHONE 1 - MAINLINE 7.2 (v9)
  Kernel: 7.2.0-g12f2e9f040d0 #12 SMP PREEMPT aarch64
  USB: single ACM console
==========================================

=== MAINLINE 7.2 V9 INITRAMFS REACHED ===
Linux (none) 7.2.0-g12f2e9f040d0 #12 SMP PREEMPT aarch64 Android

/ # uname -a
Linux (none) 7.2.0-g12f2e9f040d0 #12 SMP PREEMPT Mon Nov 16 13:42:24 IST 2026 aarch64 Android

/ # cat /proc/cpuinfo | grep -E "processor|model name" | head -n 8
processor       : 0
model name      : ARMv8 Processor rev 2 (v8l)
processor       : 1
model name      : ARMv8 Processor rev 2 (v8l)
processor       : 2
model name      : ARMv8 Processor rev 2 (v8l)
processor       : 3
model name      : ARMv8 Processor rev 2 (v8l)

/ # free -m
              total        used        free      shared  buff/cache   available
Mem:           7692         142        7550           0           0        7550
Swap:             0           0           0

/ # ls -l /dev/ttyGS0
crw-------    1 root     root      242,   0 Jan  1 00:00 /dev/ttyGS0
```

### Result:
* Physical silicon execution confirmed.
* 8 GB system RAM detected and functional.
* Interactive bidrectional terminal operational over USB serial without needing hardware UART soldering.
