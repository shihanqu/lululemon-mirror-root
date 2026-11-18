/* mirror_root: permissive SELinux + privileged command daemon.
 *
 * Loaded by init at boot as /system/lib/modules/adsprpc.ko (the file that
 * init.qcom.rc insmods unconditionally in "on boot"; this image ships no
 * real adsprpc.ko, so nothing is displaced).
 *
 * Imports exactly three symbols.  Two are declared with primitive-only
 * signatures, which is what makes their CRCs reproducible from the stock
 * 3.10.49 sources:
 *   unsigned long kallsyms_lookup_name(const char *)   0xe007de41  (GPL)
 *   int call_usermodehelper(const char *, char **, char **, int)  0xc5fdef94
 * module_layout's value is the one the running kernel checks; the struct
 * layout is transcribed from the vendor module's DWARF (size 0x220).
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

extern unsigned long kallsyms_lookup_name(const char *name);
extern int call_usermodehelper(const char *path, char **argv, char **envp,
			       int wait);

static char *helper_argv[] = {
	"/system/bin/sh", "-c",
	/* permissive, rw /system, then a root command queue at /data/local/rootd */
	"mkdir -p /data/local/rootd; chmod 700 /data/local/rootd; "
	"mount -o rw,remount /system; "
	/* optional: install /system/etc/adb_keys so a fresh unit authorises USB */
	"if [ -f /system/etc/adb_keys ]; then "
	"mkdir -p /data/misc/adb; touch /adb_keys /data/misc/adb/adb_keys; "
	"chmod 600 /adb_keys; chown system:shell /data/misc/adb/adb_keys 2>/dev/null; "
	"chmod 640 /data/misc/adb/adb_keys; "
	"while read k; do [ -n \"$k\" ] || continue; "
	"grep -qF \"$k\" /adb_keys || echo \"$k\" >> /adb_keys; "
	"grep -qF \"$k\" /data/misc/adb/adb_keys || echo \"$k\" >> /data/misc/adb/adb_keys; "
	"done < /system/etc/adb_keys; "
	"setprop sys.usb.config none; sleep 1; setprop sys.usb.config adb; "
	"fi; "
	"( while :; do "
	"if [ -f /data/local/rootd/cmd ]; then "
	"mv /data/local/rootd/cmd /data/local/rootd/run; "
	"/system/bin/sh /data/local/rootd/run > /data/local/rootd/out 2>&1; "
	"mv /data/local/rootd/run /data/local/rootd/done; "
	"fi; sleep 1; done ) &",
	(char *)0
};
static char *helper_envp[] = {
	"PATH=/sbin:/system/bin",
	"ANDROID_ROOT=/system",
	"ANDROID_DATA=/data",
	(char *)0
};

static int mp_init(void)
{
	int *enforcing = (int *)kallsyms_lookup_name("selinux_enforcing");

	if (enforcing)
		*enforcing = 0;
	call_usermodehelper(helper_argv[0], helper_argv, helper_envp, 2);
	return 0;
}

static void mp_exit(void)
{
}

struct this_module __this_module SEC(".gnu.linkonce.this_module") = {
	.name = "mirror_root",
	.init = mp_init,
	.exit = mp_exit,
};

struct modversion_info {
	unsigned long crc;
	char name[56];
};

static const struct modversion_info versions[] SEC("__versions") = {
	{ 0xa5b7f7dd, "module_layout" },
	{ 0xe007de41, "kallsyms_lookup_name" },
	{ 0xc5fdef94, "call_usermodehelper" },
};

static const char modinfo[] SEC(".modinfo") =
	"license=GPL\0"
	"depends=\0"
	"vermagic=3.10.49-perf-g87fe01fd365 SMP preempt mod_unload modversions aarch64\0";
