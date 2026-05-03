# Recovery from a bad DTB

If a custom DTB or overlay leaves your Jetson unbootable — no HDMI, no SSH, no network — you don't need to reflash from scratch. NVIDIA ships a recovery workflow that lets you boot a minimal initrd over USB and edit the rootfs remotely. This is what got me out after the failed DTBO experiment.

## What you need

- The Jetson and its USB-C cable
- A host laptop running Ubuntu 20.04 or 22.04 with NVIDIA's Jetson SDK or L4T BSP unpacked. The path is typically `~/nvidia/nvidia_sdk/JetPack_6.2_Linux_JETSON_ORIN_NANO_TARGETS/Linux_for_Tegra/`.
- A jumper or paperclip for the recovery pins on the carrier board, or the recovery button if your carrier has one.
- About 30 minutes.

## Step 1. Force Recovery mode

With the Jetson powered off:

1. Short the FC REC pins on the carrier (or hold the recovery button).
2. Power on the Jetson while still shorting the pins.
3. Release after a couple of seconds.

On the host laptop:

```bash
lsusb | grep -i nvidia
```

You should see something like `Bus 003 Device 042: ID 0955:7523 NVIDIA Corp. APX`. The "APX" device confirms recovery mode.

## Step 2. Boot the initrd over USB

From the L4T directory on the host:

```bash
cd ~/nvidia/nvidia_sdk/JetPack_6.2_Linux_JETSON_ORIN_NANO_TARGETS/Linux_for_Tegra

sudo ./tools/kernel_flash/l4t_initrd_flash.sh \
    --no-flash \
    --network usb0 \
    jetson-orin-nano-devkit-super internal
```

`--no-flash` is the key flag. It tells the script to load an initrd into the Jetson's RAM and bring up a network over USB without touching the eMMC/NVMe.

You'll see a long log. When it finishes, the Jetson is running a minimal Linux from RAM with USB networking up.

## Step 3. SSH into the initrd

The initrd brings up `usb0` at a known address (typically 192.168.1.1 on the host side, with the Jetson at 192.168.1.2).

```bash
ssh root@192.168.1.2
```

Password is empty or `nvidia` depending on BSP version. If it doesn't work, check `ip addr` on the host for the new USB interface and confirm the Jetson's address.

## Step 4. Mount the rootfs and edit boot config

Inside the initrd shell:

```bash
mkdir -p /mnt
mount /dev/mmcblk0p1 /mnt    # or /dev/nvme0n1p1 if you boot from NVMe
ls /mnt/boot/extlinux/extlinux.conf
```

Edit the file with `vi`:

```bash
vi /mnt/boot/extlinux/extlinux.conf
```

Change `DEFAULT` back to a known-good label (usually `primary`):

```
DEFAULT primary
```

Optional but recommended: remove the broken label entirely so the next reboot can't accidentally fall back into it.

Save and exit.

## Step 5. Unmount, reboot, breathe

```bash
sync
umount /mnt
reboot
```

The Jetson will exit the initrd and boot from its own storage with the restored `extlinux.conf`. You should see HDMI signal, SSH, and network come back.

## Notes

- Don't `flash.sh` from panic. It's a full reflash that wipes the rootfs and takes hours. The `--no-flash` initrd path is the right tool here.
- If you can't get the initrd to network correctly, a UART serial console is the next escalation. The Orin Nano carrier exposes UART on the 40-pin header. With a USB-UART adapter you can interrupt the boot menu and select a fallback label without needing HDMI.
- Once you're back up, keep the working `primary` label intact and never make it the only label. Always have a fallback you can pick from the U-Boot or UEFI menu.
