# SPDX-License-Identifier: GPL-2.0-only
#
# Shikra secure SPL image generation.
#
# The XBL-sec image is provided to the SPL linker script as xbl_sec.elf.
# The generated spl/u-boot-spl.elf contains the XBL-sec type-5 PT_LOAD
# segment.

# The input name is selected by CONFIG_QCOM_XBL_SEC_ELF and is resolved in
# the source-tree root. Signing is intentionally performed outside the
# U-Boot build.
QCOM_SHIKRA_XBL_SEC_INPUT := $(srctree)/$(CONFIG_QCOM_XBL_SEC_ELF:"%"=%)

QCOM_SHIKRA_XBL_SEC_ELF := xbl_sec.elf

quiet_cmd_shikra_xbl_sec = XBLSEC   $@
      cmd_shikra_xbl_sec = cp $< $@

cmd_mksplmbn = in="$<"; [ -f spl/u-boot-spl.elf ] && in="spl/u-boot-spl.elf"; \
	$(CMD_MKMBN) -o spl/u-boot-spl.mbn -l $(CONFIG_SPL_TEXT_BASE) -s $$in

# Stage the configured XBL-sec image in the output directory before linking
# the SPL ELF.
$(QCOM_SHIKRA_XBL_SEC_ELF): $(QCOM_SHIKRA_XBL_SEC_INPUT) FORCE
	$(call if_changed,shikra_xbl_sec)

spl/u-boot-spl.elf: $(QCOM_SHIKRA_XBL_SEC_ELF)
spl/u-boot-spl.mbn: spl/u-boot-spl.elf
