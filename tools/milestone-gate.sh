#!/usr/bin/env bash
# milestone-gate.sh: Automated Milestone Closing Gate verification for cmf-mainline
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

ERRORS=0

echo -e "${BLUE}=== CMF Phone 1 Mainline: Milestone Closing Gate ===${NC}"
echo "Running pre-release & audit validation..."
echo ""

# Gate 1: License & Provenance Verification
echo -n "[1/6] Checking LICENSE and PROVENANCE... "
if [ -s "LICENSE" ] && [ -s "docs/PROVENANCE.md" ]; then
    echo -e "${GREEN}PASS${NC}"
else
    echo -e "${RED}FAIL${NC} (Missing LICENSE or docs/PROVENANCE.md)"
    ERRORS=$((ERRORS + 1))
fi

# Gate 2: Gitignore Hygiene
echo -n "[2/6] Verifying .gitignore rules... "
if grep -q "!evidence" .gitignore && grep -q "\*.ko" .gitignore; then
    echo -e "${GREEN}PASS${NC} (evidence preserved, binary artifacts ignored)"
else
    echo -e "${RED}FAIL${NC} (.gitignore must explicitly whitelist !evidence and blacklist binaries)"
    ERRORS=$((ERRORS + 1))
fi

# Gate 3: Evidence Receipts Audit
echo -n "[3/6] Auditing evidence receipts... "
REQUIRED_RECEIPTS=(
    "evidence/hardware-boot-receipt.md"
    "evidence/kernel-boot-build-receipt.txt"
    "evidence/independent-audit-receipt.json"
    "evidence/wlan-build-step20-receipt.txt"
    "evidence/bt-build-step12-receipt.txt"
    "evidence/v13-module-hashes-receipt.txt"
    "evidence/eint-verified-receipt.txt"
    "evidence/btf-verified-receipt.txt"
    "evidence/qemu-console-receipt.txt"
)
MISSING_RECEIPTS=0
for r in "${REQUIRED_RECEIPTS[@]}"; do
    if [ ! -s "$r" ]; then
        echo -e "\n  ${RED}Missing receipt:${NC} $r"
        MISSING_RECEIPTS=$((MISSING_RECEIPTS + 1))
    fi
done
if [ $MISSING_RECEIPTS -eq 0 ]; then
    echo -e "${GREEN}PASS${NC} (${#REQUIRED_RECEIPTS[@]} verified receipts present)"
else
    ERRORS=$((ERRORS + 1))
fi

# Gate 4: Large Binary File Check (>20MB)
echo -n "[4/6] Scanning for accidental large binaries in git index... "
LARGE_FILES=$(git ls-files | while read -r file; do
    if [ -f "$file" ]; then
        size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null || echo 0)
        if [ "$size" -gt 20971520 ]; then # 20 MB
            echo "$file ($((size / 1048576)) MB)"
        fi
    fi
done)

if [ -z "$LARGE_FILES" ]; then
    echo -e "${GREEN}PASS${NC} (No large binaries detected)"
else
    echo -e "${RED}FAIL${NC} (Found large binaries in git: $LARGE_FILES)"
    ERRORS=$((ERRORS + 1))
fi

# Gate 5: Git Working Tree Cleanliness
echo -n "[5/6] Checking git working tree... "
if [ -z "$(git status --porcelain)" ]; then
    echo -e "${GREEN}PASS${NC} (Clean working tree)"
else
    echo -e "${YELLOW}WARN${NC} (Uncommitted changes exist in working tree)"
fi

# Gate 6: Upstream Tracking Verification
echo -n "[6/6] Checking upstream branch tracking... "
UPSTREAM=$(git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null || true)
if [ -n "$UPSTREAM" ]; then
    echo -e "${GREEN}PASS${NC} (Tracking $UPSTREAM)"
else
    echo -e "${YELLOW}WARN${NC} (No upstream tracking set; run 'git push -u origin <branch>')"
fi

echo ""
if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}=== MILESTONE CLOSING GATE: PASSED ===${NC}"
    echo "Ready for public release and upstream sync."
    exit 0
else
    echo -e "${RED}=== MILESTONE CLOSING GATE: FAILED ($ERRORS errors) ===${NC}"
    echo "Please resolve the above failures before closing this milestone."
    exit 1
fi
