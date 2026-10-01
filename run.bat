@echo off
setlocal
rem Boots the release image with an optional local USB stick.
rem Options: lan-test, trace (also logs every interrupt and CPU exception, about 6 MB a minute).
set "QEMU=C:\msys64\clang64\bin\qemu-system-i386.exe"
if not exist "%QEMU%" set "QEMU=%ProgramFiles%\qemu\qemu-system-i386.exe"
if not exist "%QEMU%" set "QEMU=qemu-system-i386"
set "IMAGE=%~dp0built\flopnix.img"
set "USB=%~dp0..\flopnix-devtools\media\usb.img"
set "LOG=%~dp0out\qemu_debug.log"
if not exist "%IMAGE%" (
    echo No built\flopnix.img found. Run build.sh or unpack the release into built\.
    exit /b 1
)
if not exist "%~dp0out" mkdir "%~dp0out"
set "NET=user,id=n0,hostfwd=tcp:127.0.0.1:2323-:23"
set "DLOG=guest_errors,cpu_reset,invalid_mem"
set "TRACE="
for %%a in (%*) do (
    if /I "%%~a"=="lan-test" set "NET=user,id=n0,net=192.168.76.0/24,dhcpstart=192.168.76.100,hostfwd=tcp:127.0.0.1:2323-192.168.76.100:23"
    if /I "%%~a"=="trace" set "TRACE=1"
)
if defined TRACE set "DLOG=%DLOG%,int"
set "DBG=-d %DLOG% -D "%LOG%""
echo QEMU log: %LOG%
if exist "%USB%" goto withusb
"%QEMU%" -drive if=floppy,format=raw,file="%IMAGE%" -boot a -m 12 -rtc base=localtime -netdev %NET% -device ne2k_pci,netdev=n0 %DBG%
goto report
:withusb
"%QEMU%" -drive if=floppy,format=raw,file="%IMAGE%" -boot a -m 12 -rtc base=localtime -netdev %NET% -device ne2k_pci,netdev=n0 -device piix3-usb-uhci,id=uhci -drive if=none,id=stick,format=raw,file="%USB%" -device usb-storage,bus=uhci.0,drive=stick %DBG%
:report
set "RC=%errorlevel%"
if not exist "%LOG%" exit /b %RC%
set "FIND=%SystemRoot%\System32\find.exe"
set "FINDSTR=%SystemRoot%\System32\findstr.exe"
for /f %%n in ('%FIND% /c "Triple fault" ^< "%LOG%"') do set "TRIPLE=%%n"
for /f %%n in ('%FIND% /c "CPU Reset" ^< "%LOG%"') do set "RESETS=%%n"
rem QEMU logs each reset twice, and power-on is the first pair.
set /a RESETS=(RESETS-1)/2
for /f %%n in ('%FINDSTR% /v /r /c:"^[ABCDEFGHIJKLMNOPQRSTUVWXYZ][ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]* *=" /c:"^CPU Reset" /c:"^Triple fault" /c:"^check_exception" /c:"^Servicing hardware" /c:"^SMM: " /c:"^ " /c:"^$" "%LOG%" ^| %FIND% /c /v ""') do set "ERRORS=%%n"
echo QEMU: %TRIPLE% triple fault(s), %RESETS% reset(s) after power-on, %ERRORS% guest error line(s)
if not "%TRIPLE%"=="0" if defined TRACE echo Search the log for "Triple fault": the v= lines above it are the faults that led to it.
if not "%TRIPLE%"=="0" if not defined TRACE echo A triple fault skips the FLOPNIX panic screen. Run "run.bat trace" to log the faults that led to it.
exit /b %RC%
