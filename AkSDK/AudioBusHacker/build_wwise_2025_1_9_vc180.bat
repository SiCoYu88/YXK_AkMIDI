@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0"
cd /d "%PROJECT_ROOT%"

if not defined WWISEROOT set "WWISEROOT=H:\Audiokinetic\2025.1.9.9197"
if not defined PLUGIN_VERSION set "PLUGIN_VERSION=2025.1.9.9197"
set "WP=%WWISEROOT%\Scripts\Build\Plugins\wp.py"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=all"

if /I "%ACTION%"=="help" goto usage
if /I "%ACTION%"=="--help" goto usage
if /I "%ACTION%"=="-h" goto usage

call :find_python
if errorlevel 1 exit /b 1

call :preflight
if errorlevel 1 exit /b 1

if /I "%ACTION%"=="all" goto action_all
if /I "%ACTION%"=="premake" goto action_premake
if /I "%ACTION%"=="build" goto action_build
if /I "%ACTION%"=="package" goto action_package
if /I "%ACTION%"=="verify" goto action_verify

echo [ERROR] Unknown action: %ACTION%
goto :usage_error

:action_all
call :premake
if errorlevel 1 exit /b 1
call :build
if errorlevel 1 exit /b 1
call :package
if errorlevel 1 exit /b 1
call :verify
if errorlevel 1 exit /b 1
echo [SUCCESS] all completed.
exit /b 0

:action_premake
call :premake
if errorlevel 1 exit /b 1
echo [SUCCESS] premake completed.
exit /b 0

:action_build
call :build
if errorlevel 1 exit /b 1
echo [SUCCESS] build completed.
exit /b 0

:action_package
call :package
if errorlevel 1 exit /b 1
echo [SUCCESS] package completed.
exit /b 0

:action_verify
call :verify
if errorlevel 1 exit /b 1
echo [SUCCESS] verify completed.
exit /b 0

:find_python
if defined PYTHON_EXE (
    if not exist "%PYTHON_EXE%" (
        echo [ERROR] PYTHON_EXE does not exist: %PYTHON_EXE%
        set "FAILED_STEP=Find Python 3"
        exit /b 1
    )
    set "PYTHON_LAUNCHER="%PYTHON_EXE%""
    goto python_found
)

py -3 -c "import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)" >nul 2>&1
if not errorlevel 1 (
    set "PYTHON_LAUNCHER=py -3"
    goto python_found
)

python -c "import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)" >nul 2>&1
if not errorlevel 1 (
    set "PYTHON_LAUNCHER=python"
    goto python_found
)

echo [ERROR] Python 3 was not found.
echo         Install Python 3 or set:
echo         set "PYTHON_EXE=C:\Path\To\Python3\python.exe"
set "FAILED_STEP=Find Python 3"
exit /b 1

:python_found
exit /b 0

:preflight
echo ============================================================
echo AudioBusHacker - Wwise 2025.1.9.9197 - vc180 / x64
echo Project: %PROJECT_ROOT%
echo Wwise:   %WWISEROOT%
echo Version: %PLUGIN_VERSION%
echo Action:  %ACTION%
echo ============================================================

if not exist "%WP%" (
    echo [ERROR] Wwise plug-in tool was not found: %WP%
    set "FAILED_STEP=Environment check"
    exit /b 1
)

if not exist "%WWISEROOT%\SDK\include\AK\SoundEngine\Common\AkTypes.h" (
    echo [ERROR] Wwise SDK is incomplete. AkTypes.h was not found.
    set "FAILED_STEP=Environment check"
    exit /b 1
)

if not exist "%PROJECT_ROOT%PremakePlugin.lua" (
    echo [ERROR] The script is not running from the plug-in root.
    set "FAILED_STEP=Environment check"
    exit /b 1
)

call %PYTHON_LAUNCHER% --version
if errorlevel 1 (
    set "FAILED_STEP=Check Python 3"
    exit /b 1
)

exit /b 0

:premake
echo.
echo ===== Generate Visual Studio 2026 x64 vc180 projects =====
call :run premake Windows_vc180 -t vc180 --disable-codesign
if errorlevel 1 exit /b 1
exit /b 0

:build
echo.
echo ===== Build SoundEngine x64 vc180 Debug / Profile / Release =====
call :run build Windows_vc180 -c Debug -x x64 -t vc180
if errorlevel 1 exit /b 1
call :run build Windows_vc180 -c Profile -x x64 -t vc180
if errorlevel 1 exit /b 1
call :run build Windows_vc180 -c Release -x x64 -t vc180
if errorlevel 1 exit /b 1
exit /b 0

:package
echo.
echo ===== Create x64 vc180 SDK package =====
call :run package Windows_vc180 -v %PLUGIN_VERSION%
if errorlevel 1 exit /b 1
call :run generate-bundle -v %PLUGIN_VERSION%
if errorlevel 1 exit /b 1
exit /b 0

:verify
echo.
echo ===== Verify x64 vc180 SDK outputs =====
set "VERIFY_FAILED=0"

call :check_file "%WWISEROOT%\SDK\x64_vc180\Debug\bin\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\SDK\x64_vc180\Profile\lib\AudioBusHackerFX.lib"
call :check_file "%WWISEROOT%\SDK\x64_vc180\Release\bin\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h"
call :check_file "%PROJECT_ROOT%AudioBusHacker_v2025.1.9_Build9197_SDK.Windows_vc180.tar.xz"

if "%VERIFY_FAILED%"=="1" (
    set "FAILED_STEP=Verify outputs"
    exit /b 1
)

echo [OK] x64 vc180 SDK outputs are present.
exit /b 0

:check_file
if not exist "%~1" (
    echo [MISSING] %~1
    set "VERIFY_FAILED=1"
) else (
    echo [OK] %~1
)
exit /b 0

:run
echo.
echo [RUN] wp.py %*
call %PYTHON_LAUNCHER% "%WP%" %*
if errorlevel 1 (
    set "FAILED_STEP=wp.py %*"
    exit /b 1
)
exit /b 0

:usage
echo Usage:
echo   %~nx0 [all^|premake^|build^|package^|verify^|help]
echo.
echo Actions:
echo   all       Generate, build, package, and verify x64 vc180 SDK (default)
echo   premake   Generate the Windows_vc180 Visual Studio 2026 project
echo   build     Build SoundEngine Debug, Profile, and Release x64 vc180
echo   package   Create the Windows_vc180 SDK package and bundle.json
echo   verify    Check x64_vc180 SDK outputs
echo.
echo Optional environment variables:
echo   WWISEROOT        Default: H:\Audiokinetic\2025.1.9.9197
echo   PYTHON_EXE       Absolute path to Python 3
echo   PLUGIN_VERSION   Default: 2025.1.9.9197
exit /b 0

:usage_error
call :usage
exit /b 2
