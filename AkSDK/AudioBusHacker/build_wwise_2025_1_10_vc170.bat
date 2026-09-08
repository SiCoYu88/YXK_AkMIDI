@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0"
cd /d "%PROJECT_ROOT%"

if not defined WWISE_2025_1_10_ROOT set "WWISE_2025_1_10_ROOT=H:\Audiokinetic\2025.1.10.9233"
set "WWISEROOT=%WWISE_2025_1_10_ROOT%"
if not defined PLUGIN_VERSION set "PLUGIN_VERSION=2025.1.10.9233"
rem VSWhere emits localized text in the active Windows code page.
set "PYTHONUTF8=0"
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
if /I "%ACTION%"=="docs" goto action_docs
if /I "%ACTION%"=="package" goto action_package
if /I "%ACTION%"=="verify" goto action_verify

echo [ERROR] Unknown action: %ACTION%
goto :usage_error

:action_all
call :premake
if errorlevel 1 exit /b 1
call :build
if errorlevel 1 exit /b 1
call :docs
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

:action_docs
call :docs
if errorlevel 1 exit /b 1
echo [SUCCESS] docs completed.
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
echo AudioBusHacker - Wwise 2025.1.10.9233 - vc170 / x64
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
echo ===== Generate Visual Studio 2022 projects =====
call :run premake Windows_vc170 -t vc170 --disable-codesign
if errorlevel 1 exit /b 1
call :run premake Authoring_Windows -t vc170 --disable-codesign
if errorlevel 1 exit /b 1
exit /b 0

:build
call :check_vs2022
if errorlevel 1 exit /b 1

echo.
echo ===== Build SoundEngine x64 vc170 Debug / Profile / Release =====
call :run build Windows_vc170 -c Debug -x x64 -t vc170
if errorlevel 1 exit /b 1
call :run build Windows_vc170 -c Profile -x x64 -t vc170
if errorlevel 1 exit /b 1
call :run build Windows_vc170 -c Release -x x64 -t vc170
if errorlevel 1 exit /b 1

echo.
echo ===== Build Authoring Debug / Release =====
call :run build Authoring_Windows -c Debug -x x64 -t vc170
if errorlevel 1 exit /b 1
call :run build Authoring_Windows -c Release -x x64 -t vc170
if errorlevel 1 exit /b 1
exit /b 0

:check_vs2022
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS2022_TOOLS="
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio Installer vswhere.exe was not found.
    set "FAILED_STEP=Find Visual Studio 2022"
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VS2022_TOOLS=%%I\Common7\Tools\VsMSBuildCmd.bat"
if not defined VS2022_TOOLS (
    echo [ERROR] Visual Studio 2022 with MSBuild was not found.
    echo         Install VS 2022 or Build Tools 2022 with Desktop development with C++.
    set "FAILED_STEP=Find Visual Studio 2022"
    exit /b 1
)
if not exist "%VS2022_TOOLS%" (
    echo [ERROR] Visual Studio 2022 build environment was not found: %VS2022_TOOLS%
    set "FAILED_STEP=Find Visual Studio 2022"
    exit /b 1
)
exit /b 0

:docs
echo.
echo ===== Generate Property Help =====
call %PYTHON_LAUNCHER% -c "import markdown, jinja2" >nul 2>&1
if errorlevel 1 (
    if "%INSTALL_DOC_DEPS%"=="1" (
        echo [INFO] Installing markdown and jinja2...
        call %PYTHON_LAUNCHER% -m pip install markdown jinja2
        if errorlevel 1 (
            set "FAILED_STEP=Install documentation dependencies"
            exit /b 1
        )
    ) else (
        echo [ERROR] Python package markdown or jinja2 is missing.
        echo         Run:
        echo         %PYTHON_LAUNCHER% -m pip install markdown jinja2
        echo         Or set INSTALL_DOC_DEPS=1 to install automatically.
        set "FAILED_STEP=Check documentation dependencies"
        exit /b 1
    )
)

call :run build Documentation
if errorlevel 1 exit /b 1
exit /b 0

:package
echo.
echo ===== Create 2025.1.10 vc170 and Authoring packages =====
for %%F in ("AudioBusHacker_v2023*.tar.xz") do (
    if exist "%%~fF" (
        echo [ERROR] Legacy package exists in project root: %%~nxF
        echo         Move it to LegacyPackages and retry.
        set "FAILED_STEP=Check legacy packages"
        exit /b 1
    )
)

call :run package Windows_vc170 -v %PLUGIN_VERSION%
if errorlevel 1 exit /b 1
call :run package Authoring_Windows -v %PLUGIN_VERSION%
if errorlevel 1 exit /b 1
call :run generate-bundle -v %PLUGIN_VERSION%
if errorlevel 1 exit /b 1
call :filter_bundle
if errorlevel 1 exit /b 1
exit /b 0

:filter_bundle
call %PYTHON_LAUNCHER% -c "import json; p=r'%PROJECT_ROOT%bundle.json'; b=json.load(open(p, encoding='utf-8')); prefix='AudioBusHacker_v2025.1.10_Build9233_'; b['files']=[f for f in b['files'] if f.get('sourceName', f.get('id', '')).startswith(prefix)]; open(p, 'w', encoding='utf-8').write(json.dumps(b, ensure_ascii=False, separators=(',', ':')) + chr(10)); raise SystemExit(0 if b['files'] else 1)"
if errorlevel 1 (
    echo [ERROR] No Wwise 2025.1.10 Build9233 packages were found in bundle.json.
    set "FAILED_STEP=Filter bundle.json"
    exit /b 1
)
echo [OK] bundle.json contains only Wwise 2025.1.10 Build9233 packages.
exit /b 0

:verify
echo.
echo ===== Verify outputs =====
set "VERIFY_FAILED=0"
set "SDK_PACKAGE=%PROJECT_ROOT%AudioBusHacker_v2025.1.10_Build9233_SDK.Windows_vc170.tar.xz"
set "AUTHORING_DEBUG_PACKAGE=%PROJECT_ROOT%AudioBusHacker_v2025.1.10_Build9233_Authoring.Windows_x64.Debug.tar.xz"
set "AUTHORING_RELEASE_PACKAGE=%PROJECT_ROOT%AudioBusHacker_v2025.1.10_Build9233_Authoring.Windows_x64.Release.tar.xz"

call :check_file "%WWISEROOT%\SDK\x64_vc170\Debug\bin\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\SDK\x64_vc170\Profile\lib\AudioBusHackerFX.lib"
call :check_file "%WWISEROOT%\SDK\x64_vc170\Release\bin\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\Authoring\x64\Debug\bin\Plugins\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\Authoring\x64\Release\bin\Plugins\AudioBusHacker.dll"
call :check_file "%WWISEROOT%\SDK\include\AK\Plugin\AudioBusHackerFXFactory.h"
call :check_file "%SDK_PACKAGE%"
call :check_file "%AUTHORING_DEBUG_PACKAGE%"
call :check_file "%AUTHORING_RELEASE_PACKAGE%"
call :check_file "%PROJECT_ROOT%bundle.json"

if "%VERIFY_FAILED%"=="0" (
    call %PYTHON_LAUNCHER% -c "import tarfile; p=r'%SDK_PACKAGE%'; expected='SDK/include/AK/Plugin/AudioBusHackerFXFactory.h'; names=(n.replace(chr(92), '/').lstrip('./') for n in tarfile.open(p, 'r:xz').getnames()); raise SystemExit(0 if any(n.endswith(expected) for n in names) else 1)"
    if errorlevel 1 (
        echo [MISSING] %SDK_PACKAGE%::SDK/include/AK/Plugin/AudioBusHackerFXFactory.h
        set "VERIFY_FAILED=1"
    ) else (
        echo [OK] %SDK_PACKAGE%::SDK/include/AK/Plugin/AudioBusHackerFXFactory.h
    )
)

if "%VERIFY_FAILED%"=="1" (
    set "FAILED_STEP=Verify outputs"
    exit /b 1
)

call %PYTHON_LAUNCHER% -c "import json; b=json.load(open(r'%PROJECT_ROOT%bundle.json', encoding='utf-8')); v=b['version']; names={f.get('sourceName', f.get('id', '')) for f in b['files']}; prefix='AudioBusHacker_v2025.1.10_Build9233_'; required={'AudioBusHacker_v2025.1.10_Build9233_SDK.Windows_vc170.tar.xz','AudioBusHacker_v2025.1.10_Build9233_Authoring.Windows_x64.Debug.tar.xz','AudioBusHacker_v2025.1.10_Build9233_Authoring.Windows_x64.Release.tar.xz'}; raise SystemExit(0 if (v['year'],v['major'],v['minor'],v['build']) == (2025,1,10,9233) and required.issubset(names) and all(n.startswith(prefix) for n in names) else 1)"
if errorlevel 1 (
    echo [ERROR] bundle.json version or required vc170/Authoring packages are incorrect.
    set "FAILED_STEP=Verify bundle.json"
    exit /b 1
)

echo [OK] bundle.json version and required vc170/Authoring package references are correct.
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
echo   %~nx0 [all^|premake^|build^|docs^|package^|verify^|help]
echo.
echo Actions:
echo   all       Generate, build, document, package, and verify vc170 (default)
echo   premake   Generate vc170 Windows and Authoring projects
echo   build     Build SoundEngine and Authoring configurations
echo   docs      Generate Property Help
echo   package   Create Windows/Authoring packages and bundle.json
echo   verify    Check primary outputs and bundle.json
echo.
echo Optional environment variables:
echo   WWISE_2025_1_10_ROOT Default: H:\Audiokinetic\2025.1.10.9233
echo   PYTHON_EXE       Absolute path to Python 3
echo   PLUGIN_VERSION   Default: 2025.1.10.9233
echo   INSTALL_DOC_DEPS Set to 1 to install markdown and jinja2
exit /b 0

:usage_error
call :usage
exit /b 2
