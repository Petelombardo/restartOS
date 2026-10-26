# restartOS 15

restartOS is a Linux system that runs entirely from RAM and **resets to a
pristine state on every boot**. The boot media holds a stack of read-only
squashfs images (the base system plus addons such as a desktop or a browser).
At boot, the initrd copies them into a tmpfs, loop-mounts them, and stacks them
with overlayfs into the root filesystem. Changes live in RAM and vanish on
reboot unless saved with `make-persistent`.

restartOS 15 is based on **Debian 13 (trixie)**. This repo holds every recipe and
script needed to build it:

- the **arm64 edition** for single-board computers (built on Armbian trixie)
- the **amd64 edition**, a hybrid BIOS/UEFI ISO (see [x86](#x86--amd64))

## How it is built

Images aren't built from a package repository. They are **copied from the build
host's own installed packages**. A recipe names seed packages, `resolve.py`
expands them to their dependency closure from the host's dpkg database,
`collect.py` lists their files, and those files are copied into an output tree.
That tree is compressed into a squashfs `.img`. So:

- the build host must run the same Debian release as the image (trixie), on the
  same architecture;
- every package in a recipe must be **installed on the build host** (the build
  stops and prints the `apt install` line otherwise);
- maintainer scripts don't run at build time; what they would do is replayed at
  boot (accounts, triggers; see [docs/recipes.md](docs/recipes.md)).

| Image | Recipe | Built by |
|---|---|---|
| `system.img` (base) | `packages.txt`, `packages-<arch>.txt`, `overlay/`, `overlay-<arch>/` | `build` |
| `static-addons-<name>.img` | `addons/<name>/` | `build-addon <name>` |
| `system-modules-<kver>.img` | the kernel's modules (per board/kernel) | see [Kernel modules](#kernel-modules) |
| initrd | `initrd/` | `initrd/build-initrd <kver>` |
| bootable board image (arm64) | a board dir with `config` | `image/make` |
| bootable ISO (amd64) | an ISO dir with `config` | `image/make-iso` |

Current addons: `labwc` (desktop: labwc, greetd, waybar, pipewire), `chromium`,
`ai` (Claude Code + pi, copied from the build user's installs, no credentials),
`awscli`, `podman`, `gparted`, `nvidia` (amd64: NVIDIA GPU firmware for nouveau), `bluetooth` (bluez + PipeWire BT, on labwc), `mpv` (on labwc), `qemu` (on labwc; KVM + GTK display, per-arch emulator).

## Repo layout

```
README.md            this file
docs/recipes.md      recipe reference: base, addons, create-package, boot-time replay, testing
local.conf.example   per-host settings (copy to local.conf; not tracked)
packages.txt         base seed packages        packages-arm64.txt / packages-amd64.txt
overlay/             files laid over the base  overlay-arm64/ (Armbian MOTD, MT7961 firmware)
build                builds the base           -> /builds/static-addons-system-trixie
build-addon          builds addons/<name>      -> /builds/static-addons-<name>
resolve.py collect.py  dependency closure and file lists from the host dpkg db
addons/<name>/       packages.txt, bases, overlay/, post-build
scripts/compress     squashfs an output tree   -> /builds/packages/<name>.img
scripts/create-package  the engine behind build-addon
scripts/restartos-env   loads local.conf for the scripts
initrd/              initrd recipe (initramfs-tools hook + restartOS boot scripts)
image/make           arm64 board image builder (run from a board dir)
image/make-iso       amd64 hybrid ISO builder (run from an ISO dir)
image/qemu           boots a finished board image under KVM
test-qemu            boots system.img + addons with a board's kernel, without make
update-initrd        legacy: refreshes an existing initrd's userspace
```

## Setting up a build host

restartOS uses a fixed workspace, **`/builds`**. Output trees, images and board
dirs live there, and the scripts refer to it by that path.

1. **Host OS:** Debian 13 trixie on the target architecture. For arm64, Armbian
   trixie provides the kernel and `armbian-firmware`.
2. **Build tools:**
   ```
   sudo apt install git python3 rsync pv squashfs-tools openssl debootstrap \
        parted kpartx dosfstools e2fsprogs qemu-system
   sudo apt install u-boot-tools        # arm64 boards (mkimage for uInitrd)
   sudo apt install xorriso isolinux syslinux-common grub-efi-amd64-bin \
        grub-pc-bin mtools ovmf         # amd64 ISO (ovmf: UEFI test in qemu)
   ```
3. **Workspace and repo:**
   ```
   sudo mkdir -p /builds && sudo chown $USER: /builds
   mkdir -p /builds/packages /builds/scripts /builds/restartOS
   git clone git@github.com:Petelombardo/restartOS.git /builds/minimal-system
   cd /builds/minimal-system
   ln -s $PWD/scripts/compress       /builds/scripts/compress
   ln -s $PWD/scripts/create-package /builds/scripts/create-package
   ln -s $PWD/image/make             /builds/restartOS/make
   ln -s $PWD/image/qemu             /builds/restartOS/qemu
   ln -s $PWD/image/make-iso         /builds/restartOS/make-iso   # amd64
   echo 15a.0 > /builds/restartOS/version     # bumped by every compress
   touch /builds/restartOS/changelog          # compress appends its comment
   cp local.conf.example local.conf           # then edit
   ```
4. **Seed packages:** install everything the recipes list, so the builds find
   it. `sudo ./build` stops with the exact `apt install` line for anything
   missing, and so does `build-addon`.

`local.conf` holds what's specific to one build host: where finished images
are uploaded (empty = no uploads), and whose Claude Code/pi installs the `ai`
addon copies. It is never committed.

## Building

Everything that builds runs as root (file ownership and setuid bits must be
exact). Build in this order: addons are resolved against the **compressed**
images in `/builds/packages`.

```
cd /builds/minimal-system

# 1. base -> /builds/packages/system.img
sudo ./build
ln -sfn static-addons-system-trixie /builds/system
cd /builds/system && sudo ./compress "what changed" && cd -

# 2. addons -> /builds/packages/static-addons-<name>.img
#    (labwc before chromium, bluetooth, mpv and qemu: their bases are system + labwc)
for a in labwc chromium bluetooth mpv qemu ai awscli podman gparted nvidia; do   # nvidia: amd64 only
	sudo ./build-addon $a
	(cd /builds/static-addons-$a && sudo ./compress "what changed")
done

# 3. kernel modules + initrd for each kernel you ship (see below)
sudo ./initrd/build-initrd <kver> /builds/restartOS/<board>/<kver>/initrd.img

# 4. bootable image (arm64 board)
cd /builds/restartOS/<board>/<kver> && sudo ./make
#    or the ISO (amd64)
cd /builds/restartOS/x86/<kver> && sudo ./make-iso
```

`compress` bumps the minor version in `/builds/restartOS/version`, appends your
comment to `/builds/restartOS/changelog`, and keeps the previous image as
`<name>.img.old`. With `UPLOAD_HOST` set in `local.conf`, it also uploads the
image.

### Kernel modules

Each kernel needs `system-modules-<kver>.img` with its modules at
`usr/lib/modules/<kver>`, and the initrd is built from the same tree:

```
mkdir -p /builds/system-modules-<kver>/usr/lib/modules
sudo cp -a /lib/modules/<kver> /builds/system-modules-<kver>/usr/lib/modules/   # or from the kernel's .deb
sudo ln -s /builds/minimal-system/scripts/compress /builds/system-modules-<kver>/compress
cd /builds/system-modules-<kver> && sudo ./compress "modules <kver>"
```

The board image picks up `/builds/packages/system-modules-$MODULES.img`, or a
copy in the board dir.

### Board images (arm64)

A board dir, e.g. `/builds/restartOS/opi5/6.18.54-ophub/`, holds what is
specific to that board and kernel. These are binaries taken from the board's
own (Armbian/vendor) image and are not in this repo:

| File | What it is |
|---|---|
| `mbr.img` | everything before the first partition: partition table area plus the board's u-boot |
| `boot.tar.gz` | the kernel, dtbs, `boot.scr`/`armbianEnv.txt` and so on, unpacked onto the partition |
| `initrd.img` | from `initrd/build-initrd` |
| `make` | symlink to `/builds/restartOS/make` |
| `config` | see below |

```
CUSTOMNAME="opi5"                  # output: /builds/restartOS-opi5.img
MODULES="6.18.54-ophub"            # system-modules-<this>.img
PACKAGES="system.img static-addons-labwc.img static-addons-chromium.img"
OFFSET=32768                       # first partition start, in sectors
FILESYSTEM="ext4"                  # fat | ext2 | ext4 (label: restartos)
INITRDNAME="/boot/uInitrd-6.18.54-ophub"   # where boot.scr loads the initrd from
USEUINITRD=1                       # wrap initrd.img with mkimage
TESTBUILD=1                        # appends -test to the name
#MBTYPE=MSDOS                      # default GPT
#EXTRACMDS="..."                   # run with the partition still mounted
#MKFSOPTS="-O ^orphan_file"
                                   # extra mkfs.ext2/ext4 options; older u-boot
                                   # (e.g. odroidhc4) may not read all of trixie's ext4 defaults
```

`make` writes `mbr.img`, sizes and creates the partition, unpacks
`boot.tar.gz`, copies the images, `version` and the initrd, and produces
`/builds/restartOS-<CUSTOMNAME>.img`, ready to `dd` to an SD card or USB stick.
The initrd finds the partition by its `restartos` label, whatever the boot
config's `root=` says.

### Testing

```
./test-qemu /builds/packages/static-addons-labwc.img -- -g      # arm64: board kernel + initrd under KVM
cd /builds/restartOS/<board>/<kver> && ./qemu                   # boot a finished board image
```

See [docs/recipes.md](docs/recipes.md#testing) for the options.

## x86 / amd64

The image recipes don't depend on the architecture: `resolve.py`, `collect.py`,
`build-addon` and `create-package` work from whatever host they run on. The
per-architecture parts are split out:

- `packages-amd64.txt`: Debian firmware and microcode instead of
  `armbian-firmware`. Firmware needs `non-free-firmware` in the host's apt
  sources.
- `overlay-arm64/` (Armbian MOTD and release file, MT7961 firmware) isn't
  applied on amd64, so x86 gets only the restartOS MOTD. Put x86-only files in
  `overlay-amd64/`.
- `build` knows each architecture's dynamic loader (arm64 and amd64).
- `initrd/build-initrd` works the same on amd64.

The kernel is the build host's Debian kernel. Build the modules image and the
initrd for it as above, then the ISO from an ISO dir:

```
K=$(uname -r)                          # e.g. 6.12.86+deb13-amd64
mkdir -p /builds/restartOS/x86/$K && cd /builds/restartOS/x86/$K
sudo /builds/minimal-system/initrd/build-initrd $K initrd.img
ln -s /builds/restartOS/make-iso make-iso
printf '%s\n' 'CUSTOMNAME="x86"' "MODULES=\"$K\"" \
	'PACKAGES="system.img static-addons-labwc.img static-addons-chromium.img static-addons-gparted.img"' \
	'TESTBUILD=1' > config
sudo ./make-iso                        # -> /builds/restartOS-x86[-test].iso
```

| `config` | |
|---|---|
| `CUSTOMNAME` | output name: `/builds/restartOS-<CUSTOMNAME>.iso` |
| `MODULES` | kernel version: `system-modules-<this>.img` (ISO dir or `/builds/packages`) |
| `PACKAGES` | images from `/builds/packages` |
| `TESTBUILD` | 1 appends `-test` to the name |
| `KERNEL` | optional, default `/boot/vmlinuz-$MODULES` |
| `INITRD` | optional, default `./initrd.img` |
| `APPEND` | optional, extra kernel command line |

`make-iso` builds a hybrid ISO that boots legacy BIOS (isolinux) and UEFI (a
standalone grub `bootx64.efi`, unsigned: turn Secure Boot off), from optical
media or `dd`'d to a USB stick. The boot loaders come from the host's Debian
packages, so the repo holds no boot binaries. The volume label is `restartos`,
which is what the initrd searches for. The EFI grub finds the ISO by a marker
file unique to each build, so another restartOS stick plugged in doesn't
confuse it. The images are grafted into the ISO from `/builds/packages`, so
`/builds` only needs room for the ISO itself.

The ISO also carries `boot.tar.gz` and `efi.tar.gz`, a prebuilt grub kit for
`scripts/install-usb`, which writes it to a stick (BIOS: `boot.img` in the MBR
and `core.img` before partition 1; UEFI: `EFI/boot/bootx64.efi` on the FAT
partition), so the live system needs no grub. Both find `boot/grub/grub.cfg` on
partition 2 of the disk they were loaded from.

Test in qemu (`test-qemu` and `image/qemu` are arm64-only):

```
qemu-system-x86_64 -m 2560 -cdrom /builds/restartOS-x86-test.iso -vga std     # BIOS
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/vars.fd                               # UEFI:
qemu-system-x86_64 -m 2560 -cdrom /builds/restartOS-x86-test.iso -vga std \
	-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	-drive if=pflash,format=raw,file=/tmp/vars.fd
```

## Credentials

No image contains passwords, API keys or personal config. The only account
password is the default `restartos` (`sudo PASSWORD=... ./build` bakes in
another), and the MOTD warns while it's unchanged. The `ai` and `awscli` addons
ship without credentials. Log in on the device and persist the config with
`make-persistent`.
