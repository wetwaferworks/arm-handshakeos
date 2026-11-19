@echo off
setlocal
cd /d "%~dp0"
echo LithoOS FEL RAM boot (raw Image, safest first try)
echo Only run this AFTER Zadig WinUSB is bound to VID 1F3A PID EFE8.
echo The 43.7 MB Image transfer over USB 2.0 FEL can take a minute. Wait for it.
echo.
sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl.bin write 0x40080000 Image write 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb write 0x4FF00000 litho-initramfs.uimg write 0x4FC00000 boot-ram.scr
echo.
echo sunxi-fel exited %ERRORLEVEL%. The panel should show LithoOS and stay up.
pause
