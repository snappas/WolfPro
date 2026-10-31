# Publishes to the throwaway test repo only. One step per run:
#   -Step repo | good | newer | noupdater | traversal | notzip | delete
param(
	[Parameter(Mandatory)][ValidateSet('repo','good','newer','noupdater','traversal','notzip','delete')][string]$Step,
	[string]$Root = 'C:\rtcw_updatetest',
	[string]$Repo = 'snappas/WolfPro-updatetest'
)
$ErrorActionPreference = 'Stop'

$zips  = Join-Path $Root 'zips'
$date  = '20261031'
$notes = Join-Path $PSScriptRoot 'sample_release_notes.md'

function Publish($tag, $dir, $title) {
	& gh release create $tag "$dir\windows-x64-$date.zip" "$dir\windows-x86-$date.zip" `
		--repo $Repo --title $title --notes-file $notes --latest
	if($LASTEXITCODE -ne 0) { throw "gh release create failed for $tag" }
}

switch($Step) {
	'repo' {
		& gh repo create $Repo --public --add-readme --description "Throwaway test releases for the WolfPro updater. Not for players."
		if($LASTEXITCODE -ne 0) { throw 'gh repo create failed' }
	}
	'good'      { Publish 'build-20261031-2222222' $zips 'Test: good update' }
	'newer'     { Publish 'build-20261101-3333333' $zips 'Test: newer release after Skip' }
	'noupdater' { Publish 'build-20261102-4444444' "$zips\noupdater" 'Test: zip without updater' }
	'traversal' { Publish 'build-20261103-5555555' "$zips\traversal" 'Test: zip with path traversal' }
	'notzip'    { Publish 'build-20261104-6666666' "$zips\notzip" 'Test: not a zip' }
	'delete' {
		# needs: gh auth refresh -h github.com -s delete_repo
		& gh repo delete $Repo --yes
		if($LASTEXITCODE -ne 0) { throw 'gh repo delete failed' }
	}
}
