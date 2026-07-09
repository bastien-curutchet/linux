// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * L1 Instruction Cache Parity Error Injection & Recovery Test
 *
 * The module can automatically run the test on load:
 *   insmod icache_parity_test.ko auto_run=1
 *
 * or the test can be run from debugfs:
 *   echo 1 > /sys/kernel/debug/powerpc/icache_parity_test
 *
 */

#include <linux/module.h>
#include <linux/debugfs.h>

static struct dentry *test_dentry;
static bool auto_run;

module_param(auto_run, bool, 0444);

static int icache_target_fn(void)
{
	int ret = 42;

	return ret;
}

static void run_icache_parity_test(void)
{
	unsigned long l1csr1;
	int ret;

	pr_info("icache_parity: Starting I-cache parity test on CPU\n");

	/* Invalidate the target */
	icbi((void *)icache_target_fn);
	isync();

	/* Enable parity error injection */
	l1csr1 = mfspr(SPRN_L1CSR1);
	l1csr1 |= L1CSR1_ICPI | L1CSR1_CPE;
	mtspr(SPRN_L1CSR1, l1csr1);
	isync();

	/*
	 * Call icache_target_fn(), it will:
	 * - generate a I-cache miss because of the icbi() call above
	 * - allocate a new line the I-cache for the icache_target_fn()
	 *     -> this new lines gets poisoned with the parity error injection
	 * - generate a I-cache hit
	 *     -> this hit triggers the parity check
	 */
	ret = icache_target_fn();
	isync();

	/* Disable parity error injection */
	l1csr1 &= ~L1CSR1_ICPI;
	mtspr(SPRN_L1CSR1, l1csr1);

	pr_info("Leaving test -> %d / %px\n", ret, icache_target_fn);
}

static ssize_t icache_parity_test_write(struct file *file,
					const char __user *buf,
					size_t count, loff_t *ppos)
{
	run_icache_parity_test();

	return count;
}

static const struct file_operations icache_parity_test_fops = {
	.write	= icache_parity_test_write,
	.open	= simple_open,
	.llseek = default_llseek,
};

static int __init icache_parity_test_init(void)
{
	pr_info("L1 I-cache parity error injection\n");
	pr_info("  echo 1 > /sys/kernel/debug/powerpc/icache_parity_test to run the test\n");
	test_dentry = debugfs_create_file("icache_parity_test", 0600,
					  arch_debugfs_dir, NULL,
					  &icache_parity_test_fops);
	if (!test_dentry) {
		pr_err("icache_parity: Failed to create debugfs file\n");
		return -ENOMEM;
	}

	if (auto_run) {
		pr_info("icache_parity: Auto-running test ...\n");
		run_icache_parity_test();
	}

	return 0;
}

static void __exit icache_parity_test_exit(void)
{
	debugfs_remove(test_dentry);
}

module_init(icache_parity_test_init);
module_exit(icache_parity_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("L1 I-cache parity error injection & recovery test for e500v2");
MODULE_AUTHOR("Bastien Curutchet <bastien.curutchet@bootlin.com>");
