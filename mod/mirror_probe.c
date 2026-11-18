/* mirror_probe: load-only canary. No imported symbols, so no symbol CRC is
 * needed except module_layout, whose value is taken from the running kernel.
 *
 * Layout below is transcribed from the DWARF of the vendor's pronto_wlan.ko
 * (struct module, /source/kernel/include/linux/module.h:223, size 0x220):
 *   name  @ 0x18 (char[56])
 *   init  @ 0x120
 *   exit  @ 0x210
 */
#define SEC(n) __attribute__((section(n), used))

struct this_module {
	unsigned char pad0[0x18];
	char name[56];
	unsigned char pad1[0x120 - 0x18 - 56];
	int (*init)(void);
	unsigned char pad2[0x210 - 0x120 - 8];
	void *exit;
	unsigned char pad3[0x220 - 0x210 - 8];
};

static int mp_init(void)
{
	return 0;
}

static void mp_exit(void)
{
}

struct this_module __this_module SEC(".gnu.linkonce.this_module") = {
	.name = "mirror_probe",
	.init = mp_init,
	.exit = mp_exit,
};

struct modversion_info {
	unsigned long crc;
	char name[56];
};

static const struct modversion_info versions[] SEC("__versions") = {
	{ 0xa5b7f7dd, "module_layout" },
};

static const char modinfo[] SEC(".modinfo") =
	"license=GPL\0"
	"depends=\0"
	"vermagic=3.10.49-perf-g87fe01fd365 SMP preempt mod_unload modversions aarch64\0";
