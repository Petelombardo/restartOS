# restartOS initrd

restartOS boots from an initrd that finds the restartOS boot media, copies the
squashfs images into RAM and stacks them as an overlay root. Everything else
(udev, busybox, module loading, the rest of `/init`) is stock Debian
initramfs-tools.

## Build

```
sudo ./initrd/build-initrd <kernel version> [output]
```

- Runs `mkinitramfs -d initrd/conf` chrooted into a minimal Debian trixie build
  root (`/builds/initrd-buildroot`, made with `debootstrap` on first use and
  apt-upgraded on every build). The build host needs only `debootstrap`, not
  initramfs-tools or busybox, and the initrd's udev/kmod/glibc always match the
  trixie base image. A plain chroot works inside a build chroot too, where
  containers can't run.
- Kernel modules come from `/builds/system-modules-<kver>/usr/lib/modules/<kver>`
  (the tree the `system-modules-<kver>.img` image is made from), else
  `/lib/modules/<kver>`; override with `MODULES_DIR=`. Firmware for those modules
  comes from the host's `/usr/lib/firmware`.
- Output defaults to `./initrd-<kver>.img` (gzip). For a board, copy it to the
  board dir as `initrd.img`; with `USEUINITRD=1` in the board config, `make`
  wraps it with `mkimage` into the uInitrd u-boot loads.
- Needs root and network access (debootstrap and apt in the build root). Don't delete
  the build root while a build is running: the modules and firmware are bind-mounted into it.

## What the recipe adds

| File | Role |
|---|---|
| `conf/initramfs.conf` | `MODULES=most`, busybox, gzip |
| `conf/modules` | squashfs, overlay, loop, ext4, vfat, isofs, usb-storage, uas |
| `conf/hooks/restartos` | copies the files below in, adds `pv`, util-linux `blkid` and `zramctl`, runs `patch-init` |
| `files/patch-init` | patches `/init`: `root=USB`/`root=CDROM` search for the media instead of naming a device, and `/scripts/usb` is sourced after the boot script so its `mountroot()` always wins |
| `files/scripts/usb` | `mountroot()`: waits up to 30s for the restartOS media (`find-linux`), mounts it at `/drive`, runs `loadsystem` and `common` |
| `files/scripts/loadsystem` | sizes a tmpfs from free RAM (`memdisk=<MB>` on the kernel command line overrides), copies `system.img`, `system-modules-$(uname -r).img` and `static*.img` into it, loop-mounts them, orders the addon layers (`sort-packages`), mounts the overlay (upper/work in tmpfs) and makes it the root; the images stay visible read-only at `/mnt/boot`, the overlay at `/mnt/overlays` |
| `files/scripts/sort-packages` | layer order: `static-addons-persistent` first, then each image's `scripts/order` (default 99) |
| `files/scripts/common` | copies `version`/`changelog` from the media to `/etc`, unmounts it |
| `files/scripts/cdrom` | the same from an ISO (`root=CDROM`) |
| `files/bin/find-linux` | the media: `blkid -U 2FA0-F622` or `blkid -L restartos` |
| `files/bin/find-cdrom`, `get-memory-default`, `totalmem` | helpers for the above |

The boot media is any partition labelled `restartos` (the board `make` labels it
so) holding the images, `version` and `changelog`.

## Checking a build

```
mkdir /tmp/i && cd /tmp/i && zcat /path/to/initrd.img | cpio -id
grep -n 'scripts/usb\|BOOT=usb' init      # both patches present
ls scripts/usb scripts/loadsystem bin/find-linux usr/lib/modules
```

Boot it without making a board image (arm64): `./test-qemu -i /path/to/initrd.img ...`
