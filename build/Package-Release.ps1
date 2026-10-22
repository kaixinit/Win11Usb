#requires -Version 5.1
[CmdletBinding()]
param([ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.]+)?$')][string]$Version='0.1.0-beta.3')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if($Version -in @('0.1.0-beta.1','0.1.0-beta.2')){throw 'These historical candidates were withdrawn and must not be repackaged.'}
$releaseHold=Join-Path $root 'release\SECURITY-HOLD.txt'
if(Test-Path -LiteralPath $releaseHold){throw 'Release packaging is paused pending the Defender startup investigation. See release/SECURITY-HOLD.txt.'}
$exe=Join-Path $root 'dist\Win11Usb.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Build dist/Win11Usb.exe first.'}
if([Diagnostics.FileVersionInfo]::GetVersionInfo($exe).ProductVersion -ne $Version){throw 'Package version does not match the EXE product version.'}
$release=Join-Path $root 'release'
$null=[IO.Directory]::CreateDirectory($release)
$staging=Join-Path $root ('work\release-staging-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $staging 'source\Win11Usb'
$runtime=Join-Path $staging 'runtime\Win11Usb'
$null=[IO.Directory]::CreateDirectory($source)
$null=[IO.Directory]::CreateDirectory($runtime)
$roots=@('src','build','tests','.github','docs','assets','third_party')
foreach($directory in $roots){
    $origin=Join-Path $root $directory
    foreach($file in @(Get-ChildItem -LiteralPath $origin -File -Recurse)){
        $relative=$file.FullName.Substring($root.Length+1)
        if($relative -match '^tests[\\/]artifacts[\\/]' -or $file.Extension -in @('.exe','.log')){continue}
        $destination=Join-Path $source $relative
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
}
foreach($name in @('README.md','README.en.md','LICENSE','CHANGELOG.md','CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md','.gitignore','.gitattributes','.editorconfig')){
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $source $name)
}
Copy-Item -LiteralPath $exe -Destination (Join-Path $runtime 'Win11Usb.exe')
foreach($name in @('README.md','README.en.md','LICENSE','CHANGELOG.md','THIRD_PARTY_NOTICES.md')){Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $runtime $name)}
foreach($directory in @('docs','third_party')){Copy-Item -LiteralPath (Join-Path $root $directory) -Destination (Join-Path $runtime $directory) -Recurse}
$exeHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $runtime 'SHA256SUMS.txt'),($exeHash+'  Win11Usb.exe'+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))
$sourceZip=Join-Path $release ("Win11Usb-v$Version-source.zip")
$runtimeZip=Join-Path $release ("Win11Usb-v$Version-windows-x64.zip")
Compress-Archive -LiteralPath $source -DestinationPath $sourceZip -Force
Compress-Archive -LiteralPath $runtime -DestinationPath $runtimeZip -Force
$lines=@($sourceZip,$runtimeZip) | ForEach-Object { ((Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant())+'  '+[IO.Path]::GetFileName($_) }
[IO.File]::WriteAllText((Join-Path $release 'SHA256SUMS.txt'),(($lines -join [Environment]::NewLine)+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))
Copy-Item -LiteralPath (Join-Path $root 'docs\RELEASE_NOTES.md') -Destination (Join-Path $release 'RELEASE_NOTES.md') -Force
Write-Host "Prepared release candidates in $release. No repository push or public release was performed."
