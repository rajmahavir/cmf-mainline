# Patch application status

Every patch in this directory was re-verified against the exact trees it is
supposed to apply to. Verification was performed on 2026-11-18 (IST) on the
build host with `git apply --check` (kernel tree) and `patch --dry-run`
(vendor tree). Nothing here is a claim from memory: the commands and the
results are recorded below.

## Kernel-side patches

Target: pristine `MT6878-Mainline/linux` at `postmarketOS/v7.2` =
`12f2e9f040d0049f3f5ce5a7e53bf5ef27b46422`, applied from the kernel root with
`-p1`, cumulatively in the order listed.

| Order | Patch | Result |
| --- | --- | --- |
| 1 | `mt6878-eint-index.patch` | applies |
| 2 | `mt6878-wifi-pmic.patch` | applies |
| 3 | `mt6878-connectivity-power-domain.patch` | applies |

Cumulative check (all three in one `git apply`) also passes.

Reproduce:

```sh
git clone --branch postmarketOS/v7.2 https://github.com/MT6878-Mainline/linux.git k
git -C k checkout 12f2e9f040d0049f3f5ce5a7e53bf5ef27b46422
for p in mt6878-eint-index mt6878-wifi-pmic mt6878-connectivity-power-domain; do
  git -C k apply --check -p1 /path/to/repo/patches/$p.patch && echo "$p OK"
done
```

## Vendor connectivity patches

Target: an unpacked Nothing CMF Phone 1 BSP (the tree these were developed
against is referred to throughout the docs as the BSP root). Each patch has a
**different root**; applying all of them from one directory does not work.

| Order | Patch | Apply from | Strip | Result |
| --- | --- | --- | --- | --- |
| 4 | `mt6878-wifi-linux-7.2.patch` | BSP root | `-p1` | applies |
| 5 | `mt6878-wifi-module-probe.patch` | `vendor/mediatek/kernel_modules/connectivity` | `-p1` | applies |
| 6 | `mt6878-bt-calibration-mainline.patch` | `vendor/mediatek/kernel_modules/connectivity/bt/mt66xx` | `-p1` | applies |

Cumulative application in that order produced `74 files changed,
545 insertions(+), 470 deletions(-)` on a staged copy of the connectivity
tree.

Use `scripts/apply-patches.sh`, which encodes these roots instead of asking
you to remember them:

```sh
CMF_KERNEL=~/cmf-build/linux CMF_BSP=~/cmf-vendor-bsp \
  scripts/apply-patches.sh --check-only   # remove --check-only to apply
```

## Two things that are easy to get wrong

1. **A dirty tree fails the check and looks like a broken patch.** `git apply`
   and `patch` compare against the *working* tree, so a tree that already
   carries Build #8 will reject `mt6878-wifi-pmic.patch` and
   `mt6878-connectivity-power-domain.patch` with context errors. `git apply
   --check` is not lying about the patch; it is telling you the tree is not
   pristine. `scripts/apply-patches.sh` detects this case and reports
   `already applied` separately (via `git apply --reverse --check`).
2. **The vendor BSP must be unmodified.** The patches were not generated
   against a BSP that was edited in place; check with
   `git -C <bsp>/vendor/.../connectivity status` if your BSP copy is under
   version control, or verify the aggregate fingerprint in
   [`docs/PROVENANCE.md`](../docs/PROVENANCE.md) before applying.

Integrity of the patch files themselves:
[`patches/MANIFEST.sha256`](./MANIFEST.sha256).
