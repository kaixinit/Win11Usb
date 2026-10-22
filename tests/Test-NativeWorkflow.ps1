#requires -Version 5.1
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $PSScriptRoot ('artifacts\native-workflow-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($fixture)
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Test-Workflow.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Cannot parse existing workflow fixture.'}
$literals=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.StringConstantExpressionAst] -and $node.StringConstantType -eq 'SingleQuotedHereString'},$true) | Sort-Object {$_.Extent.StartOffset})
if($literals.Count -ne 2){throw 'Expected the existing engine mock and initialization fixtures.'}
$engine=[IO.File]::ReadAllText((Join-Path $root 'src\engine\Win11Usb.Engine.ps1'))
$marker='$script:completionHandled = $false'
if(-not $engine.Contains($marker)){throw 'Engine entry marker missing.'}
$mockEngine=$engine.Replace($marker,$literals[0].Value+"`n"+$marker)
$engineFile=Join-Path $fixture 'engine-fixture.ps1'
$initFile=Join-Path $fixture 'initialize-fixture.ps1'
[IO.File]::WriteAllText($engineFile,$mockEngine,(New-Object Text.UTF8Encoding($true)))
[IO.File]::WriteAllText($initFile,$literals[1].Value,(New-Object Text.UTF8Encoding($true)))
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$automation=Get-ChildItem -LiteralPath (Join-Path $env:WINDIR 'Microsoft.NET\assembly\GAC_MSIL\System.Management.Automation') -Filter System.Management.Automation.dll -Recurse | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$output=Join-Path $root 'work\NativeWorkflowChecks.exe'
$arguments=@('/nologo','/target:exe','/main:NativeWorkflowChecks',"/out:$output",'/reference:System.Management.dll','/reference:System.Xaml.dll',"/reference:$automation")
foreach($assembly in @('WindowsBase','PresentationCore','PresentationFramework')){$arguments+="/reference:$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\WPF\$assembly.dll"}
foreach($name in @('MainWindow','MessageDialog','FormatDialog')){$arguments+="/resource:$(Join-Path $root ('src\Ui\'+$name+'.xaml')),$name.xaml"}
$arguments+="/resource:$(Join-Path $root 'assets\usb-eject.ico'),Win11Usb.ico"
$arguments+=@(Get-ChildItem -LiteralPath (Join-Path $root 'src\Launcher') -Filter '*.cs' -File | Select-Object -ExpandProperty FullName)
$arguments+=(Join-Path $PSScriptRoot 'NativeWorkflowChecks.cs')
& $compiler @arguments
if($LASTEXITCODE -ne 0){throw 'Native workflow integration fixture compilation failed.'}
& $output (Join-Path $root 'src\Win11Usb.Worker.ps1') $engineFile $initFile $fixture
if($LASTEXITCODE -ne 0){throw 'Native workflow integration checks failed.'}
