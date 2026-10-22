using System;
using System.IO;
using System.Reflection;
using System.Windows;

[assembly: AssemblyTitle("Windows 安装 U 盘制作工具")]
[assembly: AssemblyProduct("Win11Usb")]
[assembly: AssemblyVersion("0.1.0.0")]
[assembly: AssemblyFileVersion("0.1.0.3")]
[assembly: AssemblyInformationalVersion("0.1.0-beta.3")]

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (HasArgument(args, "--self-check"))
            {
                foreach (string name in new[] { "MainWindow.xaml", "MessageDialog.xaml", "FormatDialog.xaml", "Win11Usb.Worker.ps1", "Win11Usb.Engine.ps1", "Win11Usb.ico" })
                    AppResources.ReadBytes(name);
                return 0;
            }
            bool running = HasArgument(args, "--preview-running");
            bool empty = HasArgument(args, "--preview-empty");
            bool preview = HasArgument(args, "--preview") || running || empty;
            var app = new Application();
            var ui = new NativeWindow(preview, running, empty);
            app.Run(ui.Window);
            return 0;
        }
        catch (Exception ex)
        {
            string log = StartupLog.Write(ex);
            MessageBox.Show("启动失败：\n" + ex.Message + (log.Length == 0 ? "" : "\n\n诊断日志：" + log), "Windows 安装 U 盘", MessageBoxButton.OK, MessageBoxImage.Error);
            return 1;
        }
    }
    private static bool HasArgument(string[] args, string expected)
    {
        return Array.Exists(args, a => String.Equals(a, expected, StringComparison.OrdinalIgnoreCase));
    }
}

internal static class StartupLog
{
    internal static string Write(Exception error)
    {
        try
        {
            string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Win11Usb-Logs");
            Directory.CreateDirectory(directory);
            string path = Path.Combine(directory, "startup-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N") + ".log");
            File.WriteAllText(path, "Win11Usb 0.1.0-beta.3\r\n" + error, new System.Text.UTF8Encoding(true));
            return path;
        }
        catch { return ""; }
    }
}
