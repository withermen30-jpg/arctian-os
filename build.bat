@echo off
chcp 65001 >nul 2>&1
title Arctian Build

echo ========================================
echo    ARCTIAN BUILD (64-bit)
echo    Moduler isletim sistemi
echo ========================================
echo.

set "WSL=wsl.exe -d Debian"

REM WSL dagitimi var mi? (yoksa komutlar sessizce anlamsiz yola duserdi)
%WSL% -e true
if errorlevel 1 (
    echo [HATA] WSL dagitimi "Debian" bulunamadi.
    echo        Kurulu dagitimlari gormek icin:  wsl.exe -l -v
    pause
    exit /b 1
)

REM Proje kokunu %~dp0 ile belirle (CD farkli olsa da dogru kalir)
set "PROJ=%~dp0"
if "%PROJ:~-1%"=="\" set "PROJ=%PROJ:~0,-1%"
for /f "usebackq tokens=*" %%p in (`%WSL% wslpath -a "%PROJ%" 2^>nul`) do set "WSL_PROJECT=%%p"
if not defined WSL_PROJECT (
    echo [HATA] WSL proje yolu bulunamadi: %PROJ%
    pause
    exit /b 1
)

echo Derleniyor (%WSL_PROJECT%)...
echo.
REM Saat damgasi karsilastirmasi icin isaret dosyasi (derleme basi = "simdi")
set "MARK=%TEMP%\arctian_build_marker"
type nul > "%MARK%"
%WSL% bash -lc "cd '%WSL_PROJECT%' && make -j4"
if errorlevel 1 (
    echo.
    echo [HATA] Derleme basarisiz.
    pause
    exit /b 1
)

REM ----- Ciktilari DOGRULA: RC=0 tek basina yetmez, ISO gercekten uretildi mi? -----
set "ISO=%PROJ%\build\arctian.iso"
set "IMG=%PROJ%\build\arctian.img"

REM 1 = ISO yok, 2 = ISO var ama BU derlemeden onceye ait (pack adimi atlanmis)
for /f "usebackq delims=" %%a in (`powershell -NoProfile -Command "$i=Get-Item -LiteralPath '%ISO%' -ErrorAction SilentlyContinue; if(-not $i){2}else{if($i.Length -lt 1024){2}else{if($i.LastWriteTime -ge (Get-Item -LiteralPath '%MARK%').LastWriteTime){0}else{1}}}"`) do set "ISO_ST=%%a"

if not exist "%IMG%" (
    echo.
    echo [HATA] Disk imaji yok: %IMG%
    pause
    exit /b 1
)
if "%ISO_ST%"=="1" (
    echo.
    echo [HATA] ISO bu derlemeden sonra yenilenmedi - pack adimi calismadi.
    echo        Beklenen: %ISO%
    pause
    exit /b 1
)
if not "%ISO_ST%"=="0" (
    echo.
    echo [HATA] ISO yok veya bos: %ISO%
    echo        xorriso kurulu olmali:  sudo apt install xorriso
    pause
    exit /b 1
)

for %%f in ("%ISO%") do set "ISO_SIZE=%%~zf"

REM Hangi varyant uretildi? (DEBUG=1 -> karsilama/kurulum ekranlari atlanir)
REM version.json tek satir: { "version": "0.7" } -> 3. token, tirnaksiz
for /f "usebackq tokens=3 delims=:, " %%a in ("%PROJ%\version.json") do set "VER=%%~a"

echo.
echo ========================================
echo    ARCTIAN HAZIR
echo ========================================
echo.
echo   Surum     : %VER%  (tam derleme: DEBUG=0 - karsilama + kurulum + hesap)
echo   Debug ISO : make debug  ^<- karsilama/kurulum atlanır, arctian-debug.iso
echo.
echo   ISO imaji : %ISO%  (%ISO_SIZE% byte)   ^<- USB / CD / kalip icin
echo   Disk imaji: %IMG%
echo.
echo   QEMU ile calistir:
echo     make qemu-iso        ^(ISO'dan on yukleme^)
echo     qemu-system-x86_64 -cdrom "%ISO%" -boot d -m 512M -vga std
echo     qemu-system-x86_64 -drive file="%IMG%",format=raw -m 512M -vga std
echo.
pause
