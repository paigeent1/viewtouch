# ViewTouch ILSA price book sync (Windows 7 / PowerShell 2.0 / 32-bit Access driver).
# Reads the ILSA database READ-ONLY (safe while ILSA is open) and writes the price book tables to
# C:\ViewTouchSync\out as CSV. Only when something changed since the last run, it copies them to
# Dropbox\POS project\ilsa-pricebook\ for the Raspberry Pi. Log: C:\ViewTouchSync\sync.log
# Settings: C:\ViewTouchSync\settings.txt  (line: MDB=C:\path\to\ilsa.mdb ; DROPBOX=C:\path\to\folder)
param([switch]$Force)

if ([IntPtr]::Size -eq 8) {   # the Access (Jet) driver is 32-bit only
    $ps32 = "$env:SystemRoot\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    $a = "-NoProfile -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`""
    if ($Force) { $a += " -Force" }
    $p = Start-Process -FilePath $ps32 -ArgumentList $a -WindowStyle Hidden -Wait -PassThru
    exit $p.ExitCode
}

$Root = "C:\ViewTouchSync"
$Out = Join-Path $Root "out"
$Log = Join-Path $Root "sync.log"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
function Log($m) { $line = (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + "  " + $m; Add-Content -Path $Log -Value $line; Write-Host $line }

# --- settings ---------------------------------------------------------------------------------
$settings = @{}
$sf = Join-Path $Root "settings.txt"
if (Test-Path $sf) { foreach ($l in Get-Content $sf) { if ($l -match '^\s*(\w+)\s*=\s*(.+?)\s*$') { $settings[$matches[1].ToUpper()] = $matches[2] } } }

$mdb = $settings["MDB"]
if (-not $mdb -or -not (Test-Path $mdb)) {
    $desk = [Environment]::GetFolderPath("Desktop")
    $cands = @("C:\ilsa\ilsa.mdb", "C:\ILSA\ilsa.mdb", "C:\ilsa_\ilsa.mdb", "D:\ilsa\ilsa.mdb", (Join-Path $desk "ilsa.mdb"))
    $mdb = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $mdb) { Log "ERROR: ILSA database not found. Put MDB=C:\path\to\ilsa.mdb in $sf"; exit 2 }

$dbx = $settings["DROPBOX"]
if (-not $dbx) {
    foreach ($c in @((Join-Path $env:USERPROFILE "Dropbox\POS project"), (Join-Path $env:USERPROFILE "Dropbox\Ale House\POS project"), "C:\Dropbox\POS project")) {
        if (Test-Path $c) { $dbx = $c; break }
    }
}

# --- read the tables (read-only) --------------------------------------------------------------
$tables = @("PLU", "PRC_DATA", "PRC_NAME", "Inv_Info", "Dept", "Dept_Grp", "Pos_Flag")
$conn = New-Object System.Data.OleDb.OleDbConnection("Provider=Microsoft.Jet.OLEDB.4.0;Data Source=$mdb;Mode=Share Deny None;")
try { $conn.Open() } catch { Log "ERROR: can't open $mdb : $_"; exit 3 }

function Cell($v) {
    if ($v -eq $null -or $v -is [System.DBNull]) { return '""' }
    if ($v -is [DateTime]) { $s = $v.ToString("yyyy-MM-dd HH:mm:ss") } else { $s = $v.ToString() }
    return '"' + $s.Replace('"', '""').Replace("`r", " ").Replace("`n", " ") + '"'
}

$tmp = Join-Path $Root "tmp"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$counts = @()
foreach ($t in $tables) {
    $cmd = $conn.CreateCommand(); $cmd.CommandText = "SELECT * FROM [$t]"
    try { $rd = $cmd.ExecuteReader() } catch { Log "note: table $t not readable ($_)"; continue }
    $w = New-Object System.IO.StreamWriter((Join-Path $tmp "$t.csv"), $false, (New-Object System.Text.UTF8Encoding($false)))
    $names = @(); for ($i = 0; $i -lt $rd.FieldCount; $i++) { $names += '"' + $rd.GetName($i) + '"' }
    $w.WriteLine([string]::Join(",", $names))
    $n = 0
    while ($rd.Read()) {
        $cells = New-Object 'string[]' $rd.FieldCount
        for ($i = 0; $i -lt $rd.FieldCount; $i++) { $cells[$i] = Cell $rd.GetValue($i) }
        $w.WriteLine([string]::Join(",", $cells)); $n++
    }
    $rd.Close(); $w.Close()
    $counts += "$t=$n"
}
$conn.Close()

# --- changed since last time? ------------------------------------------------------------------
$md5 = [System.Security.Cryptography.MD5]::Create()
$sb = New-Object System.Text.StringBuilder
foreach ($f in Get-ChildItem $tmp -Filter *.csv | Sort-Object Name) {
    $h = $md5.ComputeHash([System.IO.File]::ReadAllBytes($f.FullName))
    [void]$sb.Append($f.Name + ":" + [BitConverter]::ToString($h) + ";")
}
$hash = [BitConverter]::ToString($md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($sb.ToString())))
$hf = Join-Path $Root "last_hash.txt"
$last = ""; if (Test-Path $hf) { $last = (Get-Content $hf | Select-Object -First 1) }
Set-Content -Path (Join-Path $Root "last_check.txt") -Value (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
if (($hash -eq $last) -and -not $Force) { exit 0 }   # nothing changed: quiet (last_check.txt shows it ran)

foreach ($f in Get-ChildItem $tmp -Filter *.csv) { Copy-Item $f.FullName (Join-Path $Out $f.Name) -Force }
$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$manifest = @("exported=$stamp", "database=$mdb", "computer=$env:COMPUTERNAME") + $counts
Set-Content -Path (Join-Path $Out "manifest.txt") -Value $manifest

if ($dbx) {
    $dest = Join-Path $dbx "ilsa-pricebook"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    foreach ($f in Get-ChildItem $Out) { Copy-Item $f.FullName (Join-Path $dest $f.Name) -Force }
    Set-Content -Path $hf -Value $hash
    Log ("Changed: sent to " + $dest + " (" + [string]::Join(", ", $counts) + ")")
} else {
    Log "ERROR: Dropbox folder not found. Put DROPBOX=C:\path\to\Dropbox\POS project in $sf"
    exit 4
}
exit 0
