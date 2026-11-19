@echo off
setlocal
cd /d "%~dp0"
echo LithoOS FEL UMS: expose TF1 (mmc 0) as a USB disk for Rufus.
echo Put the EXPERIMENTAL microSD in TF1. Do NOT use the stock card.
echo Only run this AFTER Zadig WinUSB is bound to VID 1F3A PID EFE8.
echo.
sunxi-fel.exe -v -p uboot u-boot-ums-with-spl.bin write 0x4FC00000 boot-ums.scr
echo.
echo sunxi-fel exited %ERRORLEVEL%.
echo If Windows shows a new raw disk, flash lithoos-rg35xx-2024.img with Rufus
echo (image is in D:\scratch\lithoos\). Triple-check the target disk.
pause
