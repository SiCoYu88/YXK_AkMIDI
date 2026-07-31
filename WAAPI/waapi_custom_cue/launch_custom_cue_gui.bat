@echo off
setlocal
cd /d "%~dp0"
python -c "import PySide6, waapi" >nul 2>nul
if errorlevel 1 (
  echo Missing Python dependencies. Run:
  echo python -m pip install -r requirements.txt
  pause
  exit /b 1
)
start "" pythonw.exe music_segment_custom_cue_gui.py
