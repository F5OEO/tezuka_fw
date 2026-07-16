@echo off
setlocal

rem Load FSBL then U-Boot over JTAG via a fresh OpenOCD session.
rem
rem IMPORTANT: power-cycle the board before every run. FSBL relies on caches/MMU
rem being disabled at reset; resuming FSBL on top of leftover state from a
rem previous JTAG session reliably crashes it with an MMU translation fault.
rem
rem JTAG USB access on Windows is granted via the driver (e.g. Zadig/WinUSB),
rem not via a per-command elevation, so there is no equivalent of "sudo" here.
rem
rem OpenOCD writes its log/command output to stderr, not stdout. If you're
rem capturing this script's output to a file instead of watching a console,
rem redirect both streams: boot_fsbl_uboot.bat > out.log 2>&1
rem
rem Usage: boot_fsbl_uboot.bat [fsbl.elf] [u-boot.elf]

set "FSBL=%~1"
if "%FSBL%"=="" set "FSBL=fsbl.elf"
set "UBOOT=%~2"
if "%UBOOT%"=="" set "UBOOT=u-boot.elf"

rem Clear out any stale OpenOCD left holding the JTAG adapter from a previous
rem (e.g. crashed) run, so the adapter open below doesn't fail with "busy".
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='openocd.exe'\" | Where-Object { $_.CommandLine -like '*tezuka.cfg*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
timeout /t 1 /nobreak >nul

openocd -f tezuka.cfg ^
    -c "init" ^
    -c "targets zynq.cpu0" ^
    -c "halt" ^
    -c "load_image %FSBL% 0x0 elf" ^
    -c "resume 0" ^
    -c "sleep 6000" ^
    -c "halt" ^
    -c "reg pc" ^
    -c "load_image %UBOOT%" ^
    -c "resume 0x04000000" ^
    -c "sleep 2000" ^
    -c "shutdown"
