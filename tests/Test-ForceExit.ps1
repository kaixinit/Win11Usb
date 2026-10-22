#requires -Version 5.1
# Runs the production C# forced-exit path against a disposable worker and child.
# The fixture does not call any storage cmdlet, mount an ISO or format a disk.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'Test-NativeWindow.ps1') -BuildOnly
$fixtureExe=Join-Path $root 'work\NativeChecks.exe'
$fixtureRoot=Join-Path $PSScriptRoot ('artifacts\native-force-exit-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($fixtureRoot)
$pidFile=Join-Path $fixtureRoot 'child.pid'
$arguments='--force-exit-fixture "{0}"' -f $pidFile
$clock=[Diagnostics.Stopwatch]::StartNew()
$parent=Start-Process -FilePath $fixtureExe -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $parent.WaitForExit(12000)){throw 'Native force exit exceeded 12 seconds.'}
    if($parent.ExitCode -ne 2){throw "Expected native force-exit code 2, got $($parent.ExitCode)."}
    if(-not (Test-Path -LiteralPath $pidFile)){throw 'Child ownership record missing.'}
    $childPid=[int][IO.File]::ReadAllText($pidFile)
    if(Get-Process -Id $childPid -ErrorAction SilentlyContinue){throw 'Tracked sleeping child survived native forced exit.'}
    Write-Output ('PASS: production C# forced exit stopped its disposable worker and tracked child, exit 2, '+[math]::Round($clock.Elapsed.TotalSeconds,2)+' seconds. No disk or ISO operations ran.')
} finally {
    if(-not $parent.HasExited){$parent.Kill();$parent.WaitForExit()}
    if(Test-Path -LiteralPath $pidFile){
        $childPid=[int][IO.File]::ReadAllText($pidFile)
        $remaining=Get-Process -Id $childPid -ErrorAction SilentlyContinue
        if($remaining -and $remaining.Path -eq $fixtureExe){$remaining.Kill();$remaining.WaitForExit()}
    }
}
