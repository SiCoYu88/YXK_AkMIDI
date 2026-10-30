@echo off
setlocal EnableExtensions
if defined PYTHON_EXE goto explicit_python
python -c "import sys; raise SystemExit(sys.version_info.major != 3)" >nul 2>&1
if not errorlevel 1 goto path_python
py -3 "%~dp0scripts\build_android.py" %*
goto done
:path_python
python "%~dp0scripts\build_android.py" %*
goto done
:explicit_python
"%PYTHON_EXE%" "%~dp0scripts\build_android.py" %*
:done
exit /b %errorlevel%
