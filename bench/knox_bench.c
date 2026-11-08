/* A bench module for the questions that cost us device boots.
 *
 * It resolves one symbol through the kprobe bootstrap (register_kprobe() looks the
 * name up for us, so no kallsyms_lookup_name is needed), prints the first three
 * words of the entry, and arms a kprobe at a chosen offset.  With skip=1 the
 * handler leaves the call through a stub, which is the pattern that panicked the
 * phone three times; with skip=0 it only observes.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <asm/ptrace.h>

static char *target = "__arm64_sys_newuname";
module_param(target, charp, 0444);
static int offset;
module_param(offset, int, 0444);
static int skip;
module_param(skip, int, 0444);
static int do_unregister;
module_param(do_unregister, int, 0444);

static unsigned long bench_resolve(const char *name)
{
	struct kprobe probe = { .symbol_name = name };
	unsigned long addr = 0;

	if (register_kprobe(&probe))
		return 0;
	addr = (unsigned long)probe.addr;
	unregister_kprobe(&probe);
	return addr;
}

static long __nocfi bench_stub(const struct pt_regs *regs)
{
	return -ENOSYS;
}

static int bench_pre(struct kprobe *p, struct pt_regs *regs)
{
	if (!skip)
		return 0;
	instruction_pointer_set(regs, (unsigned long)bench_stub);
	return 1;
}

static struct kprobe bench_kp = { .pre_handler = bench_pre };
static unsigned long bench_fn;

static int __init bench_init(void)
{
	u32 w0 = 0, w1 = 0, w2 = 0;
	int rc;

	bench_fn = bench_resolve(target);
	if (!bench_fn) {
		pr_err("bench: cannot resolve %s\n", target);
		return -ENOENT;
	}
	copy_from_kernel_nofault(&w0, (void *)bench_fn, 4);
	copy_from_kernel_nofault(&w1, (void *)(bench_fn + 4), 4);
	copy_from_kernel_nofault(&w2, (void *)(bench_fn + 8), 4);
	pr_info("bench: %s = %px words %08x %08x %08x\n", target, (void *)bench_fn, w0, w1, w2);

	bench_kp.addr = (kprobe_opcode_t *)(bench_fn + offset);
	rc = register_kprobe(&bench_kp);
	pr_info("bench: kprobe at %s+%d rc=%d skip=%d\n", target, offset, rc, skip);
	if (!rc) {
		u32 after = 0;
		copy_from_kernel_nofault(&after, (void *)(bench_fn + offset), 4);
		pr_info("bench: word at %s+%d is now %08x (brk #4 would be d4200080)\n",
			target, offset, after);
	}
	if (rc) {
		bench_kp.addr = NULL;
		return rc;
	}
	return 0;
}

static void __exit bench_exit(void)
{
	if (bench_kp.addr) {
		unregister_kprobe(&bench_kp);
		bench_kp.addr = NULL;
	}
}

module_init(bench_init);
module_exit(bench_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung bench: landing pad and skip behaviour, offline");
