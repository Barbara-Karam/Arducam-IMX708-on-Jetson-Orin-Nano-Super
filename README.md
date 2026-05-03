# Arducam IMX708 on Jetson Orin Nano Super

A working setup for the Sony IMX708 (Raspberry Pi Camera Module 3 sensor) on the Jetson Orin Nano Super 8GB running JetPack 6.2.2 (L4T R36.5.0), with ROS 2 publishers in Python and C++.

The first approach I tried — building a custom DTBO and patching the bootloader — bricked the device and required USB recovery. The second approach took ten minutes. This document covers both so others don't lose time on the first one.

**Status:** working as of May 2026. Live capture via `nvarguscamerasrc`, V4L2 device exposed at `/dev/video0`, ROS 2 image topic publishing at 30 fps.

**Hardware:** Jetson Orin Nano Super 8GB (P3767-0005 module, P3768 carrier), Arducam IMX708 Wide-Angle, connected to CAM1.

## Contents

- [TL;DR](#tldr)
- [Background](#background)
- [What didn't work: DTBO + UEFI patching](docs/failed-dtbo-approach.md)
- [What worked: Arducam installer](#what-worked-arducam-installer)
- [Step-by-step install](#step-by-step-install)
- [Capture and recording](#capture-and-recording)
- [Color correction caveat](#color-correction-caveat)
- [ROS 2 publisher (Python)](#ros-2-publisher-python)
- [ROS 2 publisher (C)](#ros-2-publisher-c)
- [Gotchas](#gotchas)
- [Recovery from a bad DTB](docs/recovery-procedure.md)
- [Resources](#resources)

## TL;DR

```bash
wget https://github.com/ArduCAM/MIPI_Camera/releases/download/v0.0.3/install_full.sh
chmod +x install_full.sh
./install_full.sh -m imx708          # reboots when done

sudo apt update
sudo apt install -y v4l-utils nvidia-l4t-gstreamer \
    nvidia-l4t-jetson-multimedia-api gstreamer1.0-tools \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav

rm -rf ~/.cache/gstreamer-1.0
gst-inspect-1.0 nvarguscamerasrc | head -5
```

## Background

The IMX708 is the sensor in the Raspberry Pi Camera Module 3. NVIDIA does not ship a driver for it, does not provide ISP tuning files, and does not include it in `jetson-io`. Available options:

| Approach | JP 6.2.2 status | Notes |
|---|---|---|
| RidgeRun `nv_imx708` driver (public repo) | Doesn't apply | Public repo ships patches only for JP 4.6 / 5.1 / 6.0 |
| Custom DTBO + `extlinux.conf` overlays | Doesn't apply | UEFI ignores `extlinux.conf` overlays on JP 6.x |
| Pre-merged DTB via `fdtoverlay` | Possible but fragile | Silent failure on SKU mismatch |
| Build kernel from source with patches | Heavy | Hours of work, breaks on point releases |
| Arducam prebuilt installer | Works | Kernel module matched to L4T 36.5 / kernel 5.15.185-tegra |

Two further surprises after the driver works:

1. NVIDIA's minimized rootfs for Orin Nano omits `nvarguscamerasrc`, `v4l2-ctl`, and the multimedia API. Tutorials assume these are present; on a fresh flash they aren't.
2. Arducam's ISP tuning is generic. Auto-exposure under-exposes, auto-WB pulls magenta. Color correction needs to happen downstream, not in camera config.

## What worked: Arducam installer

Arducam ships a one-line installer that handles the driver, DTB, and `extlinux.conf`. Verify the kernel matches a build they have:

```bash
uname -r                       # e.g. 5.15.185-tegra
cat /etc/nv_tegra_release      # e.g. R36 (release), REVISION: 5.0
```

If `uname -r` matches a kernel Arducam has packaged, the installer downloads `arducam-nvidia-l4t-kernel-t234-nx-<kernel>-<l4t>_arm64_imx708.deb`, installs the prebuilt module, places a merged DTB in `/boot/`, edits `extlinux.conf`, and reboots.

## Step-by-step install

### 1. Connect the camera

Power off completely first.

- Use CAM1 (not CAM0)
- Lift the black plastic latch
- Insert the FFC with blue contacts facing the heatsink
- Press the latch back down, tug-test the cable

### 2. Run the Arducam installer

```bash
cd ~
wget https://github.com/ArduCAM/MIPI_Camera/releases/download/v0.0.3/install_full.sh
chmod +x install_full.sh
./install_full.sh -m imx708
```

Reboot when prompted.

### 3. Verify the driver loaded

```bash
ls /dev/video0
sudo dmesg | grep -iE 'imx708|tegra-capture-vi'
```

Expected:

```
imx708 9-001a: tegracam sensor driver:imx708_v2.0.6
tegra-capture-vi: subdev imx708 9-001a bound
```

The line `imx708 10-001a: probe failed -121` is harmless — that's the kernel probing the unused CAM0 slot at the same I²C address.

### 4. Install missing user-space tools

```bash
sudo apt update
sudo apt install -y \
    v4l-utils \
    nvidia-l4t-gstreamer \
    nvidia-l4t-jetson-multimedia-api \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav

rm -rf ~/.cache/gstreamer-1.0
gst-inspect-1.0 nvarguscamerasrc | head -5
```

### 5. Confirm available capture modes

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
```

| Resolution | Frame rate | Use case |
|---|---|---|
| 4608×2592 | 14 fps | Full 12 MP stills |
| 2304×1296 | 55 fps | 1080p-class video, ML training |
| 1536×864  | 90 fps | Tracking, visual servoing |

## Capture and recording

### Snapshot via Argus

See [`examples/snapshot.sh`](examples/snapshot.sh).

### Video recording

The Orin Nano has no NVENC. `nvv4l2h264enc` is missing because the silicon is gone, not because a package is missing. Use software x264. See [`examples/record_1080p.sh`](examples/record_1080p.sh).

## Color correction caveat

Arducam's ISP tuning produces a persistent magenta cast and lifted blacks regardless of `wbmode`, `gainrange`, or `ispdigitalgainrange`. After testing:

- WB modes 0–9 produce nearly identical output. The driver appears to ignore preset switching for IMX708.
- Manual exposure changes brightness but never fixes color.
- Gray-world WB on the Argus output gives R/G/B means around 106/98/108 — already balanced — yet the visual cast remains. The issue is in the per-pixel color correction matrix, not global gains.

The right place to fix this is in the CV pipeline, against a known color chart. Don't waste time fighting the camera config.

## ROS 2 publisher (Python)

Faster to iterate on. Fine for most pipelines.

```bash
sudo apt install -y ros-humble-cv-bridge ros-humble-image-transport python3-opencv

mkdir -p ~/ros2_ws/src
cp -r ros2/imx708_camera_py ~/ros2_ws/src/

cd ~/ros2_ws
colcon build --packages-select imx708_camera_py --symlink-install
source install/setup.bash
ros2 run imx708_camera_py imx708_publisher
```

## ROS 2 publisher (C++)

Lower latency, lower CPU. Recommended for production.

```bash
sudo apt install -y ros-humble-cv-bridge ros-humble-image-transport \
    libopencv-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

cp -r ros2/imx708_camera_cpp ~/ros2_ws/src/

cd ~/ros2_ws
colcon build --packages-select imx708_camera_cpp
source install/setup.bash
ros2 run imx708_camera_cpp imx708_publisher
```

Verify from another shell:

```bash
ros2 topic hz /image_raw
ros2 topic info /image_raw
```

Run with parameters:

```bash
# Full 12 MP @ 14 fps
ros2 run imx708_camera_cpp imx708_publisher --ros-args \
    -p width:=4608 -p height:=2592 -p fps:=14

# Fast tracking @ 90 fps
ros2 run imx708_camera_cpp imx708_publisher --ros-args \
    -p width:=1536 -p height:=864 -p fps:=90
```

## Gotchas

| Symptom | Cause | Fix |
|---|---|---|
| `nvarguscamerasrc` not found | Minimal rootfs omits `nvidia-l4t-gstreamer` | Install it, then `rm -rf ~/.cache/gstreamer-1.0` |
| `nvv4l2h264enc` not found | Orin Nano has no NVENC silicon | Use `x264enc` (CPU encode) |
| `v4l2-ctl --stream-mmap` hangs | Arducam driver requires the Argus path | Use `nvarguscamerasrc` |
| Persistent magenta cast | Generic ISP tuning, not calibrated for IMX708 | Color-correct downstream |
| `imx708 10-001a: probe failed -121` | Kernel probing unused CAM0 slot | Ignore |
| GStreamer errors after V4L2 testing | `nvargus-daemon` still stopped | `sudo systemctl start nvargus-daemon` |
| `extlinux.conf` overlays not applying | Used `FDTOVERLAYS` (not a real keyword) or wrong SKU in `board_config` | Use `OVERLAYS`, match SKU, or pre-merge with `fdtoverlay` |
| Bricked boot after DTB swap | UEFI loaded a broken merged DTB | See [recovery procedure](docs/recovery-procedure.md) |
| Wi-Fi not auto-reconnecting after reboot | NetworkManager profile is one-shot | `sudo nmcli connection modify "<SSID>" connection.autoconnect yes` |

## Resources

- [Arducam Jetson Quick Start](https://docs.arducam.com/Nvidia-Jetson-Camera/Native-Camera/Quick-Start-Guide/)
- [Arducam MIPI_Camera releases](https://github.com/ArduCAM/MIPI_Camera/releases)
- [NVIDIA Jetson Linux Developer Guide (R36.5)](https://docs.nvidia.com/jetson/archives/r36.4/DeveloperGuide/)
- [NVIDIA Argus API](https://docs.nvidia.com/jetson/l4t-multimedia/group__LibargusAPI.html)
- [GStreamer plugin reference](https://gstreamer.freedesktop.org/documentation/)
- [ROS 2 Humble](https://docs.ros.org/en/humble/)
- [`cv_bridge` package](https://docs.ros.org/en/humble/p/cv_bridge/)
- [RidgeRun IMX708 driver (older JetPacks)](https://github.com/RidgeRun/NVIDIA-Jetson-IMX708-RPIV3)
- [Jetson Force Recovery and `l4t_initrd_flash.sh`](https://docs.nvidia.com/jetson/archives/r36.4/DeveloperGuide/IN/FlashingSupport.html)
- [NVIDIA Developer Forums](https://forums.developer.nvidia.com/)

## License

Documentation: CC BY 4.0
ROS 2 publishers (Python and C++): Apache-2.0
