using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using System.Text;
using System.Xml;

internal static class InspectResources
{
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] private static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] private static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")] private static extern bool FreeLibrary(IntPtr module);
    private static int Main(string[] args)
    {
        try
        {
            var assembly = Assembly.ReflectionOnlyLoadFrom(Path.GetFullPath(args[0]));
            foreach (string name in new[] { "MainWindow.xaml", "MessageDialog.xaml", "FormatDialog.xaml", "Win11Usb.Worker.ps1", "Win11Usb.Engine.ps1", "Win11Usb.ico" })
            using (var stream = assembly.GetManifestResourceStream(name))
            {
                if (stream == null || stream.Length == 0) throw new InvalidDataException("Missing resource: " + name);
                using (var hash = SHA256.Create()) Console.WriteLine(name + " | " + BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", ""));
            }
            if (assembly.GetManifestResourceStream("Win11Usb.Gui.ps1") != null) throw new InvalidDataException("Legacy scripted UI must not be embedded.");
            // Map the PE as data only: do not execute its entry point or request UAC.
            IntPtr module = LoadLibraryEx(Path.GetFullPath(args[0]), IntPtr.Zero, 0x02 | 0x20);
            if (module == IntPtr.Zero) throw new InvalidDataException("Cannot map PE resources.");
            try
            {
                IntPtr resource = FindResource(module, new IntPtr(1), new IntPtr(24));
                if (resource == IntPtr.Zero) throw new InvalidDataException("Native application manifest missing.");
                byte[] bytes = new byte[SizeofResource(module, resource)];
                Marshal.Copy(LockResource(LoadResource(module, resource)), bytes, 0, bytes.Length);
                var xml = new XmlDocument(); xml.LoadXml(Encoding.UTF8.GetString(bytes).TrimEnd('\0'));
                var level = xml.SelectSingleNode("//*[local-name()='requestedExecutionLevel']");
                if (level == null || level.Attributes["level"].Value != "requireAdministrator" || level.Attributes["uiAccess"].Value != "false") throw new InvalidDataException("Expected standard requireAdministrator manifest.");
                Console.WriteLine("PE manifest verified: requireAdministrator; uiAccess=false.");
            }
            finally { FreeLibrary(module); }
            Console.WriteLine("Resource inspection passed without executing the application.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
