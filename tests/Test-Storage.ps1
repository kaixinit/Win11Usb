$ErrorActionPreference='Stop'
$base=Split-Path $PSScriptRoot -Parent
$path=Join-Path $base 'src\engine\Win11Usb.Engine.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Parse failed' }
foreach ($name in @('Get-Target','Test-UsbDataPartition','Assert-WholeUsbIdentity','Get-WholeUsbPartitions','Get-WholeUsbLayoutSignature','Select-WholeUsbTarget','Assert-PathOutsideTarget','Assert-WholeUsbDependencies','Confirm-WholeUsbFormat','Get-WholeUsbSinglePartition','Assert-SelectedUsbSnapshot','Assert-SourceMediaIdentity','Clear-ConfirmedWholeUsb','Write-WholeUsbDiagnostic','Format-WholeUsbDisk')) {
    $fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$false)
    . ([scriptblock]::Create($fn.Extent.Text))
}
function Check([bool]$Value,[string]$Message) { if (-not $Value) { throw $Message } }
function Expect-Error([scriptblock]$Action,[string]$Expected) {
    $caught=$null
    try { & $Action } catch { $caught=$_.Exception.Message }
    Check ($caught -and $caught.Contains($Expected)) "Expected '$Expected', got '$caught'"
}
function Disk([int]$Number,[long]$Size,[string]$Name,[bool]$System=$false) {
    return [pscustomobject]@{Number=$Number;Size=$Size;FriendlyName=$Name;BusType=$(if ($System) { 'SAS' } else { 'USB' });IsBoot=$System;IsSystem=$System;IsOffline=$false;IsReadOnly=$false;LogicalSectorSize=512;PartitionStyle='GPT';NumberOfPartitions=2;Path="device$Number";SerialNumber="serial$Number"}
}
function Partition([int]$Number,[long]$Size,[string]$Letter,[string]$Type) {
    return [pscustomobject]@{DiskNumber=4;PartitionNumber=$Number;Offset=1048576;Size=$Size;DriveLetter=$Letter;Type=$Type;MbrType=0;GptType=$(if ($Type -eq 'EFI') { 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b' } else { 'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7' });AccessPaths=@("${Letter}:\");IsSystem=$false;IsBoot=$false;IsHidden=$false;IsReadOnly=$false;IsOffline=$false;IsShadowCopy=$false}
}
function Reset-Fixture([long]$Size=64GB) {
    $script:diskNumber=4; $script:target=$null; $script:targetPnpId=$null
    $script:disk=Disk 4 $Size 'Kingston DataTraveler 3.0'
    $script:allDisks=@((Disk 0 160GB 'System' $true),(Disk 2 1000GB 'Samsung PSSD T7 Touch'),$script:disk)
    $script:parts=@((Partition 1 ($Size-300MB) 'G' 'Basic'),(Partition 2 297MB 'H' 'EFI'))
    $script:actualPnp='USBSTOR\KINGSTON\ORIGINAL'
    $script:sourceMedia=[pscustomobject]@{ImagePath='D:\Windows.iso'}
    $script:launchPath='D:\Win11Usb-StepByStep-v15.ps1'
    $script:workRoot=Join-Path $PSScriptRoot 'artifacts/storage'
    $null=[IO.Directory]::CreateDirectory($script:workRoot)
    $script:diskpartMode='Success'; $script:diskpartCalls=0; $script:changedSource=$false; $global:GuiDiskSnapshot=$null
    $script:imageIdentity=[pscustomobject]@{DisplayName='Windows 10 LTSC 2021';VolumeLabel='WIN10LTSC'}
    $script:events=New-Object 'Collections.Generic.List[string]'
    $script:pathDisk=@{}
    $script:toolValid=$true; $script:wrongNewSize=$false; $script:failedClear=$false; $script:wrongFileSystem=$false; $script:foreignLetter=$false; $script:nativeFormatFails=$false; $script:desiredFs='FAT32'
    $script:fs='RAW'; $script:label=''
    $script:readAnswer='1'; $script:targetRebuildStarted=$false
}
function Get-Disk { param($Number,$ErrorAction); if ($PSBoundParameters.ContainsKey('Number')) { if ($Number -ne 4) { throw 'Unexpected disk query' }; return $script:disk }; return $script:allDisks }
function Get-CimInstance { param($ClassName,$Filter,$ErrorAction); Check ($Filter -eq 'Index=4') 'Wrong PNP query'; return [pscustomobject]@{PNPDeviceID=$script:actualPnp} }
function Get-Partition {
    param($DiskNumber,$DriveLetter,[Parameter(ValueFromPipeline=$true)]$Volume)
    process {
        if ($Volume) { return [pscustomobject]@{DiskNumber=$Volume.MappingDisk} }
        if ($PSBoundParameters.ContainsKey('DriveLetter')) {
            if ($script:foreignLetter) { return [pscustomobject]@{DiskNumber=2;PartitionNumber=1;DriveLetter=$DriveLetter} }
            return @($script:parts | Where-Object DriveLetter -eq $DriveLetter)
        }
        Check ($DiskNumber -eq 4) 'Wrong disk partition enumeration'
        return $script:parts
    }
}
function Get-Volume {
    param($FilePath,[Parameter(ValueFromPipeline=$true)]$InputObject)
    process {
        if ($PSBoundParameters.ContainsKey('FilePath')) {
            $mapped=if ($script:pathDisk.ContainsKey($FilePath)) { $script:pathDisk[$FilePath] } else { 0 }
            return [pscustomobject]@{MappingDisk=$mapped}
        }
        return [pscustomobject]@{FileSystem=$script:fs;DriveLetter=$InputObject.DriveLetter;Size=$InputObject.Size-16MB;FileSystemLabel=$script:label}
    }
}
function Write-Host { param($Object,$ForegroundColor); $script:events.Add([string]$Object) }
function Out-Host { param([Parameter(ValueFromPipeline=$true)]$InputObject); process { } }
function Read-Host { param($Prompt); return $script:readAnswer }
function Get-SourceMedia { param($Letter); if ($script:changedSource) { return [pscustomobject]@{ImagePath='D:\other.iso'} }; return $script:sourceMedia }
function Get-FreeLetter { param($SourceLetter); return 'Z' }
function Assert-Fat32Tool { param($Path); $script:events.Add('HashCheck'); if (-not $script:toolValid) { throw '工具校验失败' } }
function Invoke-WholeUsbStorageAction { param($Title,[scriptblock]$Action); return (& $Action) }
function Clear-Disk {
    param($InputObject,[switch]$RemoveData,[switch]$RemoveOEM,$Confirm,$ErrorAction)
    Check ($InputObject.Number -eq 4 -and $RemoveData -and $RemoveOEM -and -not $Confirm) 'Clear scope/options incorrect'
    $script:events.Add('ClearDisk4')
    if ($script:failedClear) { throw 'Clear failed' }
    $script:parts=@(); $script:disk.PartitionStyle='RAW'; $script:disk.NumberOfPartitions=0
}
function Initialize-Disk { param($InputObject,$PartitionStyle,$ErrorAction); Check ($InputObject.Number -eq 4 -and $PartitionStyle -eq 'MBR') 'Wrong initialization'; $script:events.Add('InitializeMBR4'); $script:disk.PartitionStyle='MBR' }
function New-Partition {
    param($InputObject,[switch]$UseMaximumSize,$MbrType,$AssignDriveLetter,$ErrorAction)
    $expectedMbrType=if($script:desiredFs -eq 'NTFS'){'IFS'}else{'FAT32'}
    Check ($InputObject.Number -eq 4 -and $UseMaximumSize -and $MbrType -eq $expectedMbrType -and -not $AssignDriveLetter) 'New partition not full capacity / expected MBR type'
    $script:events.Add('NewMaximumPartition4')
    $size=if ($script:wrongNewSize) { 31000MB } else { $InputObject.Size-2MB }
    $script:parts=@(Partition 1 $size '' 'FAT32'); $script:parts[0].MbrType=if($script:desiredFs -eq 'NTFS'){7}else{12}; $script:parts[0].GptType=''
    $script:disk.NumberOfPartitions=1
}
function Update-HostStorageCache { }
function Wait-DiskService { }
function Add-PartitionAccessPath { param($DiskNumber,$PartitionNumber,$AccessPath,$ErrorAction); Check ($DiskNumber -eq 4 -and $PartitionNumber -eq 1 -and $AccessPath -eq 'G:\') 'Wrong letter assignment'; if (-not $script:nativeFormatFails) { Check ($script:fs -eq $script:desiredFs) 'Assigned drive letter before formatting' }; $script:events.Add('AssignG4'); $script:parts[0].DriveLetter='G'; $script:parts[0].AccessPaths=@('G:\') }
function Format-Volume { param($Partition,$FileSystem,$Confirm,[switch]$Force,$ErrorAction); Check (-not $Partition.DriveLetter -and $FileSystem -eq $script:desiredFs -and -not $Confirm -and $Force) 'Native formatting must happen before drive-letter assignment'; $script:events.Add('FormatNoLetter'); if ($script:nativeFormatFails) { throw 'fixture: native FAT32 unavailable' }; $script:fs=if ($script:wrongFileSystem) { 'NTFS' } else { $FileSystem } }
function Run-Command {
    param($Program,$Arguments,$Title,$CopyFiles,$StandardInput)
    if ($Program -eq 'diskpart.exe') {
        $script:diskpartCalls++
        $commands=[IO.File]::ReadAllLines($Arguments[1]) -join '|'
        Check ($commands -ceq 'select disk 4|detail disk|list partition|clean|exit') 'DiskPart clean exceeded confirmed scope'
        if ($script:diskpartMode -eq 'Fail') { throw 'DiskPart failed fixture' }
        if ($script:diskpartMode -eq 'NoChange') { return }
        $script:parts=@();$script:disk.PartitionStyle='MBR';$script:disk.NumberOfPartitions=0
        return
    }
    Check ($Program -eq 'mock-fat32.exe' -and $Arguments.Count -eq 1 -and $Arguments[0] -eq 'G:' -and $StandardInput -eq 'Y') 'Wrong formatting target or input'
    $script:events.Add('FormatG4')
    $script:fs=if ($script:wrongFileSystem) { 'NTFS' } else { 'FAT32' }
}
function Set-Volume { param($InputObject,$NewFileSystemLabel,$ErrorAction); Check ($InputObject.DriveLetter -eq 'G' -and $NewFileSystemLabel -eq 'WIN10LTSC') 'Wrong volume label target'; $script:events.Add('LabelAfterMount'); $script:label=$NewFileSystemLabel }

foreach ($size in @(16GB,64GB,128GB,1000GB)) {
    Reset-Fixture $size
    $plan=Select-WholeUsbTarget '4' 'I'
    Check ($plan.Letter -eq 'G' -and $plan.Partitions.Count -eq 2 -and $plan.SizeMB -gt 0) 'PE dual-partition selection failed'
    Assert-WholeUsbDependencies -RequireIso
    Check (Confirm-WholeUsbFormat $plan 'FAT32') 'Confirmation unexpectedly canceled'
    $result=Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso
    Check ($script:parts.Count -eq 1 -and $result.DriveLetter -eq 'G' -and $result.Size -ge $size-16MB -and $script:fs -eq 'FAT32') 'Whole capacity result incorrect'
    Check ($script:events.IndexOf('HashCheck') -lt $script:events.IndexOf('ClearDisk4')) 'Tool checked after destructive stage'
    Check (($script:events -join ',') -match 'ClearDisk4,InitializeMBR4,NewMaximumPartition4,HashCheck,FormatNoLetter,AssignG4,LabelAfterMount') 'Mutation order incorrect'
    Check (@($script:parts | Where-Object DriveLetter -eq 'H').Count -eq 0) 'Old EFI drive remains'
}
Reset-Fixture
$script:desiredFs='NTFS';$plan=Select-WholeUsbTarget '4' 'I'
$result=Format-WholeUsbDisk $plan '' 'NTFS' -RequireIso
Check ($result.DriveLetter -eq 'G' -and $script:fs -eq 'NTFS' -and (($script:events -join ',') -match 'NewMaximumPartition4,FormatNoLetter,AssignG4,LabelAfterMount')) 'NTFS format-before-mount path failed'
Reset-Fixture
$plan=Select-WholeUsbTarget '4' 'I';$script:nativeFormatFails=$true
$result=Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso
Check ($result.DriveLetter -eq 'G' -and $script:events.Contains('FormatG4')) 'FAT32 compatibility fallback did not complete'
Reset-Fixture
$plan=Select-WholeUsbTarget 'G' 'I'
Check ($plan.Letter -eq 'G') 'Selection by letter failed'
foreach ($answer in @('','2','Q')) { $script:readAnswer=$answer; Check (-not (Confirm-WholeUsbFormat $plan 'FAT32')) 'Cancellation accepted as format' }
foreach ($protected in @('D:\Windows.iso','D:\Win11Usb-StepByStep-v15.ps1',$env:LOCALAPPDATA,$env:TEMP,$script:workRoot)) {
    Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:pathDisk[$protected]=4
    Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso } '位于即将清空'
    Check (-not $script:events.Contains('ClearDisk4')) 'Dependency on target was wiped'
}
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:sourceMedia=$null
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso } '无法定位'
Check (-not $script:events.Contains('ClearDisk4')) 'Unknown ISO backing file was wiped'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:toolValid=$false
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '工具校验失败'
Check (-not $script:events.Contains('ClearDisk4')) 'Unverified tool cleared USB'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:actualPnp='OTHER\DEVICE'
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '身份变化'
Check (-not $script:events.Contains('ClearDisk4')) 'PNP swap cleared USB'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:parts[0].Size--
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '分区布局变化'
Check (-not $script:events.Contains('ClearDisk4')) 'Changed layout cleared USB'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:disk.IsBoot=$true
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '不能是系统或启动磁盘'
Check (-not $script:events.Contains('ClearDisk4')) 'System disk was cleared'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:failedClear=$true; $script:diskpartMode='Fail'
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } 'DiskPart failed'
Check (-not $script:events.Contains('InitializeMBR4')) 'Continued after clear failure'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:wrongNewSize=$true
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '完整容量核验失败'
Check (-not $script:events.Contains('FormatG4')) 'Formatted a silently capped 31GB partition'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:foreignLetter=$true
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '盘符被其他分区使用'
Check (-not $script:events.Contains('FormatG4')) 'Formatted foreign drive letter'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I'; $script:wrongFileSystem=$true
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' } '文件系统核验失败'
Check (-not $script:events.Contains('LabelG4')) 'Accepted wrong filesystem'
Reset-Fixture (3TB)
Expect-Error { Select-WholeUsbTarget '4' 'I' } '不超过 2 TiB'
Check (-not $script:events.Contains('ClearDisk4')) 'Unsupported capacity cleared USB'
Write-Output 'PASS: whole-disk PE two-volume -> one full FAT32 volume (16/64/128/1000GiB), confirmation/cancellation, source/script/temp protection, PNP/system/layout/tool guards, no 31GB fallback, post-format/letter/failure validation. All storage/native-format calls mocked; no real disks modified.'

Reset-Fixture
$global:GuiDiskSnapshot=[pscustomobject]@{Number=4;Size=$script:disk.Size;FriendlyName=$script:disk.FriendlyName;Path=$script:disk.Path;SerialNumber=$script:disk.SerialNumber;PnpId='USBSTOR\DIFFERENT'}
Expect-Error { Select-WholeUsbTarget '4' 'I' } '界面选择时'
Check (-not $script:events.Contains('ClearDisk4')) 'UI-to-worker disk substitution was accepted'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I';$script:changedSource=$true
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso } '源 ISO'
Check (-not $script:events.Contains('ClearDisk4')) 'Changed ISO was accepted before clear'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I';$script:failedClear=$true
$result=Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso
Check ($script:diskpartCalls -eq 1 -and $result.DriveLetter -eq 'G' -and $script:fs -eq 'FAT32') 'MBR-zero fallback did not finish'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I';$script:failedClear=$true;$script:diskpartMode='NoChange'
Expect-Error { Format-WholeUsbDisk $plan 'mock-fat32.exe' -RequireIso } '仍未确认分区表已清空'
Check (-not $script:events.Contains('InitializeMBR4')) 'Unverified clean advanced to initialization'
Reset-Fixture; $plan=Select-WholeUsbTarget '4' 'I';$script:desiredFs='NTFS';$script:nativeFormatFails=$true
Expect-Error { Format-WholeUsbDisk $plan '' 'NTFS' -RequireIso } 'native FAT32 unavailable'
Check (-not $script:events.Contains('FormatG4')) 'NTFS failure silently changed filesystem'
'PASS: current storage flow, USB snapshot swap, ISO swap, Clear-Disk fallback, empty MBR, failed-clean rejection, NTFS failure has no implicit FAT32 conversion.'
