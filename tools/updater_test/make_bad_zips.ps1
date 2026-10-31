# Local only: derives deliberately bad packages from the good test zips.
param([string]$Root = 'C:\rtcw_updatetest')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$date = '20261031'

foreach($arch in 'x64', 'x86') {
	$good = "$Root\zips\windows-$arch-$date.zip"

	# no updater inside: must fail before anything is installed
	$dir = "$Root\zips\noupdater"
	New-Item -ItemType Directory -Force $dir | Out-Null
	$bad = "$dir\windows-$arch-$date.zip"
	Copy-Item $good $bad -Force
	$z = [System.IO.Compression.ZipFile]::Open($bad, 'Update')
	$z.GetEntry('wolfpro_updater.exe').Delete()
	$z.Dispose()

	# path traversal entry: the whole zip must be rejected before any write
	$dir = "$Root\zips\traversal"
	New-Item -ItemType Directory -Force $dir | Out-Null
	$bad = "$dir\windows-$arch-$date.zip"
	Copy-Item $good $bad -Force
	$z = [System.IO.Compression.ZipFile]::Open($bad, 'Update')
	$e = $z.CreateEntry('../updatetest_escape.txt')
	$w = New-Object System.IO.StreamWriter($e.Open())
	$w.Write('this file must never appear on disk')
	$w.Dispose()
	$z.Dispose()

	# not a zip at all: the digest matches, so extraction must catch it
	$dir = "$Root\zips\notzip"
	New-Item -ItemType Directory -Force $dir | Out-Null
	$bad = "$dir\windows-$arch-$date.zip"
	$bytes = New-Object byte[] 1048576
	(New-Object Random 1234).NextBytes($bytes)
	[System.IO.File]::WriteAllBytes($bad, $bytes)
}

Get-ChildItem "$Root\zips" -Recurse -Filter *.zip | ForEach-Object {
	'{0,-55} {1,10}' -f $_.FullName.Substring($Root.Length + 1), $_.Length
}
