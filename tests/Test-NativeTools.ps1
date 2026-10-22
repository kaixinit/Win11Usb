param([string]$Path = (Join-Path (Split-Path $PSScriptRoot -Parent) 'src\engine\Win11Usb.Engine.ps1'))
$ErrorActionPreference = 'Stop'
function Check([bool]$Value,[string]$Message) { if(-not $Value) { throw $Message } }
$tokens=$null; $parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$parseErrors)
Check ($parseErrors.Count -eq 0) 'v15 must parse'
foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
$script:workRoot=Join-Path $PSScriptRoot ('artifacts/native-' + [guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $script:workRoot
$fixture=Join-Path $script:workRoot 'stdin-child.cmd'
$fixtureText=@"
@echo off
set /p "confirm="
if /i not "%confirm%"=="Y" exit /b 23
<nul set /p "=Done"
exit /b 0
"@
[IO.File]::WriteAllText($fixture,$fixtureText.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.ASCIIEncoding]::new())
$cmd=Join-Path $env:SystemRoot 'System32\cmd.exe'
Run-Command -Program $cmd -Arguments @('/d','/c',$fixture) -Title 'STDIN success fixture' -StandardInput 'Y'
$log=Get-ChildItem -LiteralPath $script:workRoot -Filter 'cmd.exe-*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Check ($null -ne $log) 'Absolute program path must produce a filename-only log'
$text=[IO.File]::ReadAllText($log.FullName)
Check ($text -match 'Done') 'ReadLineAsync must capture the EOF partial line'
Check ($text -match 'ExitCode: 0') 'Y must reach standard input and exit successfully'
$before=@(Get-ChildItem -LiteralPath $script:workRoot -Filter '*.log').Count
$failed=$false
try { Run-Command -Program $cmd -Arguments @('/d','/c',$fixture) -Title 'STDIN failure fixture' -StandardInput 'N' }
catch { $failed=$_.Exception.Message -match '23' }
Check $failed 'A native failure with redirected input must throw'
Check (@(Get-ChildItem -LiteralPath $script:workRoot -Filter '*.log').Count -eq ($before+1)) 'Failure log must be retained'
$normal=Join-Path $script:workRoot 'normal-child.cmd'
[IO.File]::WriteAllText($normal,"@echo off`r`necho NormalNoInput`r`nexit /b 0`r`n",[Text.ASCIIEncoding]::new())
Run-Command -Program $cmd -Arguments @('/d','/c',$normal) -Title 'No-STDIN fixture'
$log=Get-ChildItem -LiteralPath $script:workRoot -Filter 'cmd.exe-*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$text=[IO.File]::ReadAllText($log.FullName)
Check ($text -match 'NormalNoInput' -and $text -match 'ExitCode: 0') 'Ordinary native invocation must continue to work'
Write-Host 'PASS: Windows PowerShell native input success/failure, absolute program paths, EOF partial line, no-input invocation.'
$toolRoot=Join-Path $PSScriptRoot ('artifacts/payload-' + [guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $toolRoot
$script:workRoot=$toolRoot
$toolPath=Prepare-Fat32Tool
Check ($toolPath -eq (Join-Path $toolRoot 'fat32-tool\fat32format.exe')) 'Tool must extract into the local test root'
$files=@(Get-ChildItem -LiteralPath (Join-Path $toolRoot 'fat32-tool') -File)
Check ($files.Count -eq 4) 'Binary, complete source, GPL text and notice must accompany extraction'
Check ((Get-FileHash -LiteralPath $toolPath -Algorithm SHA256).Hash -ceq 'D5320A127374AF23139730F0D01AEE8195E5FE15B63C35D48D80930ABBF7F5CB') 'Runtime binary bytes must match the pinned original'
Check ((Get-FileHash -LiteralPath (Join-Path $toolRoot 'fat32-tool\fat32formatsrc-vendor.zip') -Algorithm SHA256).Hash -ceq 'D1F350F277D14C92D7A0984D9566DD8BAA2592E6B37C63CA657217F251116989') 'Complete corresponding source must match vendor archive'
Check ([IO.File]::ReadAllText((Join-Path $toolRoot 'fat32-tool\COPYING.GPL-2.0.txt')).Contains('GNU GENERAL PUBLIC LICENSE')) 'GPL text must be preserved'
$sourceZip=[IO.Compression.ZipArchive]::new([IO.File]::OpenRead((Join-Path $toolRoot 'fat32-tool\fat32formatsrc-vendor.zip')),[IO.Compression.ZipArchiveMode]::Read)
try { Check ($null -ne $sourceZip.GetEntry('fat32formatsrc/fat32format.c')) 'Full C source must be present'; Check ($null -ne $sourceZip.GetEntry('fat32formatsrc/fat32format.dsp')) 'Corresponding build project must be present' } finally { $sourceZip.Dispose() }
$binary=[IO.File]::ReadAllBytes($toolPath)
$binary[10]=$binary[10] -bxor 1
[IO.File]::WriteAllBytes($toolPath,$binary)
$rejected=$false
try { Assert-Fat32Tool $toolPath } catch { $rejected=$true }
Check $rejected 'Tampered extracted formatter must be rejected before execution'
Write-Host 'PASS: Embedded tool extraction is local, pins verified, complete source/license retained, tampered binary rejected; only formatter /? help executed, no drive opened.'
$fakeSource=Join-Path $toolRoot 'ExitFixture.cs'
$fakeRobocopy=Join-Path $toolRoot 'robocopy.exe'
[IO.File]::WriteAllText($fakeSource,'using System; class ExitFixture { static void Main(string[] args) { Console.Write("fixture output"); Environment.Exit(int.Parse(args[0])); } }')
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe ("/out:$fakeRobocopy") $fakeSource
Check ($LASTEXITCODE -eq 0) 'Exit-code fixture failed to compile'
Run-Command $fakeRobocopy @('7') 'Robocopy exit 7 fixture'
$rejected=$false
try {Run-Command $fakeRobocopy @('8') 'Robocopy exit 8 fixture'} catch {$rejected=$_.Exception.Message -match '8'}
Check $rejected 'Robocopy exit >= 8 did not stop the workflow'
$global:GuiEngineState=[hashtable]::Synchronized(@{ForceStopRequested=$true;ActiveProcess=$null})
$rejected=$false
try {Run-Command $fakeRobocopy @('7') 'Cancelled before child launch'} catch {$rejected=$_.Exception.Message -match '用户已请求结束'}
Check ($rejected -and $null -eq $global:GuiEngineState.ActiveProcess) 'Cancellation launched a new child process'
$global:GuiEngineState=$null
'PASS: Robocopy 7/8 result boundary and cancellation-before-process-launch; only a test executable ran.'
