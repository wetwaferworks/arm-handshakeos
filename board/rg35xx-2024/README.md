# RG35XX 2024 board recipe

Target: Anbernic RG35XX 2024 (base model). Allwinner H700, four Cortex-A53 cores, 1 GB LPDDR4, 640×480 LCD, no Wi-Fi, no Bluetooth. Use the **DDR4** U-Boot/SPL build, not a DDR3 image.

## Before anything else

1. Run [`windows-backup-stock-sd.ps1`](windows-backup-stock-sd.ps1) (or `dd` plus SHA-256 on any Unix) against the **stock** card.
2. Verify the image. Put the stock card back in the device and keep playing on it.
3. All LithoOS work happens on a **second** SD card in TF1.

Play wins ties. The machine that plays Pokemon is never the machine an experiment can brick.

## What to build

| Piece | Version / config | Notes |
|-------|------------------|--------|
| Trusted Firmware-A | `PLAT=sun50i_h616`, current LTS or stable tag | Open, BSD-style. Produces `bl31.bin`. |
| U-Boot | mainline, `anbernic_rg35xx_h700_defconfig` | LPDDR4 (`CONFIG_SUNXI_DRAM_H616_LPDDR4`). Pass `BL31=`. Product: `u-boot-sunxi-with-spl.bin`. |
| Linux | **≥ 6.12** (floor: `CONFIG_SCHED_CLASS_EXT`) | Prefer a current stable. ROCKNIX was on 7.2 as of 17 August 2026. Device tree: `sun50i-h700-anbernic-rg35xx-2024.dtb` (keep the rev6 panel variant handy). |
| Config fragment | [`kernel.config.fragment`](kernel.config.fragment) | `CONFIG_PREEMPT=y`, `CONFIG_HIGH_RES_TIMERS=y`, `CONFIG_NO_HZ_FULL=y`, `CONFIG_SCHED_CLASS_EXT=y`, `CONFIG_BPF_SYSCALL=y`, Panfrost, evdev, AXP717. |
| LCD / HDMI / TF2 / OTG | [`patches/README.md`](patches/README.md) | Not in vanilla 2024 DTS. Start from the ROCKNIX H700 Linux patches, under GPL-2.0, in the kernel tree only. |

Console: `ttyS0,115200`. Sunxi SPL is written at an 8 KiB offset.

## Running the recipe

The script checks the ARM64 cross compiler and prints the exact commands. It does **not** download the kernel unless you pass `--fetch` (that step is long; this pass only verifies the recipe and the toolchain).

```sh
# from the LithoOS tree, on a Linux box
./board/rg35xx-2024/build-bootstack.sh
# CROSS_COMPILE=aarch64-linux-gnu- KERNEL_VERSION=6.12.58 ./board/rg35xx-2024/build-bootstack.sh --fetch
```

Userland: `make cross` in the repo root produces static `build/aarch64/litho-init`, which becomes `/init` on the rootfs.

## What is left before a bootable image

- Fetch and apply the LCD (and, as needed, HDMI, second SD, USB OTG) patches; record their commits.
- Build TF-A, U-Boot, and the kernel with `--fetch`.
- Assemble an SD image: SPL at 8 KiB, FAT boot (Image + dtb + extlinux), ext4 root with `litho-init` as `/init`.
- First boot goal: the panel shows the purple or green handshake screen, and the D-pad moves the tile highlight.

## QEMU virt (done) vs. a flashable RG35XX `.img` (not this pass)

A real ARM64 Linux 6.12 kernel already boots in `qemu-system-aarch64` with static `litho-init` as PID 1 and prints both handshake ledgers to the serial console. See [`board/qemu-virt/run-pid1.sh`](../qemu-virt/run-pid1.sh) and the captured excerpt in [`board/qemu-virt/pid1-serial.txt`](../qemu-virt/pid1-serial.txt).

The ready-to-flash H700 SD image is **not** built yet. Vanilla mainline still lacks the RG35XX 2024 LCD, HDMI, MMC1, and USB OTG host wiring; those come from the ROCKNIX (GPL-2.0) patch set and need care. Fetching TF-A, U-Boot, and a patched 6.12+ kernel also overruns a short session and the remaining ~32 GB disk budget if sources and object trees land together.

Exact remaining steps for `lithoos-rg35xx-2024.img`:

1. On a Linux box with ~40 GB free, back up the stock card first (`windows-backup-stock-sd.ps1` or `dd` + SHA-256). LithoOS only ever goes on a second card.
2. `./board/rg35xx-2024/build-bootstack.sh --fetch` to get TF-A (`PLAT=sun50i_h616`), mainline U-Boot `anbernic_rg35xx_h700_defconfig` (LPDDR4), and Linux ≥ 6.12.
3. Apply the ROCKNIX H700 patches listed in `patches/README.md` (LCD / HDMI / MMC1 / OTG). Record the source commits. Build `sun50i-h700-anbernic-rg35xx-2024.dtb` (rev6 panel variant on hand).
4. Assemble the SD image: SPL at 8 KiB (`dd … seek=8 bs=1024` of `u-boot-sunxi-with-spl.bin`), FAT boot partition (`Image`, dtb, extlinux `console=ttyS0,115200`), ext4 root with static `litho-init` as `/init`, `litho-gb`, and `roms/litho-demo.gb`.
5. SHA-256 the `.img`. Copy it to `D:\scratch\lithoos\` on TheWorkstation to flash when home. The handheld does not enumerate over USB under stock firmware, so the card has to be written in a reader.
