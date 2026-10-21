# restartOS 15 recipe reference

How the recipes in this repo work in detail. For setting up a build host and the
build order, start with the top-level [README](../README.md).

restartOS 15 is built from **the build host itself** (Debian 13 trixie; the arm64
edition is built on Armbian trixie): packages installed on the host are copied,
file for file, into squashfs layers. The image's user is `restartos` (uid 1000).
Every image is a *recipe* in this folder; to change an image, edit its recipe and
rebuild — don't hand-edit the output folders, they are wiped on every build.

(v14, Ubuntu noble, was built differently and is not in this repo.)

## Layout

| Path | What it is |
|---|---|
| `packages.txt` | base (system.img) seed packages; dependencies are pulled in automatically |
| `packages-<arch>.txt` | extra seeds for one architecture (`dpkg --print-architecture`: arm64, amd64) |
| `overlay/` | files laid over the base: PAM, nsswitch, sudoers, autologin, MOTD, restartos services, `/scripts` |
| `overlay-<arch>/` | laid over the base after `overlay/` on that architecture (arm64: Armbian MOTD/release, MT7961 firmware) |
| `build` | builds the base into `/builds/static-addons-system-trixie` (= `/builds/system`) |
| `resolve.py` | dependency closure from the host dpkg db; `--provided FILE` skips what lower layers have |
| `collect.py` | package file lists (merged /usr, docs/man/locale dropped, alternatives added) |
| `addons/<name>/` | addon recipes (see below) |
| `build-addon` | builds an addon recipe into `/builds/static-addons-<name>` |
| `scripts/create-package` | used by build-addon; also works by hand for quick addons (`/builds/scripts/create-package` links here) |
| `scripts/compress` | squashfs an output tree into `/builds/packages/<name>.img` (each tree's `./compress` links here) |
| `initrd/` | initrd recipe, built in a Debian container (`initrd/README.md`) |
| `test-qemu` | boots system.img + addons under KVM without running `make` |
| `update-initrd` | legacy: refreshes the userspace of an existing initrd from the host (superseded by `initrd/`) |
| `image/make` | arm64 board image builder, run from a board dir (`/builds/restartOS/make` links here) |
| `image/qemu` | boots a finished board image (`/builds/restartOS/qemu` links here) |
| `image/make-iso` | amd64 hybrid BIOS/UEFI ISO builder, run from an ISO dir (`/builds/restartOS/make-iso` links here; see the README) |

## Workflow

Everything that builds runs as root (ownership and setuid bits must be exact).
If a seed package isn't installed on the host, the build stops and prints the
`apt install` line — install it on the host first, then rebuild.

```
# base
sudo ./build                                   # -> /builds/static-addons-system-trixie
cd /builds/system && sudo ./compress "msg"     # -> /builds/packages/system.img

# addon
sudo ./build-addon labwc                       # -> /builds/static-addons-labwc
cd /builds/static-addons-labwc && sudo ./compress "msg"

# test, then the board image
./test-qemu /builds/packages/static-addons-labwc.img ... -- -g
cd /builds/restartOS/opi5/6.18.54-ophub && sudo ./make
```

`build` and `build-addon` work in `<target>.building` and only replace the target when
they finish, so a failed or interrupted build leaves the last good tree in place
(a leftover `.building` folder is always safe to delete).

Rebuild order matters: an addon is resolved against the **compressed** images in
`/builds/packages`, so rebuild + compress the base before addons that should see
the change. Addons only need rebuilding when what they depend on changes
(create-package warns when a base was built with different package versions).

`compress` names the image after the folder; the folder `/builds/system` points
to always becomes `system.img`.

## Base recipe (`packages.txt`, `overlay/`, `build`)

* **Add a package:** put it in `packages.txt`. Check whether its maintainer
  scripts do anything the base build won't (the base doesn't run postinsts):
  * creates a system account → add the name to `POSTINST_USERS` / `POSTINST_GROUPS`
    in `build` (accounts in sysusers.d are automatic; ids are copied from the host)
  * a service that should run → add it to the `systemctl --root enable` line in `build`
    (ssh is the exception: `/scripts/startup/ssh` generates host keys and starts it)
  * edits a config file (e.g. nsswitch for libnss-mdns) → ship the result in `overlay/`
  * generates a config from /usr/share (sshd_config, chrony, smartmontools' drivedb.h) → copy it in `build`
* **Accounts:** restartos password defaults to `restartos` (`sudo PASSWORD=x ./build`
  bakes another); root is locked (restartos has NOPASSWD sudo); SSH password login is
  on — headless boxes are found via avahi and ssh'd into.
  MOTD `20-restartos` warns while the password is still the default.
* **Hostname:** `restartos-hostname.service` (`/scripts/set-hostname`, early boot, before
  NetworkManager and avahi) renames the stock `restartos` to `restartos-<last 2 MAC
  octets>` (first physical NIC, wired preferred), e.g. `restartos-1a2b.local`, so several
  machines on one LAN don't collide. A custom `/etc/hostname` (made persistent) or
  `systemd.hostname=` on the kernel cmdline is left alone.
* **MT7961 firmware** (MT7921U USB wifi/BT): `overlay-arm64/usr/lib/firmware/mediatek/` holds the
  three files from Debian `firmware-mediatek` 20250410-2 (MediaTek build 2024-11-06),
  overriding armbian-firmware's 2023-05-26 copies; both desktops' adapters had hung
  together after an AP event. Refresh: `apt-get download firmware-mediatek`, `dpkg-deb -x`,
  copy `WIFI_MT7961_patch_mcu_1_2_hdr.bin`, `WIFI_RAM_CODE_MT7961_1.bin`,
  `BT_RAM_CODE_MT7961_1_2_hdr.bin`.
* **qemu-guest-agent:** nothing to enable; its udev rule starts the service only
  when the VM has the `org.qemu.guest_agent.0` virtio channel (Proxmox: Options →
  QEMU Guest Agent), so on bare metal it never runs.
* **NFS server:** `nfs-kernel-server` (pulls in nfs-common and rpcbind, so NFS mounts
  work too). `build` replays its postinsts: the `statd` and `_rpc` accounts, the ucf
  configs (`/etc/exports`, `/etc/nfs.conf`, `/etc/idmapd.conf`, both `/etc/default`
  files) and the `/var/lib/nfs` state files, and enables `nfs-server.service`, as
  Debian does. With the stock empty `/etc/exports` it serves nothing. Export
  directories on a real disk mount: the overlay root isn't mounted with
  `nfs_export=on`, so the kernel refuses to export it. Keep exports across reboots
  with `make-persistent add /etc/exports`, then `sudo exportfs -ra`.
* **LVM and software RAID:** `lvm2` volume groups and `mdadm` arrays activate by
  themselves through their udev rules when the disks appear. mdadm's postinst output
  is shipped in `overlay/`: `etc/mdadm/mdadm.conf` (no ARRAY lines; `HOMEHOST <ignore>`
  so arrays keep their names whatever the per-machine hostname is) and
  `etc/default/mdadm`. The monthly scrub and the mdmonitor mail timers aren't enabled
  (no MTA, and a live system rarely runs for a month).
* **cloud-guest-utils:** `growpart` grows a partition after its virtual disk is
  enlarged, and `ec2metadata` reads EC2 instance metadata. No maintainer scripts. GPT
  is resized with `sfdisk` (from `fdisk`), so gdisk, a Recommends, is left out.
* **Name resolution:** `hosts: files dns mdns4_minimal` — unicast DNS first (split DNS
  can use `.local` domains like corp.local), mDNS only as the fallback.
* **Locale:** set in `overlay/etc/locale.conf`; `/etc/default/locale` must stay a symlink
  to it — trixie's systemd tmpfiles rule replaces it with that symlink at every boot, so
  a plain file there is lost and the desktop session ends up without UTF-8.
* **User managers wait for restartos.service** (`user@.service.d/10-restartos.conf`):
  addons' replayed postinsts enable user units (pipewire.socket, ...); tty1/serial
  autologin would otherwise start restartos' user manager before that, it never loads
  them, and the labwc session comes up without PipeWire (portal-wlr crash-loops, the
  portal stalls ~50s, waybar dies) until the next login. Seen once the podman addon's
  extra postinsts pushed pipewire's replay past the autologin.
* **/scripts** is restartOS's own folder (from v14): `startup/` and `shutdown/` run via
  restartos.service / restartos-shutdown.service, `persistent-dirs.txt` is the
  make-persistent list, `packages/<image>` the per-image deb manifests.
* **make-persistent** saves to `static-addons-persistent.img.new` on the boot media, verifies
  it (read back + compare), then swaps it in; the previous archive stays as `.img.bak`
  (`make-persistent restore` swaps them back). It refuses to save over an archive it can't
  read. The initrd only loads `static*.img`, so `.new`/`.bak` are ignored at boot.
* Never bake credentials into an image (claude login, pi `auth.json`/`mcp.json`,
  ssh keys): log in on the device and `make-persistent add` instead.

## Addon recipes (`addons/<name>/`)

| File | Purpose |
|---|---|
| `packages.txt` | packages for the addon; deps already in the bases are skipped, missing ones included |
| `bases` | optional: images below this addon, one per line (default `/builds/packages/system.img`) |
| `overlay/` | extra files; `overlay/home/<user>/...` is owned by that user, the rest by root |
| `post-build` | optional bash run as root with `$DEST`, for non-deb content (vendor binaries) |

Current addons:
* **labwc** — labwc + xwayland, greetd (autologin restartos into labwc on VT7,
  tuigreet after logout), waybar/swaync/swaybg/swayidle/swaylock, portals, pipewire,
  polkit + lxpolkit, lxterminal (overlay config: black background at 90% opacity)/thunar/wofi, fonts (emoji, font-awesome, symbola,
  cantarell, Noto Sans Symbols 2 from the host; `etc/fonts/conf.d/05-restartos-color-emoji.conf`
  strips the emoji blocks from DejaVu/Symbola for fontconfig fallback, and `post-build`
  ships DejaVu Sans copies with the emoji unmapped — apps use the primary font's own
  glyphs, so the file itself has to lose them; needs python3-fonttools on the host). Config = the build host
  user's labwc + waybar config at the time; the portal restart in autostart is backgrounded.
  Monitor hotplug (unplug, power-cycle, deep sleep) destroys the wlroots output and
  brings it back as a new one at scale 1, so scaling is reapplied on every output
  change by kanshi (`~/.config/kanshi/config`), not a one-shot wlr-randr. kanshi
  can't match by resolution, so its catch-all profile sets no scale and runs
  `restartos-display autoscale`, which scales each output by the shorter side of
  its current mode: 1440 and up (2K, 4K, 1440p ultrawides) gets 2, 1080 gets 1.2,
  smaller gets 1. A per-monitor profile with its own `scale` overrides it (exec
  `restartos-display changed` there instead). Waybar tears down its bar on output loss and kills processes its
  on-click handlers started, so every app launcher in the waybar config goes
  through `setsid -f`. After a reconnect labwc 0.8.3 leaves windows sized to the
  full screen, covering waybar (layer bottom); a waybar reload (SIGUSR2) makes
  labwc re-fit them. Screen blanking: some monitors (the KTC H24T7) drop HDMI
  hotplug every ~12s while they have no signal, and labwc would re-add each as a
  powered-on output, so the screen came back on overnight. So rc.xml sets
  `autoEnableOutputs=no`, kanshi (`output * enable`) is the only thing
  that enables outputs, and `/usr/bin/restartos-display` (swayidle timeout/resume
  + kanshi exec) stops kanshi while idle and reloads waybar on resume/reconnect,
  and again after swaylock exits (swayidle locks via `restartos-display lock`): a
  refit done while the lock screen is up doesn't stick.
  If kanshi isn't running, outputs stay dark. Test without touching the monitor:
  `echo off > /sys/class/drm/card*-HDMI-A-1/status`, then `echo detect`.
  USB audio with a bogus mixer dB range (check `/proc/asound/cardN/usbmixer`: the
  EMEET OfficeCore M1A claims 0..0.39 dB over 0..100 steps) gets all-or-nothing
  volume with its LEDs out of sync; `etc/wireplumber/wireplumber.conf.d/51-emeet-volume.conf`
  sets `api.alsa.ignore-dB` for it so the device's own volume follows the system.
  Add other such devices to its `matches`.
  Desktops: 4 (`<desktops number="4" />`). Super+1..4 go to one, Ctrl+Super+Left/Right
  step, Shift+Super+1..4 and Ctrl+Shift+Super+Left/Right take the focused window along;
  Super+arrows stay the default SnapToEdge tiling. Debian's waybar 0.12 has no module
  for labwc's ext-workspace-v1 protocol, so the bar's desktop buttons are
  `custom/desktop1..4` running `restartos-workspaces watch N` (on click: `activate N`),
  a small client in `src/restartos-workspaces.c` that `post-build` compiles into
  `/usr/bin` (needs libwayland-dev + wayland-protocols on the host). Changing the
  number of desktops: edit rc.xml and the waybar group/buttons/style together.
  Extras: clipboard history (`wl-paste --watch cliphist store` in autostart, Super+V
  picks an entry through wofi), wlsunset night light 19:30-07:00 (autostart; needs the
  display driver's gamma control), wdisplays (monitor layout GUI; kanshi still applies
  its own config on the next output change, so copy what works into it), thunar-volman,
  ristretto (image viewer) + tumbler (thumbnail D-Bus service for ristretto/thunar).
  Clone buttons: waybar's `custom/clone-source` and `custom/clone-target` run
  `/usr/bin/launch-clone-source|target`, which open lxterminal and run the base's
  `/usr/bin/clone-*` (links to `/scripts`, made by `build`, as are the
  `/usr/share/pixmaps/clone-*.png` icons) through `sudo -n`: both need root (raw
  disk reads/writes, swapoff, swappiness, udev).
* **chromium** — bases system + labwc; chromium plus the v14 `launch-chromium` wrapper. The wrapper passes `--disable-accelerated-video-decode`: on RK3588 the V4L2 hardware decoder crashed the GPU process when Zoom screen shares arrived, and Chromium then dropped GPU acceleration entirely. Even with that, Zoom screen shares from some presenters still crashed it inside Mesa 25.0 Panfrost (`glTexImage2D` → `panfrost_ptr_unmap` → `util_copy_rect` writing past the buffer), so `overlay/etc/chromium.d/panfrost` exports `PAN_MESA_DEBUG=linear` (Debian's `/usr/bin/chromium` sources `/etc/chromium.d/*`); drop it once the base moves to a newer Mesa.
  `fonts-noto-cjk` gives Japanese/Korean/Chinese glyphs; `post-build` removes the Serif CJK faces (~51 MB), so CJK serif text falls back to Noto Sans CJK (~38 MB kept).
* **ai** — git from debs; `post-build` copies Claude Code (native binary, auto-update
  off via `/etc/claude-code/managed-settings.json`) to `/opt/claude` and pi (+ its own
  node) to `/opt/pi` from the host user's installs. No user config is shipped.
  pi is optional: if the host user has no pi install, the addon ships Claude Code only.
  `/usr/local/bin/pi` is a wrapper that sets `PI_CODING_AGENT_SESSION_DIR=.pi/sessions`
  (unless a session dir is already set by env or settings), so sessions live in the
  folder pi is started from and survive reboots when that folder is persistent.
* **podman** — podman + podman-compose with the Recommends that make it usable:
  passt (rootless network), uidmap (setuid newuidmap), catatonit, aardvark-dns
  (compose service names resolve), nftables. Overlay: `/etc/subuid`/`subgid` for
  restartos (100000:65536, rootless) and a registries.conf.d drop-in so bare image
  names resolve on docker.io. No storage.conf: the driver auto-detects (overlay, else vfs).
  Ported v14 helpers (v14 tree: `/builds/static-addons-podman.v14`): `/etc/podman.conf`
  (`ENABLED=1`, `DEVICE`, `FOLDER`, `TYPE=root|user`; persist it with make-persistent),
  `/scripts/startup/podman` mounts the disk and runs `install-podman` (binds
  `FOLDER/var/lib/containers`, `FOLDER/var/tmp`, `FOLDER/containers` → rootless storage;
  runtime dirs stay on tmpfs), then starts `restartos-podman.service`, which runs
  `FOLDER/podman/control-everything start|stop` (podman-compose on every
  `FOLDER/podman/*/*.yml`, pihole last) after network-online with no start timeout —
  restartos.service runs before the network and has a 120s limit. `TYPE=user` enables
  linger for restartos and runs compose rootless in its user session.

* **awscli** — Debian's `awscli` (AWS CLI v2, bundles its own botocore) plus python3
  and its libraries, groff-base for `aws help`. `post-build` copies the host's
  `__pycache__` next to the addon's python files so the replayed py3compile postinsts
  find them up to date instead of recompiling into tmpfs at every boot. No AWS config:
  `aws configure` on the device, then `make-persistent add /home/restartos/.aws`.
* **gparted** — bases system + labwc; gparted plus the filesystem tools the base lacks
  (mtools for FAT labels, udftools, jfsutils; trixie has no hfsprogs, so no HFS/HFS+). ext*, FAT, NTFS (ntfs-3g has
  ntfsresize), exFAT, btrfs, xfs, f2fs, LVM and LUKS tools are in the base, along with
  gddrescue. Debian's `/usr/sbin/gparted` launcher uses pkexec, which drops
  WAYLAND_DISPLAY, and root can't open Xwayland (Debian builds it without the xhost
  workaround), so the overlay adds `/usr/local/bin/gparted` (first in PATH):
  `sudo -n --preserve-env=WAYLAND_DISPLAY,XDG_RUNTIME_DIR,DISPLAY`, i.e. native Wayland as root.
  `post-build` points the menu entry at it. A hidden `gpartedbin.desktop` gives the window
  its taskbar/titlebar icon (GTK's app_id is the binary name). The polkit rule still
  lets the stock pkexec path run without a password.
* **nvidia** — amd64 only; `firmware-nvidia-graphics` (non-free-firmware), the firmware
  the kernel's nouveau driver loads. RTX 30/40 (Ampere/Ada) need its GSP images or
  nouveau doesn't drive them at all, so without this addon labwc has no display on
  them; Turing and older use it for acceleration. It's an addon for its size (~61 MB
  uncompressed). The initrd loads no GPU drivers, so nouveau probes after the images
  are stacked and finds the firmware here. Blackwell (RTX 50) has no nouveau support
  in the 6.12 kernel, so it gets no GUI either way.
* **bluetooth** — bases system + labwc (pipewire/wireplumber); `bluez` (bluetoothd +
  bluetoothctl) and `libspa-0.2-bluetooth` (PipeWire's A2DP/HFP plugin, version must
  match the base's `libspa-0.2-modules`). Install Debian's bluez on the host
  (`bluez=5.82-1.1`), not Armbian's `+rpt` build. The overlay enables
  `bluetooth.service` (bluetooth.target.wants + dbus-org.bluez alias) in case the
  replayed postinst doesn't. Pair with bluetoothctl (`power on`, `scan on`, `pair`/`trust`/`connect <MAC>`),
  then `make-persistent add /var/lib/bluetooth` so pairings survive reboots.
  WirePlumber switches earbuds to HFP (mono voice mic) when an app opens the mic and back
  to A2DP afterwards; for better listening on calls, use another mic and keep the earbuds on A2DP.
* **mpv** — bases system + labwc; just `mpv`, kept out of labwc because its ffmpeg
  stack adds ~140 MB (uncompressed) of new dependencies.
* **glusterfs** — GlusterFS built from source, not from debs: configure + make in
  `/builds/source/glusterfs/glusterfs-<ver>` first (`GLUSTER_SRC=` picks another tree).
  `packages.txt` only lists its runtime libraries; `post-build` runs `make install-strip`
  into the addon (python modules forced into `python3/dist-packages`, since configure
  computes `python3.1/site-packages`), folds `/sbin` into `usr/sbin` and drops `/var/run`
  so the base's merged-usr and `/run` symlinks aren't shadowed (`/run/gluster` comes from
  tmpfiles.d), removes headers/static libs, and enables glusterd. It then ldd-checks every
  ELF in the addon: libraries from packages no image ships stop the build with the list to
  add to `packages.txt`; libraries no package owns are copied in. Persist `/var/lib/glusterd`
  (node UUID, peers, volumes) on the device. The unversioned addon takes the newest tree.
* **glusterfs-10.5** — the glusterfs recipe pinned to the 10.5 tree: `packages.txt` and
  `overlay/` are symlinks to glusterfs's, `post-build` sets `GLUSTER_SRC` and sources
  glusterfs's. An alternative to the glusterfs addon, not a layer on it (same paths).
  The 10.5 tarball's `config.guess`/`config.sub` are stubs: run `./autogen.sh` before
  `../custom-configure`, or configure writes garbage triplets into the Makefiles.

To refresh claude/pi versions: update them on the host, then rebuild the ai addon.

### What create-package does for each addon

dpkg normally runs maintainer scripts at install time; restartOS replays them at boot:

* `scripts/packages/static-addons-<name>` — manifest (`package version`)
* `scripts/postinst.d/<pkg>.preinst|postinst|accounts` — the package's scripts and
  the system accounts it needs (with the host's uid/gid)
* `scripts/startup/00-postinst-<pkg>` → base `/scripts/postinst-run <pkg>`, which runs
  with shims (`/scripts/postinst-shims`: service calls become `systemctl --no-block`,
  dpkg-trigger / dpkg-maintscript-helper / update-initramfs are no-ops)
* `scripts/postinst.d/triggers.static-addons-<name>` — dpkg file triggers the addon
  activates (gsettings schemas, font cache, icon cache, ...), run by the base's
  `/scripts/startup/01-postinst-triggers`
* `etc/systemd/system/<unit>.d/10-restartos-postinst.conf` — `After=restartos.service`
  so services wait for their accounts/dirs

## Testing

`test-qemu` makes a throwaway disk from the images and boots the board's kernel
and initrd. Options after `--` go to `/builds/restartOS/qemu`:
`-g` graphics (window, or VNC :5901 without qemu-system-gui), `-l log -t secs`
headless, `-x cmdfile` run commands at the serial prompt and print output,
`-S shot.png` screenshot. Quit the console with Ctrl-A X.

## Known open items

* tmpfs overlay only gets ~5% of free RAM headroom (loadsystem) unless `memdisk=`
  is in armbianEnv.txt; the 6.18.54-ophub board env has none.
* waybar config uses JetBrainsMono Nerd Font glyphs (not installed anywhere),
  swaybg wallpaper path doesn't exist, waybar temperature points at x86 coretemp.
* MOTD mentions a `commands` command that doesn't exist.
* Persisting `/etc/shadow` with make-persistent is untested against boot-time
  account creation by addons.
