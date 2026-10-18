@echo off
rem Dra din Mario Party 2-ROM och slapp den pa den har filen for att bygga spelet.
rem Drag your Mario Party 2 ROM onto this file to build the game.
if "%~1"=="" (
    echo Dra ROM-filen och slapp den pa BYGG-WINDOWS.bat.
    echo Drag the ROM file onto BYGG-WINDOWS.bat.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_windows.ps1" -Rom "%~1"
pause
