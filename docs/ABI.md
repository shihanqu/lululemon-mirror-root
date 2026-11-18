# Module ABI recovery

The workspace kernel tree is not public, and a kernel module must match the target's
`struct module` layout and symbol checksums exactly. Both were recovered from files
the vendor shipped.

## 1. struct module from DWARF

`/system/lib/modules/pronto/pronto_wlan.ko` ships **unstripped** (about 67 MB). Its
debug info contains the vendor's `struct module` as compiled into that kernel:

```
DW_TAG_structure_type "module"
  DW_AT_byte_size 0x220
  DW_AT_decl_file "/source/kernel/include/linux/module.h"
  name  @ 0x18   (char[56])
  init  @ 0x120
  exit  @ 0x210
```

That yields a 544-byte section with the same offsets (`mod/mirror_root.c` writes it as
a byte array; only `name`, `init` and `exit` are ever populated, the kernel writes the
rest). Emitting a payload *larger* than the kernel's struct would be safe, smaller
would not; this is exact.

## 2. module_layout checksum

`CONFIG_MODVERSIONS=y`, so the loader requires a `__versions` entry for
`module_layout` and refuses the module if it does not match. The shipped driver
already loads on the target (it is how Wi-Fi works), so its `__versions` section is an
authoritative reference:

```
module_layout = 0xa5b7f7dd
```

Every other symbol a module imports also needs a matching entry, or the loader prints
`no symbol version for X` and returns an error (`check_version()` returns 0 for "not
found" when a `__versions` section exists — this is not a warning).

## 3. Checksums for the imported symbols

Only two symbols are imported, both with primitive-only signatures:

| symbol | declaration | CRC |
|---|---|---|
| `kallsyms_lookup_name` | `unsigned long (const char *)` | `0xe007de41` |
| `call_usermodehelper` | `int (const char *, char **, char **, int)` | `0xc5fdef94` |

They were computed with `genksyms` from Linux 3.10.49 built against the device's
`/proc/config.gz`:

```sh
make O=build ARCH=arm64 olddefconfig      # host-only kconfig
make O=build ARCH=arm64 scripts           # builds scripts/genksyms
clang --target=aarch64-linux-gnu -E -D__GENKSYMS__ <kernel c_flags> file.c \
  | build/scripts/genksyms/genksyms
```

Reliability check: run the same pipeline over the whole tree and compare against the
262 checksums the shipped driver carries. 67 of 172 shared symbols reproduce exactly;
the rest differ where the vendor patched struct types or `va_list` handling. That is
why only primitive-signature symbols are imported — the two above have no struct,
typedef or `va_list` in their type string, so compiler and header differences cannot
affect them.

`module_layout` is never recomputed: its value is copied from the shipping driver, and
the layout it attests to is transcribed from DWARF, so it is consistent by
construction.

## 4. Module format

A kernel module is a relocatable ELF. This one is produced by `clang -c` alone:

```
.text                        ax   init + exit
.gnu.linkonce.this_module    wa   0x220 bytes (struct module)
__versions                   a    {module_layout, kallsyms_lookup_name, call_usermodehelper}
.modinfo                     a    license=GPL, depends=, vermagic=<exact string>
.rodata.* / .data            a    argv/envp arrays
.rela.*                      -    R_AARCH64_ABS64, applied by the loader
```

`vermagic` must be the target's full string, because it is compared verbatim when the
module carries checksums:

```
3.10.49-perf-g87fe01fd365 SMP preempt mod_unload modversions aarch64
```

`license=GPL` is required: `kallsyms_lookup_name` is `EXPORT_SYMBOL_GPL`.

## 5. Load-time failure modes (all non-fatal)

| symptom | cause |
|---|---|
| `no symbol version for X` | imported symbol without a `__versions` entry |
| `disagrees about version of symbol X` | wrong CRC in `__versions` |
| `version magic '…' should be '…'` | vermagic string mismatch |
| `unknown parameter 'x'` | the load path passes a module parameter the module does not declare |

None of these panic: the module is refused, the kernel continues, the feature is
simply absent.
