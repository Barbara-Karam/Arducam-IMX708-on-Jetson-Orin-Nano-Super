# What didn't work: DTBO + UEFI patching

This is the path I tried first, following community guides that recommend the RidgeRun driver. It doesn't work on JetPack 6.2.x. Documenting it here so others can recognize the dead end before sinking time into it.

If you've already started down this path and bricked your boot, see [recovery-procedure.md](recovery-procedure.md).

## Goal at the time

Enable IMX708 on CAM0 by:

1. Cloning RidgeRun's `NVIDIA-Jetson-IMX708-RPIV3` driver
2. Compiling a device-tree overlay (DTBO) for the IMX708
3. Loading the overlay via `/boot/extlinux/extlinux.conf`
4. Booting and seeing `/dev/video0` appear

The plan looked clean on paper. None of the steps worked as expected on JetPack 6.2.2.

## Symptom 1: driver loads, never probes

After installing the kernel module:

```bash
sudo modprobe nv_imx708
lsmod | grep imx708          # module present
dmesg | grep imx708          # nothing
ls /dev/video0               # no such file
```

Reading `/proc/device-tree/` confirmed the live device tree had no IMX708 nodes. The driver was loaded into the kernel but had nothing to bind to.

That points at the device tree, not the driver.

## Symptom 2: `extlinux.conf` overlays silently ignored

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

- `FDTOVERLAYS` is not a real `extlinux.conf` keyword on JP 6.x. The correct one is `OVERLAYS`. The bootloader silently ignores typos — there's no warning.
- Even with `OVERLAYS` corrected, the overlay only affects the **kernel-stage** device tree. JP 6.x boots through UEFI, which loads a base DTB from QSPI and applies its own overlays before kernel handoff. By the time `extlinux.conf` is consulted, UEFI has already finished its DTB work.

Reference table for `extlinux.conf` keywords on JP 6.x:

| Keyword | What it does | Companion required | Notes |
|---|---|---|---|
| `FDT` | Path to base device tree | None | The DTB the kernel will see |
| `OVERLAYS` | Apply DTBOs to that DTB at kernel stage | Must accompany `FDT` | Comma-separated list |
| `FDTOVERLAYS` | Nothing | — | Not a real keyword. Silently ignored. |

## Symptom 3: SKU mismatch in `board_config`

I compiled the DTBO targeting `p3767-0000`. The actual board is `p3767-0005-super`. DTBOs can include a `board_config` node that filters by SKU; if the SKU doesn't match, the overlay is silently dropped.

You can confirm your real board with:

```bash
cat /proc/device-tree/compatible
```

The "p3767-0005-super" string matters. A DTBO meant for the non-super module won't apply.

## Attempted workaround: pre-merge DTB with `fdtoverlay`

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

Keep the original `primary` label as a fallback so you can roll back from the boot menu.

I did this and rebooted. The Jetson came up with no HDMI signal, no SSH, no network, no console. The merged DTB had broken something fundamental — likely because the SKU mismatch meant some nodes hadn't been updated correctly, but without serial console access I couldn't tell which.

The fallback `primary` label only helps if you can reach the boot menu, which requires HDMI. With no display attached, you're locked out.

## How I got out

USB-C Force Recovery mode plus `l4t_initrd_flash.sh --no-flash` to mount the rootfs from a host laptop and edit `extlinux.conf` remotely. Full procedure in [recovery-procedure.md](recovery-procedure.md).

## What I'd do differently

- Skip this approach entirely on JP 6.2.x. Use the Arducam installer.
- If you must work at the device-tree level, always:
  - Verify your board SKU before compiling: `cat /proc/device-tree/compatible`
  - Use `OVERLAYS`, never `FDTOVERLAYS`
  - Keep a `primary` label that points at an unmodified DTB
  - Have UART serial console access wired up before touching DTBs
- For permanent overlay application on JP 6.x, the only reliable path is flashing the overlay into QSPI via NVIDIA's `flash.sh` from a host laptop. That's a full reflash, not a runtime change.

## Why the public RidgeRun repo wasn't enough

The RidgeRun GitHub repo at the time of writing contains:

```
patches_nano/patches/4.6.4_nano_imx708_v0.1.0.patch
patches_orin_nano/patches/5.1.1_nano_imx708_v0.1.0.patch
patches_orin_nano/patches/6.0_orin_nano_imx708_v0.1.0.patch
```

No JetPack 6.2 patch. No prebuilt `.ko`. No `build.sh` script.

Several community guides reference a "v2.0.6-jp62" prebuilt with a tidy `build.sh` workflow. That build is not in the public repo. It appears to be a private or commercial RidgeRun release that those guide authors had access to. If you don't have it, the public repo gives you JP 6.0 patches and a lot of manual porting work to reach 6.2.

The Arducam installer does all of that for you and ships kernel modules built against the exact L4T point release you're running.
