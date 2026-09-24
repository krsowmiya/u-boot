# QC_SEC + SPL Image Signing (Shikra)

Packs the `qc_sec` (XBL_SEC) blob into a U-Boot SPL ELF and signs it into a
flashable `XBL` `.mbn`.

## Steps

1. Copy this folder anywhere.
2. Copy your board's `u-boot-spl.wrap-elf` into this folder.
3. Run:

   ```bash
   ./build_and_sign_spl.sh
   ```

Output: `out/spl_signed_local.mbn`.

Override defaults via env vars: `SPL_ELF`, `QC_SEC_MBN`,
`SECURITY_PROFILE`, `SECTOOLS`, `OUT_DIR`, `SIGNING_MODE`, `ARCH`.

`SIGNING_MODE=TEST` (default) is insecure/dev-only. Use `LOCAL` (with your
own keys) or `CASS`/`QTI-REMOTE` for production signing.
