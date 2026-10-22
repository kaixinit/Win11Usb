using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

internal sealed class NativeWindow
{
    internal readonly Window Window;
    private readonly bool preview;
    private readonly bool previewEmpty;
    private readonly DispatcherTimer timer;
    private readonly StringBuilder engineLog = new StringBuilder();
    private bool busy, showingRun, processingTick, closed, refreshing, runFailed;
    private IWorkflow workflow;
    private Hashtable result;
    internal Func<IWorkflow> WorkflowFactory = () => new WorkflowRunner();

    internal NativeWindow(bool preview, bool running, bool empty)
    {
        this.preview = preview; previewEmpty = empty;
        Window = (Window)XamlReader.Parse(AppResources.ReadText("MainWindow.xaml"));
        using (var input = new MemoryStream(AppResources.ReadBytes("Win11Usb.ico")))
        {
            var frame = BitmapFrame.Create(input, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
            frame.Freeze(); Window.Icon = frame;
        }
        timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(120) };
        timer.Tick += (s, e) => Tick();
        C<Button>("RefreshButton").Click += (s, e) => RefreshDevices();
        C<Button>("SelectIsoButton").Click += (s, e) => SelectIso();
        C<ComboBox>("DiskCombo").SelectionChanged += (s, e) => { SyncSelection(); if (Disk != null) Text("DiskInfo", "请核对磁盘编号、设备名称和容量。"); };
        C<RadioButton>("Fat32Option").Checked += (s, e) => { C<ComboBox>("FsCombo").SelectedIndex = 0; SyncSelection(); };
        C<RadioButton>("NtfsOption").Checked += (s, e) => { C<ComboBox>("FsCombo").SelectedIndex = 1; SyncSelection(); };
        C<Button>("StartButton").Click += (s, e) => { if (preview) ShowRunningPreview(); else if (ConfirmStart()) StartEngine(); };
        C<Button>("ForceStopButton").Click += (s, e) => { if (preview) StopPreview(); else StopWork(false); };
        Window.Closing += (s, e) => { if (busy) { e.Cancel = true; StopWork(true); } };
        Window.Closed += (s, e) => {
            closed = true; timer.Stop();
            if (workflow != null && !busy) { workflow.Dispose(); workflow = null; }
        };
        RefreshDevices();
        SetRunLayout(false); SetStage(0, false);
        if (preview) { Window.Title = "Windows 安装 U 盘制作工具 - 界面预览"; Text("FooterText", "界面预览模式：不会启动制作流程，也不会修改磁盘。"); }
        if (running) ShowRunningPreview();
        else C<TextBox>("LogBox").Text = "等待开始制作。操作日志将在这里实时显示。";
        Window.ContentRendered += (s, e) => { if (showingRun) C<TextBox>("LogBox").ScrollToEnd(); };
        timer.Start();
    }

    internal T C<T>(string name) where T : class { return (T)Window.FindName(name); }
    private void Text(string name, string value) { C<TextBlock>(name).Text = value ?? ""; }
    private static Brush Brush(string color) { return (Brush)new BrushConverter().ConvertFromString(color); }
    internal DiskItem Disk { get { return C<ComboBox>("DiskCombo").SelectedItem as DiskItem; } }
    internal string FileSystem { get { return Convert.ToString(((ComboBoxItem)C<ComboBox>("FsCombo").SelectedItem).Tag); } }
    private string IsoPath { get { return C<TextBox>("IsoPathBox").Text; } }
    private string S(string key) { return result == null ? "" : Convert.ToString(result[key]); }
    private bool B(string key) { return result != null && result[key] != null && Convert.ToBoolean(result[key]); }

    internal void SetStage(int stage, bool complete)
    {
        Text("StepText", complete ? "6/6 ·" : stage > 0 ? stage + "/6 ·" : "");
        for (int number = 1; number <= 6; number++)
        {
            bool done = complete || (stage > 0 && number < stage), current = !complete && number == stage;
            C<Image>("StageIcon" + number).Source = (ImageSource)Window.Resources[done ? "StepDoneImage" : current ? "StepCurrentImage" : "StepPendingImage"];
            C<TextBlock>("StageLabel" + number).Foreground = Brush(current ? "#2165DF" : done ? "#526782" : "#64748B");
            C<TextBlock>("StageLabel" + number).FontWeight = current ? FontWeights.SemiBold : FontWeights.Normal;
            if (number < 6) C<Border>("StageLink" + number).Background = Brush(done ? "#9BBAED" : "#DDE5EF");
        }
    }

    internal void SyncSelection()
    {
        string iso = IsoPath;
        Text("IsoNameText", iso.Length == 0 ? "选择 Windows ISO 镜像" : Path.GetFileName(iso).Replace("_", "_\u200B"));
        Text("IsoDirectoryText", iso.Length == 0 ? "支持 .iso 文件" : Path.GetDirectoryName(iso));
        C<TextBlock>("IsoNameText").Foreground = Brush(iso.Length == 0 ? "#76859A" : "#24334A");
        C<TextBlock>("IsoNameText").ToolTip = iso;
        C<TextBlock>("DiskPlaceholder").Visibility = Disk == null ? Visibility.Visible : Visibility.Collapsed;
        C<ComboBox>("DiskCombo").IsEnabled = !showingRun && !refreshing && C<ComboBox>("DiskCombo").Items.Count > 0;
        Text("WarningText", Disk == null ? "制作将清除所选 U 盘的全部分区和文件。" : "制作将清除磁盘 " + Disk.Number + " 的全部分区和文件。");
        C<Button>("StartButton").IsEnabled = !showingRun && !refreshing && Disk != null && !String.IsNullOrWhiteSpace(iso);
    }

    internal void SetRunLayout(bool running)
    {
        showingRun = running;
        foreach (string name in new[] { "SelectIsoButton", "Fat32Option", "NtfsOption" }) C<Control>(name).IsEnabled = !running;
        C<Button>("RefreshButton").IsEnabled = !running && !refreshing;
        C<Button>("StartButton").Content = running ? "制作中…" : preview && !previewEmpty ? "预览制作状态" : "开始制作";
        C<Button>("ForceStopButton").Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        SyncSelection();
    }

    private void SetDiskMetadata(string metadata)
    {
        if (Disk == null) return;
        var combo = C<ComboBox>("DiskCombo"); int index = combo.SelectedIndex;
        var replacement = Disk.Copy(); replacement.Metadata = metadata;
        combo.Items[index] = replacement; combo.SelectedIndex = index;
    }

    internal void RefreshDevices()
    {
        if (busy || refreshing || closed) return;
        var combo = C<ComboBox>("DiskCombo"); combo.Items.Clear();
        if (preview)
        {
            if (previewEmpty)
            {
                C<TextBox>("IsoPathBox").Text = "";
                Text("IsoInfo", "空状态预览；不会读取或修改磁盘。");
                Text("DiskInfo", "未检测到 U 盘，请连接后刷新。");
                C<TextBlock>("DiskInfo").Visibility = Visibility.Visible;
            }
            else
            {
                C<TextBox>("IsoPathBox").Text = @"D:\Windows ISO\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06.iso";
                combo.Items.Add(new DiskItem { Number = 3, FriendlyName = "Kingston DataTraveler 3.0", Size = (long)(57.8 * 1073741824), PartitionCount = 1,
                    Metadata = @"磁盘 3 · 57.8 GB · F:\ · 当前 1 个分区（示例）", Display = "磁盘 3 | Kingston DataTraveler 3.0 | 57.8 GB | 当前 1 个分区（示例）" });
                combo.SelectedIndex = 0;
                Text("IsoInfo", "识别结果：Windows 11 教育版 26H2 · WIN11_26H2（示例）");
                Text("DiskInfo", "示例设备。预览模式不会启动制作或修改磁盘。");
                C<TextBlock>("DiskInfo").Visibility = Visibility.Collapsed;
            }
            SyncSelection(); return;
        }
        refreshing = true; SetRunLayout(false);
        Text("DiskInfo", "正在检测 USB 设备…"); C<TextBlock>("DiskInfo").Visibility = Visibility.Visible;
        Task.Factory.StartNew(() => DiskReader.Read()).ContinueWith(task => {
            if (closed) return;
            Window.Dispatcher.BeginInvoke(new Action(() => {
                if (closed) return;
                refreshing = false;
                if (task.IsFaulted)
                {
                    string error = task.Exception.GetBaseException().Message;
                    Text("DiskInfo", "读取 USB 设备失败，请刷新重试。"); AddLog("设备检测失败：" + error);
                }
                else
                {
                    foreach (var disk in task.Result) combo.Items.Add(disk);
                    if (combo.Items.Count > 0) combo.SelectedIndex = 0;
                    Text("DiskInfo", combo.Items.Count > 0 ? "列表项是整支物理磁盘；分区数量只供核对，制作时会重建整盘。" : "未检测到 U 盘，请连接后刷新。");
                    C<TextBlock>("DiskInfo").Visibility = combo.Items.Count > 0 ? Visibility.Collapsed : Visibility.Visible;
                }
                SetRunLayout(false);
            }));
        });
    }

    private void SelectIso()
    {
        var dialog = new Microsoft.Win32.OpenFileDialog { Title = "选择 Windows 安装 ISO", Filter = "Windows ISO 映像 (*.iso)|*.iso|所有文件 (*.*)|*.*", CheckFileExists = true, Multiselect = false };
        if (dialog.ShowDialog(Window) == true)
        {
            C<TextBox>("IsoPathBox").Text = dialog.FileName;
            Text("IsoInfo", "开始制作时自动挂载并识别系统版本。"); SyncSelection();
        }
    }

    internal Window CreateDialog(string title, string heading, string message, string details, string hint, string kind, string primaryText, string secondaryText, bool defaultPrimary)
    {
        var dialog = (Window)XamlReader.Parse(AppResources.ReadText("MessageDialog.xaml"));
        dialog.Owner = Window; dialog.Icon = Window.Icon; dialog.Title = title;
        string accent = kind == "Warning" ? "#D97706" : kind == "Error" ? "#C83B35" : "#2878E8";
        ((TextBlock)dialog.FindName("KindLabel")).Text = kind == "Warning" ? "需要确认" : kind == "Error" ? "出现问题" : "提示信息";
        ((TextBlock)dialog.FindName("HeadingText")).Text = heading;
        ((TextBlock)dialog.FindName("MessageText")).Text = message;
        ((TextBlock)dialog.FindName("HintText")).Text = hint;
        ((Border)dialog.FindName("IconBadge")).Background = Brush(kind == "Warning" ? "#FFF2D9" : kind == "Error" ? "#FDE8E7" : "#E6F0FF");
        ((TextBlock)dialog.FindName("IconGlyph")).Text = kind == "Warning" ? "!" : kind == "Error" ? "×" : "i";
        ((TextBlock)dialog.FindName("IconGlyph")).Foreground = Brush(accent);
        var primary = (Button)dialog.FindName("PrimaryButton"); var secondary = (Button)dialog.FindName("SecondaryButton");
        primary.Content = primaryText; primary.Background = Brush(accent); primary.IsDefault = defaultPrimary;
        if (!String.IsNullOrWhiteSpace(details)) { ((TextBlock)dialog.FindName("DetailsText")).Text = details; ((Border)dialog.FindName("DetailsCard")).Visibility = Visibility.Visible; }
        if (String.IsNullOrWhiteSpace(secondaryText)) { secondary.Visibility = Visibility.Collapsed; primary.IsCancel = true; }
        else { secondary.Content = secondaryText; secondary.IsCancel = true; secondary.IsDefault = !defaultPrimary; }
        primary.Click += (s, e) => dialog.DialogResult = true;
        secondary.Click += (s, e) => dialog.DialogResult = false;
        dialog.ContentRendered += (s, e) => { if (defaultPrimary) primary.Focus(); else secondary.Focus(); };
        return dialog;
    }

    private bool Dialog(string title, string heading, string message, string details = "", string hint = "", string kind = "Info", string primary = "确定", string secondary = "", bool defaultPrimary = true)
    {
        return CreateDialog(title, heading, message, details, hint, kind, primary, secondary, defaultPrimary).ShowDialog() == true;
    }

    internal Window CreateFormatDialog(string details)
    {
        var dialog = (Window)XamlReader.Parse(AppResources.ReadText("FormatDialog.xaml"));
        dialog.Owner = Window; dialog.Icon = Window.Icon;
        if (FileSystem == "NTFS") ((TextBlock)dialog.FindName("FormatCompatibility")).Visibility = Visibility.Visible;
        ((TextBlock)dialog.FindName("ConfirmDetails")).Text = details;
        var cancel = (Button)dialog.FindName("CancelFormatButton"); var confirm = (Button)dialog.FindName("ConfirmFormatButton");
        cancel.Click += (s, e) => dialog.DialogResult = false;
        confirm.Click += (s, e) => dialog.DialogResult = true;
        dialog.ContentRendered += (s, e) => cancel.Focus();
        return dialog;
    }

    private bool ConfirmStart()
    {
        if (Disk == null || String.IsNullOrWhiteSpace(IsoPath)) return false;
        string details = "安装镜像\n" + IsoPath + "\n\n写入U盘\n" + Disk.Display + "\n\n文件系统\n" + FileSystem;
        if (FileSystem == "NTFS") details += "\n部分 UEFI 固件可能不支持直接从 NTFS 启动。";
        return Dialog("开始制作", "先检查本次制作目标", "程序会自动挂载并检查所选 ISO，然后开始准备流程。", details, "清理磁盘前还会再次显示设备信息并单独确认。", "Info", "继续检查", "返回检查", false);
    }

    internal void AddLog(string text)
    {
        if (String.IsNullOrWhiteSpace(text)) return;
        engineLog.AppendLine(text); C<TextBox>("LogBox").AppendText(text + Environment.NewLine); C<TextBox>("LogBox").ScrollToEnd();
        if (Regex.IsMatch(text.Trim(), @"^(已停止|准备失败)：")) runFailed = true;
        var match = Regex.Match(text, @"^安装系统：(.+)$");
        if (match.Success) { Text("IsoInfo", "识别结果：" + match.Groups[1].Value); C<TextBlock>("IsoNameText").ToolTip = IsoPath + Environment.NewLine + C<TextBlock>("IsoInfo").Text; }
        match = Regex.Match(text, @"^完成：([A-Z]):\\，(FAT32|NTFS)，全容量");
        if (match.Success && Disk != null) SetDiskMetadata(String.Format("磁盘 {0} · {1:N1} GB · {2}:\\ · 已重建 1 个分区", Disk.Number, Disk.Size / 1073741824.0, match.Groups[1].Value));
    }

    internal void UpdateProgress(string text)
    {
        text = (text ?? "").Trim();
        var match = Regex.Match(text, @"^\[(\d+)/6\]\s*(.*)$");
        if (match.Success)
        {
            SetStage(Int32.Parse(match.Groups[1].Value), false); Text("PhaseText", match.Groups[2].Value);
            C<ProgressBar>("MainProgress").Value = 0; C<ProgressBar>("MainProgress").IsIndeterminate = true;
            Text("PercentText", ""); Text("ProgressLabel", "当前操作"); Text("FileText", "等待当前文件进度…"); C<ProgressBar>("FileProgress").Value = 0; Text("ElapsedText", "");
        }
        else if ((match = Regex.Match(text, @"^>\s*(.+)$")).Success)
        {
            Text("PhaseText", match.Groups[1].Value); C<ProgressBar>("MainProgress").Value = 0; C<ProgressBar>("MainProgress").IsIndeterminate = true;
            Text("PercentText", ""); Text("ProgressLabel", text.Contains("复制") ? "复制整体" : "当前操作");
            C<ProgressBar>("FileProgress").Value = 0; Text("FileText", "等待当前文件进度…"); Text("ElapsedText", "");
        }
        match = Regex.Match(text, @"(?:整体约\s*)?(\d+(?:\.\d+)?)%");
        if (match.Success)
        {
            double value = Math.Min(100, Double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture));
            Text("PercentText", value.ToString("0.0", CultureInfo.CurrentCulture) + "%"); C<ProgressBar>("MainProgress").Value = value; C<ProgressBar>("MainProgress").IsIndeterminate = false;
        }
        match = Regex.Match(text, @"当前\s+(\d+(?:\.\d+)?)%\s+(.+)$");
        if (match.Success) { C<ProgressBar>("FileProgress").Value = Math.Min(100, Double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture)); Text("FileText", "当前文件：" + match.Groups[2].Value + " · " + match.Groups[1].Value + "%"); }
        match = Regex.Match(text, @"用时\s+(\d+:\d+)"); if (match.Success) Text("ElapsedText", "用时 " + match.Groups[1].Value);
        if (text.StartsWith("复制", StringComparison.Ordinal)) Text("ProgressLabel", "复制整体");
    }

    internal void HandlePrompt(WorkerMessage request)
    {
        string prompt = request.Text, reply = ""; bool confirmed;
        if (request.Index == 4 && prompt.Contains("编号"))
        {
            string details = String.Format("目标磁盘：磁盘 {0}  |  {1}  |  {2:N1} GB\n文件系统：{3}\n\nISO 文件：\n{4}\n\n格式化后会删除这支 U 盘的所有旧分区（包括 EFI、隐藏分区和微 PE 分区），并建立一个占用全容量的 {3} 主分区。\n\n请确认已备份，并再次核对目标磁盘编号、设备名称和容量。", Disk.Number, Disk.FriendlyName, Disk.Size / 1073741824.0, FileSystem, IsoPath);
            if (S("InstallName").Length > 0) details = "安装系统：" + S("InstallName") + "\n卷标：" + S("VolumeLabel") + "\n\n" + details;
            if (FileSystem == "NTFS") details += "\n\nNTFS 启动兼容性：部分 UEFI 固件可能不支持直接从 NTFS 启动。不确定时请取消并改选 FAT32。";
            reply = CreateFormatDialog(details).ShowDialog() == true ? "1" : "2";
        }
        else if (prompt.Contains("安全弹出 U 盘"))
        {
            var letter = Regex.Match(prompt, @"([A-Z]):");
            string details = (letter.Success ? "目标盘符：" + letter.Groups[1].Value + ":\\\n" : "") + "设备：" + Disk.FriendlyName;
            confirmed = Dialog("安全弹出 U 盘", "制作已完成", "现在尝试安全弹出目标 U 盘吗？", details, "选择“保持连接”可稍后手动弹出。", "Info", "安全弹出", "保持连接");
            reply = confirmed ? "" : "Q";
        }
        else if (prompt.Contains("弹出已识别的 ISO"))
            reply = Dialog("弹出 Windows ISO", "释放安装镜像", "是否弹出本次制作使用的 Windows ISO？", IsoPath, "选择“保留挂载”可继续使用此镜像。", "Info", "弹出 ISO", "保留挂载") ? "" : "Q";
        else if (prompt.Contains("清理") && prompt.Contains("保留临时文件"))
            reply = Dialog("清理临时文件", "释放本次制作的临时空间", "是否删除本次生成的临时 SWM 文件？", "", "这不会影响已制作完成的 U 盘。", "Info", "清理文件", "保留文件") ? "" : "Q";
        else if (prompt != "按回车执行本步；输入 Q 退出" && prompt != "按回车执行本机拆分；输入 Q 退出" && prompt != "按回车跳过拆分并开始复制；输入 Q 退出")
        {
            AddLog("已停止：无法识别交互请求，未自动确认：" + prompt); request.Reply("Q", true); return;
        }
        request.Reply(reply, false);
    }

    internal void StartEngine()
    {
        if (preview || busy) return;
        if (Disk == null || String.IsNullOrWhiteSpace(IsoPath) || !File.Exists(IsoPath)) { Dialog("无法开始制作", "请重新检查所选文件和设备", "ISO 文件不存在、无法访问，或尚未选择 U 盘。请重新选择后再开始。", "", "", "Warning"); return; }
        if (!String.Equals(Path.GetExtension(IsoPath), ".iso", StringComparison.OrdinalIgnoreCase)) { Dialog("安装镜像", "文件类型不匹配", "请选择 Windows ISO 映像文件。", "支持的文件扩展名：.iso", "", "Warning", "知道了"); return; }
        busy = true; runFailed = false; SetRunLayout(true); SetStage(1, false);
        engineLog.Clear(); C<TextBox>("LogBox").Clear();
        C<ProgressBar>("MainProgress").Value = 0; C<ProgressBar>("MainProgress").IsIndeterminate = true;
        Text("PercentText", ""); Text("PhaseText", "正在准备"); Text("LogStatus", "制作中"); Text("FileText", "正在检查安装镜像…");
        C<ProgressBar>("FileProgress").Value = 0; Text("ElapsedText", ""); Text("FooterText", "正在准备 ISO；请保持镜像文件和 U 盘可用。");
        AddLog("正在启动后台任务，准备挂载并识别 ISO…"); C<Button>("ForceStopButton").IsEnabled = true;
        try
        {
            workflow = WorkflowFactory(); workflow.Start(IsoPath, Disk, FileSystem, typeof(NativeWindow).Assembly.Location); result = workflow.State;
        }
        catch (Exception ex)
        {
            result = new Hashtable { { "ExitCode", 1 }, { "LastError", ex.Message }, { "CopyVerified", false }, { "TargetRebuildStarted", false } };
            AddLog("准备失败：" + ex.Message); workflow = null; busy = false; SetRunLayout(false); ShowResult();
        }
    }

    private void Receive(int limit)
    {
        foreach (var message in workflow.Drain(limit))
        {
            if (message.Kind == "Prompt")
            {
                try { HandlePrompt(message); }
                catch (Exception ex) { try { message.Reply("Q", true); } catch { } AddLog("已停止：处理交互请求失败：" + ex.Message); }
            }
            else if (message.Kind == "Log") { AddLog(message.Text); UpdateProgress(message.Text); }
            else if (message.Kind == "Inline") { Text("LogStatus", "实时更新"); UpdateProgress(message.Text); }
        }
    }

    internal void Tick()
    {
        if (processingTick || workflow == null || !busy) return;
        processingTick = true;
        try
        {
            foreach (var progress in workflow.ReadProgress())
            {
                if (progress.ParentActivityId < 0)
                {
                    C<ProgressBar>("MainProgress").IsIndeterminate = progress.Percent < 0;
                    if (progress.Percent >= 0) { C<ProgressBar>("MainProgress").Value = progress.Percent; Text("PercentText", progress.Percent + "%"); } else Text("PercentText", "");
                    if (!String.IsNullOrWhiteSpace(progress.Activity)) Text("PhaseText", progress.Activity);
                    Text("ProgressLabel", (progress.Activity ?? "").Contains("复制") ? "复制整体" : "当前操作");
                    if (!String.IsNullOrWhiteSpace(progress.Status)) UpdateProgress(progress.Status);
                }
                else
                {
                    if (progress.Percent >= 0) C<ProgressBar>("FileProgress").Value = progress.Percent;
                    var match = Regex.Match(progress.Status ?? "", @"([\d.]+)%\s*\|\s*(.+)$");
                    if (match.Success) Text("FileText", "当前文件：" + match.Groups[2].Value + " · " + match.Groups[1].Value + "%");
                }
                Text("LogStatus", "实时更新");
            }
            Receive(500);
            if (workflow.IsCompleted)
            {
                foreach (string record in workflow.Complete()) AddLog(record);
                Receive(Int32.MaxValue); // EndInvoke is the producer-completion barrier.
                workflow.Dispose(); workflow = null; busy = false; SetRunLayout(false); ShowResult();
            }
        }
        catch (Exception ex)
        {
            // Request cancellation before returning control if queue/stream handling fails.
            AddLog("已停止：后台通信失败：" + ex.Message);
            if (workflow != null) workflow.RequestStop();
            Text("PhaseText", "后台通信失败，请强制结束");
        }
        finally { processingTick = false; }
    }

    internal bool ResultSucceeded { get { return result != null && result["ExitCode"] != null && Convert.ToInt32(result["ExitCode"]) == 0 && B("CopyVerified") && !runFailed; } }

    private void ShowResult()
    {
        string log = "";
        try
        {
            string directory = S("WorkRoot");
            if (directory.Length == 0) directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Win11Usb-Logs", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory); log = Path.Combine(directory, "session.log");
            File.WriteAllText(log, C<TextBox>("LogBox").Text, new UTF8Encoding(true));
        }
        catch { log = ""; }
        C<ProgressBar>("MainProgress").IsIndeterminate = false; Text("LogStatus", "已结束");
        Text("FooterText", log.Length > 0 ? "日志已保存：" + log : "日志保留在窗口中，可选中并复制用于排查。");
        if (ResultSucceeded)
        {
            Text("PhaseText", "制作完成"); SetStage(6, true); Text("ProgressLabel", "制作结果"); Text("PercentText", "已完成"); C<ProgressBar>("MainProgress").Value = 100;
            string details = "设备：" + Disk.FriendlyName + "\n文件系统：" + FileSystem + "\n系统：" + S("InstallName") + "\n卷标：" + S("VolumeLabel") + "\n\n" + S("UsbResult") + "\n" + S("IsoResult");
            Dialog("制作结果", "安装 U 盘已制作完成", "安装文件已复制并通过文件大小校验。安装时请在电脑启动菜单中选择 UEFI: U盘名称。", details, "日志已保留，可用于排查。", "Info", "完成");
        }
        else
        {
            bool cancelled = !B("TargetRebuildStarted") && Regex.IsMatch(S("LastError"), "已取消|交互请求已取消|已退出");
            string heading = cancelled ? "已取消制作" : B("CopyVerified") ? "复制已校验，后续步骤未完成" : "制作未完成，请查看日志";
            Text("PhaseText", heading); Text("ProgressLabel", "停止时的进度");
            string status = B("TargetRebuildStarted") ? "目标 U 盘已经进入重建流程，请保留日志并重新制作。" : "尚未清盘，目标 U 盘未被修改。";
            Dialog("制作结果", heading, B("CopyVerified") ? "文件复制与大小校验已通过，但后续步骤出现问题。" : status, S("LastError") + (log.Length > 0 ? "\n\n日志：" + log : ""), "请保留日志，核对后再重试。", cancelled ? "Info" : "Error", "知道了");
        }
    }

    private void StopWork(bool closing)
    {
        if (!busy) return;
        if (!Dialog(closing ? "关闭制作工具" : "强制结束制作", "任务仍在运行", "强制结束会中断当前操作并关闭程序。", "如果此时正在清盘、格式化或复制，U 盘内容可能不完整，需要重新制作。自动挂载的 ISO 可能保留，可稍后在资源管理器中弹出。", "默认继续运行，避免误中断。", "Warning", closing ? "强制结束并关闭" : "强制结束", "继续制作", false)) return;
        timer.Stop(); C<Button>("ForceStopButton").IsEnabled = false;
        AddLog("收到强制结束请求，正在停止当前命令…");
        NativeLifetime.ForceExit(workflow);
    }

    internal void ShowRunningPreview()
    {
        if (!preview || previewEmpty) return;
        SetRunLayout(true); SetStage(4, false); Text("PhaseText", "正在复制拆分映像"); Text("ProgressLabel", "复制整体"); Text("PercentText", "68.0%");
        C<ProgressBar>("MainProgress").IsIndeterminate = false; C<ProgressBar>("MainProgress").Value = 68;
        Text("FileText", "当前文件：install2.swm · 36.9%"); C<ProgressBar>("FileProgress").Value = 36.9; Text("ElapsedText", "用时 04:32");
        C<TextBox>("LogBox").Clear(); SetDiskMetadata(@"磁盘 3 · 57.8 GB · F:\ · 已重建 1 个分区（示例）");
        foreach (string line in new[] { "[预览] 以下为模拟制作状态，不执行磁盘操作。", "[检查] Windows ISO 已就绪", "[检查] 已识别 Windows 11 教育版 26H2", "[检查] 目标磁盘身份核验通过", "[完成] 已清除旧分区表", "[完成] 已创建全容量主分区", "[完成] " + FileSystem + " 格式化，单分区 57.75 GB", "[检查] install.wim 大小 7.48 GiB，需要拆分", "[完成] 已生成 2 个 SWM 文件", "[复制] 安装文件已完成", "[复制] install.swm · 100.0%", "[复制] install2.swm · 36.9%" }) C<TextBox>("LogBox").AppendText(line + Environment.NewLine);
        C<TextBox>("LogBox").ScrollToEnd(); Text("LogStatus", "模拟进度"); Text("FooterText", "预览模式 · 请保持 U 盘连接，完成后将校验复制结果。"); C<Button>("ForceStopButton").IsEnabled = true;
    }

    internal void StopPreview()
    {
        SetRunLayout(false); RefreshDevices(); SetStage(0, false); Text("PhaseText", "准备就绪"); Text("ProgressLabel", "当前操作"); Text("PercentText", "0.0%");
        C<ProgressBar>("MainProgress").Value = 0; C<ProgressBar>("FileProgress").Value = 0; Text("FileText", "等待开始制作"); Text("ElapsedText", ""); Text("LogStatus", "预览已结束"); Text("FooterText", "预览模式 · 不会修改磁盘。");
    }
}

internal static class NativeLifetime
{
    internal static void ForceExit(IWorkflow workflow)
    {
        try { if (workflow != null) workflow.RequestStop(); }
        finally { Environment.Exit(2); }
    }
}
