@echo off
REM Build os88proxy.exe on this Windows machine (docs/PROXY-PLAN.md 13).
REM
REM Needs python.org's Python 3.8+ - the "Add python.exe to PATH" box ticked -
REM and nothing else. Run it from the repository root:
REM
REM     tools\build-os88proxy-exe.bat
REM
REM The .exe lands in dist\os88proxy.exe and is one file: no Python on the
REM machine it is copied to, no installer, no pip.
setlocal
if not exist tools\os88proxygui.py (
  echo Run this from the repository root, not from inside tools\.
  exit /b 1
)
python -m pip install --upgrade pyinstaller || exit /b 1
python tools\os88proxy.py --selfcheck || exit /b 1
python -m PyInstaller --onefile --noconsole --name os88proxy ^
  --paths tools --hidden-import htmsim ^
  tools\os88proxygui.py || exit /b 1
echo.
echo Built dist\os88proxy.exe - copy that one file anywhere.
