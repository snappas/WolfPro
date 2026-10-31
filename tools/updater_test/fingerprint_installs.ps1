# Fingerprints the game files of both test installs, or compares against the
# last saved fingerprint. Skips main\ (hard-linked retail paks), logs, configs
# and the update\ working folder, which are expected to change.
param(
	[Parameter(Mandatory)][ValidateSet('save','compare')][string]$Mode,
	[string]$Root = 'C:\rtcw_updatetest'
)
$ErrorActionPreference = 'Stop'

function Get-Print($inst) {
	Get-ChildItem -LiteralPath $inst -Recurse -File |
		Where-Object {
			$rel = $_.FullName.Substring($inst.Length + 1)
			$rel -notmatch '^(main|update)\\' -and $_.Extension -notin '.cfg', '.log', '.tmp' -and $rel -ne 'start_test.cmd'
		} |
		ForEach-Object {
			'{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.FullName.Substring($inst.Length + 1)
		} | Sort-Object { $_.Substring(66) }
}

foreach($arch in 'x64', 'x86') {
	$inst = "$Root\install $arch (test)"
	$file = "$Root\fingerprint-$arch.txt"
	$now = @(Get-Print $inst)
	if($Mode -eq 'save') {
		Set-Content -LiteralPath $file -Value $now -Encoding Ascii
		"[$arch] saved $($now.Count) files"
	} else {
		$was = @(Get-Content -LiteralPath $file)
		$diff = @(Compare-Object $was $now)
		if($diff.Count -eq 0) { "[$arch] unchanged ($($now.Count) files)" }
		else { "[$arch] CHANGED:"; $diff | ForEach-Object { '  {0} {1}' -f $_.SideIndicator, $_.InputObject } }
	}
	$escape = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter 'updatetest_escape.txt' -ErrorAction SilentlyContinue
	if($escape) { $escape | ForEach-Object { "ESCAPED FILE: $($_.FullName)" } }
}
