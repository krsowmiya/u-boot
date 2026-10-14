// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <blk.h>
#include <clk.h>
#include <cpu_func.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>
#include <dm/uclass.h>
#include <init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <malloc.h>
#include <mach/qclib.h>
#include <mach/spl.h>
#include <mmc.h>
#include <part.h>
#include <spl.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_SPL_BUILD)
/*
 * The SPL device tree does not describe the complete early physical layout,
 * so use Shikra's fixed map when enabling the MMU.
 */
#define SHIKRA_SPL_DEVICE_REG_LOW_BASE		0x00000000UL
#define SHIKRA_SPL_SYSTEM_IMEM_BASE		0x0c100000UL
#define SHIKRA_SPL_SYSTEM_IMEM_SIZE		0x00020000UL
#define SHIKRA_SPL_BOOTIMEM_BASE		0x0c200000UL
#define SHIKRA_SPL_BOOTIMEM_SIZE		0x001c0000UL
#define SHIKRA_SPL_DEVICE_REG_HIGH_BASE	(SHIKRA_SPL_BOOTIMEM_BASE + \
						 SHIKRA_SPL_BOOTIMEM_SIZE)
#define SHIKRA_SPL_DDR_BASE			0x80000000UL
#define SHIKRA_SPL_DDR_SIZE			0x80000000UL
#define SHIKRA_SPL_DDR2_BASE			0x880000000ULL
#define SHIKRA_SPL_DDR2_SIZE			(SZ_32G - SZ_2G)
#define SHIKRA_SPL_DEVICE_REG_LOW_SIZE		(SHIKRA_SPL_SYSTEM_IMEM_BASE - \
						 SHIKRA_SPL_DEVICE_REG_LOW_BASE)
#define SHIKRA_SPL_DEVICE_REG_HIGH_SIZE	(SHIKRA_SPL_DDR_BASE - \
						 SHIKRA_SPL_DEVICE_REG_HIGH_BASE)

static struct mm_region shikra_spl_mem_map[] = {
	{
		.virt = SHIKRA_SPL_DEVICE_REG_LOW_BASE,
		.phys = SHIKRA_SPL_DEVICE_REG_LOW_BASE,
		.size = SHIKRA_SPL_DEVICE_REG_LOW_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE,
	}, {
		.virt = SHIKRA_SPL_SYSTEM_IMEM_BASE,
		.phys = SHIKRA_SPL_SYSTEM_IMEM_BASE,
		.size = SHIKRA_SPL_SYSTEM_IMEM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		/* SPL and QCLIB execute directly from BOOT_IMEM. */
		.virt = SHIKRA_SPL_BOOTIMEM_BASE,
		.phys = SHIKRA_SPL_BOOTIMEM_BASE,
		.size = SHIKRA_SPL_BOOTIMEM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		.virt = SHIKRA_SPL_DEVICE_REG_HIGH_BASE,
		.phys = SHIKRA_SPL_DEVICE_REG_HIGH_BASE,
		.size = SHIKRA_SPL_DEVICE_REG_HIGH_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE,
	}, {
		.virt = SHIKRA_SPL_DDR_BASE,
		.phys = SHIKRA_SPL_DDR_BASE,
		.size = SHIKRA_SPL_DDR_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL_NC) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		.virt = SHIKRA_SPL_DDR2_BASE,
		.phys = SHIKRA_SPL_DDR2_BASE,
		.size = SHIKRA_SPL_DDR2_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL_NC) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		0,
	}
};

struct mm_region *qcom_spl_mem_map(void)
{
	return shikra_spl_mem_map;
}

int arm_reserve_mmu(void)
{
#if !(CONFIG_IS_ENABLED(SYS_ICACHE_OFF) && CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
	mem_map = qcom_spl_mem_map();
	gd->arch.tlb_size = PGTABLE_SIZE;
	gd->arch.tlb_addr = (unsigned long)memalign(SZ_64K, gd->arch.tlb_size);
	if (!gd->arch.tlb_addr)
		return -ENOMEM;
#endif

	return 0;
}
#endif

#define IPQ_SPL_BOOTCFG_REG_ADDR	0x1B46070
#define IPQ_SPL_BOOTCFG_DEV_MASK	GENMASK(5, 1)
#define IPQ_SPL_BOOTCFG_DEV_SHFT	0x1

#define SHIKRA_DDR_INFORMATION		"ddr_information"
#define SHIKRA_DDR_TRAINING_DATA	"ddr_training_data"
#define SHIKRA_DDR_SR_EXIT		"ddr_sr_exit"
#define SHIKRA_PMIC_COCOS		"pmic_cocos"
#define SHIKRA_PMIC_WAILUA		"pmic_wailua"
#define SHIKRA_DCB_LP4			"dcb_lp4"
#define SHIKRA_DCB_LP5			"dcb_lp5"
#define SHIKRA_ECC			"ecc"

#define SHIKRA_DDR_TRAINING_DATA_ADDR	0x0c368000
#define SHIKRA_DDR_TRAINING_DATA_SIZE	0x8000
#define SHIKRA_PMIC_COCOS_SIZE		0xbc00
#define SHIKRA_PMIC_WAILUA_SIZE	0x6000
#define SHIKRA_DCB_LP4_SIZE		0x4800
#define SHIKRA_DCB_LP5_SIZE		0x4800
#define SHIKRA_ECC_SIZE			0x400

/* DDR training-data storage partition name */
#define SHIKRA_DDR_TRAINING_PARTITION	"ddr"

#define SHIKRA_SHRM2_SPROC_CTRL_ADDR			0x0c36000
#define SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK	BIT(1)
#define SHIKRA_SHRM2_SPROC_CTRL_DEBUG_SW_RESET_BMSK	BIT(3)

/* GCC_APCS_MISC contains the RPM reset control bit. */
#define SHIKRA_GCC_APCS_MISC_ADDR			0x01491000
#define SHIKRA_GCC_APCS_MISC_RPM_RESET_BMSK		BIT(0)

#define SHIKRA_RPM_STAGING_ADDR			0x04690000
#define SHIKRA_RPM_STAGING_SIZE			0x1363c

#define QCOM_SPL_FIT_IMG_PARTITION	"uefi_a"

enum {
	IPQ_SPL_BOOTCFG_DEV_MMC = 0x0,
	IPQ_SPL_BOOTCFG_DEV_SD_EMMC	= 0x1,
	IPQ_SPL_BOOTCFG_DEV_EDL		= 0x2,
	IPQ_SPL_BOOTCFG_DEV_NAND	= 0x3,
	IPQ_SPL_BOOTCFG_DEV_EMMC_SD		= 0x4,
	IPQ_SPL_BOOTCFG_DEV_MAX
};

#if defined(CONFIG_SPL_BUILD)
/**
 * shikra_prepare_qup_clocks() - Enable QUP wrapper clocks.
 *
 * Enable wrapper clocks before detecting the QUP firmware state.
 *
 * Return: 0 on success, or a negative error code.
 */
static int shikra_prepare_qup_clocks(void)
{
	struct clk_bulk clks;
	struct uclass *uc;
	struct udevice *dev;
	int ret;

	ret = uclass_get(UCLASS_MISC, &uc);
	if (ret)
		return ret;

	uclass_foreach_dev(dev, uc) {
		if (!device_is_compatible(dev, "qcom,geni-se-qup"))
			continue;

		ret = clk_get_bulk(dev, &clks);
		if (ret)
			return ret;

		ret = clk_enable_bulk(&clks);
		if (ret)
			clk_release_bulk(&clks);

		return ret;
	}

	return -ENODEV;
}

/**
 * shikra_prepare_boot_mmc() - Initialize the boot MMC device.
 *
 * Make the boot MMC device available before loading QUP firmware.
 *
 * Return: 0 on success, or a negative error code.
 */
static int shikra_prepare_boot_mmc(void)
{
	struct udevice *dev;
	struct mmc *mmc;
	u8 boot_device_cfg;
	int seq, ret;

	boot_device_cfg =
		(readl(IPQ_SPL_BOOTCFG_REG_ADDR) & IPQ_SPL_BOOTCFG_DEV_MASK) >>
		IPQ_SPL_BOOTCFG_DEV_SHFT;

	switch (boot_device_cfg) {
	case IPQ_SPL_BOOTCFG_DEV_MMC:
		seq = 0;
		break;
	case IPQ_SPL_BOOTCFG_DEV_SD_EMMC:
		seq = 1;
		break;
	default:
		return 0;
	}

	ret = uclass_get_device_by_seq(UCLASS_MMC, seq, &dev);
	if (ret)
		return ret;

	mmc = mmc_get_mmc_dev(dev);
	if (!mmc)
		return -ENODEV;

	return mmc_init(mmc);
}

/**
 * shikra_release_qupfw() - Free the QUP firmware buffer.
 *
 * Free the dynamic QUPFW buffer before allocating the MMU table.
 */
static void shikra_release_qupfw(void)
{
#if !IS_ENABLED(CONFIG_QCOM_GENI_MINICORE)
	struct uclass *uc;
	struct udevice *dev;
	void *fw;

	if (uclass_get(UCLASS_MISC, &uc))
		return;

	uclass_foreach_dev(dev, uc) {
		if (!device_is_compatible(dev, "qcom,geni-se-qup"))
			continue;

		fw = dev_get_priv(dev);
		if (fw)
			free(fw);
		return;
	}
#endif
}

void board_init_f(ulong dummy)
{
	int ret = 0;

	memset(__bss_start, 0, __bss_end - __bss_start);

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	ret = shikra_prepare_qup_clocks();
	if (ret) {
		pr_debug("failed to prepare QUP clocks (%d)\n", ret);
		goto fail;
	}

	ret = shikra_prepare_boot_mmc();
	if (ret) {
		pr_debug("failed to prepare boot MMC (%d)\n", ret);
		goto fail;
	}

	event_notify_null(EVT_LAST_STAGE_INIT);
	shikra_release_qupfw();

	preloader_console_init();

#if !(CONFIG_IS_ENABLED(SYS_ICACHE_OFF) && CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
	ret = arm_reserve_mmu();
	if (ret) {
		pr_debug("arm_reserve_mmu() failed (%d)\n", ret);
		goto fail;
	}

	enable_caches();
#endif

	ret = qcom_spl_loader_pre_ddr(spl_boot_device());
	if (ret) {
		pr_debug("qcom_spl_loader_pre_ddr() failed (%d)\n", ret);
		goto fail;
	}

	ret = qcom_spl_invoke_qclib();
	if (ret) {
		pr_debug("qcom_spl_invoke_qclib() failed (%d)\n", ret);
		goto fail;
	}

	board_init_r(NULL, 0);

fail:
	if (ret)
		reset_cpu();
}
#endif

/*
 * Set by shikra_ddr_training_load() before QCLIB is invoked; consumed by
 * qcom_spl_soc_qclib_override() when it populates the "ddr_training_data"
 * entry's reported size. 0 means no valid training data was read back
 * from storage (first boot, or an unprovisioned/corrupt partition) - in
 * that case the entry's size instead reports the full buffer capacity,
 * since QCLIB still needs to know how much space it has available to
 * write newly-trained data into.
 */
static u32 g_ddr_training_valid_size __section(".data");

/**
 * shikra_ddr_training_get_blk_desc() - Resolve the DDR training partition's
 * block device.
 * @descp:	Set to the resolved block device on success.
 *
 * The DDR training-data partition lives on the same eMMC device as the
 * rest of SPL's boot-time partitions, so this simply mirrors
 * spl_mmc_boot_partition()'s existing lookup (fixed UCLASS_MMC devnum 0)
 * rather than re-deriving the boot device from spl_boot_device().
 *
 * Return: 0 on success, or a negative error code if the block device
 * can't be found.
 */
static int shikra_ddr_training_get_blk_desc(struct blk_desc **descp)
{
	if (!IS_ENABLED(CONFIG_MMC))
		return -ENOSYS;

	*descp = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	if (!*descp)
		return -ENODEV;

	return 0;
}

/**
 * shikra_ddr_training_load() - Load saved DDR training data from storage.
 *
 * On the open boot flow, SPL - not SBL1 - owns storage access, so SPL
 * (not QCLIB) must read any previously-saved DDR training data off the
 * dedicated storage partition (SHIKRA_DDR_TRAINING_PARTITION) into the
 * fixed buffer QCLIB will be told about (SHIKRA_DDR_TRAINING_DATA_ADDR),
 * before QCLIB is invoked. qcom_spl_soc_qclib_override() then reports
 * g_ddr_training_valid_size (set here) as the "ddr_training_data" entry's
 * size, so QCLIB can tell a valid restore blob from an empty/allocated
 * buffer and skip full retraining where it supports doing so.
 *
 * This is entirely optional and non-fatal: if the partition can't be
 * found or read (e.g. first boot, before anything has ever been saved),
 * g_ddr_training_valid_size is left at 0 and QCLIB simply performs full
 * DDR training against an empty buffer, exactly as it did before this
 * support existed.
 */
static void shikra_ddr_training_load(void)
{
	struct blk_desc *desc;
	struct disk_partition info;
	lbaint_t blocks;
	int ret;

	g_ddr_training_valid_size = 0;

	printf("DDR training: looking up partition '%s' on boot media\n",
	       SHIKRA_DDR_TRAINING_PARTITION);

	ret = shikra_ddr_training_get_blk_desc(&desc);
	if (ret) {
		pr_err("DDR training: boot device not supported (%d)\n", ret);
		return;
	}

	ret = part_get_info_by_name(desc, SHIKRA_DDR_TRAINING_PARTITION, &info);
	if (ret < 0) {
		pr_err("DDR training: partition not found (%d)\n", ret);
		return;
	}

	blocks = DIV_ROUND_UP(SHIKRA_DDR_TRAINING_DATA_SIZE, info.blksz);
	if (blocks > info.size)
		blocks = info.size;

	printf("DDR training: reading %lu block(s) (blksz=%lu) from partition '%s' (start=0x%lx) into buffer 0x%08x\n",
	       (unsigned long)blocks, (unsigned long)info.blksz,
	       SHIKRA_DDR_TRAINING_PARTITION, (unsigned long)info.start,
	       SHIKRA_DDR_TRAINING_DATA_ADDR);

	if (blk_dread(desc, info.start, blocks,
		     (void *)SHIKRA_DDR_TRAINING_DATA_ADDR) != blocks) {
		pr_err("DDR training: failed to read partition '%s'\n",
		       SHIKRA_DDR_TRAINING_PARTITION);
		return;
	}

	g_ddr_training_valid_size = min_t(u32, (u32)blocks * info.blksz,
					  SHIKRA_DDR_TRAINING_DATA_SIZE);

	printf("DDR training: loaded %u byte(s) of saved training data from '%s' into 0x%08x\n",
	       g_ddr_training_valid_size, SHIKRA_DDR_TRAINING_PARTITION,
	       SHIKRA_DDR_TRAINING_DATA_ADDR);
}

/**
 * shikra_ddr_training_save() - Persist DDR training data QCLIB asked to save.
 * @entry:	The "ddr_training_data" interface-table entry, as updated by
 *		QCLIB on return - its address/size describe the blob to save.
 *
 * Called from qcom_spl_soc_post_qclib_routine() only when QCLIB set
 * QCLIB_ATTR_SAVE_TO_STORAGE on the "ddr_training_data" entry, i.e. QCLIB
 * performed DDR training and wants the result written back to storage.
 * If QCLIB did not set that attribute, this is never called and nothing
 * is written.
 *
 * Writes entry->size bytes starting at entry->address to the DDR
 * training storage partition, so a later boot's shikra_ddr_training_load()
 * can restore it. Failures are logged but non-fatal to boot: DRAM is
 * already up and trained for the current boot, only the saved copy on
 * storage is left stale/missing until a future successful save.
 */
static void shikra_ddr_training_save(const struct interface_table_entry *entry)
{
	struct blk_desc *desc;
	struct disk_partition info;
	lbaint_t blocks;
	int ret;

	if (!entry->address || !entry->size) {
		pr_err("DDR training: QCLIB requested save with no data\n");
		return;
	}

	ret = shikra_ddr_training_get_blk_desc(&desc);
	if (ret) {
		pr_err("DDR training: boot device not supported for save (%d)\n",
		       ret);
		return;
	}

	ret = part_get_info_by_name(desc, SHIKRA_DDR_TRAINING_PARTITION, &info);
	if (ret < 0) {
		pr_err("DDR training: partition not found for save (%d)\n", ret);
		return;
	}

	blocks = DIV_ROUND_UP(entry->size, info.blksz);
	if (blocks > info.size) {
		pr_err("DDR training: data (%u bytes) too large for partition\n",
		       entry->size);
		return;
	}

	printf("DDR training: writing %u byte(s) (%lu block(s), blksz=%lu) from 0x%llx to partition '%s' (start=0x%lx)\n",
	       entry->size, (unsigned long)blocks, (unsigned long)info.blksz,
	       entry->address, SHIKRA_DDR_TRAINING_PARTITION,
	       (unsigned long)info.start);

	if (blk_dwrite(desc, info.start, blocks,
		      (void *)(uintptr_t)entry->address) != blocks) {
		pr_err("DDR training: failed to write partition '%s'\n",
		       SHIKRA_DDR_TRAINING_PARTITION);
		return;
	}

	printf("DDR training: successfully wrote training data to partition '%s'\n",
	       SHIKRA_DDR_TRAINING_PARTITION);
}

int qcom_spl_soc_qclib_override(struct interface_table *table,
				const void *fit, int images_node)
{
	static const struct {
		const char *name;
		u32 size;
	} fit_blobs[] = {
		{ SHIKRA_PMIC_COCOS, SHIKRA_PMIC_COCOS_SIZE },
		{ SHIKRA_PMIC_WAILUA, SHIKRA_PMIC_WAILUA_SIZE },
		{ SHIKRA_DCB_LP4, SHIKRA_DCB_LP4_SIZE },
		{ SHIKRA_DCB_LP5, SHIKRA_DCB_LP5_SIZE },
		{ SHIKRA_ECC, SHIKRA_ECC_SIZE },
	};
	struct interface_table_entry ddr_training_entry;
	uint i;
	int ret;

	table->global_attributes |= QCLIB_GA_ENABLE_UART_LOGGING;
	for (i = 0; i < ARRAY_SIZE(fit_blobs); i++) {
		ret = qclib_add_iftbl_fit_blob(table, fit, images_node,
					       fit_blobs[i].name, fit_blobs[i].size);
		if (ret)
			return ret;
	}

	/*
	 * First boot (nothing valid read from storage): hand QCLIB the
	 * empty/allocated buffer at its full capacity so it has room to
	 * write freshly-trained data into.
	 * Subsequent boots (shikra_ddr_training_load() read saved data):
	 * report the actual valid size read, so QCLIB can restore from it
	 * and skip full retraining where supported.
	 */
	ret = qclib_add_iftbl_entry(table, SHIKRA_DDR_TRAINING_DATA,
				    SHIKRA_DDR_TRAINING_DATA_ADDR,
				    g_ddr_training_valid_size ?
				    g_ddr_training_valid_size :
				    SHIKRA_DDR_TRAINING_DATA_SIZE);
	if (ret)
		return ret;

	ret = qcom_spl_get_iftbl_entry_by_name(table, SHIKRA_DDR_TRAINING_DATA,
					       &ddr_training_entry);
	if (ret)
		return ret;

	printf("DDR training: giving QCLIB '%s' entry addr=0x%llx size=%u attributes=0x%x (%s)\n",
	       SHIKRA_DDR_TRAINING_DATA, ddr_training_entry.address,
	       ddr_training_entry.size, ddr_training_entry.attributes,
	       g_ddr_training_valid_size ?
	       "restore data from storage" :
	       "empty/allocated buffer, first boot");

	ret = qclib_add_iftbl_entry(table, SHIKRA_DDR_INFORMATION, 0, 0);
	if (ret)
		return ret;

	return qclib_add_iftbl_entry(table, SHIKRA_DDR_SR_EXIT, 0, 0);
}

void qcom_spl_soc_shrm_reset(void)
{
	writel(SHIKRA_SHRM2_SPROC_CTRL_DEBUG_SW_RESET_BMSK |
	       SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK,
	       (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);
	writel(SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK,
	       (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);
	writel(0, (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);
}

int qcom_spl_soc_pre_qclib_routine(void)
{
	qcom_spl_soc_shrm_reset();

	/*
	 * Read any previously-saved DDR training data off storage into the
	 * fixed buffer before qcom_spl_soc_qclib_override() populates the
	 * "ddr_training_data" interface-table entry below - the entry's
	 * reported size depends on whether a valid blob was found here.
	 */
	shikra_ddr_training_load();

	return 0;
}

void qcom_spl_soc_rpm_reset(void)
{
	clrbits_le32((void *)SHIKRA_GCC_APCS_MISC_ADDR,
		     SHIKRA_GCC_APCS_MISC_RPM_RESET_BMSK);
}

int spl_check_board_image(struct spl_image_info *spl_image,
			  const struct spl_boot_device *bootdev)
{
	qcom_spl_soc_rpm_reset();

	return 0;
}

int qcom_spl_soc_post_qclib_routine(struct interface_table *table)
{
	struct interface_table_entry entry;
	int ret;

	/*
	 * Clear the full memory region reserved for the RPM image before loading
	 * the post-DDR FIT, including its zero-initialized data.
	 */
	memset((void *)SHIKRA_RPM_STAGING_ADDR, 0, SHIKRA_RPM_STAGING_SIZE);

	ret = qcom_spl_get_iftbl_entry_by_name(table, SHIKRA_DDR_SR_EXIT,
					       &entry);
	if (ret)
		return ret;

	qclib_set_ddr_sr_exit_address(entry.address);

	/*
	 * QCLIB may have performed DDR training (first boot, or any boot
	 * where restore from storage wasn't possible/sufficient) and
	 * updated the "ddr_training_data" entry's address/size to describe
	 * the resulting blob. It signals this back to SPL by setting
	 * QCLIB_ATTR_SAVE_TO_STORAGE on that entry; only in that case does
	 * SPL write the blob to the DDR training storage partition. If the
	 * attribute isn't set (e.g. QCLIB successfully restored from the
	 * data SPL already supplied, so nothing changed), nothing is
	 * written back.
	 */
	ret = qcom_spl_get_iftbl_entry_by_name(table, SHIKRA_DDR_TRAINING_DATA,
					       &entry);
	if (ret)
		return ret;

	printf("DDR training: QCLIB returned '%s' entry addr=0x%llx size=%u attributes=0x%x\n",
	       SHIKRA_DDR_TRAINING_DATA, entry.address, entry.size,
	       entry.attributes);

	if (entry.attributes & QCLIB_ATTR_SAVE_TO_STORAGE) {
		printf("DDR training: SAVE_TO_STORAGE bit is SET - saving training data to partition '%s'\n",
		       SHIKRA_DDR_TRAINING_PARTITION);
		shikra_ddr_training_save(&entry);
	} else {
		printf("DDR training: SAVE_TO_STORAGE bit is NOT set - skipping write to partition '%s'\n",
		       SHIKRA_DDR_TRAINING_PARTITION);
	}

	return 0;
}

int spl_mmc_boot_partition(const u32 boot_device)
{
	struct blk_desc *desc;
	struct disk_partition info;
	int partition;

	if (!IS_ENABLED(CONFIG_MMC))
		return -ENOSYS;

	desc = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	if (!desc)
		return -ENODEV;

	partition = part_get_info_by_name(desc, QCOM_SPL_FIT_IMG_PARTITION,
					  &info);
	if (partition < 0)
		return -ENOENT;

	return partition;
}

/**
 * spl_boot_device() - Get the boot device.
 *
 * Return: U-Boot boot device identifier, or -EINVAL if the device is invalid.
 */
u32 spl_boot_device(void)
{
	u8 boot_device_cfg =
	(readl(IPQ_SPL_BOOTCFG_REG_ADDR) & IPQ_SPL_BOOTCFG_DEV_MASK) >>
	IPQ_SPL_BOOTCFG_DEV_SHFT;
	u32 boot_device_smem;

	switch (boot_device_cfg) {
	case IPQ_SPL_BOOTCFG_DEV_MMC:
		boot_device_smem = BOOT_DEVICE_MMC1;
		printf("Selected boot device: MMC\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_SD_EMMC:
		boot_device_smem = BOOT_DEVICE_MMC2;
		printf("Selected boot device: SD MMC\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_EDL:
		boot_device_smem = BOOT_DEVICE_USB;
		printf("Selected boot device: SPI-NAND\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_NAND:
		boot_device_smem = BOOT_DEVICE_NAND;
		printf("Selected boot device: SPI-NAND\n");
		break;
	default:
		pr_err("Invalid boot device configured: %d\n",
		       boot_device_cfg);
		return -EINVAL;
	}

	return boot_device_smem;
}
