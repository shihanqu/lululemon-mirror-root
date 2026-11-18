# Replacing the Android userspace

Measured on a Mirror with the Qualcomm MSM8916 board (kernel `3.10.49-perf-g87fe01fd365`,
Android 6.0.1, `MMB29M/11120023:userdebug/test-keys`). Every number below was read from
the running device.

The root module is what makes these experiments possible; nothing here modifies the boot
chain. All of it is reversible.

---

## 1. Display

### fb0

| | |
|---|---|
| `/proc/fb` | `0 mdssfb_80000`, `1 mdssfb_a0000` |
| node | `crw-rw---- system graphics /dev/graphics/fb0` |
| geometry | 1920×1080, 32 bpp, `yres_virtual` 2160 (two buffers), `line_length` 7680 |
| memory | `smem_start=0x19e8000`, `smem_len=16588800` (15.8 MiB) |
| panel | `msm_fb_panel_info`: `TC358775 video mode dsi2lvds bridge`, `primary_panel=1` |
| fb1 | `writeback panel`, 1280×1440 (unused) |

### Access rules found

* `write()` on fb0 is **not implemented**: `dd` returns `ENODEV`. Only `mmap` plus
  `FBIOPUT_VSCREENINFO`/`FBIOPAN_DISPLAY` reach the display.
* The node is `system:graphics 0660`, so uid 0 alone is not enough. The adb shell tree has
  `CapEff=0xc0` (SETUID/SETGID only) and gets `EACCES`. A process with the full capability
  set — anything spawned by the root module — opens it normally.
* The adb shell and `su` cannot stop init services: `setprop ctl.stop surfaceflinger`
  produces `init: sys_prop: Unable to stop service ctl [surfaceflinger] uid:0`. There are no
  `/system`-imported init files (`import` lines all resolve to the boot ramdisk), so services
  cannot be added or removed from the system image.

### Who owns the mixer

With SurfaceFlinger running, the MDSS mixer scans **only** the app's HWC layer:

```
--[ Control path #0 - MIPI_VIDEO]--
Intf Mixer #0  res=1920x1080
SSPP #3 type=RGB ndx=8 stage=2 alpha=0xff blend_op=1
	src 1920x1080 format=13 (RGBA_8888)
	front_buf addr=0x0000000001200000 size=8294400
Total pipes=1
```

Pixels written into fb0 memory at that point are stored but never displayed: the opaque
overlay covers them. They persist for minutes, which is why "the pattern is still in the
framebuffer" proves nothing about what is on the panel.

### Taking the display

SurfaceFlinger is `service surfaceflinger /system/bin/surfaceflinger`, `class main` — a
14 KB stub that links `libsurfaceflinger.so`. Since `/system` is writable, it can be
replaced by a sleeping script, which keeps init satisfied ("running") with no composer.
With the compositor out of the way:

```
Total pipes=0                 # msm_fb does not scan by itself
FBIOPUT_VSCREENINFO=0
FBIOPAN_DISPLAY=0
SSPP #3 type=RGB stage=1
	src 1920x1080 format=13 (RGBA_8888)
	front_buf addr=0x00000000019e9000 size=16588800
Total pipes=1
```

`0x19e9000` is fb0's `smem_start` (+0x1000), 16 588 800 bytes: **the panel is scanning the
framebuffer**. After restoring SurfaceFlinger, the mixer lists two pipes — fb0 as the base
layer plus the app overlay — which is exactly why direct fb0 drawing is invisible while
Android runs.

Procedure used (reversible; the original binary was backed up and the restore verified by
md5 `b5cb516f38b6291b6d0b43e9410e55f3`):

```sh
# stub in place of the composer, then let init restart the service into it
rm -f /system/bin/surfaceflinger
cp stub.sh /system/bin/surfaceflinger && chmod 755 /system/bin/surfaceflinger
pkill -9 -f /system/bin/surfaceflinger
# ... draw ...
# restore
rm -f /system/bin/surfaceflinger
cp sf.real /system/bin/surfaceflinger && chmod 755 /system/bin/surfaceflinger && chown root:shell /system/bin/surfaceflinger
pkill -9 -f /system/bin/surfaceflinger
```

Notes: `cp` over the running binary fails with `Text file busy` — unlink first. The
`surfaceflinger` service runs as `system` in group `graphics`, so a replacement binary gets
fb0 access without any capability tricks.

---

## 2. Audio

Standard ALSA, no vendor daemon required for the kernel interface:

```
0 [msm8x16sndcards]: msm8x16-snd-car - msm8x16-snd-card-sbc
00-00 MultiMedia1    playback/capture
00-01 MultiMedia2    playback/capture
00-04 LPA            playback
00-05 Primary MI2S_RX Hostless
00-09 (COMPR)        playback
...
```

`/dev/snd/controlC0`, `pcmC0D*`, `comprC0D*` are all present.

## 3. Wi-Fi

`/sys/class/ieee80211/phy0` exists and `wlan0` carries a `phy80211` symlink — the vendor
driver is an nl80211 driver, so `wpa_supplicant`/`hostapd` are usable as-is. The driver is
still the stock `wlan.ko`; the root image does not touch it.

## 4. Camera

```
v4l-subdev0..2   msm_csiphy
v4l-subdev3..4   msm_csid
v4l-subdev9      msm_ispif
v4l-subdev8      vfe
v4l-subdev7      cpp
v4l-subdev4/6    msm_actuator / ov5640
video0           msm-config
video1           msm-sensor
```

The sensor is an **OV5640** driven through the MSM ISP; `/dev/video0..1` are the vendor
control nodes, not a standard V4L2 capture API. Consumers are Android's `mm-camera` daemon
and its proprietary ioctls.

## 5. Video decode

`/dev/video32` and `/dev/video33` exist (vidc decoder/encoder) but have empty names and
expose the firmware RPC protocol. There is no V4L2 stateful/stateless codec interface on
this kernel; hardware decode is reachable only through Android's OMX/MediaCodec.

## 6. GPU

`/dev/kgsl-3d0` only. There is no `/dev/dri`, so freedreno cannot drive the Adreno 306
here: 3D acceleration is KGSL plus Android's EGL blobs.

---

## What this means

One design, the one that does not depend on Android's app layer: replace
`/system/bin/surfaceflinger` with your own binary. init starts it at boot as uid
`system` in group `graphics`, so fb0 opens without capability tricks; the mixer
base layer is the framebuffer, panned explicitly; no Java, no SF, no zygote.

Cost: hardware video decode and the camera are not reachable, because both need
Android userspace (OMX/MediaCodec and mm-camera). 3D is limited to what you
implement over KGSL, since freedreno has no `/dev/dri` to bind to. What remains is
a 1920×1080, 32 bpp, two-buffer framebuffer you own, plus ALSA, nl80211 Wi-Fi, USB
and the eMMC.

A framework-free boot that keeps init's service set is not possible from the system
image alone — the composer is the lever, because it is a file in `/system`.

## Device-side services worth knowing

`/init.mirror.rc`:

```
service mapd /system/bin/mapd        # mirror AP daemon
service cfgchkup /system/bin/cfgchkup
on boot
    stop adsprpcd
on property:mirror.cfg.ready=true
    insmod /system/lib/modules/smsc75xx.ko
```

`stop adsprpcd` is why `/lib/modules/adsprpc.ko` is free for the root payload, and `mapd` is
the daemon behind the mirror's access-point mode.

## Verification tool

`tools/fb_probe.c` is the freestanding static binary used for these measurements: raw arm64
syscalls, no libc, `-DPAINT=0` reads geometry and samples, `-DPAINT=1` paints, `-DPAINT=2`
clears. Built with:

```sh
clang --target=aarch64-linux-gnu -fuse-ld=lld -ffreestanding -nostdlib -static \
      -O2 -Wl,-e,_start -DPAINT=0 -o fb_probe tools/fb_probe.c
```
