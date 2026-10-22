$ErrorActionPreference = 'Stop'
$base = Split-Path $PSScriptRoot -Parent
$path = Join-Path $base 'src\engine\Win11Usb.Engine.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Script parse failed' }
foreach ($name in @('Initialize-DeviceEject','Get-UsbDeviceChain','Get-UsbEjectCandidate','Invoke-UsbEjectRequest','Get-UsbEjectFailure','Assert-UsbEjectIdentity','Eject-CompletedUsb','Dismount-SourceMedia','Start-UsbEjectOnly','Read-EjectChoice')) {
    $fn = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name}, $false)
    . ([scriptblock]::Create($fn.Extent.Text))
}
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Expect-Error([scriptblock]$Action, [string]$Expected) {
    $caught = $null
    try { & $Action } catch { $caught = $_.Exception.Message }
    Check ($null -ne $caught -and $caught.Contains($Expected)) "Expected '$Expected', got '$caught'"
}
# Compile the real native bindings; exercise the tree only through mocks below.
Initialize-DeviceEject
# All eject operations below use mocked native boundaries. Never call native Request.
$script:diskId = 'USBSTOR\Disk&Ven_Lenovo&Prod_Lenovo_K110\ABC00904&0'
$script:usbId = 'USB\VID_1234&PID_5678\ABC00904'
function Node([string]$Id, [string]$Service, [string]$Child, [uint32]$Caps = 4) {
    return [pscustomobject]@{InstanceId=$Id; Service=$Service; ServiceKnown=$true; Capabilities=$Caps; CapabilitiesKnown=$true; ChildrenKnown=$true; OnlyChildId=$Child}
}
function NormalChain([string]$Service = 'USBSTOR') {
    return @((Node $script:diskId 'disk' '' 0), (Node $script:usbId $Service $script:diskId))
}
foreach ($service in @('USBSTOR','UASPStor')) {
    $candidate = Get-UsbEjectCandidate (NormalChain $service) $script:diskId
    Check ($candidate.InstanceId -eq $script:usbId) "Wrong $service candidate"
}
$bridgeId = 'SCSI\Adapter\Example'
$longChain = @((Node $script:diskId 'disk' '' 0), (Node $bridgeId 'disk' $script:diskId 0), (Node $script:usbId 'UASPStor' $bridgeId))
Check ((Get-UsbEjectCandidate $longChain $script:diskId).InstanceId -eq $script:usbId) 'Unique intermediate path rejected'
foreach ($service in @('usbccgp','USBHUB3','usbhub','disk','')) {
    Expect-Error { Get-UsbEjectCandidate (NormalChain $service) $script:diskId } '未找到可安全移除'
}
foreach ($change in @('wrongChild','multipleChildren','unknownChildren','unknownService','unknownCaps','notRemovable','interface')) {
    $nodes = NormalChain
    switch ($change) {
        'wrongChild' { $nodes[1].OnlyChildId = 'OTHER\DEVICE' }
        'multipleChildren' { $nodes[1].OnlyChildId = $null }
        'unknownChildren' { $nodes[1].ChildrenKnown = $false }
        'unknownService' { $nodes[1].ServiceKnown = $false }
        'unknownCaps' { $nodes[1].CapabilitiesKnown = $false }
        'notRemovable' { $nodes[1].Capabilities = 2 }
        'interface' { $nodes[1].InstanceId = 'USB\VID_1234&PID_5678&MI_00\ABC00904' }
    }
    Expect-Error { Get-UsbEjectCandidate $nodes $script:diskId } '取消自动弹出'
}
Expect-Error { Get-UsbEjectCandidate (NormalChain) 'OTHER\DISK' } '关系核验失败'
$reason = Get-UsbEjectFailure ([pscustomobject]@{Code=0x17;Veto=8})
Check ($reason.Contains('不支持此弹出请求') -and -not $reason.Contains('占用') -and -not $reason.Contains('关闭')) 'Veto 8 misreported as open handles'
$reason = Get-UsbEjectFailure ([pscustomobject]@{Code=0x17;Veto=5})
Check $reason.Contains('文件句柄') 'Actual handle veto not explained'
$reason = Get-UsbEjectFailure ([pscustomobject]@{Code=5;Veto=8})
Check (-not $reason.Contains('拒绝类型') -and -not $reason.Contains('不支持')) 'Unrelated error interpreted uninitialized veto'

$script:workRoot = $null
$script:diskNumber = 4
$script:targetPnpId = $script:diskId
$script:target = [pscustomobject]@{Number=4;FriendlyName='Lenovo K110'}
$script:partitionDisk = 4
$script:currentId = $script:diskId
$script:requestCount = 0
$script:requestedId = ''
$script:chainReads = 0
$script:changeCandidate = $false
$script:requestResult = [pscustomobject]@{Code=0;Veto=-1;Detail=''}
function Get-Target { return [pscustomobject]@{Number=4;FriendlyName='Lenovo K110'} }
function Get-Partition { param($DriveLetter,$ErrorAction); return [pscustomobject]@{DiskNumber=$script:partitionDisk} }
function Get-CimInstance { param($ClassName,$Filter,$ErrorAction); return [pscustomobject]@{PNPDeviceID=$script:currentId} }
function Get-UsbDeviceChain {
    param($InstanceId)
    Check ($InstanceId -eq $script:diskId) 'Inspected wrong disk'
    $script:chainReads++
    $nodes = NormalChain
    if ($script:changeCandidate -and $script:chainReads -ge 2) { $nodes[1].InstanceId = 'USB\VID_9999&PID_9999\OTHER' }
    return $nodes
}
function Invoke-UsbEjectRequest { param($InstanceId); $script:requestCount++; $script:requestedId=$InstanceId; return $script:requestResult }
$message = Eject-CompletedUsb 'G'
Check ($message.Contains('已安全弹出') -and $script:requestCount -eq 1 -and $script:requestedId -eq $script:usbId) 'USB parent was not requested exactly once'
$script:requestCount=0; $script:partitionDisk=3
Expect-Error { Eject-CompletedUsb 'G' } '盘符已经变化'
Check ($script:requestCount -eq 0) 'Wrong disk still ejected'
$script:partitionDisk=4; $script:currentId='OTHER\DISK'
Expect-Error { Eject-CompletedUsb 'G' } '身份已经变化'
Check ($script:requestCount -eq 0) 'Changed identity still ejected'
$script:currentId=$script:diskId; $script:changeCandidate=$true; $script:chainReads=0
Expect-Error { Eject-CompletedUsb 'G' } '身份或状态变化'
Check ($script:requestCount -eq 0) 'Changed parent still ejected'
$script:changeCandidate=$false; $script:chainReads=0
$script:requestResult=[pscustomobject]@{Code=0x17;Veto=5;Detail='PRIVATE\DETAIL'}
Expect-Error { Eject-CompletedUsb 'G' } '文件句柄'
Check ($script:requestCount -eq 1) 'Veto retried on a higher ancestor'

# ISO eject must return only a concise string even if the cmdlet emits an object.
$snapshot = [pscustomobject]@{Letter='H';UniqueId='ISO1';Label='Windows10';Size=8000000000;ImagePath='D:\image.iso';DevicePath='DVD1'}
$script:sourceSnapshot=$snapshot
function Get-Volume { param($DriveLetter,$ErrorAction); return [pscustomobject]@{Size=8000000000} }
function Get-SourceMedia { param($Letter); return $script:sourceSnapshot }
function Dismount-DiskImage { param($ImagePath,$StorageType,$Confirm,$ErrorAction); return [pscustomobject]@{ImagePath=$ImagePath;StorageType=1} }
function Get-DiskImage { param($ImagePath,$ErrorAction); return [pscustomobject]@{Attached=$false} }
$isoResult = @(Dismount-SourceMedia $snapshot)
Check ($isoResult.Count -eq 1 -and $isoResult[0] -is [string] -and $isoResult[0].Contains('已弹出 ISO')) 'ISO eject leaked MSFT_DiskImage object'

# Dedicated eject mode cannot call formatting or the full creation workflow.
$script:requestCount=0; $script:chainReads=0; $script:requestResult=[pscustomobject]@{Code=0;Veto=-1;Detail=''}
$script:completionHandled=$false
function Assert-Administrator { }
function Read-Host { param($Prompt); return '' }
function Run-DiskPart { throw 'Forbidden: formatting called' }
function Start-UsbSteps { throw 'Forbidden: creation workflow called' }
function Initialize-Disk { throw 'Forbidden: disk initialization called' }
function New-Partition { throw 'Forbidden: new partition called' }
Start-UsbEjectOnly 'G'
Check ($script:completionHandled -and $script:requestCount -eq 1) 'Standalone eject mode failed'
Write-Output 'PASS: native PS5.1 compilation; mocked USBSTOR/UASP topology, hub/composite/multiple-child rejection, identity changes, correct veto messages/no retry, clean ISO output, eject-only mode. No real eject or formatting performed.'
