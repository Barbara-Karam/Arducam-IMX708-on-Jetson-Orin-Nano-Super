# What didn't work: DTBO patching, then a JetPack 6.0 downgrade

This is the path I tried first, then second, then third before finally giving up and using the Arducam installer. None of it worked on this hardware with this sensor in 2026, and I burned about four sleepless days proving it. Documenting it here so others can recognize the dead ends before going down them.

If you've already started down one of these paths and bricked your boot, jump to [recovery-procedure.md](recovery-procedure.md).

## Round 1: DTBO patching on JetPack 6.2.2

### Goal at the time

Enable IMX708 on the JetPack version I'd just flashed by:

1. Cloning RidgeRun's `NVIDIA-Jetson-IMX708-RPIV3` driver
2. Compiling a device-tree overlay (DTBO) for the IMX708
3. Loading the overlay via `/boot/extlinux/extlinux.conf`
4. Booting and seeing `/dev/video0` appear

The plan looked clean on paper. None of the steps worked as expected.

### Symptom 1: driver loads, never probes

After installing the kernel module:

```bash
sudo modprobe nv_imx708
lsmod | grep imx708          # module present
dmesg | grep imx708          # nothing
ls /dev/video0               # no such file
```

Reading `/proc/device-tree/` confirmed the live device tree had no IMX708 nodes. The driver was loaded into the kernel but had nothing to bind to. That points at the device tree, not the driver.

### Symptom 2: `extlinux.conf` overlays silently ignored

I started with this in `/boot/extlinux/extlinux.conf`:

```
LABEL primary
    MENU LABEL primary kernel
    LINUX /boot/Image
    INITRD /boot/initrd
    FDT /boot/dtb/kernel_tegra234-p3768-0000+p3767-0005-nv.dtb
    FDTOVERLAYS /boot/tegra234-p3768-imx708.dtbo
    APPEND ${cbootargs} ...
```

Two problems:

- `FDTOVERLAYS` is not a real `extlinux.conf` keyword on JP 6.x. The correct one is `OVERLAYS`. The bootloader silently ignores typos — there's no warning, no log line, nothing.
- Even with `OVERLAYS` corrected, the overlay only affects the **kernel-stage** device tree. JP 6.x boots through UEFI, which loads a base DTB from QSPI and applies its own overlays before kernel handoff. By the time `extlinux.conf` is consulted, UEFI has already finished its DTB work.

Reference table for `extlinux.conf` keywords on JP 6.x:

| Keyword | What it does | Companion required | Notes |
|---|---|---|---|
| `FDT` | Path to base device tree | None | The DTB the kernel will see |
| `OVERLAYS` | Apply DTBOs to that DTB at kernel stage | Must accompany `FDT` | Comma-separated list |
| `FDTOVERLAYS` | Nothing | — | Not a real keyword. Silently ignored. |

### Symptom 3: SKU mismatch in `board_config`

I compiled the DTBO targeting `p3767-0000`. The actual board is `p3767-0005-super`. DTBOs can include a `board_config` node that filters by SKU; if the SKU doesn't match, the overlay is silently dropped.

You can confirm your real board with:

```bash
cat /proc/device-tree/compatible
```

The "p3767-0005-super" string matters. A DTBO meant for the non-super module won't apply.

### Attempted workaround: pre-merge DTB with `fdtoverlay`

The textbook fix when UEFI overlays don't work is to merge the overlay into the base DTB ahead of time and tell `extlinux.conf` to load the merged result:

```bash
sudo fdtoverlay \
    -i /boot/dtb/kernel_tegra234-p3768-0000+p3767-0005-nv.dtb \
    -o /boot/tegra234-p3768-imx708-merged.dtb \
    /boot/tegra234-p3768-imx708.dtbo
```

Then in `extlinux.conf`:

```
LABEL imx708
    MENU LABEL IMX708 Camera (merged DTB)
    LINUX /boot/Image
    INITRD /boot/initrd
    FDT /boot/tegra234-p3768-imx708-merged.dtb
    APPEND ${cbootargs} root=/dev/mmcblk0p1 rw rootwait rootfstype=ext4 ...
```

I kept the original `primary` label as a fallback, which on paper means I could roll back from the boot menu.

### What actually happened

I rebooted. The Jetson came up with no HDMI signal, no SSH, no network, no console. The merged DTB had broken something fundamental — likely because the SKU mismatch meant some nodes hadn't been updated correctly, but without serial console access I couldn't tell which.

The fallback `primary` label only helps if you can reach the boot menu, which requires HDMI. With no display attached, I was locked out.

### How I got out

USB-C Force Recovery mode plus `l4t_initrd_flash.sh --no-flash` to mount the rootfs from a host laptop and edit `extlinux.conf` remotely. Full procedure in [recovery-procedure.md](recovery-procedure.md). When the initrd path didn't cooperate, I gave up and reflashed the whole thing with SDK Manager.

## Round 2: Downgrade to JetPack 6.0 and apply the patches "properly"

The RidgeRun public repo has a JetPack 6.0 patch file. The logic was: if there's no JP 6.2 patch but there's a 6.0 one, downgrade to the version it was written for and follow the recipe exactly.

### What "downgrading" actually means on Orin Nano

You can't just `apt install` a different JetPack version. The bootloader, QSPI firmware, kernel, and userspace all have to match. The only reliable way to switch JetPack versions is to reflash the entire device, and on Orin Nano that means:

1. Install **NVIDIA SDK Manager** on a host laptop running Ubuntu 22.04 (it does not run on 24.04 reliably as of this writing)
2. Put the Jetson into Force Recovery mode (USB-C + REC button)
3. Pick the JetPack version, target storage, and components in SDK Manager
4. Wait 30–60 minutes for it to flash QSPI, kernel, and rootfs
5. Walk through the OEM first-boot setup over USB serial or HDMI
6. Reinstall any apt packages, reconfigure the user, and reconnect to Wi-Fi
7. Repeat every time something goes wrong

If you've never used SDK Manager before, expect another half-day of "why doesn't this work" the first time. It's GUI-only, it's slow, it sometimes fails partway through and you start over, and certain steps need the Jetson to already be online which it can't be until those steps finish.

### What broke

After flashing JP 6.0 and applying the patch series in `patches_orin_nano/patches/6.0_orin_nano_imx708_v0.1.0.patch`:

- The kernel rebuild emitted hundreds of warnings about Tegra-specific symbols. Some patches expected files that had been moved or renamed between L4T point releases inside JetPack 6.0 itself.
- After resolving conflicts manually, the kernel built but panicked on boot with a Tegra capture subsystem fault. Different panic each time, depending on which subset of patches I'd applied.
- I tried applying only the device-tree parts of the patch and skipping the driver parts. That gave me a kernel that booted, but `nv_imx708` failed to insmod.
- I tried applying only the driver parts and skipping the device-tree parts. That gave me a working `insmod` but a driver with nothing to bind to, exactly like Round 1.

After the fifth time I'd reflashed with SDK Manager, I stopped counting. I had the entire SDK Manager workflow memorized — pick image, click flash, ignore the "manual setup" dialog, plug in keyboard and HDMI, type my username, set up Wi-Fi, `apt update`, install build deps, clone repo, apply patch, build kernel, install module, reboot, kernel panic, repeat.

I also tried a fix I found in a Reddit thread, on the theory that maybe a hobbyist had figured something out the official channels hadn't. They had not.

### The other "fix" path: build a recent kernel from scratch

The other option discussed in NVIDIA forums is to download the L4T BSP source, port the IMX708 patches forward to whatever kernel version you're running, build the whole tree, install, and pray. This is a real path for people who do Tegra development daily. For a one-off camera bring-up on a project that needed to ship next week, it's a black hole.

### Why I gave up on RidgeRun

Several community guides reference a "v2.0.6-jp62" prebuilt with a tidy `build.sh` workflow that works on JetPack 6.2. That build is not in the public repo. It appears to be a private or commercial RidgeRun release that those guide authors had access to. If you don't have it, the public repo gives you JP 6.0 patches and the manual porting work I just described.

The RidgeRun GitHub repo at the time of writing contains:

```
patches_nano/patches/4.6.4_nano_imx708_v0.1.0.patch
patches_orin_nano/patches/5.1.1_nano_imx708_v0.1.0.patch
patches_orin_nano/patches/6.0_orin_nano_imx708_v0.1.0.patch
```

No JetPack 6.2 patch. No prebuilt `.ko`. No `build.sh` script. The Arducam installer ships kernel modules built against the exact L4T point release I was running, in 30 seconds, with one command.

## Round 3: Arducam installer

This is what works. See the main [README](../README.md) for the recipe. It does in ten minutes what I spent four days failing at.

## What I'd do differently

- Skip RidgeRun on JP 6.2.x entirely. The public repo doesn't support it, and the people referencing JP 6.2 builds have access to something the rest of us don't.
- Don't downgrade JetPack to chase a driver. SDK Manager reflashes are slow, brittle, and the underlying problem (patches not surviving kernel point releases) follows you anyway.
- If you're going to touch device trees on JP 6.x, have UART serial console hardware wired up before you start. Without it, a single bad DTB and your only recovery path is `l4t_initrd_flash.sh` from a host laptop, which has its own setup story.
- Always check the vendor's own GitHub releases first. In hindsight, the Arducam installer was three Google searches away from where I started, but I went with the most cited solution instead of the simplest one.
- For permanent overlay application on JP 6.x, the only reliable path is flashing the overlay into QSPI via NVIDIA's `flash.sh` from a host laptop. That's a full reflash, not a runtime change. Useful to know but not what you want for a camera bring-up.
