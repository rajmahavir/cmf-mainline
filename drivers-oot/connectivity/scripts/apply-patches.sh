#!/usr/bin/env bash
#
# Apply the CMF Phone 1 (MT6878) patch set to a pristine kernel tree and a
# pristine Nothing BSP copy.
#
# Every patch has its own root; that is the detail that breaks hand-written
# apply loops. See patches/APPLY-STATUS.md for the verified matrix.
#
#   CMF_KERNEL=~/cmf-build/linux CMF_BSP=~/cmf-vendor-bsp \
#     scripts/apply-patches.sh --check-only
#
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PATCH_DIR=$REPO_ROOT/patches
KERNEL=${CMF_KERNEL:-$HOME/mt6878-mainline/linux}
BSP=${CMF_BSP:-$HOME/cmf-vendor-bsp}
KERNEL_BASE=${CMF_KERNEL_BASE:-12f2e9f040d0049f3f5ce5a7e53bf5ef27b46422}

CHECK_ONLY=0
DO_KERNEL=1
DO_VENDOR=1
ALLOW_DIRTY=0

usage() {
    cat <<'EOF'
Usage: apply-patches.sh [options]

  --check-only      verify every patch, change nothing
  --kernel-only     skip the vendor connectivity patches
  --vendor-only     skip the kernel patches
  --allow-dirty     do not abort when the kernel tree has local modifications
  --kernel PATH     kernel source (default $CMF_KERNEL or ~/cmf-build/linux)
  --bsp PATH        BSP root (default $CMF_BSP or ~/cmf-vendor-bsp)
  -h, --help        this text

Kernel patches require a tree at the base commit recorded in CMF_KERNEL_BASE.
Vendor patches require an unmodified BSP; check the fingerprint in
docs/PROVENANCE.md first.
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --check-only) CHECK_ONLY=1 ;;
        --kernel-only) DO_VENDOR=0 ;;
        --vendor-only) DO_KERNEL=0 ;;
        --allow-dirty) ALLOW_DIRTY=1 ;;
        --kernel) KERNEL=$2; shift ;;
        --bsp) BSP=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

# name : root-kind : plevel
PATCHES=(
    "mt6878-eint-index.patch|kernel|1"
    "mt6878-wifi-pmic.patch|kernel|1"
    "mt6878-connectivity-power-domain.patch|kernel|1"
    "mt6878-wifi-linux-7.2.patch|bsp|1"
    "mt6878-wifi-module-probe.patch|connectivity|1"
    "mt6878-bt-calibration-mainline.patch|bt66xx|1"
)

root_for() {
    case $1 in
        kernel)       printf '%s\n' "$KERNEL" ;;
        bsp)          printf '%s\n' "$BSP" ;;
        connectivity) printf '%s\n' "$BSP/vendor/mediatek/kernel_modules/connectivity" ;;
        bt66xx)       printf '%s\n' "$BSP/vendor/mediatek/kernel_modules/connectivity/bt/mt66xx" ;;
    esac
}

fail=0
note() { printf '%-40s %s\n' "$1" "$2"; }

echo "== integrity =="
if [[ -f $PATCH_DIR/MANIFEST.sha256 ]]; then
    if (cd "$PATCH_DIR" && sha256sum -c --quiet MANIFEST.sha256); then
        note "patches/MANIFEST.sha256" "OK"
    else
        note "patches/MANIFEST.sha256" "MISMATCH - refusing to apply"
        exit 1
    fi
else
    note "patches/MANIFEST.sha256" "missing (run scripts/export-patches.sh)"
fi

echo
echo "== kernel tree: $KERNEL =="
if [[ $DO_KERNEL == 1 ]]; then
    if [[ ! -d $KERNEL/.git && ! -f $KERNEL/.git ]]; then
        note "$KERNEL" "not a git repository" ; exit 1
    fi
    head_now=$(git -C "$KERNEL" rev-parse HEAD)
    if [[ $head_now != "$KERNEL_BASE" ]]; then
        note "HEAD" "$head_now (expected $KERNEL_BASE)"
        [[ $ALLOW_DIRTY == 1 ]] || { echo "refusing: pass --allow-dirty to try anyway" >&2; exit 1; }
    else
        note "HEAD" "$KERNEL_BASE"
    fi
    if [[ -n $(git -C "$KERNEL" status --porcelain) && $ALLOW_DIRTY != 1 ]]; then
        note "worktree" "has local modifications - use a pristine checkout or --allow-dirty"
        exit 1
    fi
fi

echo
echo "== applying =="
for entry in "${PATCHES[@]}"; do
    name=${entry%%|*}
    rest=${entry#*|}
    kind=${rest%%|*}
    plevel=${rest##*|}
    case $kind in
        kernel) [[ $DO_KERNEL == 1 ]] || continue ;;
        *)      [[ $DO_VENDOR == 1 ]] || continue ;;
    esac
    root=$(root_for "$kind")
    file=$PATCH_DIR/$name
    if [[ ! -f $file ]]; then note "$name" "patch file missing"; fail=1; continue; fi
    if [[ ! -d $root ]]; then note "$name" "root missing: $root"; fail=1; continue; fi

    if [[ $kind == kernel ]]; then
        if git -C "$root" apply --check -p"$plevel" "$file" 2>/dev/null; then
            verdict=applies
        elif git -C "$root" apply --reverse --check -p"$plevel" "$file" 2>/dev/null; then
            verdict="already applied (tree is not pristine)"
            note "$name" "$verdict"
            continue
        else
            verdict=FAILS
        fi
        if [[ $CHECK_ONLY == 0 && $verdict == applies ]]; then
            if git -C "$root" apply -p"$plevel" "$file"; then
                verdict="applied"
            else
                verdict="FAILED to apply"
                fail=1
            fi
        fi
    else
        if (cd "$root" && patch -p"$plevel" --dry-run -s -f -i "$file") >/dev/null 2>&1; then
            verdict=applies
        elif (cd "$root" && patch -p"$plevel" --dry-run -s -f -R -i "$file") >/dev/null 2>&1; then
            verdict="already applied (BSP not pristine)"
            note "$name" "$verdict"
            continue
        else
            verdict=FAILS
        fi
        if [[ $CHECK_ONLY == 0 && $verdict == applies ]]; then
            if (cd "$root" && patch -p"$plevel" -s -f -i "$file"); then
                verdict="applied"
            else
                verdict="FAILED to apply"
                fail=1
            fi
        fi
    fi

    note "$name [$kind -p$plevel]" "$verdict"
    if [[ $verdict == "FAILS" ]]; then
        fail=1
    fi
done

echo
if [[ $fail == 1 ]]; then
    echo "result: FAILED - see patches/APPLY-STATUS.md for the expected matrix" >&2
    exit 1
fi
echo "result: OK$([[ $CHECK_ONLY == 1 ]] && echo ' (check only, nothing written)')"
