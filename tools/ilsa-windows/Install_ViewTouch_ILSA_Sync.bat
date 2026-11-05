@echo off
rem Installs the ViewTouch ILSA price book sync: every 15 minutes, read-only, sends only changes.
rem Run it once from the folder holding these three files. Safe to run again (it replaces the old one).
setlocal
set "SRC=%~dp0"
set "DEST=C:\ViewTouchSync"
set "PS32=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS32%" set "PS32=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
echo === ViewTouch ILSA price book sync ===
if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%SRC%ViewTouch_ILSA_Sync.ps1" "%DEST%\" >nul || goto :fail
copy /Y "%SRC%ViewTouch_ILSA_Sync_hidden.vbs" "%DEST%\" >nul || goto :fail
if not exist "%DEST%\settings.txt" (
  echo MDB=C:\ilsa\data\ilsacorp.mdb>"%DEST%\settings.txt"
  echo # DROPBOX=C:\Users\YOU\Dropbox\POS project>>"%DEST%\settings.txt"
)
if not "%~1"=="" echo MDB=%~1>>"%DEST%\settings.txt"
echo.
echo 1. Sending the first price book now (this can take a minute)...
"%PS32%" -NoProfile -ExecutionPolicy Bypass -File "%DEST%\ViewTouch_ILSA_Sync.ps1" -Force
echo.
type "%DEST%\sync.log" 2>nul | more +0
echo.
echo 2. Scheduling it every 15 minutes...
schtasks /Create /TN "ViewTouch ILSA Sync" /TR "wscript.exe \"%DEST%\ViewTouch_ILSA_Sync_hidden.vbs\"" /SC MINUTE /MO 15 /F >nul && echo    Scheduled: "ViewTouch ILSA Sync" every 15 minutes (only sends when something changed). || goto :fail
echo.
echo Done. The log is C:\ViewTouchSync\sync.log
echo If the database or Dropbox folder was not found, edit C:\ViewTouchSync\settings.txt and run this again.
pause
exit /b 0
:fail
echo SOMETHING FAILED - take a photo of this window for Bob.
pause
exit /b 1
