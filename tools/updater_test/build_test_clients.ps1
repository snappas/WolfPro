# Builds "old" and "new" WolfPro clients and lays out test installs and zips
# under -Root. Touches nothing in the repo or in the retail install.
param(
	[string]$Root = 'C:\rtcw_updatetest',
	[string]$Retail = 'C:\rtcw_vk\main',
	[string]$Repo = 'snappas/WolfPro-updatetest'
)
$ErrorActionPreference = 'Stop'

$src     = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$oldHash = '1111111aaa'
$newHash = '2222222bbb'
$oldDate = '20261030'
$newDate = '20261031'
$helper  = Join-Path $PSScriptRoot 'build_test_client.cmd'
$bsdtar  = 'C:\Windows\System32\tar.exe'

New-Item -ItemType Directory -Force "$Root\zips" | Out-Null

foreach($arch in 'x64', 'x86') {
	foreach($v in @(@('old', $oldHash), @('new', $newHash))) {
		Write-Host "=== building $arch-$($v[0]) ($($v[1]))"
		& cmd /c "`"$helper`" $arch $($v[1]) $($v[0]) `"$src`" `"$Root`""
		if($LASTEXITCODE -ne 0) { throw "build failed: $arch-$($v[0]) (step $LASTEXITCODE)" }
	}
}

foreach($arch in 'x64', 'x86') {
	$deps = if($arch -eq 'x64') { "$src\deps64\bin" } else { "$src\deps\bin" }

	# date-stamp the pk3s and add the runtime DLLs, as release.yml does
	foreach($v in @(@('old', $oldDate), @('new', $newDate))) {
		$stage = "$Root\stage\$arch-$($v[0])"
		Copy-Item "$deps\*.dll" $stage -Force
		foreach($p in 'bin', 'server', 'assets') {
			Move-Item -Force "$stage\wolfpro\wolfpro_$p.pk3" "$stage\wolfpro\wolfpro_$p-$($v[1]).pk3"
		}
	}

	# test install: retail paks hard-linked, plus the old build
	$inst = "$Root\install $arch (test)"
	# back to a pristine old install: drop anything earlier test runs left
	if(Test-Path -LiteralPath "$inst\wolfpro") {
		Remove-Item -LiteralPath "$inst\wolfpro" -Recurse -Force
	}
	if(Test-Path -LiteralPath "$inst\update") {
		Remove-Item -LiteralPath "$inst\update" -Recurse -Force
	}
	New-Item -ItemType Directory -Force "$inst\main", "$inst\wolfpro" | Out-Null
	Get-ChildItem $Retail -File -Filter *.pk3 |
		Where-Object { $_.Name -match '^(pak0|mp_pak|mp_bin|sp_pak)' } |
		ForEach-Object {
			$dst = Join-Path "$inst\main" $_.Name
			if(-not (Test-Path -LiteralPath $dst)) {
				New-Item -ItemType HardLink -Path $dst -Target $_.FullName | Out-Null
			}
		}
	$stage = "$Root\stage\$arch-old"
	Get-ChildItem $stage -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
		ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $inst -Force }
	Copy-Item "$stage\wolfpro\*.pk3" "$inst\wolfpro\" -Force

	$launcher = @(
		'@echo off',
		'cd /d "%~dp0"',
		"start `"`" `"%~dp0wolfmp_vk_$arch.exe`" +set cl_updateRepo $Repo +set r_fullscreen 0"
	)
	Set-Content -LiteralPath "$inst\start_test.cmd" -Value $launcher -Encoding Ascii

	# release-shaped zip: root exes/dlls plus wolfpro\*.pk3 only
	$pack = "$Root\pack\$arch"
	if(Test-Path $pack) { Remove-Item -Recurse -Force $pack }
	New-Item -ItemType Directory -Force "$pack\wolfpro" | Out-Null
	$stage = "$Root\stage\$arch-new"
	Get-ChildItem $stage -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
		ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $pack -Force }
	Copy-Item "$stage\wolfpro\*.pk3" "$pack\wolfpro\" -Force

	$zip = "$Root\zips\windows-$arch-$newDate.zip"
	if(Test-Path $zip) { Remove-Item -Force $zip }
	$items = @(Get-ChildItem $pack -Name)
	& $bsdtar -a -c -f $zip -C $pack @items
	if($LASTEXITCODE -ne 0) { throw "zip failed: $zip" }

	Write-Host "=== $zip"
	& $bsdtar -t -f $zip
	$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
	Write-Host "sha256 $h  size $((Get-Item $zip).Length)"
}
Write-Host "=== done"
