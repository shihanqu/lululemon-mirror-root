# ADB access: USB keys and adb over Wi-Fi

Reproducing, from a stock unit, the two things that make a mirror reachable without
a touch panel: a host key that `adbd` accepts, and an optional TCP listener that
survives power cycles.

Everything below is done by the module at boot; the image only supplies two small
files. Nothing is stored on the host side except your own ADB key pair.

---

## Prerequisites

* A unit already carrying the root module (see the main README), or any system image
  you are willing to patch.
* Your host's ADB public key. `adb` creates it on first use; if
  `~/.android/adbkey.pub` is missing:

  ```sh
  adb keygen ~/.android/adbkey
  ```

  The private half stays on your machine and is never copied anywhere.
* Optional: a TCP port you want `adbd` to listen on (conventionally `5555`).

## 1. Patch the image

```sh
ADB_KEYS=~/.android/adbkey.pub ADB_TCP_PORT=5555 \
    scripts/patch_image.sh system.img system-rooted.img
```

Resulting image contents:

| path | content | mode |
|---|---|---|
| `/lib/modules/adsprpc.ko` | the root module | 0644 root:root |
| `/etc/adb_keys` | your public key, one line as `adb keygen` wrote it | 0644 root:root |
| `/etc/adb_tcp_port` | the port, no trailing newline | 0644 root:root |

`ADB_KEYS` is what makes a fresh unit reachable at all; `ADB_TCP_PORT` is the optional
network transport. Omit either and the corresponding step is skipped.

## 2. Flash and boot

```sh
scripts/flash.sh system-rooted.img
```

At boot `init` loads the module from `/system/lib/modules/adsprpc.ko`. The module
then, in a background subshell with retries:

1. sets SELinux permissive and remounts `/system` read-write;
2. copies `/system/etc/adb_keys` to `/data/misc/adb/adb_keys` (`0640 system:shell`)
   when the content differs;
3. bind-mounts that file over the ramdisk `/adb_keys` (see below for why);
4. writes `service.adb.tcp.port` from `/system/etc/adb_tcp_port` if it is present;
5. restarts `adbd` when any of the above changed anything;
6. repeats steps 2–5 up to twelve times, five seconds apart, until the configured
   port is listening.

`insmod` itself returns immediately — only the mount of `/system` happens in the
foreground — so init is never delayed by the adb work.

## 3. Connect

USB:

```sh
adb devices            # <serial>   device
```

Wi-Fi:

```sh
adb connect <mirror-ip>:5555
adb devices            # <mirror-ip>:5555   device
```

There is no discovery for this daemon: after an `adb kill-server`, a host reboot, or
a DHCP address change, run `adb connect` again with the current address.

## 4. Verify

```sh
adb shell getprop service.adb.tcp.port          # 5555, or -1 when disabled
adb shell netstat -ln | grep 5555               # listener present
adb shell cat /proc/modules                     # mirror_root
adb shell getenforce                            # Permissive
adb shell /system/xbin/su 0 id                  # uid=0(root)
adb shell grep ' /adb_keys ' /proc/mounts      # bind mount present
```

With the cable out, everything above still answers over TCP — that is the point of
the exercise.

---

## Why this is not one line of `setprop`

Two platform behaviours on this firmware turn the obvious approach into a silent
failure. Both are handled by the module; knowing them is what makes diagnosis
possible.

**1. `adbd` prefers the ramdisk `/adb_keys`.** When that file exists — it does on
this boot image — the daemon builds its allowed-key list from it and ignores
`/data/misc/adb/adb_keys`. USB still works without it because the framework
(`UsbDebuggingManager`) approves USB debugging over its own socket and writes keys
into `/data`. TCP has no such helper, so a TCP transport can never authenticate
against a key that is only in `/data`. Bind-mounting the `/data` file over
`/adb_keys` puts both paths in agreement.

**2. `service.adb.tcp.port` is writable only from the `shell` SELinux domain.** The
property write returns success and is dropped for any other caller — root included,
so `su 0 setprop …` silently does nothing and the listener never appears. The module
writes it as

```sh
runcon u:r:shell:s0 setprop service.adb.tcp.port 5555
```

with `su 2000 setprop …` as a fallback for images without `runcon`. The listener
only exists after `adbd` restarts, which the module triggers itself.

`persist.adb.tcp.port` is not usable here: the property service does not persist it
on this firmware and `adbd` does not read it, so the port has to be re-asserted at
every boot. That is what the retry loop does.

---

## Troubleshooting

| symptom | cause | check |
|---|---|---|
| `adb devices` shows `unauthorized` over USB | key not in the daemon's list | `/system/etc/adb_keys` exists? `adb shell cat /data/misc/adb/adb_keys` (needs root) contains your line? |
| TCP says `failed to authenticate` while USB works | `/adb_keys` precedence — key in `/data` only | `grep ' /adb_keys ' /proc/mounts`; first line of `/adb_keys` should be your key |
| TCP says `Connection refused` | no listener | `getprop service.adb.tcp.port`; `netstat -ln \| grep <port>` |
| property stays `-1` after `setprop` | wrong SELinux domain | run it under `runcon u:r:shell:s0` |
| works until an OTA flashes `/system` | the image was replaced | re-patch and reflash |

All of these are non-fatal: the boot chain is untouched and the unit still boots.

## Disabling the network listener

```sh
# as a full-capability shell (the root daemon at /data/local/rootd)
rm /system/etc/adb_tcp_port
pkill -9 adbd        # init restarts it without the TCP property
```

USB access, including the key installed in `/data/misc/adb/adb_keys`, is unaffected.

## Security

TCP access is key-authenticated — possession of the private half of the key in
`/etc/adb_keys` is required — but the listener is open on whatever network the mirror
is on. Keep it on a trusted LAN, and remove `/system/etc/adb_tcp_port` when you do
not need it. The repo contains no keys, addresses or identifiers: the public key is
installed from your own file at patch time and lives only in the image you build.
