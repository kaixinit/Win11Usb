#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$artifacts=Join-Path $PSScriptRoot 'artifacts'
$null=[IO.Directory]::CreateDirectory($artifacts)
$shell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$cases=@('Test-ImagePlan.ps1','Test-ImageIdentity.ps1','Test-Storage.ps1','Test-Eject.ps1','Test-NativeTools.ps1','Test-Workflow.ps1','Test-Prompts.ps1','Test-Display.ps1','Test-ForceExit.ps1','Test-NativeWindow.ps1','Test-NativeWorkflow.ps1')
$results=@()
foreach($case in $cases){
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $ErrorActionPreference='Continue'
    $output=& $shell -NoProfile -STA -ExecutionPolicy RemoteSigned -File (Join-Path $PSScriptRoot $case) 2>&1
    $code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    $results+=[pscustomobject]@{Name=$case;Passed=($code -eq 0);ExitCode=$code;Seconds=[math]::Round($clock.Elapsed.TotalSeconds,2);Output=($output -join "`n")}
    Write-Host ("{0}: {1}" -f $(if($code -eq 0){'PASS'}else{'FAIL'}),$case)
    if($code -ne 0){Write-Host ($output -join "`n")}
}
$record=[pscustomobject]@{UtcTime=[DateTime]::UtcNow.ToString('o');PowerShell=$PSVersionTable.PSVersion.ToString();OS=[Environment]::OSVersion.VersionString;EngineSHA256=(Get-FileHash -LiteralPath (Join-Path $root 'src\engine\Win11Usb.Engine.ps1') -Algorithm SHA256).Hash;Tests=$results}
[IO.File]::WriteAllText((Join-Path $artifacts 'test-results.json'),($record | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($true)))
if(@($results | Where-Object {-not $_.Passed}).Count){throw 'Regression tests failed. See tests/artifacts/test-results.json.'}
Write-Host "All $($results.Count) suites passed. No physical disk formatting or actual eject was performed."
