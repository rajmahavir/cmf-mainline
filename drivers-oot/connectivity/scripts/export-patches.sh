#!/usr/bin/env bash
#
# Regenerate the kernel-side patches in patches/ from a live kernel worktree.
#
# The published patches are hand-exported snapshots. Snapshots rot: a fix made
# in the build tree at 19:30 is not in a patch exported at 07:45, and nothing
# tells you. This script re-exports them from the tree that actually built the
# image you flashed, using a temporary git index so your worktree and real
# index are untouched.
#
#   scripts/export-patches.sh --kernel-src ~/cmf-build/linux --out patches
#   scripts/export-patches.sh --kernel-src ... --vendor-patches   # also re-export vendor set
#
set -euo pipefail

KERNEL_SRC=${CMF_KERNEL:-$HOME/mt6878-mainline/linux}
OUT=patches
DO_VENDOR=0
BSP=${CMF_BSP:-$HOME/cmf-vendor-bsp}
BSP_PATCHED=${CMF_BSP_PATCHED:-}

usage() {
    cat <<'EOF'
Usage: export-patches.sh [options]

  --kernel-src PATH   kernel worktree holding the built state (default $CMF_KERNEL)
  --out DIR           output directory (default patches)
  --base REV          base commit to diff against (default HEAD)
  --vendor-patches    additionally re-export the three vendor patches; needs
                      --bsp-pristine DIR (unmodified BSP) and CMF_BSP_PATCHED,
                      which defaults to --bsp-pristine's connectivity copy under
                      the current build tree
  --bsp DIR           BSP root used for the fingerprint (default $CMF_BSP)
  --verify            run scripts/apply-patches.sh --check-only afterwards
  -h, --help          this text

The three kernel patches are defined by their path sets, so re-export keeps the
same split:

  mt6878-eint-index.patch                  drivers/pinctrl/mediatek/pinctrl-mt6878.c
                                           drivers/pinctrl/mediatek/pinctrl-mtk-mt6878.h
  mt6878-wifi-pmic.patch                   drivers/regulator/{Kconfig,Makefile,mt636x-rf-regulator.c}
                                           drivers/spmi/spmi-mtk-pmif.c
  mt6878-connectivity-power-domain.patch   drivers/pmdomain/mediatek/mtk-pm-domains.c
                                           drivers/pmdomain/mediatek/mt6878-pm-domains.h
                                           include/dt-bindings/power/mediatek,mt6878-power.h

Edit PATHS_* below if you move code between subsystems.
EOF
}

BASE=HEAD
VERIFY=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --kernel-src) KERNEL_SRC=$2; shift ;;
        --out) OUT=$2; shift ;;
        --base) BASE=$2; shift ;;
        --vendor-patches) DO_VENDOR=1 ;;
        --bsp) BSP=$2; shift ;;
        --bsp-pristine) BSP_PATCHED=$2; shift ;;
        --verify) VERIFY=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

[[ -d $KERNEL_SRC ]] || { echo "kernel source not found: $KERNEL_SRC" >&2; exit 1; }
mkdir -p "$OUT"

PATHS_EINT=(drivers/pinctrl/mediatek/pinctrl-mt6878.c drivers/pinctrl/mediatek/pinctrl-mtk-mt6878.h)
PATHS_PMIC=(drivers/regulator/Kconfig drivers/regulator/Makefile drivers/regulator/mt636x-rf-regulator.c drivers/spmi/spmi-mtk-pmif.c)
PATHS_PWR=(drivers/pmdomain/mediatek/mtk-pm-domains.c drivers/pmdomain/mediatek/mt6878-pm-domains.h include/dt-bindings/power/mediatek,mt6878-power.h)

export_patch() {
    # $1 = output name, rest = paths. Untracked new files are included by
    # building an intent-to-add index in $TMPIDX (the real index is not used).
    local name=$1; shift
    local idx; idx=$(mktemp)
    rm -f "$idx"
    GIT_INDEX_FILE=$idx git -C "$KERNEL_SRC" read-tree "$BASE"
    GIT_INDEX_FILE=$idx git -C "$KERNEL_SRC" add -N -- "$@" >/dev/null 2>&1 || true
    GIT_INDEX_FILE=$idx git -C "$KERNEL_SRC" diff --no-color --binary -- "$@" > "$OUT/$name"
    rm -f "$idx"
    if [[ -s $OUT/$name ]]; then
        printf '%-46s %s lines\n' "$name" "$(wc -l < "$OUT/$name")"
    else
        printf '%-46s EMPTY - no changes vs %s for those paths\n' "$name" "$BASE"
        rm -f "$OUT/$name"
    fi
}

echo "== kernel patches from $KERNEL_SRC (base $BASE) =="
export_patch mt6878-eint-index.patch                     "${PATHS_EINT[@]}"
export_patch mt6878-wifi-pmic.patch                      "${PATHS_PMIC[@]}"
export_patch mt6878-connectivity-power-domain.patch      "${PATHS_PWR[@]}"

if [[ $DO_VENDOR == 1 ]]; then
    CONN_REL=vendor/mediatek/kernel_modules/connectivity
    PRISTINE=${BSP_PATCHED:-}
    [[ -n $PRISTINE && -d $PRISTINE ]] || {
        echo
        echo "vendor re-export needs a pristine BSP copy:" >&2
        echo "  --bsp-pristine /path/to/unmodified/cmf-vendor-bsp" >&2
        echo "and CMF_BSP_PATCHED pointing at the BSP copy that carries the port." >&2
        exit 1
    }
    echo
    echo "== vendor patches (diff -ru against pristine) =="
    diff -ruN --no-dereference "$PRISTINE/$CONN_REL" "$BSP/$CONN_REL" \
        | sed "s|$PRISTINE/||; s|$BSP/||" > "$OUT/mt6878-wifi-linux-7.2.patch" || true
    printf '%-46s %s lines\n' mt6878-wifi-linux-7.2.patch "$(wc -l < "$OUT/mt6878-wifi-linux-7.2.patch")"
fi

echo
echo "== manifest =="
(cd "$OUT" && sha256sum *.patch > MANIFEST.sha256 && cat MANIFEST.sha256)

if [[ $VERIFY == 1 ]]; then
    echo
    echo "== verifying exported patches against a pristine tree =="
    echo "set CMF_KERNEL to a clean checkout at the base commit, then run:"
    echo "  scripts/apply-patches.sh --check-only"
fi

echo
echo "Next: commit the regenerated patches together with the build number they"
echo "correspond to, and record the kernel vermagic in the commit message."
