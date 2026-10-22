using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Management;
using System.Reflection;
using System.Text;

internal static class AppResources
{
    internal static byte[] ReadBytes(string name)
    {
        using (Stream input = typeof(AppResources).Assembly.GetManifestResourceStream(name))
        {
            if (input == null) throw new InvalidOperationException("缺少内置文件：" + name);
            using (var output = new MemoryStream()) { input.CopyTo(output); return output.ToArray(); }
        }
    }
    internal static string ReadText(string name)
    {
        using (var reader = new StreamReader(new MemoryStream(ReadBytes(name)), Encoding.UTF8, true)) return reader.ReadToEnd();
    }
}

internal sealed class DiskItem
{
    public int Number { get; set; }
    public string FriendlyName { get; set; }
    public long Size { get; set; }
    public string Path { get; set; }
    public string SerialNumber { get; set; }
    public string PnpId { get; set; }
    public int PartitionCount { get; set; }
    public string Metadata { get; set; }
    public string Display { get; set; }
    internal DiskItem Copy() { return (DiskItem)MemberwiseClone(); }
}

internal static class DiskReader
{
    internal static List<DiskItem> Read()
    {
        var result = new List<DiskItem>();
        var scope = new ManagementScope(@"\\.\root\Microsoft\Windows\Storage");
        var options = new EnumerationOptions { ReturnImmediately = false, Timeout = TimeSpan.FromSeconds(15) };
        using (var search = new ManagementObjectSearcher(scope, new ObjectQuery("SELECT * FROM MSFT_Disk WHERE BusType = 7"), options))
        using (var disks = search.Get())
        foreach (ManagementObject disk in disks)
        using (disk)
        {
            if (Convert.ToBoolean(disk["IsBoot"]) || Convert.ToBoolean(disk["IsSystem"]) || Convert.ToBoolean(disk["IsOffline"]) || Convert.ToBoolean(disk["IsReadOnly"])) continue;
            long size = Convert.ToInt64(disk["Size"]);
            if (size < 8L * 1024 * 1024 * 1024) continue;
            int number = Convert.ToInt32(disk["Number"]);
            string pnp = "";
            int devices = 0;
            using (var deviceQuery = new ManagementObjectSearcher(new ManagementScope(@"\\.\root\cimv2"), new ObjectQuery("SELECT PNPDeviceID FROM Win32_DiskDrive WHERE Index = " + number), options))
            using (var physical = deviceQuery.Get())
            foreach (ManagementObject item in physical)
            using (item) { devices++; pnp = Convert.ToString(item["PNPDeviceID"]); }
            if (devices != 1 || String.IsNullOrWhiteSpace(pnp)) continue;
            var letters = new List<string>();
            int count = 0;
            using (var partQuery = new ManagementObjectSearcher(scope, new ObjectQuery("SELECT DriveLetter FROM MSFT_Partition WHERE DiskNumber = " + number), options))
            using (var partitions = partQuery.Get())
            foreach (ManagementObject partition in partitions)
            using (partition)
            {
                count++;
                string letter = Convert.ToString(partition["DriveLetter"]);
                if (letter.Length == 1 && letter[0] >= 'A' && letter[0] <= 'Z') letters.Add(letter + @":\");
            }
            string name = Convert.ToString(disk["FriendlyName"]);
            result.Add(new DiskItem {
                Number = number, FriendlyName = name, Size = size, Path = Convert.ToString(disk["Path"]),
                SerialNumber = Convert.ToString(disk["SerialNumber"]), PnpId = pnp, PartitionCount = count,
                Metadata = String.Format("磁盘 {0} · {1:N1} GB · {2} · 当前 {3} 个分区", number, size / 1073741824.0, letters.Count == 0 ? "未分配盘符" : String.Join(" / ", letters), count),
                Display = String.Format("整盘：磁盘 {0}  |  {1}  |  总容量 {2:N1} GB  |  当前 {3} 个分区", number, name, size / 1073741824.0, count)
            });
        }
        result.Sort((a, b) => a.Number.CompareTo(b.Number));
        return result;
    }
}

internal sealed class WorkerMessage
{
    internal string Kind;
    internal string Text;
    internal int Index;
    internal Action<string, bool> Reply;
}

internal sealed class WorkerProgress
{
    internal int ParentActivityId;
    internal int Percent;
    internal string Activity;
    internal string Status;
}

internal interface IWorkflow : IDisposable
{
    Hashtable State { get; }
    bool IsCompleted { get; }
    void Start(string isoPath, DiskItem disk, string fileSystem, string launcherPath);
    IEnumerable<WorkerMessage> Drain(int limit);
    IEnumerable<WorkerProgress> ReadProgress();
    IEnumerable<string> Complete();
    void RequestStop();
}
