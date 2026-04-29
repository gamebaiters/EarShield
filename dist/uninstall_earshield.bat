@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo   EarShield - Uninstaller (Windows)
echo ============================================
echo.

set "PLUGINS_DIR=%APPDATA%\TS3Client\plugins"
set "SETTINGS_DB=%APPDATA%\TS3Client\settings.db"
set "CONFIG_INI=%APPDATA%\TS3Client\plugins\EarShield.ini"

set /p WIPE=Delete EarShield.ini config too? [N=keep, Y=full wipe] (default N):
set /p BACKUP=Create a backup on Desktop before deleting? [Y=yes, N=no] (default Y):

if /I "%WIPE%"=="" set "WIPE=N"
if /I "%BACKUP%"=="" set "BACKUP=Y"

echo.
echo Closing TeamSpeak (if running)...
taskkill /F /IM ts3client_win64.exe >nul 2>&1
taskkill /F /IM ts3client_win32.exe >nul 2>&1
timeout /t 1 /nobreak >nul

if /I "%BACKUP%"=="Y" (
    for /f "tokens=1-4 delims=/-. " %%a in ("%date%") do set "STAMP=%%d%%c%%b"
    for /f "tokens=1-2 delims=:." %%h in ("%time: =0%") do set "STAMP=!STAMP!_%%h%%i"
    set "BACKUP_DIR=%USERPROFILE%\Desktop\EarShield_Backup_!STAMP!"
    echo Backing up to "!BACKUP_DIR!"
    mkdir "!BACKUP_DIR!" >nul 2>&1
    if exist "%PLUGINS_DIR%"   xcopy /E /I /Q /Y "%PLUGINS_DIR%"  "!BACKUP_DIR!\plugins"   >nul
    if exist "%SETTINGS_DB%"   copy /Y "%SETTINGS_DB%" "!BACKUP_DIR!\settings.db" >nul
    if exist "%CONFIG_INI%"    copy /Y "%CONFIG_INI%"  "!BACKUP_DIR!\EarShield.ini" >nul
)

echo Removing EarShield binaries...
del /F /Q "%PLUGINS_DIR%\EarShield_win64.dll"        2>nul
del /F /Q "%PLUGINS_DIR%\EarShield_win32.dll"        2>nul
del /F /Q "%PLUGINS_DIR%\volumeleveler_win64.dll"    2>nul
del /F /Q "%PLUGINS_DIR%\volumeleveler_win32.dll"    2>nul
del /F /Q "%PLUGINS_DIR%\VolumeLeveler_win64.dll"    2>nul

echo Removing log files...
del /F /Q "%APPDATA%\TS3Client\earshield*.log"       2>nul

where sqlite3 >nul 2>&1
if %ERRORLEVEL%==0 (
    echo Cleaning settings.db Plugins entries...
    sqlite3 "%SETTINGS_DB%" "DELETE FROM Plugins WHERE value LIKE '%%EarShield%%' OR value LIKE '%%volumeleveler%%'; VACUUM;" 2>nul
) else (
    echo [warning] sqlite3 not found in PATH - skipping settings.db cleanup.
)

if /I "%WIPE%"=="Y" (
    echo Removing EarShield.ini...
    del /F /Q "%CONFIG_INI%" 2>nul
) else (
    echo Keeping EarShield.ini.
)

echo.
echo Done.
echo Type EXIT and press Enter to close this window.
endlocal
cmd /k
