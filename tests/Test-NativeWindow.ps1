#requires -Version 5.1
[CmdletBinding()]
param([switch]$BuildOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$automation=Get-ChildItem -LiteralPath (Join-Path $env:WINDIR 'Microsoft.NET\assembly\GAC_MSIL\System.Management.Automation') -Filter System.Management.Automation.dll -Recurse | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$output=Join-Path $root 'work\NativeChecks.exe'
$sources=@(Get-ChildItem -LiteralPath (Join-Path $root 'src\Launcher') -Filter '*.cs' -File | Select-Object -ExpandProperty FullName)
$arguments=@('/nologo','/target:exe','/main:NativeChecks',"/out:$output",'/reference:System.Management.dll','/reference:System.Xaml.dll',"/reference:$automation")
foreach($assembly in @('WindowsBase','PresentationCore','PresentationFramework')){$arguments+="/reference:$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\WPF\$assembly.dll"}
foreach($name in @('MainWindow','MessageDialog','FormatDialog')){$arguments+="/resource:$(Join-Path $root ('src\Ui\'+$name+'.xaml')),$name.xaml"}
$arguments+="/resource:$(Join-Path $root 'assets\usb-eject.ico'),Win11Usb.ico"
$arguments+=$sources
$arguments+=(Join-Path $PSScriptRoot 'NativeChecks.cs')
& $compiler @arguments
if($LASTEXITCODE -ne 0){throw 'Native test host compilation failed.'}
if(-not $BuildOnly){& $output;if($LASTEXITCODE -ne 0){throw 'Native UI/worker checks failed.'}}
