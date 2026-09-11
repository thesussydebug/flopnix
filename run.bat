@echo off
setlocal
rem Boots the release image with an optional local USB stick.
set "QEMU=C:\msys64\clang64\bin\qemu-system-i386.exe"
if not exist "%QEMU%" set "QEMU=%ProgramFiles%\qemu\qemu-system-i386.exe"
if not exist "%QEMU%" set "QEMU=qemu-system-i386"
set "IMAGE=%~dp0built\flopnix.img"
if not exist "%IMAGE%" (
    echo No built\flopnix.img found. Run build.sh or unpack the release into built\.
    exit /b 1
)
set "NET=user,id=n0"
if /I "%~1"=="lan-test" set "NET=user,id=n0,net=192.168.76.0/24,dhcpstart=192.168.76.100"
if exist "%~dp0usb.img" goto withusb
"%QEMU%" -drive if=floppy,format=raw,file="%IMAGE%" -boot a -m 64 -rtc base=localtime -netdev %NET% -device ne2k_pci,netdev=n0
exit /b %errorlevel%
:withusb
"%QEMU%" -drive if=floppy,format=raw,file="%IMAGE%" -boot a -m 64 -rtc base=localtime -netdev %NET% -device ne2k_pci,netdev=n0 -device piix3-usb-uhci,id=uhci -drive if=none,id=stick,format=raw,file="%~dp0usb.img" -device usb-storage,bus=uhci.0,drive=stick
exit /b %errorlevel%
