using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

// Integration of the production C# bridge, original worker/engine, native prompt
// handlers and result presentation. Storage/native-command boundaries are mocks
// extracted from Test-Workflow.ps1; this test never accesses a physical disk.
internal static class NativeWorkflowChecks
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static string Quote(string value) { return "'" + value.Replace("'", "''") + "'"; }

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            string worker = File.ReadAllText(args[0]), engine = File.ReadAllText(args[1]), initialize = File.ReadAllText(args[2]);
            string[] cases = { "DirectFAT32", "SplitFAT32", "SplitNTFS", "CancelFormat", "CorruptCopy", "CopyFailure", "MissingBoot", "BadImage", "MountFailure", "AlreadyMounted", "ExistingMountFailure", "EjectVeto", "KeepMedia", "KeepSwm", "CleanupFailure", "NoMatchingIso", "UnknownPrompt" };
            foreach (string name in cases) RunCase(name, worker, engine, initialize, Path.Combine(args[3], name));
            Console.WriteLine("PASS: 17 native workflow scenarios; no physical disk, ISO mount or real copy.");
            app.Shutdown();
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }

    private static void RunCase(string name, string worker, string engine, string initialize, string root)
    {
        Directory.CreateDirectory(root);
        string filesystem = name == "SplitNTFS" ? "NTFS" : "FAT32";
        var ui = new NativeWindow(true, false, false);
        new System.Windows.Interop.WindowInteropHelper(ui.Window).EnsureHandle();
        var disk = new DiskItem { Number = 4, FriendlyName = "Fixture USB", Size = 17179869184, Path = "FIXTURE-PATH", SerialNumber = "FIXTURE-SERIAL", PnpId = "USB-FIXTURE", Metadata = "磁盘 4 · 模拟设备", Display = "Fixture USB · 16 GiB" };
        ui.C<ComboBox>("DiskCombo").Items.Clear(); ui.C<ComboBox>("DiskCombo").Items.Add(disk); ui.C<ComboBox>("DiskCombo").SelectedIndex = 0;
        ui.C<RadioButton>(filesystem == "NTFS" ? "NtfsOption" : "Fat32Option").IsChecked = true;
        ui.C<TextBox>("IsoPathBox").Text = Path.Combine(root, "windows_11_25h2.iso");
        ui.SyncSelection();
        var logs = new List<string>(); int formatPrompts = 0;
        string script = "$global:FixtureRoot=" + Quote(root) + ";$global:Case=" + Quote(name) + ";\r\n" + initialize + "\r\n" + worker;
        using (var runner = new WorkflowRunner(script, engine))
        try
        {
            runner.Start(Path.Combine(root, "windows_11_25h2.iso"), disk, filesystem, "fixture.exe");
            typeof(NativeWindow).GetField("result", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(ui, runner.State);
            var clock = Stopwatch.StartNew();
            while (!runner.IsCompleted)
            {
                Check(clock.Elapsed.TotalSeconds < 15, "Native workflow timed out: " + name);
                Receive(ui, runner, name, filesystem, logs, ref formatPrompts);
                foreach (var progress in runner.ReadProgress()) if (!String.IsNullOrWhiteSpace(progress.Status)) ui.UpdateProgress(progress.Status);
                Thread.Sleep(5);
            }
            foreach (string record in runner.Complete()) { logs.Add(record); ui.AddLog(record); }
            Receive(ui, runner, name, filesystem, logs, ref formatPrompts);
            bool failed = Array.IndexOf(new[] { "CancelFormat", "CorruptCopy", "CopyFailure", "MissingBoot", "BadImage", "MountFailure", "ExistingMountFailure", "CleanupFailure", "NoMatchingIso", "UnknownPrompt" }, name) >= 0;
            Check(runner.State["ExitCode"] != null && Convert.ToInt32(runner.State["ExitCode"]) == (failed ? 1 : 0), name + ": unexpected result: " + runner.State["LastError"]);
            Check(ui.ResultSucceeded == !failed, name + ": native UI falsely classified outcome");
            if (Array.IndexOf(new[] { "CancelFormat", "MissingBoot", "BadImage", "MountFailure", "ExistingMountFailure", "NoMatchingIso" }, name) >= 0)
                Check(!Convert.ToBoolean(runner.State["TargetRebuildStarted"]), name + ": failed preflight reached rebuild");
            if (!failed) Check(formatPrompts == 1 && String.Join("\n", logs).Contains("复制文件大小一致"), name + ": final verification or format confirmation missing");
            if (name == "CancelFormat") Check(formatPrompts == 1 && !Convert.ToBoolean(runner.State["CopyVerified"]), "Cancel format still copied files");
            if (name == "EjectVeto") Check(Convert.ToString(runner.State["UsbResult"]).Contains("尚未安全弹出"), "Eject warning lost");
            ScheduleDialog(ui, dialog => {
                Check(dialog.Title == "制作结果", "Wrong final result dialog");
                string heading = ((TextBlock)dialog.FindName("HeadingText")).Text;
                Check((heading == "安装 U 盘已制作完成") == !failed, name + ": wrong final heading");
                dialog.DialogResult = true;
            });
            typeof(NativeWindow).GetMethod("ShowResult", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(ui, null);
            Console.WriteLine("PASS: native workflow " + name);
        }
        finally
        {
            if (!runner.IsCompleted) runner.RequestStop();
            ui.Window.Close();
        }
    }

    private static void Receive(NativeWindow ui, IWorkflow runner, string name, string filesystem, List<string> logs, ref int formatPrompts)
    {
        foreach (var message in runner.Drain(Int32.MaxValue))
        {
            if (message.Kind == "Prompt")
            {
                bool format = message.Index == 4 && message.Text.Contains("编号");
                bool eject = message.Text.Contains("安全弹出 U 盘") || message.Text.Contains("弹出已识别的 ISO");
                bool cleanup = message.Text.Contains("清理") && message.Text.Contains("保留临时文件");
                if (format) formatPrompts++;
                if (format || eject || cleanup) ScheduleDialog(ui, dialog => {
                    if (format)
                    {
                        Check(dialog.Title == "最后确认：整盘格式化", "Wrong destructive dialog");
                        Check(((Button)dialog.FindName("CancelFormatButton")).IsDefault, "Format confirmation default changed");
                        Check((((TextBlock)dialog.FindName("FormatCompatibility")).Visibility == Visibility.Visible) == (filesystem == "NTFS"), "NTFS compatibility visibility wrong");
                        string details = ((TextBlock)dialog.FindName("ConfirmDetails")).Text;
                        Check(details.Contains("磁盘 4") && details.Contains(filesystem), "Format dialog target lost");
                    }
                    bool accept = !(format && name == "CancelFormat") && !(eject && name == "KeepMedia") && !(cleanup && name == "KeepSwm");
                    dialog.DialogResult = accept;
                });
                try { ui.HandlePrompt(message); }
                catch { message.Reply("Q", true); throw; }
            }
            else { logs.Add(message.Text); if (message.Kind == "Log") ui.AddLog(message.Text); ui.UpdateProgress(message.Text); }
        }
    }

    private static void ScheduleDialog(NativeWindow ui, Action<Window> action)
    {
        ui.Window.Dispatcher.BeginInvoke(DispatcherPriority.Normal, new Action(() => {
            Check(ui.Window.OwnedWindows.Count == 1, "Expected one owned modal dialog");
            action(ui.Window.OwnedWindows[0]);
        }));
    }
}
