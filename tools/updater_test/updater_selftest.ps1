# Exercises wolfpro_updater.exe alone in a temp folder; no game and no GitHub.
# whoami stands in for the game: it starts, rejects the argument and exits.
param([string]$Updater = "C:\rtcw_updatetest\stage\x64-new\wolfpro_updater.exe")
$ErrorActionPreference = "Stop"
$root = Join-Path $env:TEMP "updater test (1)"
if (Test-Path $root) { Get-ChildItem $root -Recurse -Force | ForEach-Object { $_.Attributes = 'Normal' }; Remove-Item $root -Recurse -Force }
$inst = "$root\install"; $stg = "$root\staging"; $stg3 = "$root\staging3"; $stgBad = "$root\stagingbad"
New-Item -ItemType Directory "$inst\wolfpro","$stg\wolfpro","$inst\update","$stg3\wolfpro","$stgBad\update" | Out-Null
Set-Content -NoNewline "old" "$inst\a.exe"; Set-Content -NoNewline "oldpk" "$inst\wolfpro\wolfpro_bin-20261004.pk3"
Set-Content -NoNewline "new" "$stg\a.exe"; Set-Content -NoNewline "dll" "$stg\b.dll"; Set-Content -NoNewline "newpk" "$stg\wolfpro\wolfpro_bin-20261018.pk3"
Set-Content -NoNewline "v3" "$stg3\a.exe"; Set-Content -NoNewline "v3pk" "$stg3\wolfpro\wolfpro_bin-20261018.pk3"
Set-Content -NoNewline "evil" "$stgBad\update\pending.txt"
Copy-Item $Updater $stg\wolfpro_updater.exe
$pend = "$inst\update\pending.txt"
function WritePending($s, $t) { @("pid 0","install $inst","staging $s","tag $t","from abc1234","relaunch $env:SystemRoot\System32\whoami.exe","arg $root\x y.txt") | Set-Content $pend }
function Show($l) {
	"--- $l"
	Get-ChildItem $inst -Recurse -File | Where-Object { $_.FullName -notlike '*\update\*' } | ForEach-Object { $_.FullName.Substring($inst.Length) + " = " + $(if ($_.Name -eq "wolfpro_updater.exe") { "(updater, $($_.Length) bytes)" } else { Get-Content -Raw $_.FullName }) }
	if (Test-Path "$inst\update\result.txt") { Get-Content "$inst\update\result.txt"; Remove-Item "$inst\update\result.txt" }
	"backup\a.exe = " + $(if (Test-Path "$inst\update\backup\a.exe") { Get-Content -Raw "$inst\update\backup\a.exe" } else { "(none)" })
	"backup rolledfrom: " + $(if (Test-Path "$inst\update\backup\manifest.txt") { (Select-String "^rolledfrom" "$inst\update\backup\manifest.txt").Line } else { "(none)" })
	"backup.new present: " + (Test-Path "$inst\update\backup.new")
	".new leftovers: " + @(Get-ChildItem $inst -Recurse -File -Filter *.new | Where-Object { $_.FullName -notlike '*\update\*' }).Count
	"pending present: " + (Test-Path $pend)
}
function Run($a) { & "$stg\wolfpro_updater.exe" @a; Start-Sleep 2 }

WritePending $stg "v2"; Run @("--apply",$pend); Show "1 apply v2 (expect ok, backup a.exe=old)"
WritePending $stg "v2"; Run @("--rollback",$pend); Show "2 rollback (expect original, rolledback v2)"
WritePending $stg "v2"; Run @("--apply",$pend); Show "3 re-apply v2 (expect ok, backup a.exe=old)"

# replace-phase failure: pk3 open without delete sharing, so a.exe is replaced, then the pk3 swap fails
$lock = [System.IO.File]::Open("$inst\wolfpro\wolfpro_bin-20261018.pk3", 'Open', 'Read', 'Read')
WritePending $stg3 "v3"; Run @("--apply",$pend)
$lock.Close()
Show "4 failed apply v3 (expect failed tag v3, install still v2, backup a.exe=old rolledfrom v2, no backup.new)"

# interrupted install: backup.new with a manifest wins over backup, then is removed
New-Item -ItemType Directory "$inst\update\backup.new" | Out-Null
Set-Content -NoNewline "v2bak" "$inst\update\backup.new\a.exe"
@("tag v2","rolledfrom v3","file a.exe") | Set-Content "$inst\update\backup.new\manifest.txt"
WritePending $stg "v3"; Run @("--rollback",$pend); Show "5 rollback after interrupted v3 (expect a.exe=v2bak, rolledback v3, backup.new gone, backup intact)"

# unsafe staging entry under update\
WritePending $stgBad "bad"; Run @("--apply",$pend); Show "6 apply with update\ entry (expect failed, unsafe path)"

# manual rollback from the install dir: message box, result.txt still written
$p = Start-Process "$inst\wolfpro_updater.exe" -ArgumentList "--rollback" -PassThru
Start-Sleep 3
$p.Refresh()
"message box title: " + $p.MainWindowTitle
if (!$p.HasExited) { $p | Stop-Process -Force }
Show "7 manual rollback from install dir (expect rolledback, original state)"
"updater still present: " + (Test-Path "$inst\wolfpro_updater.exe")
