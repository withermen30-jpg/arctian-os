@echo off
chcp 65001 >nul 2>&1
title Arctian Build

echo ========================================
echo    ARCTIAN BUILD (64-bit)
echo    Moduler isletim sistemi
echo ========================================
echo.

set "WSL=wsl.exe -d Debian"

for /f "usebackq tokens=*" %%p in (`%WSL% wslpath -a "%CD%"`) do set "WSL_PROJECT=%%p"
if not defined WSL_PROJECT (
    echo [HATA] WSL proje yolu bulunamadi.
    pause
    exit /b 1
)

echo Derleniyor (%WSL_PROJECT%)...
%WSL% bash -lc "cd '%WSL_PROJECT%' && make -j4"
if errorlevel 1 (
    echo.
    echo [HATA] Derleme basarisiz.
    pause
    exit /b 1
)

echo.
echo ========================================
echo    ARCTIAN HAZIR
echo ========================================
echo.
echo   Disk imaji: build\arctian.img
echo.
echo   QEMU ile calistir:
echo     qemu-system-x86_64 -drive file="%~dp0build\arctian.img",format=raw -m 512M -vga std
echo.
pause
