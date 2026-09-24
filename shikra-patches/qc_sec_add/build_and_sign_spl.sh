#!/bin/bash
set -e

# ---- Paths (override via environment) ----
SPL_ELF="${SPL_ELF:-./u-boot-spl.wrap-elf}"
QC_SEC_MBN="${QC_SEC_MBN:-./qc_sec_local_signed.mbn}"
SECURITY_PROFILE="${SECURITY_PROFILE:-./shikra_security_profile.xml}"
SECTOOLS="${SECTOOLS:-/pkg/sectools/v2/latest/Linux/sectools}"
OUT_DIR="${OUT_DIR:-./out}"
SIGNING_MODE="${SIGNING_MODE:-TEST}"
ARCH="${ARCH:-64}"

if [ ! -f "$SPL_ELF" ]; then
    echo "ERROR: SPL ELF not found: $SPL_ELF" >&2
    echo "Copy your board's u-boot-spl.wrap-elf here, or set SPL_ELF=<path>." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# ---- 1. Pack qc_sec into the SPL ELF ----
python3 pack_xbl_sec_standalone.py \
    -x "$QC_SEC_MBN" \
    -e "$SPL_ELF" \
    -o "$OUT_DIR/spl_shikra_local_signed.elf" \
    -a "$ARCH"

# ---- 2. Sign the combined ELF into a flashable XBL mbn ----
"$SECTOOLS" secure-image "$OUT_DIR/spl_shikra_local_signed.elf" \
    --outfile "$OUT_DIR/spl_signed_local.mbn" \
    --security-profile "$SECURITY_PROFILE" \
    --image-id XBL \
    --sign \
    --signing-mode "$SIGNING_MODE"

echo "Done. Output: $OUT_DIR/spl_signed_local.mbn"
