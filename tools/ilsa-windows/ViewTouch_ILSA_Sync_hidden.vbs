' Runs the sync with no window (Task Scheduler calls this every 15 minutes).
Set sh = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
win = sh.ExpandEnvironmentStrings("%SystemRoot%")
ps = win & "\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
If Not fs.FileExists(ps) Then ps = win & "\System32\WindowsPowerShell\v1.0\powershell.exe"
sh.Run """" & ps & """ -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""C:\ViewTouchSync\ViewTouch_ILSA_Sync.ps1""", 0, True
