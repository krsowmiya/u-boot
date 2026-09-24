# SPDX-License-Identifier: GPL-2.0+
#
# Shikra secure SPL image generation.
#
# The XBL-sec image is provided to the SPL linker script as xbl_sec.elf.
# The generated spl/u-boot-spl.elf contains the XBL-sec type-5 PT_LOAD
# segment.

# The input path is selected by CONFIG_QCOM_XBL_SEC_ELF. Signing settings
# remain make variables so build-environment details are not encoded in the
# board defconfig.
QCOM_SHIKRA_XBL_SEC_INPUT := $(CONFIG_QCOM_XBL_SEC_ELF:"%"=%)
QCOM_SHIKRA_SECURITY_PROFILE ?= $(srctree)/../qc_sec_add/shikra_security_profile.xml
QCOM_SHIKRA_SECTOOLS ?= /pkg/sectools/v2/latest/Linux/sectools
QCOM_SHIKRA_SIGNING_MODE ?= TEST

QCOM_SHIKRA_XBL_SEC_ELF := xbl_sec.elf
QCOM_SHIKRA_SIGNED_MBN := spl/spl_signed_local.mbn

quiet_cmd_shikra_xbl_sec = XBLSEC   $@
      cmd_shikra_xbl_sec = cp $< $@

# Stage the configured XBL-sec image in the output directory before linking
# the SPL ELF.
$(QCOM_SHIKRA_XBL_SEC_ELF): $(QCOM_SHIKRA_XBL_SEC_INPUT) FORCE
	$(call if_changed,shikra_xbl_sec)

spl/u-boot-spl.elf: $(QCOM_SHIKRA_XBL_SEC_ELF)

quiet_cmd_shikra_sign_xbl = XBL SIGN $@
      cmd_shikra_sign_xbl = $(QCOM_SHIKRA_SECTOOLS) secure-image $< \
	--outfile $@ \
	--security-profile $(QCOM_SHIKRA_SECURITY_PROFILE) \
	--image-id XBL \
	--sign \
	--signing-mode $(QCOM_SHIKRA_SIGNING_MODE)

$(QCOM_SHIKRA_SIGNED_MBN): spl/u-boot-spl.elf \
		$(QCOM_SHIKRA_SECURITY_PROFILE) FORCE
	$(call if_changed,shikra_sign_xbl)

# Build the signed image as part of the default top-level build, after the
# SPL ELF has been linked. Avoid adding this dependency to the recursive SPL
# build.
ifeq ($(CONFIG_SPL_BUILD),)
all: $(QCOM_SHIKRA_SIGNED_MBN)
endif
