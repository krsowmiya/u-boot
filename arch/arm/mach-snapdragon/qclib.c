// SPDX-License-Identifier: GPL-2.0
/*
 * QCLIB interface-table protocol: generic, SoC-agnostic orchestration of the
 * hand-off to the QCLIB firmware image, plus __weak extension points a
 * per-board spl-<soc>.c may override to populate SoC-specific table entries
 * and provide SoC-specific reset sequences.
 */

#include <cpu_func.h>
#include <errno.h>
#include <fdt_support.h>
#include <image.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <mach/qclib.h>

static u64 g_qcsdi_address __section(".data");
static u64 g_log_buffer_entry __section(".data");
static u64 g_ddr_sr_exit_address __section(".data");

/*
 * Which QCLIB stage the next qcom_spl_invoke_qclib() call should run as.
 * Defaults to stage 1 (the common/initial QCLIB entry). A caller that
 * needs a later invocation (e.g. the stage-2 ramdump collector entry)
 * sets this explicitly via qclib_set_stage() before calling
 * qcom_spl_invoke_qclib() again, instead of passing a stage argument.
 */
static u32 g_qclib_stage __section(".data") = QCLIB_STAGE_1;

void qclib_set_stage(u32 stage)
{
	g_qclib_stage = stage;
}

u64 qclib_get_ddr_sr_exit_address(void)
{
	return g_ddr_sr_exit_address;
}

void qclib_set_ddr_sr_exit_address(u64 address)
{
	g_ddr_sr_exit_address = address;
}

u64 qclib_get_qcsdi_address(void)
{
	return g_qcsdi_address;
}

void qclib_set_qcsdi_address(u64 addr)
{
	g_qcsdi_address = addr;
}

u64 qclib_get_log_buffer_entry(void)
{
	return g_log_buffer_entry;
}

void qclib_set_log_buffer_entry(u64 address)
{
	g_log_buffer_entry = address;
}

static inline void qclib_init_iftbl(struct interface_table *table)
{
	if (!table)
		return;

	memset(table, 0, sizeof(*table));
	memcpy(table->magic_key, MAGIC_KEY, strlen(MAGIC_KEY));
	table->version = IF_TABLE_VERSION;
	table->num_entries = 0;
	table->max_entries = MAX_ENTRIES;
	table->global_attributes = 0;
}

int qclib_add_iftbl_entry(struct interface_table *table,
			  const char *name, u64 address, u32 size)
{
	uint entry_idx = table->num_entries;

	if (entry_idx >= table->max_entries)
		return -ENOSPC;

	memset(&table->if_table_entries[entry_idx], 0,
	       sizeof(table->if_table_entries[entry_idx]));
	memcpy(table->if_table_entries[entry_idx].entry_name, name, strlen(name));
	table->if_table_entries[entry_idx].address = address;
	table->if_table_entries[entry_idx].size = size;
	table->num_entries = entry_idx + 1;

	return 0;
}

int qclib_add_iftbl_fit_blob(struct interface_table *table,
			     const void *fit, int images_node,
			     const char *name, u32 size)
{
	int node, ret;
	u64 address;

	node = fdt_subnode_offset(fit, images_node, name);
	if (node < 0)
		return -ENOENT;

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, node, &address);
	if (ret)
		return ret;

	return qclib_add_iftbl_entry(table, name, address, size);
}

bool __weak qcom_spl_soc_check_dload_mode(void)
{
	return false;
}

int __weak qcom_spl_soc_pre_qclib_routine(void)
{
	qcom_spl_soc_shrm_reset();

	return 0;
}

int __weak qcom_spl_soc_qclib_override(struct interface_table *table,
				       const void *fit, int images_node)
{
	return 0;
}

int __weak qcom_spl_soc_post_qclib_routine(struct interface_table *if_tbl)
{
	return 0;
}

int __weak qclib_populate_interface_table(struct interface_table *if_tbl,
					  const void *fit, int images_node)
{
	int ret;

	memset(if_tbl, 0, sizeof(struct interface_table));
	memcpy(if_tbl->magic_key, MAGIC_KEY, strlen(MAGIC_KEY));
	if_tbl->version = IF_TABLE_VERSION;
	if_tbl->num_entries = 0;
	if_tbl->max_entries = MAX_ENTRIES;

	/* Reserve the common qclib_log_buffer slot. */
	ret = qclib_add_iftbl_entry(if_tbl, QCLIB_LOG_BUFFER, 0, 0);
	if (ret)
		return ret;

	return qcom_spl_soc_qclib_override(if_tbl, fit, images_node);
}

static int qclib_find_fit_nodes(const void *fit, int *images_node,
				int *qclib_node, int *ramdump_node)
{
	*images_node = fdt_subnode_offset(fit, 0, "images");
	if (*images_node < 0) {
		pr_err("Failed to find images node in FIT\n");
		return -ENOENT;
	}

	/*
	 * QCLIB's own "qclib_1" FIT node is the jump target for every
	 * stage. Stage 2 re-invokes the *same* QCLIB firmware image
	 * already resident from stage 1 - it does not jump into a
	 * separate "qclib_2" binary or into the ramdump image itself.
	 */
	*qclib_node = fdt_subnode_offset(fit, *images_node, "qclib_1");
	if (*qclib_node < 0) {
		pr_err("Failed to find qclib_1 node in FIT\n");
		return -ENOENT;
	}

	if (g_qclib_stage == QCLIB_STAGE_2) {
		/*
		 * Only the ramdump image's load address is needed here (to
		 * publish via the "ramdump_metadata" interface-table entry
		 * below) - QCLIB itself, resolved above, remains the jump
		 * target.
		 */
		*ramdump_node = fdt_subnode_offset(fit, *images_node, "ramdump");
		if (*ramdump_node < 0) {
			pr_err("Failed to find ramdump node in FIT\n");
			return -ENOENT;
		}
	}

	return 0;
}

int qcom_spl_invoke_qclib(void)
{
	int ret;
	int images_node;
	int qclib_node;
	int ramdump_node = -1;
	const void *fit;
	struct interface_table if_tbl;
	u64 entry_point;
	ulong load_addr;
	u32 stage = g_qclib_stage;

	/* Get FIT image from SPL load address */
	fit = (const void *)CONFIG_SPL_LOAD_FIT_ADDRESS;

	printf("QCLIB invoke stage %u: fit=%p\n", stage, fit);

	ret = qclib_find_fit_nodes(fit, &images_node, &qclib_node, &ramdump_node);
	if (ret)
		return ret;

	ret = qcom_spl_soc_pre_qclib_routine();
	if (ret)
		return ret;

	/*
	 * qclib_node always resolves to QCLIB's own FIT node ("qclib_1") -
	 * entry_point/load_addr below are always QCLIB's own addresses,
	 * for both stage 1 and stage 2, so stage 2 jumps back into the
	 * same already-resident QCLIB firmware rather than into the
	 * ramdump image directly.
	 */
	ret = qcom_spl_get_fit_img_entry_point((void *)fit, qclib_node, &entry_point);
	if (ret) {
		pr_err("Failed to get qcom-lib-%u entry point (%d)\n", stage, ret);
		return ret;
	}

	load_addr = 0;
	if (fit_image_get_load(fit, qclib_node, &load_addr))
		load_addr = (ulong)entry_point;

	if (stage == QCLIB_STAGE_2) {
		ulong ramdump_load_addr = 0;

		/*
		 * The ramdump collector image was already staged into DDR
		 * by the post-ddr FIT reload before this call - fetch its
		 * load address here purely to hand it to QCLIB via the
		 * interface table.
		 */
		if (fit_image_get_load(fit, ramdump_node, &ramdump_load_addr)) {
			pr_err("Failed to get ramdump load address\n");
			return -ENOENT;
		}

		printf("Ramdump image loaded in DDR at address: 0x%08lx\n",
		       ramdump_load_addr);

		/*
		 * The ramdump collector needs a fresh interface table -
		 * qclib_init_iftbl() must run again right before this 2nd
		 * QCLIB entry, since reusing/extending the stage-1 table
		 * (whose board-specific pmic/dcb/ddr entries from
		 * qcom_spl_soc_qclib_override() are not valid inputs for
		 * the crash-recovery path) would carry stale entries into
		 * this invocation.
		 */
		qclib_init_iftbl(&if_tbl);

		/*
		 * Pass the ramdump image's load address into the interface
		 * table under "ramdump_metadata" so QCLIB can locate the
		 * collector's crash-context metadata.
		 */
		ret = qclib_add_iftbl_entry(&if_tbl, RAMDUMP_METADATA,
					    ramdump_load_addr, 0);
		printf("\r\n FILL only ramdump data into interface table");
		printf(" (ramdump_metadata @ 0x%08lx)\n", ramdump_load_addr);
		if (ret)
			return ret;
	} else {
		printf("\r\n FILL all data into interface table");
		ret = qclib_populate_interface_table(&if_tbl, fit, images_node);
		if (ret)
			return ret;
	}

	printf("Jumping to qcom-lib-%u at 0x%llx\n", stage, entry_point);
	printf("qcom-lib-%u load address from FIT: 0x%llx\n", stage,
		 load_addr);

	/*
	 * QCLIB is a separately-linked firmware image, not an AAPCS64
	 * callee - a plain C function-pointer call risks the compiler
	 * caching a live value in a callee-saved register across the jump.
	 * Pin the args/target to fixed registers and clobber everything the
	 * ABI doesn't guarantee QCLIB will preserve.
	 */
	{
		register void *x0 asm("x0") = &if_tbl;
		register void *x1 asm("x1") = NULL;
		register u64 x8 asm("x8") = entry_point;
		u64 saved_x18;

		/* U-Boot keeps global data in x18 on AArch64. */
		asm volatile ("str x18, %1\n\t"
			      "blr x8\n\t"
			      "ldr x18, %1"
			      : "+r" (x0), "=m" (saved_x18)
			      : "r" (x1), "r" (x8)
			      : "x2", "x3", "x4", "x5", "x6", "x7", "x9", "x10",
				"x11", "x12", "x13", "x14", "x15", "x16", "x17",
				"x19", "x20", "x21", "x22", "x23", "x24", "x25",
				"x26", "x27", "x28", "x30", "cc", "memory");
	}

	return qcom_spl_soc_post_qclib_routine(&if_tbl);
}