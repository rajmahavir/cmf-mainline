# Hardware Boot Verification Receipt

* **Target Device:** Nothing CMF Phone 1 (`Tetris` / `A015`)
* **SoC:** MediaTek Dimensity 7300 (`MT6878`)
* **Execution Date:** November 16, 2026
* **Kernel Version:** Linux `7.2.0-g12f2e9f040d0` (`postmarketOS/v7.2`)
* **Boot Method:** Bootloader Fastboot flash of the diagnostic image to `boot_a`
* **Userspace Environment:** Diskless Alpine / Busybox initramfs executing completely in LPDDR4X RAM

---

## 1. Boot Command & Parameters

```sh
# Diagnostic boot image with the fdt_shim wrapper and candidate device tree
fastboot flash boot_a out/diagnostic-v9-final/boot.img
fastboot reboot
```

The capability probe returned `FAILED (remote: 'unknown command')` for
`fastboot boot`. The v9 test wrote the diagnostic to `boot_a`. After the test,
the saved `boot_a` image was restored and Android completed boot on slot A.

* **Bootloader:** Little Kernel (LK) with Android boot image header v4 support
* **Memory Destination:** `0x40000000`
* **Console Command Line:** `console=tty0`. The initramfs opens the USB serial shell on `/dev/ttyGS0`.

---

## 2. USB CDC ACM enumeration

The initramfs configured one CDC ACM function through configfs. The shell
ran on the phone's `/dev/ttyGS0`. The test host opened
`/dev/cu.usbmodem01234567891` on macOS.

## 3. Physical serial console output

```text
[    2.288013] === MAINLINE 7.2 V9 INITRAMFS REACHED ===
[    2.313395] initramfs: ACM console ready on /dev/ttyGS0
=== CMF mainline v9 shell — type commands ===
CMF_V9_SHELL_OK
7.2.0-g12f2e9f040d0
uid=0(root) gid=0(root)
CMF_V9_SHELL_DONE
```

### Result:
* Physical silicon execution confirmed.
* Interactive bidirectional terminal operational over USB serial without needing hardware UART soldering.
