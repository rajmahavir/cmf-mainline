# Security and privacy

## Never commit
| Class | Examples |
|---|---|
| Credentials | Wi-Fi passphrases, NetworkManager connection files, SSH private keys, Tailscale state, AVB/module signing keys |
| Device-unique data | partition dumps, NVRAM/calibration (`nvdata`, `nvcfg`, `protect*`, `proinfo`), IMEI, serial numbers, MAC addresses |
| Personal network details | home IPs, SSIDs, host names, user names |
| Binaries | boot images, kernel modules, firmware, `area.bin` (belongs to one phone's reservation) |

`.gitignore` blocks the common file types. Before pushing, check:
```sh
git ls-files | grep -E '\.(img|ko|pem|key|cpio|tar\.gz)$|area\.(bin|json)'   # must print nothing
```

## Things to know about running this port
- The USB serial console gives an **unauthenticated root shell** to anything plugged into the phone's USB port (by design, for recovery). Disable `serial-getty@ttyGS0` and `cmf-emergency-serial` if that matters for you.
- A self-signed `boot_a` means verified boot is in "orange" (unlocked) state: the bootloader does not protect the kernel from tampering.
- The internal Linux root is **not encrypted**. Android cannot read it, but anyone with physical access and an unlocked bootloader can.

## Reporting
Open a GitHub issue for bugs. For something sensitive, use GitHub's private vulnerability reporting on this repository.
