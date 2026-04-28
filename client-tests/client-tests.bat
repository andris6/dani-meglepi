@echo off

echo =========================
echo dani-meglepi-client-tests
echo =========================

echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
	echo Run this script with admin privileges!
	pause
	exit
)

echo (00) Quit.
echo (01) High IRQL fault (Kernel-mode).
echo (02) Buffer overflow.
echo (03) Code overwrite.
echo (04) Stack trash.
echo (05) High IRQL fault (User-mode).
echo (06) Stack overflow.
echo (07) Hardcoded breakpoint.
echo (08) Double Free.
echo (09) Trigger HAL Timer Watchdog.
echo (10) Hang with IRP.
echo (11) Hang with DPC.
echo (12) Deadlock.
echo (13) Paged Leak.
echo (14) Nonpaged Leak.

echo.

set /p choice=

if %choice%==00 (
	exit
) else if %choice%==01 (
	.\notmyfaultc.exe /crash 0x01
) else if %choice%==02 (
	.\notmyfaultc.exe /crash 0x02
) else if %choice%==03 (
	.\notmyfaultc.exe /crash 0x03
) else if %choice%==04 (
	.\notmyfaultc.exe /crash 0x04
) else if %choice%==05 (
	.\notmyfaultc.exe /crash 0x05
) else if %choice%==06 (
	.\notmyfaultc.exe /crash 0x06
) else if %choice%==07 (
	.\notmyfaultc.exe /crash 0x07
) else if %choice%==08 (
	.\notmyfaultc.exe /crash 0x08
) else if %choice%==09 (
	.\notmyfaultc.exe /crash 0x09
) else if %choice%==10 (
	.\notmyfaultc.exe /hang 0x01
) else if %choice%==11 (
	.\notmyfaultc.exe /hang 0x02
) else if %choice%==12 (
	.\notmyfaultc.exe /hang 0x03
) else if %choice%==13 (
	.\notmyfaultc.exe /leak 0x01
) else if %choice%==14 (
	.\notmyfaultc.exe /leak 0x02
) else (
	echo Unrecognized operation. Exiting.
	pause
	exit
)
