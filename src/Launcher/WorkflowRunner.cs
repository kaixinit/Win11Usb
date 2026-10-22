using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.Threading;
using System.Threading.Tasks;

// Created after explicit Start confirmation. No AMSI, logging, language-mode or
// execution-policy overrides are applied to the standard PowerShell host.
internal sealed class WorkflowRunner : IWorkflow
{
    private readonly string workerScript;
    private readonly string engineScript;
    private readonly BlockingCollection<object> queue = new BlockingCollection<object>();
    private Runspace runspace;
    private PowerShell shell;
    private IAsyncResult handle;
    private Task startup;
    public Hashtable State { get; private set; }
    public bool IsCompleted { get { return startup != null && startup.IsCompleted && (handle == null || handle.IsCompleted); } }

    internal WorkflowRunner() : this(AppResources.ReadText("Win11Usb.Worker.ps1"), AppResources.ReadText("Win11Usb.Engine.ps1")) { }
    internal WorkflowRunner(string worker, string engine) { workerScript = worker; engineScript = engine; }

    public void Start(string isoPath, DiskItem disk, string fileSystem, string launcherPath)
    {
        State = Hashtable.Synchronized(new Hashtable {
            { "ActiveProcess", null }, { "MountedByApp", false }, { "ForceStopRequested", false },
            { "IsoPath", isoPath }, { "ExitCode", null }, { "CopyVerified", false },
            { "TargetRebuildStarted", false }, { "LastError", "" }
        });
        DiskItem snapshot = disk.Copy();
        startup = Task.Factory.StartNew(() => Initialize(isoPath, snapshot, fileSystem, launcherPath));
    }

    private void Initialize(string isoPath, DiskItem disk, string fileSystem, string launcherPath)
    {
        try
        {
            runspace = RunspaceFactory.CreateRunspace(InitialSessionState.CreateDefault());
            runspace.ApartmentState = ApartmentState.STA;
            runspace.ThreadOptions = PSThreadOptions.ReuseThread;
            runspace.Open();
            if (Convert.ToBoolean(State["ForceStopRequested"])) return;
            runspace.SessionStateProxy.SetVariable("GuiPromptQueue", queue);
            runspace.SessionStateProxy.SetVariable("GuiEngineState", State);
            runspace.SessionStateProxy.SetVariable("GuiIsoPath", isoPath);
            runspace.SessionStateProxy.SetVariable("GuiEngineScript", engineScript);
            runspace.SessionStateProxy.SetVariable("GuiLauncherPath", launcherPath);
            runspace.SessionStateProxy.SetVariable("GuiDiskNumber", disk.Number.ToString());
            runspace.SessionStateProxy.SetVariable("GuiFileSystem", fileSystem);
            runspace.SessionStateProxy.SetVariable("GuiDiskSnapshot", disk.Copy());
            shell = PowerShell.Create();
            shell.Runspace = runspace;
            shell.AddScript(workerScript);
            handle = shell.BeginInvoke();
        }
        catch (Exception ex)
        {
            State["ExitCode"] = 1; State["LastError"] = ex.Message;
            throw;
        }
    }

    private static object Property(object item, string name)
    {
        PSPropertyInfo property = PSObject.AsPSObject(item).Properties[name];
        return property == null ? null : property.Value;
    }

    public IEnumerable<WorkerMessage> Drain(int limit)
    {
        var messages = new List<WorkerMessage>();
        object item;
        while (messages.Count < limit && queue.TryTake(out item))
        {
            object request = item;
            string kind = Convert.ToString(Property(item, "Kind"));
            var message = new WorkerMessage { Kind = kind, Text = Convert.ToString(Property(item, kind == "Prompt" ? "Prompt" : "Text")) };
            if (kind == "Prompt")
            {
                message.Index = Convert.ToInt32(Property(item, "Index"));
                message.Reply = (response, cancelled) => {
                    var wrapped = PSObject.AsPSObject(request);
                    wrapped.Properties["Response"].Value = response;
                    wrapped.Properties["Cancelled"].Value = cancelled;
                    ((ManualResetEvent)wrapped.Properties["Ready"].Value).Set();
                };
            }
            messages.Add(message);
        }
        return messages;
    }

    public IEnumerable<WorkerProgress> ReadProgress()
    {
        var result = new List<WorkerProgress>();
        if (startup == null || !startup.IsCompleted || shell == null) return result;
        foreach (var progress in shell.Streams.Progress.ReadAll())
            if (progress.RecordType != ProgressRecordType.Completed)
                result.Add(new WorkerProgress { ParentActivityId = progress.ParentActivityId, Percent = progress.PercentComplete, Activity = progress.Activity, Status = progress.StatusDescription });
        return result;
    }

    public IEnumerable<string> Complete()
    {
        var result = new List<string>();
        try
        {
            if (startup != null) startup.GetAwaiter().GetResult();
            if (handle == null) return result;
            foreach (var output in shell.EndInvoke(handle)) if (output != null) result.Add(output.ToString());
            if (shell.InvocationStateInfo.State == PSInvocationState.Failed) throw shell.InvocationStateInfo.Reason;
        }
        catch (Exception ex) { State["ExitCode"] = 1; State["LastError"] = ex.Message; result.Add("已停止：" + ex.Message); }
        return result;
    }

    private void StopChild()
    {
        var child = State == null ? null : State["ActiveProcess"] as Process;
        if (child == null) return;
        try { if (!child.HasExited) { child.Kill(); child.WaitForExit(1500); } } catch { }
    }

    public void RequestStop()
    {
        if (State != null) State["ForceStopRequested"] = true;
        StopChild();
        foreach (var message in Drain(Int32.MaxValue)) if (message.Reply != null) { try { message.Reply("Q", true); } catch { } }
        if (shell != null)
        {
            try { var stop = shell.BeginStop(null, null); stop.AsyncWaitHandle.WaitOne(1800); } catch { }
        }
        StopChild();
        // Forced exit must not block in Runspace.Dispose on a storage operation.
    }

    public void Dispose()
    {
        if (shell != null) { shell.Dispose(); shell = null; }
        if (runspace != null) { runspace.Close(); runspace.Dispose(); runspace = null; }
        if (!queue.IsAddingCompleted) queue.CompleteAdding();
        queue.Dispose();
    }
}
