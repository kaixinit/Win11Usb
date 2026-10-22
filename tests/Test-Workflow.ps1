#requires -Version 5.1
# Executes the real worker and complete real engine entry point against virtual
# files and mocked disk/native-command boundaries. It never enumerates or alters disks.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$engine=[IO.File]::ReadAllText((Join-Path $root 'src\engine\Win11Usb.Engine.ps1'))
$worker=[IO.File]::ReadAllText((Join-Path $root 'src\Win11Usb.Worker.ps1'))
function Check($Value,$Message) { if (-not $Value) { throw $Message } }
$mockEngine=@'
function Assert-Administrator { }
function Get-SourceMedia { param($Letter); return [pscustomobject]@{Letter=$Letter;UniqueId='ISO1';Label='WINISO';Size=9GB;ImagePath=$global:GuiIsoPath;DevicePath='DVD1'} }
function Assert-SourceMediaIdentity { }
function Assert-WholeUsbDependencies { param([switch]$RequireIso) }
function Get-WindowsImage { param($ImagePath,$Index,$ErrorAction); if ($global:Case -in @('BadImage','ExistingMountFailure')) { throw 'corrupt image fixture' }; return [pscustomobject]@{ImageName='Windows 11 Education';EditionId='Education';InstallationType='Client';MajorVersion=10;MinorVersion=0;Build=26200;SPBuild=1} }
function Get-Target { return [pscustomobject]@{Number=4;FriendlyName='Fixture USB';Size=16GB} }
function Assert-WholeUsbIdentity { return Get-Target }
function Select-WholeUsbTarget {
    param($Choice,$SourceLetter)
    $null=Read-Host '选择整支 U 盘：输入上表磁盘编号，或该 U 盘的一个盘符'
    $script:diskNumber=4;$script:target=Get-Target
    return [pscustomobject]@{Letter=$global:UsbLetter;SizeMB=16000;Partitions=@(1,2);Layout='fixture'}
}
function Prepare-Fat32Tool { return 'fixture-format.exe' }
function Format-WholeUsbDisk {
    param($Plan,$ToolPath,$FileSystem,[switch]$RequireIso)
    $global:Events.Add('Format:'+$FileSystem)
    $global:GuiEngineState['TargetRebuildStarted']=$true
    return [pscustomobject]@{PartitionNumber=1;Size=16GB;DriveLetter=$global:UsbLetter}
}
function Test-Path {
    param($LiteralPath,$PathType)
    if ($global:Case -eq 'MissingBoot' -and $LiteralPath -like '*bootx64.efi') { return $false }
    return $true
}
function New-Item {
    param($ItemType,$Path,$ErrorAction)
    if (-not $Path.StartsWith($global:FixtureRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Forbidden filesystem path: '+$Path }
    return [IO.Directory]::CreateDirectory($Path)
}
function Get-ChildItem {
    param($LiteralPath,[switch]$File,[switch]$Recurse,$Filter)
    if ($Filter -eq 'install*.swm') { return $global:Swm }
    return $global:Files
}
function Get-Item {
    param($LiteralPath,$ErrorAction)
    $source=@($global:Files | Where-Object FullName -eq $LiteralPath)
    if ($source.Count) { return $source[0] }
    if (-not $global:Copied.ContainsKey($LiteralPath)) { throw 'Missing copied file: '+$LiteralPath }
    $length=$global:Copied[$LiteralPath]
    if ($global:Case -eq 'CorruptCopy' -and $LiteralPath -like '*setup.exe') { $length-- }
    return [pscustomobject]@{Length=$length}
}
function Get-Partition { param($DiskNumber,$PartitionNumber); return [pscustomobject]@{DiskNumber=4;PartitionNumber=1;DriveLetter=$global:UsbLetter} }
function Get-Volume { param([Parameter(ValueFromPipeline=$true)]$InputObject); process { return [pscustomobject]@{DriveLetter=$global:UsbLetter;FileSystem=$global:GuiFileSystem} } }
function Run-Command {
    param($Program,$Arguments,$Title,$CopyFiles,$StandardInput)
    $global:Events.Add($Program)
    if ($Program -eq 'dism.exe') {
        $staging=Join-Path $script:workRoot 'staging'
        $global:Swm=@([pscustomobject]@{Name='install.swm';FullName=(Join-Path $staging 'install.swm');Length=4000MB},[pscustomobject]@{Name='install2.swm';FullName=(Join-Path $staging 'install2.swm');Length=3GB})
        return
    }
    if ($Program -ne 'robocopy.exe') { throw 'Unexpected native command: '+$Program }
    if ($global:Case -eq 'CopyFailure') { throw 'robocopy fixture exit 8' }
    foreach ($file in $CopyFiles) {
        $relative=if ($Arguments[0] -eq ($global:IsoLetter+':\')) { $file.FullName.Substring(3) } else { $file.Name }
        $global:Copied[(Join-Path $Arguments[1] $relative)]=[long]$file.Length
    }
}
function Remove-Item { param($LiteralPath,[switch]$Force); $global:Events.Add('Cleanup'); if($global:Case -eq 'CleanupFailure'){throw 'cleanup failed fixture'} }
function Eject-CompletedUsb { param($Letter); $global:Events.Add('EjectUsb');if($global:Case -eq 'EjectVeto'){throw 'veto fixture'};return 'U 盘已安全弹出。' }
function Dismount-SourceMedia { param($Snapshot); $global:Events.Add('EjectIso');return 'ISO 已弹出。' }
if($global:Case -eq 'UnknownPrompt'){function Confirm-InstallImageStep {param($Plan);$null=Read-Host 'UNKNOWN PROMPT fixture'}}
'@
$marker='$script:completionHandled = $false'
Check ($engine.Contains($marker)) 'Engine entry marker missing'
$mockedEngine=$engine.Replace($marker,$mockEngine+"`n"+$marker)
$cases=@('DirectFAT32','SplitFAT32','SplitNTFS','CancelFormat','CorruptCopy','CopyFailure','MissingBoot','BadImage','MountFailure','AlreadyMounted','ExistingMountFailure','EjectVeto','KeepMedia','KeepSwm','CleanupFailure','NoMatchingIso','UnknownPrompt')
foreach ($case in $cases) {
    $queue=New-Object 'System.Collections.Concurrent.BlockingCollection[object]'
    $shared=[hashtable]::Synchronized(@{ForceStopRequested=$false;MountedByApp=$false;ExitCode=$null;CopyVerified=$false;TargetRebuildStarted=$false})
    $runspace=[runspacefactory]::CreateRunspace();$runspace.ApartmentState='STA';$runspace.Open()
    $ps=[powershell]::Create();$ps.Runspace=$runspace
    try {
        $fixture=Join-Path $PSScriptRoot ('artifacts/workflow-'+$case+'-'+[guid]::NewGuid().ToString('N'))
        $null=[IO.Directory]::CreateDirectory($fixture)
        $runspace.SessionStateProxy.SetVariable('FixtureRoot',$fixture)
        $runspace.SessionStateProxy.SetVariable('Case',$case)
        $runspace.SessionStateProxy.SetVariable('GuiEngineState',$shared)
        $runspace.SessionStateProxy.SetVariable('GuiPromptQueue',$queue)
        $runspace.SessionStateProxy.SetVariable('GuiEngineScript',$mockedEngine)
        $runspace.SessionStateProxy.SetVariable('GuiIsoPath',(Join-Path $fixture 'windows_11_25h2.iso'))
        $runspace.SessionStateProxy.SetVariable('GuiDiskNumber','4')
        $runspace.SessionStateProxy.SetVariable('GuiFileSystem',$(if($case -eq 'SplitNTFS'){'NTFS'}else{'FAT32'}))
        $null=$ps.AddScript(@'
$env:LOCALAPPDATA=$global:FixtureRoot
$global:Events=New-Object 'Collections.Generic.List[string]'
$global:Copied=@{};$global:Swm=@();$global:Attached=$global:Case -in @('AlreadyMounted','ExistingMountFailure')
$letters=@(90..70 | ForEach-Object {[string][char]$_} | Where-Object {-not (Get-PSDrive -Name $_ -ErrorAction SilentlyContinue)})
if($letters.Count -lt 2){throw 'Two free PowerShell-only drive names required'}
$global:IsoLetter=$letters[0];$global:UsbLetter=$letters[1]
New-PSDrive -Name $global:IsoLetter -PSProvider FileSystem -Root $global:FixtureRoot -Scope Global | Out-Null
New-PSDrive -Name $global:UsbLetter -PSProvider FileSystem -Root $global:FixtureRoot -Scope Global | Out-Null
$global:Files=@(foreach($relative in @('setup.exe','efi\boot\bootx64.efi','sources\boot.wim','sources\install.wim')) {
    $length=if($relative -like '*install.wim'){if($global:Case -in @('SplitFAT32','SplitNTFS','KeepSwm','CleanupFailure')){8GB}else{4054128535}}else{1000}
    [pscustomobject]@{FullName=($global:IsoLetter+':\'+$relative);Name=[IO.Path]::GetFileName($relative);Length=[long]$length}
})
function global:Get-DiskImage {
    param($ImagePath,$Volume,$ErrorAction)
    if($Volume -and $global:Case -eq 'NoMatchingIso'){throw 'not matching ISO fixture'}
    return [pscustomobject]@{Attached=$global:Attached;ImagePath=$global:GuiIsoPath}
}
function global:Mount-DiskImage {
    param($ImagePath,$StorageType,[switch]$PassThru,$ErrorAction)
    $global:Events.Add('Mount')
    if($global:Case -eq 'MountFailure'){throw 'mount fixture failed'}
    $global:Attached=$true;return [pscustomobject]@{Attached=$true;ImagePath=$ImagePath}
}
function global:Get-Volume {param($ErrorAction); return [pscustomobject]@{DriveType='CD-ROM';DriveLetter=$global:IsoLetter}}
function global:Dismount-DiskImage {param($ImagePath,$StorageType,$Confirm,$ErrorAction);$global:Events.Add('DismountOwnedIso');$global:Attached=$false}
'@)
        $null=$ps.Invoke();Check (-not $ps.HadErrors) 'Fixture initialization failed'
        $ps.Commands.Clear();$null=$ps.AddScript($worker)
        $handle=$ps.BeginInvoke();$messages=New-Object 'Collections.Generic.List[string]';$prompts=New-Object 'Collections.Generic.List[string]'
        $deadline=[DateTime]::UtcNow.AddSeconds(15)
        while(-not $handle.IsCompleted -or $queue.Count) {
            if([DateTime]::UtcNow -gt $deadline){throw "Workflow timed out: $case"}
            $item=$null
            if($queue.TryTake([ref]$item,25)) {
                if($item.Kind -eq 'Prompt') {
                    $prompts.Add($item.Prompt)
                    if($item.Prompt -eq 'UNKNOWN PROMPT fixture'){$item.Cancelled=$true}
                    $item.Response=if($item.Index -eq 4){if($case -eq 'CancelFormat'){'2'}else{'1'}}elseif($case -eq 'KeepMedia' -and $item.Prompt -match '弹出'){'Q'}elseif($case -eq 'KeepSwm' -and $item.Prompt -match '清理'){'Q'}else{''}
                    $item.Ready.Set() | Out-Null
                }else{$messages.Add([string]$item.Text)}
            }
        }
        $null=$ps.EndInvoke($handle)
        $events=$runspace.SessionStateProxy.GetVariable('Events')
        $failed=$case -in @('CancelFormat','CorruptCopy','CopyFailure','MissingBoot','BadImage','MountFailure','ExistingMountFailure','CleanupFailure','NoMatchingIso','UnknownPrompt')
        Check ($shared['ExitCode'] -eq $(if($failed){1}else{0})) "Wrong exit result: $case / $($shared['LastError'])"
        Check ([bool]$shared['CopyVerified'] -eq (-not $failed -or $case -eq 'CleanupFailure')) "Wrong verification result: $case"
        if($case -in @('CancelFormat','MissingBoot','BadImage','MountFailure','ExistingMountFailure','NoMatchingIso')){Check (-not $shared['TargetRebuildStarted']) "Failed preflight modified disk: $case"}
        if($case -eq 'AlreadyMounted'){Check (-not $events.Contains('Mount')) 'Re-mounted existing ISO'}
        if($failed -and $case -notin @('MountFailure','ExistingMountFailure')){Check $events.Contains('DismountOwnedIso') 'Failed job left app-owned ISO without cleanup'}
        if($case -eq 'ExistingMountFailure'){Check (-not $events.Contains('DismountOwnedIso')) 'Failed job unmounted a pre-existing ISO'}
        if($case -like 'Split*'){Check $events.Contains('dism.exe') 'Large WIM was not split'}
        if($case -eq 'DirectFAT32'){Check (-not $events.Contains('dism.exe')) 'Small WIM was split'}
        if($case -eq 'EjectVeto'){Check ([string]$shared['UsbResult']).Contains('尚未安全弹出') 'Veto was not retained as a warning'}
        if($case -eq 'KeepMedia'){Check (-not $events.Contains('EjectUsb') -and -not $events.Contains('EjectIso')) 'Skipped eject still executed'}
        if($case -eq 'KeepSwm'){Check (-not $events.Contains('Cleanup')) 'Skipped cleanup still deleted SWM'}
        if(-not $failed){Check (($messages -join "`n") -match '制作完成：.+复制文件大小一致') 'Final verification log missing'}
        Write-Output "PASS: workflow $case"
    } finally {
        if($ps.InvocationStateInfo.State -eq 'Running'){$ps.Stop()}
        $ps.Dispose();$runspace.Dispose();$queue.Dispose()
    }
}
