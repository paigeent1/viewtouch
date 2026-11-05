ViewTouch ILSA price book sync (for the ILSA Windows 7 computer)

1. Copy these 4 files into one folder on the ILSA computer (e.g. the Desktop\ViewTouchSync folder).
2. Double-click Install_ViewTouch_ILSA_Sync.bat
   - It reads the ILSA database READ-ONLY (safe while ILSA is open),
   - sends the first price book to Dropbox\POS project\ilsa-pricebook\ right away,
   - and schedules itself every 15 minutes ("ViewTouch ILSA Sync" in Task Scheduler).
     Each run sends only when prices/items changed. No window pops up.
3. If it says the database or Dropbox folder wasn't found: open C:\ViewTouchSync\settings.txt,
   put the right paths on lines like these (no # in front), save, and run the installer again:
     MDB=C:\ilsa\ilsa.mdb
     DROPBOX=C:\Users\Owner\Dropbox\POS project
Log: C:\ViewTouchSync\sync.log      Remove: schtasks /Delete /TN "ViewTouch ILSA Sync" /F
