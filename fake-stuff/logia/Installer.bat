@echo off

echo.
echo ===============================================
echo ==============  Logia Installer  ==============
echo              Copyright Andras Baki.            
echo ===============================================
echo.

echo.
echo -----------------------------------------------
echo Installing Logia, please wait...               
echo -----------------------------------------------
echo.

call "%~dp0resources\deploy.bat" >nul 2>&1

echo.
echo -----------------------------------------------
echo Logia was installed successfully!              
echo -----------------------------------------------
echo.

pause

