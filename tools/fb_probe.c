/* fbtest / fbpeek: drive /dev/graphics/fb0 from a freestanding static binary.
 *
 * No libc, raw arm64 syscalls, 256-byte ioctl buffers (the kernel copies the
 * full struct fb_var_screeninfo / fb_fix_screeninfo, so an exact-size mirror
 * struct would be overflowed on copy-out).
 *
 * Build: clang --target=aarch64-linux-gnu -fuse-ld=lld -ffreestanding \
 *            -nostdlib -static -O2 -Wl,-e,_start -DPAINT=0|1 fbtest.c -o fb
 */
typedef unsigned long u64;

static long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
			 : "memory");
	return x0;
}
static long sys3(long n, long a, long b, long c) { return sys6(n, a, b, c, 0, 0, 0); }
static long sys4(long n, long a, long b, long c, long d) { return sys6(n, a, b, c, d, 0, 0); }
static long sys1(long n, long a) { return sys6(n, a, 0, 0, 0, 0, 0); }

#define SYS_openat 56
#define SYS_ioctl 29
#define SYS_mmap 222
#define SYS_write 64
#define SYS_nanosleep 101
#define SYS_exit 93

#define AT_FDCWD -100
#define O_RDWR 2
#define PROT_RW 3
#define MAP_SHARED 1

static unsigned get32(const unsigned char *b, int off)
{
	return (unsigned)b[off] | (unsigned)b[off + 1] << 8 |
	       (unsigned)b[off + 2] << 16 | (unsigned)b[off + 3] << 24;
}
static u64 get64(const unsigned char *b, int off)
{
	return (u64)get32(b, off) | (u64)get32(b, off + 4) << 32;
}
static void zero(unsigned char *b, int n)
{
	for (int i = 0; i < n; i++) b[i] = 0;
}
static int slen(const char *s)
{
	int n = 0;
	while (s[n]) n++;
	return n;
}
static void puts_(const char *s) { sys4(SYS_write, 1, (long)s, slen(s), 0); }
static void putn(long v)
{
	char b[24];
	int i = 24;
	int neg = v < 0;
	unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
	b[--i] = 0;
	do { b[--i] = '0' + (u % 10); u /= 10; } while (u);
	if (neg) b[--i] = '-';
	puts_(&b[i]);
}
static void sleep_ms(long ms)
{
	struct { long s, ns; } ts = { ms / 1000, (ms % 1000) * 1000000 };
	sys3(SYS_nanosleep, (long)&ts, (long)&ts, 0);
}

void _start(void)
{
	unsigned char var[256], fix[256];
	long fd, ret;
	char *fb;
	unsigned xres, yres, xres_virt, yres_virt, bpp;

	zero(var, sizeof var);
	zero(fix, sizeof fix);

	fd = sys4(SYS_openat, AT_FDCWD, (long)"/dev/graphics/fb0", O_RDWR, 0);
	puts_("open fb0=");
	putn(fd);
	puts_("\n");
	if (fd < 0) sys1(SYS_exit, 1);

	ret = sys3(SYS_ioctl, fd, 0x4600, (long)var);
	if (ret) { puts_("FBIOGET_VSCREENINFO failed\n"); sys1(SYS_exit, 1); }
	xres = get32(var, 0);
	yres = get32(var, 4);
	xres_virt = get32(var, 8);
	yres_virt = get32(var, 12);
	bpp = get32(var, 24);
	puts_("screen ");
	putn(xres); puts_("x"); putn(yres);
	puts_(" virt "); putn(xres_virt); puts_("x"); putn(yres_virt);
	puts_(" bpp "); putn(bpp);
	puts_(" xoff "); putn(get32(var, 16));
	puts_(" yoff "); putn(get32(var, 20));
	puts_("\n");

	ret = sys3(SYS_ioctl, fd, 0x4602, (long)fix);
	if (ret) { puts_("FBIOGET_FSCREENINFO failed\n"); sys1(SYS_exit, 1); }
	u64 smem = get64(fix, 16);
	unsigned smem_len = get32(fix, 24);
	unsigned line_length = get32(fix, 48);
	puts_("smem_start=");
	putn((long)smem);
	puts_(" smem_len=");
	putn(smem_len);
	puts_(" line_length=");
	putn(line_length);
	puts_("\n");

	u64 len = (u64)line_length * yres_virt;
	fb = (char *)sys6(SYS_mmap, 0, (long)len, PROT_RW, MAP_SHARED, fd, 0);
	if ((long)fb < 0) { puts_("mmap failed\n"); sys1(SYS_exit, 1); }
	puts_("mmap ok len=");
	putn((long)len);
	puts_("\n");

	unsigned *p = (unsigned *)fb;
	unsigned long row_words = line_length / 4;

#if PAINT
	/* Program the mixer with this buffer: fb_set_par + pan. */
	unsigned char act[256];
	zero(act, sizeof act);
	puts_("FBIOGET_VSCREENINFO again\n");
	sys3(SYS_ioctl, fd, 0x4600, (long)act);
	ret = sys3(SYS_ioctl, fd, 0x4601, (long)act);
	puts_("FBIOPUT_VSCREENINFO=");
	putn(ret);
	puts_("\n");
	ret = sys3(SYS_ioctl, fd, 0x4606, (long)act);
	puts_("FBIOPAN_DISPLAY=");
	putn(ret);
	puts_("\n");

	for (int frame = 0; frame < 40; frame++) {
		for (unsigned long y = 0; y < yres_virt; y++) {
			unsigned col = (PAINT == 2) ? 0u :
				       (((y / 60) % 2) ? 0xffff00ffu : 0xff00ff00u);
			unsigned *row = p + y * row_words;
			for (unsigned long x = 0; x < row_words; x++) row[x] = col;
		}
		if (frame == 0) {
			puts_("readback px0=");
			putn(p[0]);
			puts_("\n");
		}
		sleep_ms(50);
	}
	ret = sys3(SYS_ioctl, fd, 0x4606, (long)act);
	puts_("final pan=");
	putn(ret);
	puts_("\n");
	puts_("painted 40 frames, boot yoff=");
	putn(get32(var, 20));
	puts_("\n");
#else
	for (int i = 0; i < 6; i++) {
		unsigned *q = p + (unsigned long)(i * 300) * row_words;
		puts_("row "); putn(i * 300);
		puts_(" px0="); putn(q[0]);
		puts_(" px960="); putn(q[960]);
		puts_("\n");
	}
#endif
	sys1(SYS_exit, 0);
}
