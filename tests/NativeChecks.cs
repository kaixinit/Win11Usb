using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Windows;
using System.Windows.Controls;

internal static class NativeChecks
{
    private static void Check(bool ok, string text) { if (!ok) throw new Exception(text); }
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length > 0 && args[0] == "--sleep-child") { Thread.Sleep(30000); return 0; }
            if (args.Length > 1 && args[0] == "--force-exit-fixture")
            {
                var runner = new WorkflowRunner("Start-Sleep -Seconds 300", "unused fixture");
                runner.Start("fixture.iso", new DiskItem { Number = 4 }, "NTFS", "fixture.exe");
                var child = Process.Start(new ProcessStartInfo(typeof(NativeChecks).Assembly.Location, "--sleep-child") { UseShellExecute = false, CreateNoWindow = true });
                runner.State["ActiveProcess"] = child;
                File.WriteAllText(args[1], child.Id.ToString());
                NativeLifetime.ForceExit(runner);
                return 9;
            }
            if (args.Length > 0 && args[0] == "--show-preview")
            {
                var app = new Application();
                var ui = new NativeWindow(true, args.Length > 1 && args[1] == "running", args.Length > 1 && args[1] == "empty");
                if (args.Length > 2 && args[2] == "format") ui.Window.ContentRendered += (s, e) => {
                    ui.C<RadioButton>("NtfsOption").IsChecked = true;
                    ui.CreateFormatDialog("[安全预览] 此窗口没有制作后台，两个按钮都不会修改磁盘。\n\n设备：示例 U 盘 · 磁盘 3 · 57.8 GB\n文件系统：NTFS\n\nISO：D:\\Windows ISO\\zh-cn_windows_11_business_editions_version_26h2_x64_dvd_74d48a06.iso\n\n此处用于检查长文本换行、默认取消、按钮可见及 NTFS 兼容提示。").ShowDialog();
                };
                app.Run(ui.Window); return 0;
            }
            Check(!AutomationLoaded(), "Test host loaded PowerShell before UI construction");
            CheckUi();
            Check(!AutomationLoaded(), "Native startup/preview initialized PowerShell");
            Console.WriteLine("PASS native startup: WPF created, PowerShell not loaded; previews isolated");
            CheckBridge();
            Console.WriteLine("PASS native worker bridge: snapshot, filesystem, queue, progress, prompt response, tail logs, explicit outcome");
            CheckCancellation();
            Console.WriteLine("PASS native cancellation: bounded stop, prompt released, tracked child stopped");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }

    private static bool AutomationLoaded() { return AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "System.Management.Automation"); }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CheckUi()
    {
        var ui = new NativeWindow(true, false, false);
        new System.Windows.Interop.WindowInteropHelper(ui.Window).EnsureHandle();
        Check(ui.FileSystem == "FAT32", "Filesystem default changed");
        ui.C<RadioButton>("NtfsOption").IsChecked = true;
        Check(ui.FileSystem == "NTFS", "NTFS selection did not reach engine mapping");
        var format = ui.CreateFormatDialog(new String('长', 6000));
        Check(((TextBlock)format.FindName("FormatCompatibility")).Visibility == Visibility.Visible, "NTFS compatibility hidden");
        Check(((Button)format.FindName("CancelFormatButton")).IsDefault && !((Button)format.FindName("ConfirmFormatButton")).IsDefault, "Destructive default is not cancel");
        Check(((TextBlock)format.FindName("ConfirmDetails")).Text.Length == 6000, "Long confirmation truncated");
        ui.C<RadioButton>("Fat32Option").IsChecked = true;
        Check(ui.FileSystem == "FAT32", "FAT32 selection did not reach engine mapping");
        ui.UpdateProgress("[4/6] 复制安装文件和 SWM 到 U 盘 F:\\");
        ui.UpdateProgress("复制拆分映像 SWM [======] 整体约 68.0% 用时 04:32 | 当前 36.9% install2.swm");
        Check(ui.C<ProgressBar>("MainProgress").Value == 68 && ui.C<ProgressBar>("FileProgress").Value == 36.9, "Overall/file progress mixed");
        Check(ui.C<TextBlock>("ElapsedText").Text == "用时 04:32" && ui.C<TextBlock>("FileText").Text.Contains("install2.swm"), "Progress details lost");
        Check(Object.ReferenceEquals(ui.C<Image>("StageIcon3").Source, ui.Window.Resources["StepDoneImage"]), "Completed stage wrong");
        Check(Object.ReferenceEquals(ui.C<Image>("StageIcon4").Source, ui.Window.Resources["StepCurrentImage"]), "Current stage wrong");
        double width = ui.Window.Width, height = ui.Window.Height;
        ui.ShowRunningPreview();
        Check(!ui.C<Button>("StartButton").IsEnabled && !ui.C<ComboBox>("DiskCombo").IsEnabled && !ui.C<RadioButton>("Fat32Option").IsEnabled, "Running inputs not locked");
        Check(width == ui.Window.Width && height == ui.Window.Height, "Running state resized window");
        ui.WorkflowFactory = () => { throw new Exception("Preview reached worker factory"); };
        ui.StartEngine();
        ui.StopPreview();
        Check(ui.C<Button>("StartButton").IsEnabled && ui.C<ComboBox>("DiskCombo").IsEnabled, "Preview stop did not restore input");
        Check(ui.C<TextBox>("LogBox").Text.Contains("install2.swm"), "Stopping preview cleared logs");
        bool cancelled = false;
        ui.HandlePrompt(new WorkerMessage { Kind = "Prompt", Text = "UNKNOWN fixture", Index = 9, Reply = (reply, cancel) => { cancelled = reply == "Q" && cancel; } });
        Check(cancelled, "Unknown prompt was confirmed");
        var dialog = ui.CreateDialog("title", "heading", "message", "details", "hint", "Warning", "继续", "取消", false);
        Check(((Button)dialog.FindName("SecondaryButton")).IsDefault, "Non-default confirm changed");
        ui.Window.Close();
        var empty = new NativeWindow(true, false, true);
        empty.ShowRunningPreview();
        Check(empty.C<ComboBox>("DiskCombo").Items.Count == 0 && !empty.C<Button>("StartButton").IsEnabled, "Empty state allowed workflow");
        Check(empty.C<TextBlock>("DiskPlaceholder").Visibility == Visibility.Visible, "Empty placeholder hidden");
        empty.Window.Close();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CheckBridge()
    {
        string script = @"
$ErrorActionPreference='Stop'
if($global:GuiDiskSnapshot.Number -ne 4 -or $global:GuiDiskSnapshot.PnpId -cne 'USB-FIXTURE' -or $global:GuiFileSystem -cne 'NTFS'){throw 'Snapshot bridge mismatch'}
Write-Progress -Activity '复制文件' -Status '整体约 68.0% 用时 04:32' -PercentComplete 68
$request=[pscustomobject]@{Kind='Prompt';Index=4;Prompt='fixture';Response='';Cancelled=$false;Ready=[Threading.ManualResetEvent]::new($false)}
try {
 $global:GuiPromptQueue.Add($request)
 if(-not $request.Ready.WaitOne(10000)){throw 'Prompt timed out'}
 if($request.Cancelled -or $request.Response -cne '2'){throw 'Prompt bridge mismatch'}
}finally{$request.Ready.Dispose()}
1..1001 | ForEach-Object {$global:GuiPromptQueue.Add([pscustomobject]@{Kind='Log';Text=('tail-'+$_)})}
$global:GuiEngineState['CopyVerified']=$true
$global:GuiEngineState['ExitCode']=0
";
        using (var runner = new WorkflowRunner(script, "unused fixture"))
        {
            var disk = new DiskItem { Number = 4, FriendlyName = "Fixture", Size = 16000000000, PnpId = "USB-FIXTURE" };
            runner.Start("fixture.iso", disk, "NTFS", "fixture.exe");
            disk.PnpId = "CHANGED-AFTER-START";
            bool responded = false, progress = false; int lines = 0;
            var clock = Stopwatch.StartNew();
            while (!runner.IsCompleted && clock.Elapsed.TotalSeconds < 20)
            {
                foreach (var p in runner.ReadProgress()) if (p.Percent == 68 && p.ParentActivityId < 0) progress = true;
                foreach (var message in runner.Drain(500))
                {
                    if (message.Kind == "Prompt") { Check(message.Index == 4, "Prompt index wrong"); message.Reply("2", false); responded = true; }
                    if (message.Kind == "Log") lines++;
                }
                Thread.Sleep(10);
            }
            Check(runner.IsCompleted, "Native worker failed to finish");
            foreach (string output in runner.Complete()) Console.WriteLine(output);
            foreach (var p in runner.ReadProgress()) if (p.Percent == 68) progress = true;
            foreach (var message in runner.Drain(Int32.MaxValue)) if (message.Kind == "Log") lines++;
            Check(responded && progress && lines == 1001, "Queue/progress/tail lost");
            Check(runner.State["ExitCode"] != null && Convert.ToInt32(runner.State["ExitCode"]) == 0 && Convert.ToBoolean(runner.State["CopyVerified"]), "Explicit result not preserved: " + runner.State["LastError"]);
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CheckCancellation()
    {
        string script = @"
$request=[pscustomobject]@{Kind='Prompt';Index=8;Prompt='waiting';Response='';Cancelled=$false;Ready=[Threading.ManualResetEvent]::new($false)}
try {$global:GuiPromptQueue.Add($request);$null=$request.Ready.WaitOne()} finally {$request.Ready.Dispose()}
";
        using (var runner = new WorkflowRunner(script, "unused fixture"))
        {
            runner.Start("fixture.iso", new DiskItem { Number = 4 }, "FAT32", "fixture.exe");
            using (var child = Process.Start(new ProcessStartInfo(typeof(NativeChecks).Assembly.Location, "--sleep-child") { UseShellExecute = false, CreateNoWindow = true }))
            try
            {
            runner.State["ActiveProcess"] = child;
            var clock = Stopwatch.StartNew();
            while (!runner.IsCompleted && clock.ElapsedMilliseconds < 300) Thread.Sleep(10);
            clock.Restart(); runner.RequestStop();
            Check(clock.Elapsed.TotalSeconds < 5, "Stop request blocked");
            Check(child.HasExited, "Tracked child survived cancellation");
            clock.Restart(); while (!runner.IsCompleted && clock.Elapsed.TotalSeconds < 5) Thread.Sleep(10);
            Check(runner.IsCompleted && Convert.ToBoolean(runner.State["ForceStopRequested"]), "Cancellation failed");
            foreach (var ignored in runner.Complete()) { }
            }
            finally { if (!child.HasExited) { child.Kill(); child.WaitForExit(); } }
        }
    }
}
