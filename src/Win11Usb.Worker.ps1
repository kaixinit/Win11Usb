#requires -Version 5.1
# Hosted in the EXE's background STA runspace; never a separate PowerShell process.
$ErrorActionPreference = 'Stop'
function global:Write-Host {
    [CmdletBinding()]
    param([Parameter(Position=0,ValueFromPipeline=$true)][object[]]$Object,
        [ConsoleColor]$ForegroundColor,[ConsoleColor]$BackgroundColor,
        [switch]$NoNewline,[string]$Separator=' ')
    process {
        $text = if ($null -eq $Object) { '' } else { (@($Object | ForEach-Object { [string]$_ }) -join $Separator) }
        $kind = if ($NoNewline -or $text.Contains("`r")) { 'Inline' } else { 'Log' }
        $global:GuiPromptQueue.Add([pscustomobject]@{Kind=$kind;Text=$text.TrimStart("`r")})
    }
}
function global:Read-Host {
    param([string]$Prompt)
    if ($global:GuiEngineState['ForceStopRequested']) { throw '用户已请求结束制作。' }
    $global:GuiPromptIndex++
    switch ($global:GuiPromptIndex) {
        1 { if ($Prompt -notlike '已挂载 ISO 的 DVD 盘符*') { throw 'ISO 输入顺序不匹配，已停止。' }; return $global:GuiIsoLetter }
        2 { if ($Prompt -notlike '选择整支 U 盘*') { throw '磁盘输入顺序不匹配，已停止。' }; return [string]$global:GuiDiskNumber }
        3 { if ($Prompt -ne '请输入编号（回车默认取消）') { throw '文件系统输入顺序不匹配，已停止。' }; return $(if ($global:GuiFileSystem -eq 'FAT32') { '1' } else { '2' }) }
    }
    $request = [pscustomobject]@{Kind='Prompt';Index=$global:GuiPromptIndex;Prompt=$Prompt;Response='';Cancelled=$false;Ready=[Threading.ManualResetEvent]::new($false)}
    try {
        $global:GuiPromptQueue.Add($request)
        $request.Ready.WaitOne() | Out-Null
        if ($request.Cancelled -or $global:GuiEngineState['ForceStopRequested']) { throw '交互请求已取消，已停止制作。' }
        return [string]$request.Response
    } finally { $request.Ready.Dispose() }
}
$global:GuiPromptIndex = 0
$global:GuiEngineExitCode = $null
$global:GuiMountedIsoByApp = $false
try {
    Write-Host "正在检查 ISO 文件：$global:GuiIsoPath"
    $image = $null
    try { $image = Get-DiskImage -ImagePath $global:GuiIsoPath -ErrorAction Stop } catch { }
    if (-not $image -or -not $image.Attached) {
        Write-Host '正在自动挂载 ISO…'
        $image = Mount-DiskImage -ImagePath $global:GuiIsoPath -StorageType ISO -PassThru -ErrorAction Stop
        $global:GuiMountedIsoByApp = $true
        $global:GuiEngineState['MountedByApp'] = $true
    }
    $letters = @(
        foreach ($volume in @(Get-Volume -ErrorAction Stop | Where-Object { [string]$_.DriveType -eq 'CD-ROM' -and $_.DriveLetter })) {
            try {
                $mounted = @(Get-DiskImage -Volume $volume -ErrorAction Stop | Where-Object { $_.Attached -and [string]::Equals([string]$_.ImagePath,$global:GuiIsoPath,[StringComparison]::OrdinalIgnoreCase) })
                if ($mounted.Count) { [string]$volume.DriveLetter }
            } catch { }
        }
    ) | Sort-Object -Unique
    if ($letters.Count -ne 1) { throw "无法唯一确定所选 ISO 的 DVD 盘符（匹配到 $($letters.Count) 个），已停止。" }
    $global:GuiIsoLetter = [string]$letters[0]
    Write-Host "ISO 已就绪：$($global:GuiIsoLetter):\"
    & ([scriptblock]::Create($global:GuiEngineScript)) -GuiMode
    if ($null -eq $global:GuiEngineExitCode) { throw '制作引擎未返回明确结果，已停止。' }
    $global:GuiEngineState['ExitCode'] = [int]$global:GuiEngineExitCode
} catch {
    $global:GuiEngineState['ExitCode'] = 1
    $global:GuiEngineState['LastError'] = $_.Exception.Message
    Write-Host "准备失败：$($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($global:GuiEngineState['ExitCode'] -ne 0 -and $global:GuiMountedIsoByApp -and -not $global:GuiEngineState['ForceStopRequested']) {
        try {
            Write-Host '正在释放本程序挂载的 ISO…'
            Dismount-DiskImage -ImagePath $global:GuiIsoPath -StorageType ISO -Confirm:$false -ErrorAction Stop | Out-Null
            $global:GuiEngineState['MountedByApp'] = $false
            Write-Host '已释放本程序挂载的 ISO。'
        } catch { Write-Host "ISO 保留挂载，可在资源管理器中弹出：$($_.Exception.Message)" -ForegroundColor Yellow }
    }
}
