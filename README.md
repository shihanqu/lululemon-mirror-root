# Lululemon Mirror Root

Persistent root on the Qualcomm MSM8916 generation of the Lululemon Mirror, obtained
by replacing a boot-time kernel module inside the system image.

No bootloader unlock. No kernel exploit. No soldering, JTAG or serial console. The
boot and recovery partitions are never written; the kernel stays exactly as shipped.

Verified on two units: kernels `3.10.49-perf-g87fe01fd365` (2022-06-16) and
`3.10.49-perf-g4869d64961b` (2022-10-31), arm64, `CONFIG_MODVERSIONS=y`,
`CONFIG_MODULE_SIG` off, `CONFIG_KALLSYMS_ALL=y`.

---

## Result

| | |
|---|---|
| root | `/system/xbin/su` returns `uid=0(root)` from an ordinary adb shell |
| SELinux | `Permissive` |
| `/system` | remounted read-write at boot |
| persistence | root exists on every boot, with or without Wi-Fi |
| privileged ops | `/data/local/rootd` executes commands with the kernel's full capability set |
| boot chain | boot, recovery, aboot and modem are untouched |

---

## Hardware requirements

* **Mirror with the Qualcomm board** — Snapdragon 410 (MSM8916), Android 6.0.1.
  The newer NXP i.MX8M Mini generation is **not** supported: its boot chain is locked
  differently and this module ABI does not apply.
* **A userdebug system image for the same firmware generation** that ships
  `/system/xbin/su` (mode 04750) and `test-keys` — for example the glass-panes
  `mirror-android-2` image. The stock kernel must remain in place; this project never
  touches it.
* **USB data cable** and a host that can run `adb`/`fastboot`. The mirror's micro-USB
  port is the only connection needed.
* Nothing else: no touch input, no network access and no serial port are required to
  complete the procedure.

Host tooling: `python3`, `e2fsprogs` (`debugfs`, `e2fsck`), `clang` with the
`aarch64-linux-gnu` target, `adb`/`fastboot`, about 6 GB of free disk space.

---

## How it works

1. `init` runs `insmod /system/lib/modules/adsprpc.ko` in its `on boot` action. That
   path does not exist in the target image, so the load simply failed before this
   project existed.
2. We put a 4 KB module at that path. `init` — uid 0, full capabilities — loads it
   every boot, before the framework starts.
3. The module's `init` runs in that root context and:
   * writes `0` to the kernel's `selinux_enforcing`,
   * remounts `/system` read-write,
   * installs `/system/etc/adb_keys` into `/adb_keys` and `/data/misc/adb/adb_keys`
     and cycles `sys.usb.config` so `adbd` re-reads them,
   * starts a command daemon, `/data/local/rootd`, with kernel credentials.
4. `/system/xbin/su`, already present and setuid-root in userdebug images, gives any
   adb shell uid 0.

The trigger is init's own boot action, so the path depends on nothing else: not the
Wi-Fi stack, not the mirror app, not the state of `/data`.

---

## Build

```sh
scripts/build.sh            # produces build/mirror_root.ko
```

The module is a single relocatable object; no kernel tree, no `kbuild`, no linker.
That is possible because the ABI is recovered from the vendor's own module — see
[docs/ABI.md](docs/ABI.md).

`mod/mirror_probe.c` builds a zero-import canary (`scripts/build.sh probe`) that is
useful for validating a different firmware build before risking the real payload: it
only proves that the struct layout, vermagic and `__versions` entry are accepted.

---

## Patch the image

```sh
ADB_KEYS=~/.android/adbkey.pub ADB_TCP_PORT=5555 \
    scripts/patch_image.sh system.img system-rooted.img
```

The script:

* converts the Android sparse image to raw ext4 if necessary,
* writes `build/mirror_root.ko` to `/lib/modules/adsprpc.ko` (0644, root:root),
* writes the adb public key to `/system/etc/adb_keys` (0644, root:root),
* runs `e2fsck -fy`,
* re-encodes the result as a sparse image.

The vendor Wi-Fi driver and its symlink are left exactly as shipped.

`ADB_TCP_PORT` is optional and enables `adbd` on TCP as well, so the unit can be
reached over the network. Two platform details make that work, and both are handled
by the module:

* `adbd` builds its key list from the ramdisk `/adb_keys` when that file exists and
  ignores `/data/misc/adb/adb_keys`, so the module bind-mounts the key file over
  `/adb_keys`; otherwise TCP transports can never authenticate, while USB still can
  because the framework approves those itself.
* `service.adb.tcp.port` is writable only from the `shell` SELinux domain — a uid-0
  write is silently dropped — so the module writes it with
  `runcon u:r:shell:s0 setprop …` and restarts `adbd` to open the listener.

The key file is not optional in practice. `ro.adb.secure=1` comes from the boot
ramdisk's `default.prop`, so `/adb_keys` and `/data/misc/adb/adb_keys` are the only
ways to authorise a host, and a unit that has never seen your key sits at
`unauthorized` — with no touch input, the on-glass prompt cannot be accepted. The
module installs the key at boot and restarts `adbd`; the install is idempotent and a
no-op when the file is absent.

## Flash

```sh
scripts/flash.sh system-rooted.img
```

which is `fastboot flash system` on a sparse image, followed by `fastboot reboot`.
Flashing takes about a minute. Boot, recovery, aboot and modem are not written.

> Flashing replaces the system partition. Keep the original image: rollback is
> reflashing it.

---

## Verify

```sh
adb shell getenforce                       # Permissive
adb shell cat /proc/modules                # mirror_root present
adb shell /system/xbin/su 0 id             # uid=0(root)
adb shell su 0 sh -c 'mount | grep /system'  # rw
adb connect <mirror-ip>:5555               # if ADB_TCP_PORT was used
```

Privileged commands run through the daemon:

```sh
adb shell su 0 sh -c 'echo "id; grep CapEff /proc/self/status" > /data/local/rootd/cmd'
sleep 2
adb shell su 0 cat /data/local/rootd/out   # CapEff: 0000003fffffffff, context kernel
```

`/data/local/rootd` is mode 0700 root:root, so only uid 0 can enqueue work.

---

## Beyond root

[docs/USERSPACE.md](docs/USERSPACE.md) documents the one userspace design that does
not depend on Android's app layer: `/system/bin/surfaceflinger` is replaced by your own
binary, init keeps it running, and fb0 becomes the mixer base layer — geometry, ioctl
sequence and access rules are recorded there. `tools/fb_probe.c` is the freestanding
static binary used to verify it, `tools/sf_stub.sh` the placeholder used while testing.

## Limits

* **The daemon is root.** It runs anything placed in `/data/local/rootd/cmd`. That is
  the point (it bypasses the adb shell's reduced capability bounding set), but treat
  the device as fully compromised.
* **The adb shell itself has a small capability set** inherited from `adbd`
  (`CapBnd=0xc0`). From `su` you can write `/system` and read files by ownership, but
  `mount`, `insmod` and other capability-gated calls must go through `rootd`.
* **`adsprpc.ko` is occupied.** If the image ships a real `adsprpc.ko`, it is replaced
  by this module and the DSP RPC driver will not load. The target image does not
  contain it; check with `debugfs -R "ls /lib/modules" system.img` before patching.
* A module that fails to load cannot brick the device: the boot chain is untouched and
  the failure is a missing feature, recovered by reflashing.
* SELinux is off, so all policy denials become audit-only. This is not a security
  measure; it is a development/unlock technique for hardware you own.

---

## Rollback

Reflash the original system image with `fastboot flash system`. No other partition
was modified.

---

## License

MIT. Use only on hardware you own. Disabling SELinux and installing a root daemon has
obvious security consequences.
