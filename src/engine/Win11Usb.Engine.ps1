#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z]:?$')]
    [string]$EjectUsb,
    [ValidatePattern('^[A-Za-z]:?$')]
    [string]$InspectIso,
    [ValidatePattern('^(?:[A-Za-z]:?|[0-9]+)$')]
    [string]$FormatUsb,
    [switch]$GuiMode
)
$ErrorActionPreference = 'Stop'
$script:launchPath = if (-not [string]::IsNullOrWhiteSpace([string]$global:GuiLauncherPath)) { [string]$global:GuiLauncherPath } else { $PSCommandPath }
$script:targetPnpId = $null
$script:targetRebuildStarted = $false

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '请先打开“管理员”PowerShell，再运行本脚本。'
    }
}

function Read-Letter([string]$Prompt) {
    $letter = (Read-Host $Prompt).Trim().TrimEnd(':', '\').ToUpperInvariant()
    if ($letter -notmatch '^[A-Z]$') { throw '盘符只能输入一个字母，例如 F 或 G。' }
    return $letter
}

function Step([int]$Number, [string]$Title) {
    Write-Host "`n[$Number/6] $Title" -ForegroundColor Cyan
    $answer = Read-Host '按回车执行本步；输入 Q 退出'
    if ($answer -ne '') { throw '已退出，不再执行后续步骤。' }
}

function Confirm-Fat32([string]$Letter, $Disk, [string]$Action) {
    Write-Host "`n格式化确认" -ForegroundColor Yellow
    Write-Host "  磁盘 $($Disk.Number)  |  $($Disk.FriendlyName)  |  $([math]::Round($Disk.Size/1GB,1)) GB"
    Write-Host "  目标盘符：${Letter}:\"
    Write-Host "  安装系统：$($script:imageIdentity.DisplayName)  |  卷标：$($script:imageIdentity.VolumeLabel)"
    Write-Host "  $Action"
    Write-Host '  格式化会删除目标分区全部文件。' -ForegroundColor Yellow
    Write-Host '  1  格式化为 FAT32，继续制作安装 U 盘'
    Write-Host '  2  取消并退出'
    while ($true) {
        $answer = (Read-Host '请输入编号（回车默认取消）').Trim()
        if ($answer -eq '1') { return $true }
        if ($answer -eq '' -or $answer -eq '2' -or $answer -ieq 'Q') { return $false }
        Write-Host '请输入 1 或 2；回车取消。' -ForegroundColor Yellow
    }
}

function Read-EjectChoice([string]$Prompt) {
    while ($true) {
        $answer = (Read-Host $Prompt).Trim()
        if ($answer -eq '') { return $true }
        if ($answer -ieq 'Q') { return $false }
        Write-Host '请按回车，或输入 Q。' -ForegroundColor Yellow
    }
}

function Complete-UsbSession([string]$Letter, [string]$SourceLetter) {
    Write-Host "`n制作完成  |  ${Letter}:  |  $($script:target.FriendlyName)" -ForegroundColor Green
    Write-Host "系统：$($script:imageIdentity.DisplayName)  |  卷标：$($script:imageIdentity.VolumeLabel)"
    if ($script:targetFileSystem -eq 'NTFS') {
        Write-Host 'NTFS · 文件大小校验通过 · UEFI 启动文件已就绪（固件需支持 NTFS）'
    } else {
        Write-Host 'FAT32 · 文件大小校验通过 · UEFI 启动文件已就绪'
    }
    Write-Host '安装时在启动菜单选择 UEFI: U盘名称。'
    $usbResult = "U 盘 ${Letter}: 保持连接。"
    $isoResult = 'ISO 保持挂载。'
    if (Read-EjectChoice "按回车安全弹出 U 盘 ${Letter}:；输入 Q 跳过") {
        try { $usbResult = Eject-CompletedUsb $Letter; Write-Host $usbResult -ForegroundColor Green }
        catch { $usbResult = "U 盘尚未安全弹出：$($_.Exception.Message)"; Write-Host "制作已完成，但 $usbResult" -ForegroundColor Yellow }
    } else { Write-Host $usbResult -ForegroundColor DarkCyan }
    if (Read-EjectChoice "按回车弹出已识别的 ISO / DVD（${SourceLetter}:）；输入 Q 退出并保留挂载") {
        try { $isoResult = Dismount-SourceMedia $script:sourceMedia; Write-Host $isoResult -ForegroundColor Green }
        catch { $isoResult = "ISO 未弹出：$($_.Exception.Message)"; Write-Host "制作已完成；$isoResult" -ForegroundColor Yellow }
    } else { Write-Host '已退出，ISO 保持挂载。' -ForegroundColor DarkCyan }
    if ($global:GuiEngineState) { $global:GuiEngineState['UsbResult'] = $usbResult; $global:GuiEngineState['IsoResult'] = $isoResult }
    $script:completionHandled = $true
}


function Get-SourceMedia([string]$Letter) {
    $volume = Get-Volume -DriveLetter $Letter -ErrorAction Stop
    if ([string]$volume.DriveType -ne 'CD-ROM') { return $null }
    $image = $null
    try {
        $images = @(Get-DiskImage -Volume $volume -ErrorAction Stop | Where-Object { $_.Attached -and [int]$_.StorageType -eq 1 })
        if ($images.Count -eq 1) { $image = $images[0] }
    } catch { }
    return [pscustomobject]@{Letter=$Letter; UniqueId=$volume.UniqueId; Label=$volume.FileSystemLabel; Size=$volume.Size; ImagePath=$image.ImagePath; DevicePath=$image.DevicePath}
}

function Dismount-SourceMedia($Snapshot) {
    if (-not $Snapshot) { throw '未能识别原来的 DVD / ISO，已跳过弹出。' }
    $present = @(Get-Volume -DriveLetter $Snapshot.Letter -ErrorAction SilentlyContinue)
    if ($present.Count -eq 0 -or $present[0].Size -eq 0) { return 'ISO / DVD 已经弹出。' }
    $current = Get-SourceMedia $Snapshot.Letter
    if (-not $current -or $current.Size -eq 0) { return 'ISO / DVD 已经弹出。' }
    if ($current.UniqueId -cne $Snapshot.UniqueId -or $current.Label -cne $Snapshot.Label -or
        $current.Size -ne $Snapshot.Size -or $current.ImagePath -cne $Snapshot.ImagePath -or $current.DevicePath -cne $Snapshot.DevicePath) {
        throw 'DVD 盘符或映像身份已经变化，已跳过弹出。'
    }
    if ($Snapshot.ImagePath) {
        $null = Dismount-DiskImage -ImagePath $Snapshot.ImagePath -StorageType ISO -Confirm:$false -ErrorAction Stop
        $after = Get-DiskImage -ImagePath $Snapshot.ImagePath -ErrorAction Stop
        if ($after.Attached) { throw 'ISO 仍处于挂载状态，请在资源管理器中手动弹出。' }
    } else {
        # Shell eject also supports virtual DVD drives that Storage cannot map to an ISO file.
        $shell = $null; $folder = $null; $item = $null
        try {
            $shell = New-Object -ComObject Shell.Application
            $folder = $shell.Namespace(17)
            $item = $folder.ParseName($Snapshot.Letter + ':')
            if (-not $item) { throw '找不到原来的 DVD 驱动器。' }
            $item.InvokeVerb('Eject')
        } finally {
            foreach ($com in @($item, $folder, $shell)) {
                if ($null -ne $com -and [Runtime.InteropServices.Marshal]::IsComObject($com)) { $null = [Runtime.InteropServices.Marshal]::FinalReleaseComObject($com) }
            }
        }
        $removed = $false
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            Start-Sleep -Milliseconds 250
            $volumes = @(Get-Volume -DriveLetter $Snapshot.Letter -ErrorAction SilentlyContinue)
            if ($volumes.Count -eq 0 -or $volumes[0].Size -eq 0) { $removed = $true; break }
        }
        if (-not $removed) { throw '尚未确认 DVD 已弹出。请关闭占用它的窗口，再在资源管理器中弹出。' }
    }
    return "已弹出 ISO / DVD（$($Snapshot.Letter):）。"
}

function Initialize-DeviceEject {
    if ('Win11Usb.NativeEjectV12' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
namespace Win11Usb {
    public sealed class DeviceNode {
        public string InstanceId;
        public uint Capabilities;
        public bool CapabilitiesKnown;
        public string Service;
        public bool ServiceKnown;
        public bool ChildrenKnown;
        public string OnlyChildId;
    }
    public sealed class EjectResult {
        public uint Code;
        public int Veto;
        public string Detail;
    }
    public static class NativeEjectV12 {
        const uint NoSuchNode = 0x0D;
        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern uint CM_Locate_DevNodeW(out uint node, string id, uint flags);
        [DllImport("cfgmgr32.dll", ExactSpelling=true)]
        static extern uint CM_Get_Parent(out uint parent, uint node, uint flags);
        [DllImport("cfgmgr32.dll", ExactSpelling=true)]
        static extern uint CM_Get_Child(out uint child, uint node, uint flags);
        [DllImport("cfgmgr32.dll", ExactSpelling=true)]
        static extern uint CM_Get_Sibling(out uint sibling, uint node, uint flags);
        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern uint CM_Get_Device_IDW(uint node, [Out] StringBuilder id, uint length, uint flags);
        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern uint CM_Get_DevNode_Registry_PropertyW(uint node, uint property, out uint type, [Out] byte[] buffer, ref uint length, uint flags);
        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
        static extern uint CM_Request_Device_EjectW(uint node, out int veto, [Out] StringBuilder name, uint length, uint flags);
        static string Id(uint node) {
            var id = new StringBuilder(512);
            uint code = CM_Get_Device_IDW(node, id, 512, 0);
            if (code != 0) throw new InvalidOperationException("读取设备身份失败，系统代码 0x" + code.ToString("X"));
            return id.ToString();
        }
        static DeviceNode Read(uint node) {
            var info = new DeviceNode();
            info.InstanceId = Id(node);
            uint type, length = 4;
            var caps = new byte[4];
            uint code = CM_Get_DevNode_Registry_PropertyW(node, 0x10, out type, caps, ref length, 0);
            info.CapabilitiesKnown = code == 0 && type == 4 && length == 4;
            if (info.CapabilitiesKnown) info.Capabilities = BitConverter.ToUInt32(caps, 0);
            length = 4096;
            var service = new byte[length];
            code = CM_Get_DevNode_Registry_PropertyW(node, 0x05, out type, service, ref length, 0);
            info.ServiceKnown = code == 0 && type == 1 && length >= 2 && length <= service.Length && length % 2 == 0;
            if (info.ServiceKnown) info.Service = Encoding.Unicode.GetString(service, 0, (int)length).TrimEnd('\0');
            uint child, sibling;
            code = CM_Get_Child(out child, node, 0);
            if (code == NoSuchNode) { info.ChildrenKnown = true; return info; }
            if (code != 0) return info;
            string childId = Id(child);
            code = CM_Get_Sibling(out sibling, child, 0);
            info.ChildrenKnown = code == 0 || code == NoSuchNode;
            if (code == NoSuchNode) info.OnlyChildId = childId;
            return info;
        }
        public static DeviceNode[] GetChain(string diskId) {
            uint node;
            uint code = CM_Locate_DevNodeW(out node, diskId, 0);
            if (code != 0) throw new InvalidOperationException("无法定位原设备，系统代码 0x" + code.ToString("X"));
            var chain = new List<DeviceNode>();
            var seen = new HashSet<uint>();
            for (int depth = 0; depth < 24; depth++) {
                if (!seen.Add(node)) throw new InvalidOperationException("设备父子关系异常，已取消弹出。");
                var info = Read(node);
                chain.Add(info);
                // The first physical USB node is a strict boundary: never climb to its hub.
                if (info.InstanceId.StartsWith("USB\\VID_", StringComparison.OrdinalIgnoreCase)) return chain.ToArray();
                uint parent;
                code = CM_Get_Parent(out parent, node, 0);
                if (code == NoSuchNode) return chain.ToArray();
                if (code != 0) throw new InvalidOperationException("读取设备父节点失败，系统代码 0x" + code.ToString("X"));
                node = parent;
            }
            throw new InvalidOperationException("设备父子关系过深，已取消弹出。");
        }
        public static EjectResult Request(string id) {
            uint node;
            uint code = CM_Locate_DevNodeW(out node, id, 0);
            if (code != 0) return new EjectResult { Code = code, Veto = -1, Detail = "" };
            int veto = 0;
            var name = new StringBuilder(new string('\0', 260), 260);
            code = CM_Request_Device_EjectW(node, out veto, name, 260, 0);
            // Veto output is meaningful only for CR_REMOVE_VETOED.
            return new EjectResult { Code = code, Veto = code == 0x17 ? veto : -1, Detail = code == 0x17 ? name.ToString().TrimEnd('\0') : "" };
        }
    }
}
'@
}

function Get-UsbDeviceChain([string]$InstanceId) {
    Initialize-DeviceEject
    return [Win11Usb.NativeEjectV12]::GetChain($InstanceId)
}

function Get-UsbEjectCandidate([object[]]$Nodes, [string]$DiskInstanceId) {
    if ($Nodes.Count -lt 2 -or $Nodes[0].InstanceId -ine $DiskInstanceId) { throw '设备父子关系核验失败，已取消弹出。' }
    for ($index = 1; $index -lt $Nodes.Count; $index++) {
        $parent = $Nodes[$index]
        if (-not $parent.ChildrenKnown -or -not $parent.OnlyChildId -or $parent.OnlyChildId -ine $Nodes[$index - 1].InstanceId) {
            throw '设备包含其他子设备，或无法核验唯一子设备，已取消自动弹出。请从任务栏安全删除硬件。'
        }
        if ($parent.InstanceId -like 'USB\VID_*') {
            if (-not $parent.ServiceKnown -or $parent.Service -notin @('USBSTOR', 'UASPStor') -or
                $parent.InstanceId -match '&MI_' -or -not $parent.CapabilitiesKnown -or
                ($parent.Capabilities -band 4) -eq 0) {
                throw '未找到可安全移除的独立 USB 存储设备，已取消自动弹出。请从任务栏安全删除硬件。'
            }
            return $parent
        }
    }
    throw '未找到对应的 USB 存储设备，已取消自动弹出。请从任务栏安全删除硬件。'
}

function Invoke-UsbEjectRequest([string]$InstanceId) {
    Initialize-DeviceEject
    return [Win11Usb.NativeEjectV12]::Request($InstanceId)
}

function Get-UsbEjectFailure($Result) {
    $code = '0x{0:X}' -f [uint32]$Result.Code
    if ($Result.Code -ne 0x17) { return "Windows 未能完成安全弹出（系统代码 $code）。请从任务栏安全删除硬件。" }
    $description = switch ([int]$Result.Veto) {
        1 { '该设备不支持此移除操作' }
        2 { '设备仍在等待关闭操作完成' }
        3 { '应用程序阻止移除，请关闭使用 U 盘的程序后重试' }
        4 { '系统服务阻止移除' }
        5 { '仍有未关闭的文件句柄，请关闭使用 U 盘的窗口和程序后重试' }
        6 { '设备拒绝移除请求' }
        7 { '驱动程序拒绝移除请求' }
        8 { '该设备节点不支持此弹出请求' }
        9 { '设备电力不足，无法完成移除操作' }
        10 { '设备不能禁用' }
        11 { '驱动程序不支持此移除操作' }
        12 { '当前权限不足以移除设备' }
        13 { '设备已经被移除' }
        default { '系统拒绝移除，未提供明确原因' }
    }
    return "$description（系统代码 $code，拒绝类型 $($Result.Veto)）。请从任务栏安全删除硬件。"
}

function Assert-UsbEjectIdentity([string]$Letter) {
    $disk = Get-Target
    $partitions = @(Get-Partition -DriveLetter $Letter -ErrorAction Stop)
    if ($partitions.Count -ne 1 -or $partitions[0].DiskNumber -ne $disk.Number) { throw 'U 盘盘符已经变化，已取消弹出。' }
    $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$($disk.Number)" -ErrorAction Stop)
    if ($devices.Count -ne 1 -or -not $script:targetPnpId -or $devices[0].PNPDeviceID -cne $script:targetPnpId) {
        throw 'U 盘设备身份已经变化，已取消弹出。'
    }
}

function Eject-CompletedUsb([string]$Letter) {
    Assert-UsbEjectIdentity $Letter
    $nodes = @(Get-UsbDeviceChain $script:targetPnpId)
    $candidate = Get-UsbEjectCandidate $nodes $script:targetPnpId
    Assert-UsbEjectIdentity $Letter
    $latest = Get-UsbEjectCandidate @(Get-UsbDeviceChain $script:targetPnpId) $script:targetPnpId
    if ($latest.InstanceId -cne $candidate.InstanceId -or $latest.Service -cne $candidate.Service -or
        $latest.Capabilities -ne $candidate.Capabilities) { throw '弹出前设备身份或状态变化，已取消弹出。' }
    $result = Invoke-UsbEjectRequest $latest.InstanceId
    if ($script:workRoot) {
        $details = "Disk=$script:targetPnpId`r`nEject=$($latest.InstanceId)`r`nCode=0x$(([uint32]$result.Code).ToString('X'))`r`nVeto=$($result.Veto)`r`nDetail=$($result.Detail)`r`n"
        try { [IO.File]::WriteAllText((Join-Path $script:workRoot 'eject-usb.log'), $details, (New-Object Text.UTF8Encoding($true))) } catch { }
    }
    if ($result.Code -ne 0) { throw (Get-UsbEjectFailure $result) }
    return "U 盘 ${Letter}: 已安全弹出，可以拔下。"
}

function Start-UsbEjectOnly([string]$Letter) {
    Assert-Administrator
    $Letter = $Letter.TrimEnd(':').ToUpperInvariant()
    $partitions = @(Get-Partition -DriveLetter $Letter -ErrorAction Stop)
    if ($partitions.Count -ne 1) { throw '无法确定该盘符对应的唯一磁盘。' }
    $script:diskNumber = [int]$partitions[0].DiskNumber
    $script:target = $null
    $script:target = Get-Target
    $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$script:diskNumber" -ErrorAction Stop)
    if ($devices.Count -ne 1 -or -not $devices[0].PNPDeviceID) { throw '无法识别该 U 盘设备。' }
    $script:targetPnpId = $devices[0].PNPDeviceID
    Write-Host "安全弹出  |  ${Letter}:  |  磁盘 $script:diskNumber  |  $($script:target.FriendlyName)" -ForegroundColor Cyan
    if (Read-EjectChoice '按回车安全弹出此 U 盘；输入 Q 退出') {
        Write-Host (Eject-CompletedUsb $Letter) -ForegroundColor Green
    } else { Write-Host '已退出，U 盘保持连接。' }
    $script:completionHandled = $true
}


function Get-InstallImageBaseIdentity([string]$ImagePath) {
    # Read the source installation image, never the running computer's OS.
    $identity = [pscustomobject]@{
        DisplayName = 'Windows 安装介质（版本未识别）'
        VolumeLabel = 'WINSETUP'
        DetectionNote = '使用通用卷标 WINSETUP。'
        ImagePath = $ImagePath
        ImageIndex = 1
        ImageName = ''
        ImageDescription = ''
        EditionId = ''
        InstallationType = ''
        Version = ''
        IsLtsc = $false
        ReadError = ''
    }
    try {
        $metadata = @(Get-WindowsImage -ImagePath $ImagePath -Index 1 -ErrorAction Stop)[0]
        if ($null -eq $metadata) { throw '安装映像未返回版本信息。' }
    } catch {
        $identity.DetectionNote = '读取安装映像信息失败，使用通用卷标 WINSETUP。'
        $identity.ReadError = $_.Exception.Message
        return $identity
    }

    # These are DISM ImageInfoObject properties. A missing property is tolerated
    # so that an unsupported image still receives a neutral, valid FAT32 label.
    $values = @{}
    foreach ($propertyName in @('ImageName', 'ImageDescription', 'EditionId', 'InstallationType', 'ProductType', 'MajorVersion', 'MinorVersion', 'Build', 'SPBuild')) {
        $property = $metadata.PSObject.Properties[$propertyName]
        $values[$propertyName] = if ($null -ne $property) { $property.Value } else { $null }
    }
    $name = ([string]$values.ImageName -replace '[\x00-\x1F\x7F]', ' ').Trim()
    $description = [string]$values.ImageDescription
    $edition = ([string]$values.EditionId).Trim()
    $installationType = ([string]$values.InstallationType).Trim()
    $productType = ([string]$values.ProductType).Trim()
    $major = 0; $minor = 0; $build = 0; $revision = 0
    [void][int]::TryParse([string]$values.MajorVersion, [ref]$major)
    [void][int]::TryParse([string]$values.MinorVersion, [ref]$minor)
    [void][int]::TryParse([string]$values.Build, [ref]$build)
    [void][int]::TryParse([string]$values.SPBuild, [ref]$revision)
    $identity.ImageName = $name
    $identity.ImageDescription = $description
    $identity.EditionId = $edition
    $identity.InstallationType = $installationType
    if ($major -gt 0 -and $build -gt 0) { $identity.Version = '{0}.{1}.{2}.{3}' -f $major, $minor, $build, $revision }

    # Server builds may share a build number with Windows clients. Server wins
    # over all client/name hints before the Windows 10 / 11 build distinction.
    $server = $installationType -match '^Server' -or $productType -match '^(ServerNT|LanmanNT)$' -or $edition -match '^Server' -or $name -match '\bWindows\s+Server\b'
    $client = $installationType -ieq 'Client' -or $productType -ieq 'WinNT' -or $name -match '\bWindows\s+(10|11)\b'
    $ltsc = (($name + ' ' + $description) -match '\b(LTSC|LTSB)\b') -or $edition -match '^(EnterpriseS|EnterpriseSN|IoTEnterpriseS)(Eval)?$'
    $identity.IsLtsc = [bool]$ltsc

    if ($server) {
        $identity.DisplayName = if ($name -match '\bWindows\s+Server\b') { $name } else { 'Windows Server' }
        $identity.VolumeLabel = 'WINSERVER'
    } elseif ($client -and $major -eq 10 -and $build -ge 10240) {
        $family = if ($build -ge 22000) { 'Windows 11' } else { 'Windows 10' }
        $identity.VolumeLabel = if ($build -ge 22000) { 'WIN11' } else { 'WIN10' }
        if ($name -match '^Windows\s+(10|11)\b') {
            $identity.DisplayName = $name -replace '^Windows\s+(10|11)\b', $family
        } else {
            $identity.DisplayName = $family
        }
        if ($ltsc) {
            $identity.VolumeLabel += '_LTSC'
            if ($identity.DisplayName -notmatch '\b(LTSC|LTSB)\b') { $identity.DisplayName += ' LTSC' }
        }
    }
    if ($identity.VolumeLabel -ne 'WINSETUP') {
        $identity.DetectionNote = '按安装映像索引 1 的系统类型、版本及版本名称识别；卷标：' + $identity.VolumeLabel + '。'
    } else {
        $identity.DetectionNote = '安装映像版本无法可靠识别，使用通用卷标 WINSETUP。'
    }
    return $identity
}


function Get-ReleaseHint([string]$Text, [bool]$IsLtsc) {
    $families = @([regex]::Matches($Text, '(?i)windows[ _-]+(10|11)(?![0-9])') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    $hasLtsc = $Text -match '(?i)(?<![a-z0-9])(LTSC|LTSB)(?![a-z])'
    $hasStandardRelease = $Text -match '(?i)(?<![a-z0-9])[0-9]{2}H[12](?![a-z0-9])'
    $pattern = if ($IsLtsc) { '(?i)(?:LTSC|LTSB)[ _-]*(20[0-9]{2})(?![0-9])' } else { '(?i)(?<![a-z0-9])([0-9]{2}H[12])(?![a-z0-9])' }
    $releases = @([regex]::Matches($Text, $pattern) | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Select-Object -Unique)
    return [pscustomobject]@{Families=$families; IsLtsc=$hasLtsc; HasStandardRelease=$hasStandardRelease; Releases=$releases}
}

function Get-ReleaseRules {
    # These are compatible image header builds, not a range of future versions.
    # 19041 and 26100 can be shared bases and need an explicit release hint.
    return @{
        '10:LTSC:2015' = @(10240)
        '10:LTSC:2016' = @(14393)
        '10:LTSC:2019' = @(17763)
        '10:LTSC:2021' = @(19041,19044)
        '11:LTSC:2024' = @(26100)
        '10:Standard:20H2' = @(19041,19042)
        '10:Standard:21H1' = @(19041,19043)
        '10:Standard:21H2' = @(19041,19044)
        '10:Standard:22H2' = @(19041,19045)
        '11:Standard:21H2' = @(22000)
        '11:Standard:22H2' = @(22621)
        '11:Standard:23H2' = @(22621,22631)
        '11:Standard:24H2' = @(26100)
        '11:Standard:25H2' = @(26100,26200)
        '11:Standard:26H1' = @(28000)
        '11:Standard:26H2' = @(26300)
    }
}

function Resolve-InstallImageRelease($Identity, [string]$IsoPath = '') {
    $result = [pscustomobject]@{Release=''; Source=''; Status='Unknown'; Note='发行版本未确认，保留基础卷标。'}
    $family = switch -Regex ($Identity.VolumeLabel) { '^WIN10' { '10' }; '^WIN11' { '11' }; default { '' } }
    $version = $null
    if (-not $family -or -not [version]::TryParse($Identity.Version, [ref]$version)) { return $result }
    $kind = if ($Identity.IsLtsc) { 'LTSC' } else { 'Standard' }
    $rules = Get-ReleaseRules
    $metadataText = $Identity.ImageName + ' ' + $Identity.ImageDescription
    $metadataHint = Get-ReleaseHint $metadataText $Identity.IsLtsc
    # Never use directory names, timestamps or the running computer's registry.
    $isoName = if ($IsoPath) { [IO.Path]::GetFileNameWithoutExtension($IsoPath) } else { '' }
    $isoHint = Get-ReleaseHint $isoName $Identity.IsLtsc
    $conflict = $false
    if ($metadataHint.Releases.Count -gt 1 -or $isoHint.Releases.Count -gt 1 -or $isoHint.Families.Count -gt 1) { $conflict = $true }
    if ($isoHint.Families.Count -eq 1 -and $isoHint.Families[0] -ne $family) { $conflict = $true }
    if ($isoHint.Families.Count -eq 1 -and ($isoHint.IsLtsc -or $isoHint.Releases.Count -or $isoHint.HasStandardRelease) -and $isoHint.IsLtsc -ne $Identity.IsLtsc) { $conflict = $true }
    $metadataRelease = if ($metadataHint.Releases.Count -eq 1) { $metadataHint.Releases[0] } else { '' }
    $isoRelease = if ($isoHint.Families.Count -eq 1 -and $isoHint.Releases.Count -eq 1) { $isoHint.Releases[0] } else { '' }
    if ($metadataRelease -and $isoRelease -and $metadataRelease -ne $isoRelease) { $conflict = $true }
    foreach ($hint in @($metadataRelease, $isoRelease)) {
        if ($hint) {
            $key = "${family}:${kind}:$hint"
            if (-not $rules.ContainsKey($key) -or $version.Build -notin $rules[$key]) { $conflict = $true }
        }
    }
    if ($conflict) {
        $result.Status = 'Conflict'
        $result.Note = '映像信息与 ISO 名称或版本规则不一致，发行版本未确认，保留基础卷标。'
        return $result
    }
    if ($metadataRelease) {
        $result.Release=$metadataRelease; $result.Source='映像名称'; $result.Status='ImageMetadata'
    } elseif ($isoRelease) {
        $result.Release=$isoRelease; $result.Source='ISO 文件名'; $result.Status='IsoNameHint'
    } else {
        $matches = @($rules.Keys | Where-Object { $_ -like "${family}:${kind}:*" -and $version.Build -in $rules[$_] })
        if ($matches.Count -eq 1) {
            $result.Release=$matches[0].Split(':')[2]; $result.Source='映像构建号与版本类型'; $result.Status='BuildRule'
        }
    }
    if ($result.Release) {
        $result.Note = if ($result.Status -eq 'IsoNameHint') {
            '发行版本依据 ISO 文件名辅助识别，已核对映像系统类型及兼容构建号；不能检测同系列 ISO 被错误重命名的情况。'
        } else { '发行版本依据' + $result.Source + '识别。' }
    }
    return $result
}

function Get-InstallImageIdentity([string]$ImagePath, [string]$IsoPath = '') {
    $identity = Get-InstallImageBaseIdentity $ImagePath
    $releaseInfo = Resolve-InstallImageRelease $identity $IsoPath
    $identity | Add-Member -NotePropertyName Release -NotePropertyValue $releaseInfo.Release
    $identity | Add-Member -NotePropertyName ReleaseSource -NotePropertyValue $releaseInfo.Source
    $identity | Add-Member -NotePropertyName ReleaseStatus -NotePropertyValue $releaseInfo.Status
    $identity | Add-Member -NotePropertyName IsoName -NotePropertyValue $(if ($IsoPath) { [IO.Path]::GetFileName($IsoPath) } else { '' })
    if ($releaseInfo.Release) {
        $family = if ($identity.VolumeLabel -like 'WIN10*') { '10' } else { '11' }
        if ($identity.IsLtsc) { $identity.VolumeLabel = "WIN${family}LTSC" }
        else { $identity.VolumeLabel = "WIN${family}_$($releaseInfo.Release)" }
        if ($identity.DisplayName -notmatch ('(?<![a-z0-9])' + [regex]::Escape($releaseInfo.Release) + '(?![a-z0-9])')) {
            $identity.DisplayName += ' ' + $releaseInfo.Release
        }
    }
    if ($identity.IsLtsc -and $identity.VolumeLabel -in @('WIN10_LTSC','WIN11_LTSC')) {
        $identity.VolumeLabel = $identity.VolumeLabel.Replace('_', '')
    }
    if ($identity.VolumeLabel.Length -gt 11 -or $identity.VolumeLabel -cnotmatch '^[A-Z0-9_]+$') { throw '生成的卷标无效，尚未格式化 U 盘。' }
    if ($identity.VolumeLabel -ne 'WINSETUP' -and $identity.VolumeLabel -ne 'WINSERVER') {
        $identity.DetectionNote = $releaseInfo.Note
    }
    return $identity
}

function Show-InstallImageIdentity($Identity) {
    Write-Host "安装系统：$($Identity.DisplayName)" -ForegroundColor Cyan
    if ($Identity.Version) { Write-Host "映像版本：$($Identity.Version)" }
    if ($Identity.IsoName) { Write-Host "ISO 文件：$($Identity.IsoName)" }
    Write-Host "U 盘卷标：$($Identity.VolumeLabel)"
    if ($Identity.DetectionNote) { Write-Host $Identity.DetectionNote -ForegroundColor DarkCyan }
}

function Start-IsoInspection([string]$Letter) {
    Assert-Administrator
    $Letter = $Letter.TrimEnd(':').ToUpperInvariant()
    $root = "${Letter}:\"
    $snapshot = Get-SourceMedia $Letter
    if (-not $snapshot) { throw '该盘符不是已挂载的 ISO / DVD。' }
    $image = $null
    foreach ($name in @('install.wim','install.esd','install.swm')) {
        $candidate = Join-Path $root ('sources\' + $name)
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $image = $candidate; break }
    }
    if (-not $image) { throw '该 ISO 中未找到安装映像。' }
    Write-Host "Windows 安装介质识别  |  ${Letter}:" -ForegroundColor Green
    Show-InstallImageIdentity (Get-InstallImageIdentity $image $snapshot.ImagePath)
    Write-Host '只读识别完成。' -ForegroundColor Green
    $script:completionHandled = $true
}


function Test-UsbDataPartition($Partition, [string]$PartitionStyle) {
    if ($Partition.IsSystem -or $Partition.IsBoot -or $Partition.IsHidden -or
        $Partition.IsReadOnly -or $Partition.IsOffline -or $Partition.IsShadowCopy) { return $false }
    if ($PartitionStyle -eq 'GPT') {
        $type=[guid]::Empty
        if (-not [guid]::TryParse([string]$Partition.GptType, [ref]$type)) { return $false }
        return $type -eq [guid]'EBD0A0A2-B9E5-4433-87C0-68B6B72699C7'
    }
    if ($PartitionStyle -ne 'MBR' -or $null -eq $Partition.MbrType) { return $false }
    $type=-1
    try { $type=[Convert]::ToInt32($Partition.MbrType) }
    catch {
        # Also accept adapted enum names while the numeric CIM value remains authoritative.
        switch (([string]$Partition.MbrType).Trim().ToUpperInvariant()) {
            'FAT12' { $type=1 }
            'FAT16' { $type=4 }
            'HUGE' { $type=6 }
            'IFS' { $type=7 }
            'FAT32' { $type=12 }
        }
    }
    # Windows data partitions only; extended containers, EFI, recovery and dynamic types are excluded.
    return $type -in @(0x01, 0x04, 0x06, 0x07, 0x0B, 0x0C, 0x0E)
}

function Get-DataPartitions {
    $disk=Get-Target
    if ($disk.NumberOfPartitions -eq 0) { return }
    @(Get-Partition -DiskNumber $script:diskNumber -ErrorAction Stop | Where-Object {
        Test-UsbDataPartition $_ ([string]$disk.PartitionStyle)
    })
}

function Show-TargetPartitionDetails {
    Write-Host '当前目标分区信息（仅查看）：' -ForegroundColor Yellow
    Get-Partition -DiskNumber $script:diskNumber -ErrorAction Stop |
        Select-Object DiskNumber,PartitionNumber,DriveLetter,Type,MbrType,GptType,
            @{Name='GB';Expression={[math]::Round($_.Size/1GB,2)}},IsHidden,IsSystem,IsBoot |
        Format-List | Out-Host
}


function Get-FreeLetter([string]$SourceLetter) {
    $used = @($SourceLetter)
    $used += @(Get-Volume -ErrorAction Stop | Where-Object DriveLetter | ForEach-Object { [string]$_.DriveLetter })
    $used += @(Get-PSDrive -PSProvider FileSystem | ForEach-Object Name)
    foreach ($code in 70..90) {
        $candidate = [string][char]$code
        if ($used -notcontains $candidate) { return $candidate }
    }
    throw '没有可用盘符，已停止。'
}

function Get-Target {
    $disk = Get-Disk -Number $script:diskNumber -ErrorAction Stop
    if ($disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or
        $disk.IsOffline -or $disk.IsReadOnly -or $disk.Size -lt 8GB) {
        throw '目标必须是联机、可写、至少 8 GB 的 USB 磁盘，不能是系统或启动磁盘。'
    }
    if ($script:target -and ($disk.Size -ne $script:target.Size -or
        $disk.FriendlyName -cne $script:target.FriendlyName -or
        $disk.Path -cne $script:target.Path -or $disk.SerialNumber -cne $script:target.SerialNumber)) {
        throw '目标 U 盘身份变化，已停止。'
    }
    if ($script:targetPnpId) {
        $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$($disk.Number)" -ErrorAction Stop)
        if ($devices.Count -ne 1 -or $devices[0].PNPDeviceID -cne $script:targetPnpId) { throw '目标 U 盘设备身份变化，已停止。' }
    }
    return $disk
}

function Set-ProgressTheme {
    $theme=[pscustomobject]@{HostData=$Host.PrivateData; Foreground=$null; Background=$null; Style=$null; View=$null}
    try {
        if ($null -ne $theme.HostData.ProgressForegroundColor -and $null -ne $theme.HostData.ProgressBackgroundColor) {
            $theme.Foreground=$theme.HostData.ProgressForegroundColor
            $theme.Background=$theme.HostData.ProgressBackgroundColor
            $theme.HostData.ProgressForegroundColor='Cyan'
            $theme.HostData.ProgressBackgroundColor='Black'
        }
    } catch { }
    try {
        $style=Get-Variable -Name PSStyle -ErrorAction SilentlyContinue
        if ($style -and $style.Value.Progress) {
            $theme.Style=$style.Value.Progress
            $theme.View=$theme.Style.View
            $theme.Style.View='Classic'
        }
    } catch { }
    return $theme
}

function Restore-ProgressTheme($Theme) {
    if (-not $Theme) { return }
    try {
        if ($null -ne $Theme.Foreground) { $Theme.HostData.ProgressForegroundColor=$Theme.Foreground }
        if ($null -ne $Theme.Background) { $Theme.HostData.ProgressBackgroundColor=$Theme.Background }
        if ($null -ne $Theme.View) { $Theme.Style.View=$Theme.View }
    } catch { }
}

function Wait-DiskService {
    $view=New-CommandView '等待磁盘服务释放'
    $theme=Set-ProgressTheme
    try {
        while ($view.Clock.Elapsed.TotalSeconds -lt 15) {
            $view.Percent=[math]::Min(99.9,$view.Clock.Elapsed.TotalSeconds/15*100)
            Show-CommandProgress $view
            Start-Sleep -Milliseconds 125
        }
        Show-CommandProgress $view 'done'
    } finally { Close-ProgressLine $view; Restore-ProgressTheme $theme }
}


function Clear-NativeProgress($View) {
    if ($View.NativeOpen) {
        $ProgressPreference='Continue'
        Write-Progress -Id 8301 -ParentId 8300 -Activity '当前文件' -Completed
        Write-Progress -Id 8300 -Activity $View.Title -Completed
        $View.NativeOpen=$false
    }
}

function Show-CommandProgress($View, [string]$Result = '', [int]$AvailableColumns = 0) {
    if (-not $View.Interactive -or $View.NativeFailed -or $AvailableColumns -gt 0) {
        Show-ConsoleProgressFallback $View $Result $AvailableColumns
        return
    }
    if ($Result) {
        Clear-NativeProgress $View
        Show-ConsoleProgressFallback $View $Result $AvailableColumns
        return
    }
    $now=$View.Clock.ElapsedMilliseconds
    if ($now-$View.LastDraw -lt 250) { return }
    $View.LastDraw=$now
    $elapsed=$View.Clock.Elapsed.TotalSeconds
    $duration='{0:00}:{1:00}' -f [int][math]::Floor($elapsed/60), ([int][math]::Floor($elapsed)%60)
    $remaining=-1
    $percent=$View.Percent
    $operation='正在执行，请稍候…'
    if ($View.FileSizes.Count -gt 0) {
        $bytes=$View.CompletedBytes
        if ($View.CurrentPath -and -not $View.CompletedFiles.ContainsKey($View.CurrentPath)) {
            $bytes += $View.CurrentSize * [math]::Max(0.0,$View.CurrentPercent)/100
        }
        $percent=0.0
        if ($View.TotalBytes -gt 0) { $percent=[math]::Min(99.9,100*$bytes/$View.TotalBytes) }
        $operation='已处理约 {0:0.00} / {1:0.00} GiB' -f ($bytes/1GB),($View.TotalBytes/1GB)
        if ($elapsed -ge 3 -and $bytes -gt 0 -and $percent -lt 99.9) {
            $remaining=[int][math]::Min(2147483646.0,[math]::Ceiling([math]::Max(0.0,$View.TotalBytes-$bytes)/($bytes/$elapsed)))
        }
        $status='整体约 {0,5:0.0}%  |  用时 {1}' -f $percent,$duration
    } elseif ($percent -ge 0) { $status='{0,5:0.0}%  |  用时 {1}' -f $percent,$duration }
    else { $status="执行中  |  用时 $duration" }
    if ($View.Title -eq '等待磁盘服务释放') {
        $remaining=[int][math]::Max(0.0,[math]::Ceiling(15-$elapsed))
        $status="剩余 $remaining 秒  |  用时 $duration"
        $operation='等待系统释放磁盘服务，随后继续分配盘符。'
    }
    try {
        $ProgressPreference='Continue'
        Write-Progress -Id 8300 -Activity $View.Title -Status $status -CurrentOperation $operation -PercentComplete ([int][math]::Floor($percent)) -SecondsRemaining $remaining
        $View.NativeOpen=$true
        if ($View.FileSizes.Count -gt 0 -and $View.CurrentPath) {
            $filePercent=[math]::Min(100.0,[math]::Max(0.0,$View.CurrentPercent))
            $name=[IO.Path]::GetFileName($View.CurrentPath)
            Write-Progress -Id 8301 -ParentId 8300 -Activity '当前文件' -Status ('{0,5:0.0}%  |  {1}' -f $filePercent,$name) -PercentComplete ([int][math]::Floor($filePercent))
        }
    } catch {
        $View.NativeFailed=$true
        try { Clear-NativeProgress $View } catch { }
        $View.LastDraw=-10000L
        Show-ConsoleProgressFallback $View $Result $AvailableColumns
    }
}


function Measure-ConsoleText([string]$Text) {
    $width = 0
    for ($index = 0; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        $category = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($Text, $index)
        if ($category -in @([Globalization.UnicodeCategory]::NonSpacingMark, [Globalization.UnicodeCategory]::EnclosingMark, [Globalization.UnicodeCategory]::Control)) { continue }
        $code = [char]::ConvertToUtf32($Text, $index)
        if ([char]::IsHighSurrogate($character)) { $index++ }
        if (($code -ge 0x1100 -and $code -le 0x115F) -or ($code -ge 0x2E80 -and $code -le 0xA4CF) -or
            ($code -ge 0xAC00 -and $code -le 0xD7A3) -or ($code -ge 0xF900 -and $code -le 0xFAFF) -or
            ($code -ge 0xFE10 -and $code -le 0xFE6F) -or ($code -ge 0xFF01 -and $code -le 0xFF60) -or
            ($code -ge 0xFFE0 -and $code -le 0xFFE6) -or $code -ge 0x1F300) { $width += 2 }
        else { $width++ }
    }
    return $width
}

function Fit-ConsoleText([string]$Text, [int]$Columns) {
    if ($Columns -le 0) { return '' }
    if ((Measure-ConsoleText $Text) -le $Columns) { return $Text }
    while ($Text.Length -gt 0 -and (Measure-ConsoleText $Text) -gt $Columns - 1) {
        $remove = 1
        if ([char]::IsLowSurrogate($Text[$Text.Length - 1])) { $remove = 2 }
        $Text = $Text.Substring(0, $Text.Length - $remove)
    }
    return $Text + '~'
}

function Get-OutputPercent([string]$Line) {
    # Native tools may put many carriage-return updates into one PowerShell line.
    $result = $null
    foreach ($part in ($Line -split "[`r`n]")) {
        $clean = $part.Replace([string][char]8, '')
        if ($clean -match '(?<![\d.,])(?<percent>\d{1,3}(?:[.,]\d+)?)\s*%[\[\]=\s]*$' -or
            $clean -match '^\s*(?<percent>\d{1,3}(?:[.,]\d+)?)\s*百分比(?:已完成)?\s*$') {
            $percent = [double]::Parse($Matches.percent.Replace(',', '.'), [Globalization.CultureInfo]::InvariantCulture)
            if ($percent -ge 0 -and $percent -le 100) { $result = $percent }
        }
    }
    return $result
}

function ConvertTo-NativeArgument([string]$Argument) {
    # Windows quoted arguments must double backslashes before a quote or the closing quote.
    $escaped = [regex]::Replace($Argument, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function New-CommandView([string]$Title, [object[]]$CopyFiles = @()) {
    $sizes = @{}
    $total = 0.0
    foreach ($file in $CopyFiles) { $sizes[$file.FullName] = [double]$file.Length; $total += $file.Length }
    $interactive = $false
    try { $interactive = $Host.Name -eq 'ConsoleHost' -and -not [Console]::IsOutputRedirected } catch { }
    return [pscustomobject]@{
        NativeOpen=$false; NativeFailed=$false; Title=$Title; Clock=[Diagnostics.Stopwatch]::StartNew(); Percent=-1.0;
        LastDraw=-10000L; Width=0; LineOpen=$false; Interactive=$interactive;
        FileSizes=$sizes; TotalBytes=$total; CompletedBytes=0.0; CompletedFiles=@{};
        CurrentPath=''; CurrentSize=0.0; CurrentPercent=-1.0; CurrentFailed=$false
    }
}

function Update-CommandView($View, [string]$Program, [string]$Line) {
    if ($Program -eq 'robocopy.exe' -and $Line -match '^\s*\d+\s+(?<path>(?:[A-Za-z]:\\|\\\\).+?)\s*$') {
        $path = $Matches.path
        if ($View.FileSizes.ContainsKey($path)) {
            if ($View.CurrentPath -and $View.CurrentPath -ne $path -and -not $View.CurrentFailed -and
                -not $View.CompletedFiles.ContainsKey($View.CurrentPath)) {
                $View.CompletedBytes += $View.CurrentSize
                $View.CompletedFiles[$View.CurrentPath] = $true
            }
            $View.CurrentPath = $path
            $View.CurrentSize = $View.FileSizes[$path]
            $View.CurrentPercent = 0.0
            $View.CurrentFailed = $false
            if ($View.CurrentSize -eq 0) { $View.CurrentPercent = 100.0 }
            return
        }
    }
    $percent = Get-OutputPercent $Line
    if ($null -ne $percent) {
        if ($Program -eq 'robocopy.exe') {
            $View.CurrentPercent = $percent
            if ($percent -ge 100 -and $View.CurrentPath -and -not $View.CompletedFiles.ContainsKey($View.CurrentPath)) {
                $View.CompletedBytes += $View.CurrentSize
                $View.CompletedFiles[$View.CurrentPath] = $true
            }
        } else { $View.Percent = $percent }
    } elseif ($Program -eq 'robocopy.exe' -and $Line -match '(?i)\b(?:ERROR|FAILED)\b|错误|失敗|失败') {
        $View.CurrentFailed = $true
    }
}

function Show-ConsoleProgressFallback($View, [string]$Result = '', [int]$AvailableColumns = 0) {
    $now = $View.Clock.ElapsedMilliseconds
    $interval = 150
    if (-not $View.Interactive) { $interval = 5000 }
    if (-not $Result -and $now - $View.LastDraw -lt $interval) { return }
    $View.LastDraw = $now
    $seconds = [long][math]::Floor($View.Clock.Elapsed.TotalSeconds)
    $duration = '{0:00}:{1:00}' -f [long][math]::Floor($seconds / 60), ($seconds % 60)
    $percent = $View.Percent
    $copy = $View.FileSizes.Count -gt 0
    if ($copy) {
        $currentBytes = 0.0
        if ($View.CurrentPath -and -not $View.CompletedFiles.ContainsKey($View.CurrentPath)) {
            $currentBytes = $View.CurrentSize * [math]::Max(0.0, $View.CurrentPercent) / 100
        }
        $percent = 0.0
        if ($View.TotalBytes -gt 0) { $percent = [math]::Min(99.9, 100 * ($View.CompletedBytes + $currentBytes) / $View.TotalBytes) }
    }
    $color = 'Cyan'
    $status = '执行中'
    if ($percent -ge 0) {
        $status = $percent.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture) + '%'
        if ($copy) { $status = '整体约 ' + $status }
    }
    if ($Result -eq 'done') { $percent = 100.0; $status = '100.0% 已完成'; $color = 'Green' }
    if ($Result -eq 'error') { $status = '失败'; $color = 'Red' }
    $columns = 80
    if ($AvailableColumns -ge 10) { $columns = $AvailableColumns }
    else {
        try {
            if ([Console]::WindowWidth -ge 10) { $columns = [Console]::WindowWidth }
            if ([Console]::BufferWidth -ge 10) { $columns = [math]::Min($columns, [Console]::BufferWidth) }
        } catch { }
    }
    $barLength = 16
    if ($columns -lt 90) { $barLength = 10 }
    if ($columns -lt 65) { $barLength = 6 }
    if ($percent -ge 0) {
        $filled = [int][math]::Floor($barLength * $percent / 100)
        $bar = '[' + ('=' * $filled) + (' ' * ($barLength - $filled)) + ']'
    } else {
        $position = [int][math]::Floor($now / 250) % $barLength
        $bar = '[' + ('-' * $position) + '>' + ('-' * ($barLength - $position - 1)) + ']'
    }
    $text = "$($View.Title) $bar $status 用时 $duration"
    if ((Measure-ConsoleText $text) -ge $columns) { $text = "$($View.Title) $status $duration" }
    if ((Measure-ConsoleText $text) -ge $columns) { $text = "$status $duration" }
    if ($copy -and -not $Result -and $View.CurrentPath) {
        $current = $View.CurrentPercent.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture) + '%'
        $detail = " | 当前 $current"
        if ((Measure-ConsoleText ($text + $detail)) -lt $columns) { $text += $detail }
        $name = [IO.Path]::GetFileName($View.CurrentPath)
        $remaining = $columns - 2 - (Measure-ConsoleText $text)
        if ($remaining -ge 8) { $text += ' ' + (Fit-ConsoleText $name $remaining) }
    }
    $text = Fit-ConsoleText $text ($columns - 1)
    $width = Measure-ConsoleText $text
    if ($View.Interactive) {
        $padding = [math]::Max(0, [math]::Min($View.Width, $columns - 1) - $width)
        Write-Host -NoNewline ("`r" + $text + (' ' * $padding)) -ForegroundColor $color
        $View.Width = $width
        $View.LineOpen = $true
    } else { Write-Host $text -ForegroundColor $color }
}

function Close-ProgressLine($View) {
    Clear-NativeProgress $View
    if ($View.LineOpen) { Write-Host ''; $View.LineOpen = $false; $View.Width = 0 }
}

function Run-Command([string]$Program, [string[]]$Arguments, [string]$Title = '', [object[]]$CopyFiles = @(), $StandardInput = $null) {
    $programPath = if ([IO.Path]::IsPathRooted($Program)) { [IO.Path]::GetFullPath($Program) } else { Join-Path "$env:SystemRoot\System32" $Program }
    $Program = [IO.Path]::GetFileName($programPath)
    if (-not $Title) {
        $Title = $Program
        switch ($Program) {
            'diskpart.exe' { $Title = '格式化 FAT32' }
            'dism.exe' { $Title = '拆分 install.wim' }
            'robocopy.exe' { $Title = '复制文件' }
        }
    }
    $outputFile = Join-Path $script:workRoot ($Program + '-' + [guid]::NewGuid().ToString('N') + '.log')
    $commandText = "$Program " + (($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    Write-Host "`n> ${Program}：$Title" -ForegroundColor DarkCyan
    Write-Host ("日志：" + [IO.Path]::GetFileName($outputFile)) -ForegroundColor DarkGray
    $view = New-CommandView $Title $CopyFiles
    $writer = New-Object IO.StreamWriter($outputFile, $false, (New-Object Text.UTF8Encoding($true)))
    $writer.AutoFlush = $false
    $lastLogFlush = 0L
    $writer.WriteLine('> ' + $commandText)
    $writer.Flush()
    $process = New-Object Diagnostics.Process
    $process.StartInfo.FileName = $programPath
    $process.StartInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' '
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.WorkingDirectory = "$env:SystemRoot\System32"
    $process.StartInfo.RedirectStandardInput = $null -ne $StandardInput
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $codePage = [Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage
    try { $codePage = [int](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage' -Name OEMCP -ErrorAction Stop).OEMCP } catch { }
    $encoding = [Text.Encoding]::GetEncoding($codePage)
    $process.StartInfo.StandardOutputEncoding = $encoding
    $process.StartInfo.StandardErrorEncoding = $encoding
    $theme=Set-ProgressTheme
    try {
        if ($global:GuiEngineState -and $global:GuiEngineState['ForceStopRequested']) { throw '用户已请求结束制作。' }
        $null = $process.Start()
        if ($global:GuiEngineState) { $global:GuiEngineState['ActiveProcess'] = $process }
        if ($global:GuiEngineState -and $global:GuiEngineState['ForceStopRequested']) { throw '用户已请求结束制作。' }
        if ($null -ne $StandardInput) {
            $process.StandardInput.WriteLine([string]$StandardInput)
            $process.StandardInput.Close()
        }
        # ReadLineAsync splits both CR and LF, without waiting for the command to finish.
        # Read both streams concurrently so a full stderr pipe cannot block the command.
        $reads = @(
            [pscustomobject]@{Reader=$process.StandardOutput; Pending=$process.StandardOutput.ReadLineAsync(); Done=$false; ErrorStream=$false},
            [pscustomobject]@{Reader=$process.StandardError; Pending=$process.StandardError.ReadLineAsync(); Done=$false; ErrorStream=$true}
        )
        Show-CommandProgress $view
        while ($true) {
            foreach ($read in $reads) {
                $drained = 0
                while (-not $read.Done -and $read.Pending.IsCompleted -and $drained -lt 200) {
                    $line = $read.Pending.GetAwaiter().GetResult()
                    if ($null -eq $line) { $read.Done = $true; break }
                    $writer.WriteLine($line)
                    Update-CommandView $view $Program $line
                    if ($line.Trim() -and $read.ErrorStream) {
                        Close-ProgressLine $view
                        Write-Host $line
                    }
                    $read.Pending = $read.Reader.ReadLineAsync()
                    $drained++
                }
            }
            if ($view.Clock.ElapsedMilliseconds-$lastLogFlush -ge 1000) {
                $writer.Flush()
                $lastLogFlush=$view.Clock.ElapsedMilliseconds
            }
            Show-CommandProgress $view
            if ($process.HasExited -and $reads[0].Done -and $reads[1].Done) { break }
            Start-Sleep -Milliseconds 75
        }
        $process.WaitForExit()
        $code = $process.ExitCode
        $writer.WriteLine("ExitCode: $code")
        $writer.Flush()
        $failed = ($Program -eq 'robocopy.exe' -and $code -ge 8) -or ($Program -ne 'robocopy.exe' -and $code -ne 0)
        if ($failed) {
            Show-CommandProgress $view 'error'
            Close-ProgressLine $view
            Get-Content -LiteralPath $outputFile -Encoding UTF8 -Tail 25 | ForEach-Object {
                if ($null -eq (Get-OutputPercent ([string]$_))) { Write-Host $_ -ForegroundColor Red }
            }
            throw "$Program 失败，退出码：$code。停止后续步骤。命令输出：$outputFile"
        }
        Show-CommandProgress $view 'done'
        Close-ProgressLine $view
    } finally {
        Close-ProgressLine $view
        Restore-ProgressTheme $theme
        $writer.Dispose()
        # Do not leave the command running if the user stops the script or the reader fails.
        try { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() } } catch { }
        if ($global:GuiEngineState) { $global:GuiEngineState['ActiveProcess'] = $null }
        $process.Dispose()
    }
}

function Run-DiskPart([string[]]$Commands) {
    $null = Get-Target
    $file = Join-Path $script:workRoot ('diskpart-' + [guid]::NewGuid().ToString('N') + '.txt')
    @("select disk $script:diskNumber") + $Commands + @('exit') | Set-Content -LiteralPath $file -Encoding ASCII
    Write-Host '正在格式化已确认的目标分区，详情写入日志。' -ForegroundColor DarkGray
    Run-Command 'diskpart.exe' @('/s', $file)
    Wait-DiskService
    Update-HostStorageCache
}

function Get-InstallImagePlan([string]$Source, [object[]]$Files) {
    $fat32Limit = [long]4294967295
    $wimPath = Join-Path $Source 'sources\install.wim'
    $esdPath = Join-Path $Source 'sources\install.esd'
    $swmPath = Join-Path $Source 'sources\install.swm'
    $wimFile = $Files | Where-Object FullName -eq $wimPath | Select-Object -First 1
    $esdFile = $Files | Where-Object FullName -eq $esdPath | Select-Object -First 1
    $swmFile = $Files | Where-Object FullName -eq $swmPath | Select-Object -First 1
    if (-not $wimFile -and -not $esdFile -and -not $swmFile) { throw 'ISO 缺少安装映像。' }
    if ($wimFile -and $swmFile) { throw 'ISO 同时包含 WIM 和 SWM，请使用原版 ISO。' }
    if ($esdFile -and ($wimFile -or $swmFile)) { throw 'ISO 同时包含多种安装映像，无法唯一确定安装来源，尚未清盘。' }
    if ($wimFile -and $wimFile.Length -le 0) { throw 'install.wim 为空，已停止，尚未格式化 U 盘。' }
    $split = $null -ne $wimFile -and $wimFile.Length -gt $fat32Limit
    foreach ($file in $Files) {
        if ($file.Length -gt $fat32Limit -and -not ($split -and $file.FullName -eq $wimPath)) {
            throw "$($file.Name) 超过 FAT32 单文件限制。"
        }
    }
    if ($wimFile) { $images = @($wimFile) }
    elseif ($esdFile) { $images = @($esdFile) }
    else {
        $swmDirectory = [IO.Path]::GetDirectoryName($swmPath)
        $images = @($Files | Where-Object {
            [IO.Path]::GetDirectoryName($_.FullName) -eq $swmDirectory -and $_.Name -like 'install*.swm'
        } | Sort-Object Name)
        $numbers = @($images | ForEach-Object {
            if ($_.Name -ieq 'install.swm') { 1 }
            elseif ($_.Name -match '^install([2-9]|[1-9][0-9]+)\.swm$') { [int]$Matches[1] }
            else { throw 'SWM 分卷文件名无效，尚未清盘。' }
        } | Sort-Object)
        for ($part = 0; $part -lt $numbers.Count; $part++) {
            if ($numbers[$part] -ne $part + 1) { throw 'SWM 分卷不连续或不完整，尚未清盘。' }
        }
    }
    if (@($images | Where-Object Length -le 0).Count) { throw '安装映像为空，尚未清盘。' }
    return [pscustomobject]@{
        HasWim = $null -ne $wimFile
        WimPath = $wimPath
        RequiresSplit = [bool]$split
        Fat32Limit = $fat32Limit
        ImageFiles = $images
    }
}

function Confirm-InstallImageStep($Plan) {
    $title = if ($Plan.HasWim) { '检查 install.wim 大小并按需拆分' } else { '检查安装映像大小' }
    Write-Host "`n[3/6] $title" -ForegroundColor Cyan
    foreach ($image in $Plan.ImageFiles) {
        Write-Host "  文件：$($image.FullName)"
        Write-Host ('  大小：{0:N2} GiB（{1:N0} 字节）' -f ($image.Length / 1GB), $image.Length)
    }
    Write-Host ('  FAT32 单文件上限：4 GiB（{0:N0} 字节）' -f $Plan.Fat32Limit)
    Write-Host '本流程固定按 4 GiB 阈值处理 install.wim；选择 NTFS 时也保持相同拆分规则。' -ForegroundColor DarkGray
    if ($Plan.RequiresSplit) {
        Write-Host 'install.wim 超过上限，需要在本机拆分为 SWM 后再复制。' -ForegroundColor Yellow
        $prompt = '按回车执行本机拆分；输入 Q 退出'
    } else {
        Write-Host '安装映像未超过上限，无需拆分，将直接复制并校验原文件。' -ForegroundColor Green
        $prompt = '按回车跳过拆分并开始复制；输入 Q 退出'
    }
    while ($true) {
        $answer = (Read-Host $prompt).Trim()
        if ($answer -eq '') { return }
        if ($answer -ieq 'Q') { throw '已退出，不再执行后续步骤。' }
        Write-Host '请按回车，或输入 Q。' -ForegroundColor Yellow
    }
}


function Get-Fat32ToolPayload {
    # Ridgecrop fat32format 1.07, GPL; corresponding source and license included.
    return '
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
'
}

function Assert-WholeUsbIdentity {
    $disk = Get-Target
    $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$($disk.Number)" -ErrorAction Stop)
    if ($devices.Count -ne 1 -or -not $script:targetPnpId -or $devices[0].PNPDeviceID -cne $script:targetPnpId) {
        throw '选中 U 盘的设备身份变化，已停止。'
    }
    return $disk
}

function Assert-SelectedUsbSnapshot($Disk, [string]$PnpId) {
    $snapshot = $global:GuiDiskSnapshot
    if (-not $snapshot) {
        if ($GuiMode) { throw '缺少界面选择时的磁盘身份快照，已停止。' }
        return
    }
    if ($Disk.Number -ne $snapshot.Number -or $Disk.Size -ne $snapshot.Size -or
        $Disk.FriendlyName -cne $snapshot.FriendlyName -or $Disk.Path -cne $snapshot.Path -or
        $Disk.SerialNumber -cne $snapshot.SerialNumber -or -not $snapshot.PnpId -or $PnpId -cne $snapshot.PnpId) {
        throw 'U 盘与界面选择时的设备不一致。请刷新设备、重新核对后再制作，尚未清盘。'
    }
}

function Assert-SourceMediaIdentity {
    $snapshot = $script:sourceMedia
    if (-not $snapshot -or -not $snapshot.ImagePath) { throw '无法定位原来的 ISO 文件，已停止。' }
    $current = Get-SourceMedia $snapshot.Letter
    if (-not $current -or $current.UniqueId -cne $snapshot.UniqueId -or $current.Label -cne $snapshot.Label -or
        $current.Size -ne $snapshot.Size -or $current.ImagePath -cne $snapshot.ImagePath -or $current.DevicePath -cne $snapshot.DevicePath) {
        throw '源 ISO 的盘符或映像身份已经变化，已停止。'
    }
}

function Get-WholeUsbPartitions {
    $disk = Assert-WholeUsbIdentity
    if ($disk.NumberOfPartitions -eq 0) { return }
    return @(Get-Partition -DiskNumber $disk.Number -ErrorAction Stop | Sort-Object PartitionNumber)
}

function Get-WholeUsbLayoutSignature([object[]]$Partitions) {
    return ConvertTo-Json -InputObject @($Partitions | Sort-Object PartitionNumber | ForEach-Object {
        [pscustomobject]@{Number=$_.PartitionNumber; Offset=$_.Offset; Size=$_.Size; Letter=[string]$_.DriveLetter; MbrType=[string]$_.MbrType; GptType=[string]$_.GptType; AccessPaths=@($_.AccessPaths)}
    }) -Depth 4 -Compress
}

function Select-WholeUsbTarget([string]$Choice = '', [string]$SourceLetter = '') {
    $usbDisks = @(Get-Disk | Where-Object {
        $_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem -and
        -not $_.IsOffline -and -not $_.IsReadOnly -and $_.Size -ge 8GB
    })
    $usbDisks | Select-Object Number,FriendlyName,@{Name='GB';Expression={[math]::Round($_.Size/1GB,1)}} | Format-Table -AutoSize | Out-Host
    if (-not $usbDisks.Count) { throw '未找到可操作的 USB 磁盘。' }
    if (-not $Choice) { $Choice = Read-Host '选择整支 U 盘：输入上表磁盘编号，或该 U 盘的一个盘符' }
    $Choice = $Choice.Trim().TrimEnd(':','\').ToUpperInvariant()
    $chosenPartition = $null
    if ($Choice -match '^\d+$') { $script:diskNumber = [int]$Choice }
    elseif ($Choice -match '^[A-Z]$') {
        if ($Choice -eq $SourceLetter) { throw '不能选择 ISO / DVD 盘符作为目标。' }
        $byLetter = @(Get-Partition -DriveLetter $Choice -ErrorAction Stop)
        if ($byLetter.Count -ne 1) { throw '该盘符未对应唯一分区，请用磁盘编号选择。' }
        $chosenPartition = $byLetter[0]
        $script:diskNumber = [int]$chosenPartition.DiskNumber
    } else { throw '请输入一个盘符字母或列表中的磁盘编号。' }
    if ($script:diskNumber -notin @($usbDisks.Number)) { throw '选择的不是列表中的可操作 USB 磁盘，已停止。' }
    $script:target = $null
    $script:targetPnpId = $null
    $script:target = Get-Target
    $devices = @(Get-CimInstance Win32_DiskDrive -Filter "Index=$script:diskNumber" -ErrorAction Stop)
    if ($devices.Count -ne 1 -or -not $devices[0].PNPDeviceID) { throw '无法唯一识别选中的 USB 设备，尚未清盘。' }
    $script:targetPnpId = [string]$devices[0].PNPDeviceID
    Assert-SelectedUsbSnapshot $script:target $script:targetPnpId
    $disk = Assert-WholeUsbIdentity
    if ($disk.Size -gt 2TB -or $disk.LogicalSectorSize -notin @(512,4096)) {
        throw '当前全容量 FAT32 流程支持不超过 2 TiB、逻辑扇区为 512 或 4096 字节的 USB 磁盘，尚未清盘。'
    }
    $partitions = @(Get-WholeUsbPartitions)
    $letter = if ($chosenPartition -and $chosenPartition.DriveLetter) { [string]$chosenPartition.DriveLetter } else { '' }
    if (-not $letter) {
        $ordinary = @($partitions | Where-Object {
            $_.DriveLetter -and (Test-UsbDataPartition $_ ([string]$disk.PartitionStyle))
        } | Sort-Object Size -Descending)
        if ($ordinary.Count) { $letter = [string]$ordinary[0].DriveLetter }
    }
    if (-not $letter) { $letter = Get-FreeLetter $SourceLetter }
    if ($letter -eq $SourceLetter) { throw 'U 盘盘符不能与 ISO 相同。' }
    Write-Host "选中的整支 U 盘：磁盘 $($disk.Number)  |  $($disk.FriendlyName)  |  $([math]::Round($disk.Size/1GB,1)) GB" -ForegroundColor Cyan
    if ($partitions.Count) {
        $partitions | Select-Object PartitionNumber,DriveLetter,Type,@{Name='GB';Expression={[math]::Round($_.Size/1GB,2)}} | Format-Table -AutoSize | Out-Host
    } else { Write-Host '当前没有分区。' }
    return [pscustomobject]@{Letter=$letter; Partitions=$partitions; Layout=(Get-WholeUsbLayoutSignature $partitions); SizeMB=[long][math]::Floor(($disk.Size-2MB)/1MB)}
}

function Assert-PathOutsideTarget([string]$Path, [string]$Purpose) {
    if (-not $Path -or -not [IO.Path]::IsPathRooted($Path)) { throw "无法确定${Purpose}所在磁盘，尚未清盘。" }
    $volumes = @(Get-Volume -FilePath $Path -ErrorAction Stop)
    if ($volumes.Count -ne 1) { throw "无法唯一确定${Purpose}所在卷，尚未清盘。" }
    $partitions = @($volumes[0] | Get-Partition -ErrorAction Stop)
    if ($partitions.Count -ne 1) { throw "无法唯一确定${Purpose}所在磁盘，尚未清盘。" }
    if ($partitions[0].DiskNumber -eq $script:diskNumber) {
        throw "${Purpose}位于即将清空的 U 盘上。请移到电脑的其他磁盘后再运行，尚未清盘。"
    }
}

function Assert-WholeUsbDependencies([switch]$RequireIso) {
    $null = Assert-WholeUsbIdentity
    if ($RequireIso) {
        if (-not $script:sourceMedia -or -not $script:sourceMedia.ImagePath) {
            throw '无法定位挂载 ISO 的原文件，不能核验来源安全，尚未清盘。请用资源管理器挂载保存在其他磁盘上的 ISO。'
        }
        Assert-PathOutsideTarget $script:sourceMedia.ImagePath '源 ISO 文件'
        Assert-SourceMediaIdentity
    }
    Assert-PathOutsideTarget $script:launchPath '脚本文件'
    Assert-PathOutsideTarget $env:LOCALAPPDATA '本机临时工作目录'
    Assert-PathOutsideTarget $env:TEMP 'TEMP 临时目录'
    Assert-PathOutsideTarget $env:TMP 'TMP 临时目录'
    if ((Get-Location).Provider.Name -eq 'FileSystem') { Assert-PathOutsideTarget (Get-Location).ProviderPath '当前 PowerShell 工作目录' }
    if ($script:workRoot) { Assert-PathOutsideTarget $script:workRoot '本次工作目录' }
}

function Read-UsbFileSystemChoice {
    Write-Host "`n选择 U 盘文件系统" -ForegroundColor Cyan
    Write-Host '  1  FAT32（推荐，UEFI 兼容性更广；install.wim 超过 4 GiB 时拆分）'
    Write-Host '  2  NTFS（部分 UEFI 固件不能直接从 NTFS 启动；install.wim 仍按相同 4 GiB 规则拆分）'
    Write-Host '  3  取消'
    while ($true) {
        $answer = (Read-Host '请输入编号（回车默认取消）').Trim()
        if ($answer -eq '1') { return 'FAT32' }
        if ($answer -eq '2') { return 'NTFS' }
        if ($answer -eq '' -or $answer -eq '3' -or $answer -ieq 'Q') { return $null }
        Write-Host '请输入 1、2 或 3；回车取消。' -ForegroundColor Yellow
    }
}

function Confirm-WholeUsbFormat($Plan, [string]$FileSystem) {
    $disk = Assert-WholeUsbIdentity
    Write-Host "`n整盘格式化确认" -ForegroundColor Yellow
    Write-Host "  磁盘 $($disk.Number)  |  $($disk.FriendlyName)  |  $([math]::Round($disk.Size/1GB,1)) GB"
    Write-Host "  完成后只保留一个盘符：$($Plan.Letter):\  |  卷标：$($script:imageIdentity.VolumeLabel)"
    Write-Host "  安装系统：$($script:imageIdentity.DisplayName)"
    Write-Host "  删除这支 U 盘的全部 $($Plan.Partitions.Count) 个旧分区，包括微PE、EFI和隐藏分区。" -ForegroundColor Yellow
    Write-Host "  整支 U 盘的全部文件会被删除；重建为一个占满可用容量的 $FileSystem 主分区。" -ForegroundColor Yellow
    if ($FileSystem -eq 'NTFS') { Write-Host '  提醒：部分 UEFI 固件不支持直接从 NTFS 启动。' -ForegroundColor Yellow }
    Write-Host "  1  确认已备份，整盘格式化为 $FileSystem"
    Write-Host '  2  取消并退出'
    while ($true) {
        $answer = (Read-Host '请输入编号（回车默认取消）').Trim()
        if ($answer -eq '1') { return $true }
        if ($answer -eq '' -or $answer -eq '2' -or $answer -ieq 'Q') { return $false }
        Write-Host '请输入 1 或 2；回车取消。' -ForegroundColor Yellow
    }
}

function Assert-Fat32Tool([string]$Path) {
    $expected = 'D5320A127374AF23139730F0D01AEE8195E5FE15B63C35D48D80930ABBF7F5CB'
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash -cne $expected) {
        throw '内置 FAT32 格式化工具校验失败，已停止。'
    }
}

function Test-Fat32ToolRuntime([string]$Path) {
    # The /? branch only prints usage and exits; it never opens a drive.
    Assert-Fat32Tool $Path
    $process = New-Object Diagnostics.Process
    $process.StartInfo.FileName = $Path
    $process.StartInfo.Arguments = '/?'
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.WorkingDirectory = "$env:SystemRoot\System32"
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    try {
        $null = $process.Start()
        $output = $process.StandardOutput.ReadToEndAsync()
        $errorOutput = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) { throw '内置 FAT32 工具启动检查超时，尚未清盘。' }
        $process.WaitForExit()
        $help = $output.GetAwaiter().GetResult() + $errorOutput.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 1 -or $help -notmatch 'Version 1\.07' -or $help -notmatch 'Usage Fat32Format') {
            throw '内置 FAT32 工具无法正常启动，尚未清盘。'
        }
    } finally {
        try { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() } } catch { }
        $process.Dispose()
    }
}

function Prepare-Fat32Tool {
    # The payload carries the original binary, its complete corresponding source,
    # GPL text and origin notice. There is no runtime network download.
    Add-Type -AssemblyName System.IO.Compression
    $bytes = [Convert]::FromBase64String((Get-Fat32ToolPayload))
    $memory = New-Object IO.MemoryStream(,$bytes)
    $archive = New-Object IO.Compression.ZipArchive($memory,[IO.Compression.ZipArchiveMode]::Read)
    $directory = Join-Path $script:workRoot 'fat32-tool'
    $null = New-Item -ItemType Directory -Path $directory -ErrorAction Stop
    $allowed = @('fat32format.exe','fat32formatsrc-vendor.zip','COPYING.GPL-2.0.txt','NOTICE.txt')
    try {
        if ($archive.Entries.Count -ne $allowed.Count) { throw '内置工具包结构无效，尚未清盘。' }
        foreach ($name in $allowed) {
            $entry = $archive.GetEntry($name)
            if (-not $entry -or $entry.Length -gt 2MB) { throw '内置工具包条目无效，尚未清盘。' }
            $input = $entry.Open()
            $output = [IO.File]::Open((Join-Path $directory $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
            try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
        }
    } finally { $archive.Dispose(); $memory.Dispose() }
    $path = Join-Path $directory 'fat32format.exe'
    Assert-Fat32Tool $path
    if ((Get-FileHash -LiteralPath (Join-Path $directory 'fat32formatsrc-vendor.zip') -Algorithm SHA256).Hash -cne 'D1F350F277D14C92D7A0984D9566DD8BAA2592E6B37C63CA657217F251116989') {
        throw '内置格式化工具的配套源码校验失败，尚未清盘。'
    }
    Test-Fat32ToolRuntime $path
    return $path
}

function Write-WholeUsbDiagnostic([string]$Stage, $ErrorRecord = $null) {
    if (-not $script:workRoot) { return }
    $path = Join-Path $script:workRoot 'whole-disk-errors.log'
    $lines = New-Object 'Collections.Generic.List[string]'
    $lines.Add('===== ' + (Get-Date -Format s) + ' | ' + $Stage + ' =====')
    if ($ErrorRecord) {
        $lines.Add(($ErrorRecord | Format-List * -Force | Out-String -Width 220))
        if ($ErrorRecord.Exception) {
            $lines.Add(($ErrorRecord.Exception | Format-List * -Force | Out-String -Width 220))
            $dataProperty = $ErrorRecord.Exception.PSObject.Properties['ErrorData']
            if ($dataProperty -and $dataProperty.Value) {
                $lines.Add(($dataProperty.Value | Format-List * -Force | Out-String -Width 220))
                $properties = $dataProperty.Value.PSObject.Properties['CimInstanceProperties']
                if ($properties) { $lines.Add(($properties.Value | Select-Object Name,Value,CimType | Format-List | Out-String -Width 220)) }
            }
        }
    }
    try {
        $disk = Assert-WholeUsbIdentity
        $lines.Add(($disk | Select-Object Number,FriendlyName,Size,PartitionStyle,NumberOfPartitions,IsBoot,IsSystem,IsReadOnly,IsOffline,Path,SerialNumber | Format-List | Out-String -Width 220))
        $lines.Add((Get-WholeUsbLayoutSignature @(Get-WholeUsbPartitions)))
    } catch { $lines.Add('当前目标状态无法安全核验：' + $_.Exception.Message) }
    try {
        [IO.File]::AppendAllText($path, ($lines -join "`r`n") + "`r`n", (New-Object Text.UTF8Encoding($true)))
        Write-Host "诊断日志：$path" -ForegroundColor DarkGray
    } catch { }
}

function Clear-ConfirmedWholeUsb([switch]$RequireIso) {
    $disk = Assert-WholeUsbIdentity
    $partitions = @(Get-WholeUsbPartitions)
    if ($partitions.Count -eq 0 -and $disk.PartitionStyle -in @('RAW','MBR')) {
        Write-Host "U 盘分区表已清空（$($disk.PartitionStyle)，0 个分区），继续重建。" -ForegroundColor DarkCyan
        return
    }
    $primaryError = $null
    try {
        Clear-Disk -InputObject $disk -RemoveData -RemoveOEM -Confirm:$false -ErrorAction Stop
        Wait-DiskService
        Update-HostStorageCache -ErrorAction Stop
        $disk = Assert-WholeUsbIdentity
        $partitions = @(Get-WholeUsbPartitions)
        if ($disk.PartitionStyle -in @('RAW','MBR') -and $partitions.Count -eq 0) { return }
        $primaryError = "Clear-Disk 已返回成功，但刷新后状态仍为 PartitionStyle=$($disk.PartitionStyle)，分区数=$($partitions.Count)。"
        Write-WholeUsbDiagnostic 'Clear-Disk 返回成功但清盘核验未通过' $primaryError
    } catch {
        $primaryError = $_
        Write-WholeUsbDiagnostic 'Clear-Disk 失败' $primaryError
        Update-HostStorageCache -ErrorAction Stop
        $disk = Assert-WholeUsbIdentity
        $partitions = @(Get-WholeUsbPartitions)
        if ($disk.PartitionStyle -in @('RAW','MBR') -and $partitions.Count -eq 0) {
            Write-Host "接口报告失败，但已确认同一 U 盘为 $($disk.PartitionStyle) 且没有分区，继续制作。" -ForegroundColor Yellow
            return
        }
    }
    # The whole-disk confirmation also covers this equivalent backend. Re-check
    # the USB identity and source paths even when Clear-Disk returned success but
    # left a stale or partially cleared layout.
    Assert-WholeUsbDependencies -RequireIso:$RequireIso
    $disk = Assert-WholeUsbIdentity
    $file = Join-Path $script:workRoot ('diskpart-clean-' + [guid]::NewGuid().ToString('N') + '.txt')
    @("select disk $($disk.Number)", 'detail disk', 'list partition', 'clean', 'exit') | Set-Content -LiteralPath $file -Encoding ASCII -ErrorAction Stop
    Write-Host '清盘后状态未核验通过，已重新检查目标盘与源文件，改用 DiskPart 兼容清盘。' -ForegroundColor Yellow
    try {
        $null = Assert-WholeUsbIdentity
        Run-Command 'diskpart.exe' @('/s', $file) '删除整支 U 盘旧分区（兼容方式）'
        Wait-DiskService
        Update-HostStorageCache -ErrorAction Stop
        $disk = Assert-WholeUsbIdentity
        $partitions = @(Get-WholeUsbPartitions)
        if ($disk.PartitionStyle -notin @('RAW','MBR') -or $partitions.Count -ne 0) {
            throw "DiskPart 执行后仍未确认分区表已清空（PartitionStyle=$($disk.PartitionStyle)，分区数=$($partitions.Count)），已停止，不建立或格式化新分区。"
        }
    } catch {
        Write-WholeUsbDiagnostic 'DiskPart 清盘失败或结果核验失败' $_
        throw
    }
}

function Invoke-WholeUsbStorageAction([string]$Title, [scriptblock]$Action) {
    Write-Host "> $Title" -ForegroundColor DarkCyan
    Add-Content -LiteralPath (Join-Path $script:workRoot 'whole-disk.log') -Value ((Get-Date -Format s) + ' ' + $Title) -Encoding UTF8
    $theme = Set-ProgressTheme
    try {
        Write-Progress -Id 8300 -Activity $Title -Status '执行中' -PercentComplete -1
        $result = & $Action
        Write-Host "$Title 已完成。" -ForegroundColor Green
        return $result
    } catch {
        Write-WholeUsbDiagnostic $Title $_
        throw
    } finally { Write-Progress -Id 8300 -Activity $Title -Completed; Restore-ProgressTheme $theme }
}


function Get-WholeUsbSinglePartition([string]$FileSystem = 'FAT32') {
    $disk = Assert-WholeUsbIdentity
    $parts = @(Get-WholeUsbPartitions)
    $expectedMbrType = if ($FileSystem -eq 'NTFS') { 7 } else { 12 }
    $actualMbrType = -1
    if ($parts.Count -eq 1) {
        try { $actualMbrType = [Convert]::ToInt32($parts[0].MbrType) }
        catch { if ([string]$parts[0].MbrType -eq 'IFS') { $actualMbrType = 7 } elseif ([string]$parts[0].MbrType -eq 'FAT32') { $actualMbrType = 12 } }
    }
    if ($disk.PartitionStyle -ne 'MBR' -or $parts.Count -ne 1 -or $actualMbrType -ne $expectedMbrType -or
        $parts[0].DiskNumber -ne $disk.Number -or $parts[0].Offset -lt 1MB -or
        $parts[0].Offset -gt 8MB -or $parts[0].Size -lt $disk.Size - 16MB -or
        $parts[0].Offset + $parts[0].Size -gt $disk.Size) {
        throw '整盘单分区或完整容量核验失败，已停止后续操作。'
    }
    return $parts[0]
}

function Format-WholeUsbDisk($Plan, [string]$ToolPath, [string]$FileSystem = 'FAT32', [switch]$RequireIso) {
    if ($FileSystem -eq 'FAT32') { Assert-Fat32Tool $ToolPath }
    Assert-WholeUsbDependencies -RequireIso:$RequireIso
    if ((Get-WholeUsbLayoutSignature @(Get-WholeUsbPartitions)) -cne $Plan.Layout) { throw '确认后 U 盘分区布局变化，尚未清盘。' }
    $disk = Assert-WholeUsbIdentity
    $script:targetRebuildStarted = $true
    if ($global:GuiEngineState) { $global:GuiEngineState['TargetRebuildStarted'] = $true }
    $null = Invoke-WholeUsbStorageAction '删除选中 U 盘的全部旧分区' {
        Clear-ConfirmedWholeUsb -RequireIso:$RequireIso
    }
    Update-HostStorageCache
    $disk = Assert-WholeUsbIdentity
    $remainingPartitions = @(Get-WholeUsbPartitions)
    if ($remainingPartitions.Count -ne 0 -or $disk.PartitionStyle -notin @('RAW','MBR')) {
        $detail = "PartitionStyle=$($disk.PartitionStyle)，分区数=$($remainingPartitions.Count)"
        Write-WholeUsbDiagnostic '整盘重建前清盘核验失败' $detail
        throw "清盘核验失败（$detail），已停止。请查看 whole-disk-errors.log。"
    }
    if ($disk.PartitionStyle -eq 'RAW') {
        $null = Invoke-WholeUsbStorageAction '建立 MBR 分区表' { Initialize-Disk -InputObject $disk -PartitionStyle MBR -ErrorAction Stop }
        Update-HostStorageCache
        $disk = Assert-WholeUsbIdentity
    } else {
        Write-Host '磁盘已是空的 MBR 分区表，跳过重复初始化。' -ForegroundColor DarkCyan
    }
    if ($disk.PartitionStyle -ne 'MBR' -or $disk.NumberOfPartitions -ne 0) { throw '分区表核验失败，已停止。' }
    $mbrPartitionType = if ($FileSystem -eq 'NTFS') { 'IFS' } else { 'FAT32' }
    $null = Invoke-WholeUsbStorageAction "创建全容量 $FileSystem 主分区" {
        New-Partition -InputObject $disk -UseMaximumSize -MbrType $mbrPartitionType -AssignDriveLetter:$false -ErrorAction Stop
    }
    Wait-DiskService
    Update-HostStorageCache
    $partition = Get-WholeUsbSinglePartition $FileSystem
    $existing = @(Get-Partition -DriveLetter $Plan.Letter -ErrorAction SilentlyContinue)
    if ($existing.Count -and ($existing.Count -ne 1 -or $existing[0].DiskNumber -ne $script:diskNumber -or $existing[0].PartitionNumber -ne $partition.PartitionNumber)) {
        throw '预定盘符被其他分区使用，已停止。'
    }
    $null = Assert-WholeUsbIdentity
    $compatFormat = $false
    if ($FileSystem -eq 'FAT32') {
        Assert-Fat32Tool $ToolPath
        try {
            $null = Invoke-WholeUsbStorageAction '格式化全容量 FAT32（暂不分配盘符）' {
                Format-Volume -Partition $partition -FileSystem FAT32 -Confirm:$false -Force -ErrorAction Stop
            }
        } catch {
            $compatFormat = $true
            Write-Host "系统原生无盘符 FAT32 格式化不可用：$($_.Exception.Message)" -ForegroundColor Yellow
            Write-Host '改用兼容格式化工具；如果 Windows 弹出“需要格式化”提示，请点取消，工具会继续格式化并校验。' -ForegroundColor Yellow
            $null = Assert-WholeUsbIdentity
            Add-PartitionAccessPath -DiskNumber $script:diskNumber -PartitionNumber $partition.PartitionNumber -AccessPath "$($Plan.Letter):\" -ErrorAction Stop
            Update-HostStorageCache
            $partition = Get-WholeUsbSinglePartition $FileSystem
            if ($partition.DriveLetter -ne $Plan.Letter) { throw '兼容格式化前的新分区盘符核验失败，已停止。' }
            $byLetter = @(Get-Partition -DriveLetter $Plan.Letter -ErrorAction Stop)
            if ($byLetter.Count -ne 1 -or $byLetter[0].DiskNumber -ne $script:diskNumber -or $byLetter[0].PartitionNumber -ne $partition.PartitionNumber) { throw '兼容格式化前盘符映射变化，已停止。' }
            $null = Assert-WholeUsbIdentity
            Run-Command $ToolPath @("$($Plan.Letter):") '格式化全容量 FAT32（兼容方式）' @() 'Y'
        }
    } else {
        $null = Invoke-WholeUsbStorageAction '格式化全容量 NTFS' {
            Format-Volume -Partition $partition -FileSystem NTFS -Confirm:$false -Force -ErrorAction Stop
        }
    }
    Update-HostStorageCache
    $partition = Get-WholeUsbSinglePartition $FileSystem
    $volume = $partition | Get-Volume -ErrorAction Stop
    $expectedFormatLetter = if ($compatFormat) { $Plan.Letter } else { $null }
    $hasUnexpectedLetter = if ($expectedFormatLetter) { $volume.DriveLetter -ne $expectedFormatLetter } else { [bool]$volume.DriveLetter }
    if ($volume.FileSystem -ne $FileSystem -or $hasUnexpectedLetter -or $volume.Size -lt $partition.Size - 1GB) { throw "全容量 $FileSystem 文件系统核验失败，已停止。" }
    if (-not $compatFormat) {
        $null = Assert-WholeUsbIdentity
        Add-PartitionAccessPath -DiskNumber $script:diskNumber -PartitionNumber $partition.PartitionNumber -AccessPath "$($Plan.Letter):\" -ErrorAction Stop
        Update-HostStorageCache
    }
    $partition = Get-WholeUsbSinglePartition $FileSystem
    $volume = $partition | Get-Volume -ErrorAction Stop
    if ($volume.FileSystem -ne $FileSystem -or $volume.DriveLetter -ne $Plan.Letter) { throw "$FileSystem 盘符分配后核验失败，已停止复制。" }
    $null = Set-Volume -InputObject $volume -NewFileSystemLabel $script:imageIdentity.VolumeLabel -ErrorAction Stop
    Update-HostStorageCache
    $partition = Get-WholeUsbSinglePartition $FileSystem
    $volume = $partition | Get-Volume -ErrorAction Stop
    $byLetter = @(Get-Partition -DriveLetter $Plan.Letter -ErrorAction Stop)
    if ($partition.DriveLetter -ne $Plan.Letter -or $byLetter.Count -ne 1 -or $byLetter[0].DiskNumber -ne $script:diskNumber -or $byLetter[0].PartitionNumber -ne $partition.PartitionNumber -or $volume.FileSystemLabel -cne $script:imageIdentity.VolumeLabel -or $volume.FileSystem -ne $FileSystem) { throw "$FileSystem 格式化后盘符或卷标核验失败，已停止复制。" }
    return $partition
}

function Start-WholeUsbFormatOnly([string]$Choice) {
    Assert-Administrator
    $script:sourceMedia = $null
    $script:imageIdentity = [pscustomobject]@{DisplayName='普通 FAT32 U 盘';VolumeLabel='USB_FAT32'}
    $plan = Select-WholeUsbTarget $Choice
    Assert-WholeUsbDependencies
    $script:workRoot = Join-Path $env:LOCALAPPDATA ('Win11Usb-Step-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $script:workRoot -ErrorAction Stop
    $fileSystem = Read-UsbFileSystemChoice
    if (-not $fileSystem) { throw '已取消整盘格式化，U 盘未被修改。' }
    $script:targetFileSystem = $fileSystem
    if ($fileSystem -eq 'NTFS') { $script:imageIdentity.VolumeLabel = 'USB_NTFS' }
    $tool = if ($fileSystem -eq 'FAT32') { Prepare-Fat32Tool } else { '' }
    if (-not (Confirm-WholeUsbFormat $plan $fileSystem)) { throw '已取消整盘格式化，U 盘未被修改。' }
    $partition = Format-WholeUsbDisk $plan $tool $fileSystem
    Write-Host "整盘格式化完成：$($plan.Letter):\  |  $fileSystem  |  $([math]::Round($partition.Size/1GB,2)) GB  |  一个分区。" -ForegroundColor Green
    if (Read-EjectChoice "按回车安全弹出 U 盘 $($plan.Letter):；输入 Q 跳过") {
        try { Write-Host (Eject-CompletedUsb $plan.Letter) -ForegroundColor Green }
        catch { Write-Host "格式化已完成，但 U 盘尚未安全弹出：$($_.Exception.Message)" -ForegroundColor Yellow }
    }
    $script:completionHandled = $true
}


function Start-UsbSteps {
    Assert-Administrator
    Write-Host 'Windows 安装 U 盘：逐步命令行版 v19（FAT32 / NTFS 与 4 GiB 拆分优化）' -ForegroundColor Green
    Write-Host '先双击 Windows ISO 挂载。确认后删除所选 U 盘全部分区和文件，再重建一个全容量 FAT32 或 NTFS 分区。'

    Write-Host "`n[1/6] 确认 ISO 和目标 U 盘" -ForegroundColor Cyan
    $sourceLetter = Read-Letter '已挂载 ISO 的 DVD 盘符（例如 G）'
    $source = "${sourceLetter}:\"
    foreach ($relative in @('efi\boot\bootx64.efi', 'sources\boot.wim', 'setup.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $relative) -PathType Leaf)) { throw "ISO 缺少 $relative，请检查盘符。" }
    }
    try { $script:sourceMedia = Get-SourceMedia $sourceLetter } catch { $script:sourceMedia = $null }
    Write-Host "正在识别 $source 的安装系统与发行版本……" -ForegroundColor DarkCyan
    $files = @(Get-ChildItem -LiteralPath $source -File -Recurse)
    $imagePlan = Get-InstallImagePlan $source $files
    $wim = $imagePlan.WimPath
    $split = $imagePlan.RequiresSplit
    $script:imageIdentity = Get-InstallImageIdentity $imagePlan.ImageFiles[0].FullName $script:sourceMedia.ImagePath
    if ($script:imageIdentity.ReadError) { throw ('无法读取安装映像，尚未清盘。请检查 ISO 完整性或 DISM 兼容性：' + $script:imageIdentity.ReadError) }
    $volumeLabel = $script:imageIdentity.VolumeLabel
    Show-InstallImageIdentity $script:imageIdentity

    $wholePlan = Select-WholeUsbTarget -SourceLetter $sourceLetter
    $letter = $wholePlan.Letter
    $sizeMB = $wholePlan.SizeMB
    Assert-WholeUsbDependencies -RequireIso
    if (($files | Measure-Object Length -Sum).Sum + 512MB -gt [long]$sizeMB * 1MB) { throw 'U 盘分区容量不足。' }
    $script:workRoot = Join-Path $env:LOCALAPPDATA ('Win11Usb-Step-' + [guid]::NewGuid().ToString('N'))
    $staging = Join-Path $script:workRoot 'staging'
    if ($split) {
        $local = New-Object IO.DriveInfo([IO.Path]::GetPathRoot($script:workRoot))
        if ($local.AvailableFreeSpace -lt (Get-Item -LiteralPath $wim).Length + 512MB) { throw '本机临时空间不足，尚未格式化 U 盘。' }
    }
    $null = New-Item -ItemType Directory -Path $script:workRoot
    Write-Host "检查通过：ISO $source；U 盘 ${letter}:\，$($script:target.FriendlyName)，$([math]::Round($script:target.Size/1GB,1)) GB。"
    Write-Host "本次临时目录：$script:workRoot"
    if ($global:GuiEngineState) {
        $global:GuiEngineState['WorkRoot'] = $script:workRoot
        $global:GuiEngineState['VolumeLabel'] = $volumeLabel
        $global:GuiEngineState['InstallName'] = $script:imageIdentity.DisplayName
        $global:GuiEngineState['TargetLetter'] = $letter
    }

    $fileSystem = Read-UsbFileSystemChoice
    if (-not $fileSystem) { throw '已取消整盘格式化，U 盘未被修改。' }
    $script:targetFileSystem = $fileSystem
    $tool = if ($fileSystem -eq 'FAT32') { Prepare-Fat32Tool } else { '' }
    Write-Host "`n[2/6] 将整支 U 盘重建为一个全容量 $fileSystem 分区" -ForegroundColor Cyan
    if (-not (Confirm-WholeUsbFormat $wholePlan $fileSystem)) { throw '已取消整盘格式化，U 盘未被修改。' }
    $partition = Format-WholeUsbDisk $wholePlan $tool $fileSystem -RequireIso
    $partNumber = $partition.PartitionNumber
    $sizeMB = [long][math]::Floor($partition.Size/1MB)
    $destination = "${letter}:\"
    Write-Host "完成：$destination，$fileSystem，全容量 $([math]::Round($partition.Size/1GB,2)) GB，一个分区。" -ForegroundColor Green

    Assert-SourceMediaIdentity
    Confirm-InstallImageStep $imagePlan
    $swm = @()
    if ($split) {
        $null = New-Item -ItemType Directory -Path $staging
        Run-Command 'dism.exe' @('/Split-Image', "/ImageFile:$wim", "/SWMFile:$(Join-Path $staging 'install.swm')", '/FileSize:4000')
        $swm = @(Get-ChildItem -LiteralPath $staging -File -Filter 'install*.swm')
        if (-not (Test-Path -LiteralPath (Join-Path $staging 'install.swm')) -or
            @($swm | Where-Object { $_.Length -le 0 -or $_.Length -gt $imagePlan.Fat32Limit }).Count) { throw '拆分结果不完整，已停止复制。' }
        $total = ($files | Where-Object FullName -ne $wim | Measure-Object Length -Sum).Sum + ($swm | Measure-Object Length -Sum).Sum
        if ($total + 128MB -gt [long]$sizeMB * 1MB) { throw '拆分后容量不足，已停止复制。' }
        Write-Host "完成：本机生成 $($swm.Count) 个 SWM 文件。" -ForegroundColor Green
    }

    if ($split) { Step 4 "复制安装文件和 SWM 到 U 盘 $destination" }
    else { Write-Host "`n[4/6] 复制安装文件到 U 盘 $destination" -ForegroundColor Cyan }
    $null = Get-Target
    Assert-SourceMediaIdentity
    $volume = Get-Partition -DiskNumber $script:diskNumber -PartitionNumber $partNumber | Get-Volume
    if ($volume.DriveLetter -ne $letter -or $volume.FileSystem -ne $fileSystem) { throw '复制前盘符或文件系统变化，已停止。' }
    $copyArgs = @($source, $destination, '/E', '/COPY:DAT', '/DCOPY:T', '/R:2', '/W:2', '/BYTES', '/FP', '/NC', '/NDL', '/V', '/NJH')
    if ($split) { $copyArgs += @('/XF', 'install.wim') }
    Write-Host '整体进度按文件大小估算；当前文件进度会随文件切换重新开始。'
    $copyFiles = @($files | Where-Object { -not $split -or $_.FullName -ne $wim })
    Run-Command 'robocopy.exe' $copyArgs '复制安装文件' $copyFiles
    if ($split) { Run-Command 'robocopy.exe' @($staging, (Join-Path $destination 'sources'), '/E', '/COPY:DAT', '/R:2', '/W:2', '/BYTES', '/FP', '/NC', '/NDL', '/V', '/NJH') '复制拆分映像 SWM' $swm }
    Write-Host '完成：文件复制已结束。' -ForegroundColor Green

    Step 5 '检查复制结果'
    $null = Get-Target
    foreach ($file in $files) {
        if ($split -and $file.FullName -eq $wim) { continue }
        $relative = $file.FullName.Substring($source.Length)
        $copied = Get-Item -LiteralPath (Join-Path $destination $relative) -ErrorAction Stop
        if ($copied.Length -ne $file.Length) { throw "复制大小不一致：$relative" }
    }
    foreach ($file in $swm) {
        $copied = Get-Item -LiteralPath (Join-Path $destination ('sources\' + $file.Name)) -ErrorAction Stop
        if ($copied.Length -ne $file.Length) { throw "SWM 文件大小不一致：$($file.Name)" }
    }
    Write-Host "制作完成：$destination，复制文件大小一致，efi\boot\bootx64.efi 已就绪。" -ForegroundColor Green
    if ($global:GuiEngineState) { $global:GuiEngineState['CopyVerified'] = $true }
    Write-Host '清理后将询问是否安全弹出 U 盘及 ISO。'

    Write-Host "`n[6/6] 清理本次临时 SWM" -ForegroundColor Cyan
    if ($swm.Count -eq 0) {
        Write-Host '本次未拆分映像，自动跳过 SWM 清理。' -ForegroundColor Green
    } else {
        $cleanup = Read-Host '按回车清理；输入 Q 保留临时文件'
        if ($cleanup -eq '') {
            if (Test-Path -LiteralPath $staging) {
                foreach ($file in $swm) { Remove-Item -LiteralPath $file.FullName -Force }
                [IO.Directory]::Delete($staging, $false)
            }
            Write-Host '临时 SWM 已清理。终端输出保留供你查看。' -ForegroundColor Green
        } else { Write-Host "制作已经完成，临时文件保留在：$script:workRoot" }
    }
    Complete-UsbSession $letter $sourceLetter
}

$script:completionHandled = $false
$exitCode = 0
try {
    if (@($EjectUsb,$InspectIso,$FormatUsb | Where-Object { $_ }).Count -gt 1) { throw 'EjectUsb、InspectIso、FormatUsb 只能使用一种模式。' }
    if ($FormatUsb) { Start-WholeUsbFormatOnly $FormatUsb }
    elseif ($InspectIso) { Start-IsoInspection $InspectIso }
    elseif ($EjectUsb) { Start-UsbEjectOnly $EjectUsb }
    else { Start-UsbSteps }
}
catch {
    $exitCode = 1
    if ($global:GuiEngineState) { $global:GuiEngineState['LastError'] = $_.Exception.Message }
    Write-Host "`n已停止：$($_.Exception.Message)" -ForegroundColor Red
    if ($script:workRoot) { Write-Host "本次临时文件保留在：$script:workRoot" }
    if (-not $EjectUsb -and -not $InspectIso) { Write-Host 'Windows 系统若弹出格式化提示，请点取消，不要同时启动其他格式化工具。' }
}
finally { if (-not $GuiMode -and -not $script:completionHandled) { $null = Read-Host "`n按回车结束脚本" } }
if ($GuiMode) { $global:GuiEngineExitCode = $exitCode }
else { exit $exitCode }
