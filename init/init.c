/* Freestanding /init for the QEMU bench.
 *
 * Two things matter here and neither is possible on the phone: panic_on_oops=0,
 * so a fault keeps its evidence instead of rebooting, and a child process to take
 * the fault, so init survives and can report what happened.
 */
typedef unsigned long u64;
typedef long i64;

static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e)
{
	register i64 x8 __asm__("x8") = n;
	register i64 x0 __asm__("x0") = a;
	register i64 x1 __asm__("x1") = b;
	register i64 x2 __asm__("x2") = c;
	register i64 x3 __asm__("x3") = d;
	register i64 x4 __asm__("x4") = e;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
	return x0;
}

#define SYS_dup3 24
#define SYS_mkdirat 34
#define SYS_mount 40
#define SYS_openat 56
#define SYS_close 57
#define SYS_read 63
#define SYS_write 64
#define SYS_exit 93
#define SYS_reboot 142
#define SYS_uname 160
#define SYS_clone 220
#define SYS_wait4 260
#define SYS_finit_module 273
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(SYS_write, 1, (i64)s, slen(s), 0, 0); }

static void outdec(i64 v)
{
	char b[24];
	int i = 23;
	int neg = v < 0;
	u64 u = neg ? (u64)(-v) : (u64)v;
	b[23] = 0;
	if (!u) b[--i] = '0';
	while (u) { b[--i] = (char)('0' + u % 10); u /= 10; }
	if (neg) b[--i] = '-';
	out(&b[i]);
}

static void write_file(const char *path, const char *val)
{
	i64 fd = sc(SYS_openat, AT_FDCWD, (i64)path, O_WRONLY, 0, 0);
	if (fd < 0) return;
	sc(SYS_write, fd, (i64)val, slen(val), 0, 0);
	sc(SYS_close, fd, 0, 0, 0, 0);
}

static i64 read_small(const char *path, char *buf, i64 cap)
{
	i64 fd = sc(SYS_openat, AT_FDCWD, (i64)path, O_RDONLY, 0, 0);
	i64 n = 0, r;
	if (fd < 0) return -1;
	while (n < cap - 1 && (r = sc(SYS_read, fd, (i64)(buf + n), cap - 1 - n, 0, 0)) > 0)
		n += r;
	buf[n] = 0;
	sc(SYS_close, fd, 0, 0, 0, 0);
	return n;
}

static void dump(const char *path, int max)
{
	char buf[1024];
	i64 fd = sc(SYS_openat, AT_FDCWD, (i64)path, O_RDONLY, 0, 0);
	i64 n;
	if (fd < 0) { out("[init] cannot open "); out(path); out("\n"); return; }
	out("----- "); out(path); out(" -----\n");
	while (max > 0 && (n = sc(SYS_read, fd, (i64)buf, sizeof(buf), 0, 0)) > 0) {
		sc(SYS_write, 1, (i64)buf, n, 0, 0);
		max -= (int)n;
	}
	out("\n----- end -----\n");
	sc(SYS_close, fd, 0, 0, 0, 0);
}

static void mount_at(const char *src, const char *tgt, const char *type)
{
	i64 rc = sc(SYS_mount, (i64)src, (i64)tgt, (i64)type, 0, 0);
	if (rc < 0) { out("[init] mount "); out(type); out(" failed "); outdec(-rc); out("\n"); }
}

static void load(const char *path, const char *params)
{
	i64 fd = sc(SYS_openat, AT_FDCWD, (i64)path, O_RDONLY, 0, 0);
	i64 rc;
	if (fd < 0) { out("[init] cannot open "); out(path); out(" errno "); outdec(-fd); out("\n"); return; }
	out("[init] finit_module "); out(path); out(" params=\""); out(params); out("\"\n");
	rc = sc(SYS_finit_module, fd, (i64)params, 0, 0, 0);
	out("[init] finit_module rc=");
	outdec(rc);
	out("\n");
	sc(SYS_close, fd, 0, 0, 0, 0);
}

void _start(void)
{
	char params[256];
	char uts[390];
	i64 fd, pid, status = 0;

	fd = sc(SYS_openat, AT_FDCWD, (i64)"/dev/console", O_WRONLY, 0, 0);
	if (fd >= 0) {
		sc(SYS_dup3, fd, 0, 0, 0, 0);
		sc(SYS_dup3, fd, 1, 0, 0, 0);
		sc(SYS_dup3, fd, 2, 0, 0, 0);
	}
	out("\n[init] guest up\n");

	sc(SYS_mkdirat, AT_FDCWD, (i64)"/proc", 0755, 0, 0);
	sc(SYS_mkdirat, AT_FDCWD, (i64)"/sys", 0755, 0, 0);
	mount_at("proc", "/proc", "proc");
	mount_at("sysfs", "/sys", "sysfs");

	/* Evidence survives a fault only if the kernel does not reboot on it. */
	write_file("/proc/sys/kernel/panic_on_oops", "0");
	write_file("/proc/sys/kernel/panic_on_warn", "0");
	write_file("/proc/sys/kernel/panic", "0");
	write_file("/proc/sys/kernel/printk", "8 4 1 7");

	dump("/proc/version", 512);
	dump("/proc/cpuinfo", 3000);

	params[0] = 0;
	if (read_small("/bench.conf", params, sizeof(params)) > 0) {
		u64 n = slen(params);
		while (n && (params[n - 1] == '\n' || params[n - 1] == '\r')) params[--n] = 0;
	}
	load("/lib/knox_bench.ko", params);

	/* The probe is armed; now make the call from a child so init survives. */
	pid = sc(SYS_clone, 17, 0, 0, 0, 0);
	if (pid == 0) {
		i64 rc = sc(SYS_uname, (i64)uts, 0, 0, 0, 0);
		out("[child] uname rc=");
		outdec(rc);
		out("\n");
		sc(SYS_exit, 0, 0, 0, 0, 0);
		for (;;) { }
	}
	sc(SYS_wait4, pid, (i64)&status, 0, 0, 0);
	out("[init] child status=");
	outdec(status);
	out("\n");

	dump("/proc/modules", 2048);
	out("[init] done, powering off\n");
	sc(SYS_reboot, 0xfee1dead, 672274793, 0x4321fedc, 0, 0);
	sc(SYS_exit, 0, 0, 0, 0, 0);
	for (;;) { }
}
