using System.IO;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace PhoneBridge.Desktop;

/// <summary>
/// Starts phonebridge_service.exe in --ui mode and talks to it with one JSON object per line:
///   engine -> app : {"event":"status",...} {"event":"ready"} {"event":"error","code":"..."}
///   app -> engine : {"cmd":"set","camera":true,"mic":false} {"cmd":"quit"}
/// Events are raised on background threads: the window must marshal them to the UI thread.
/// </summary>
/// <summary>One status line reported by the engine (see windows/service/control/control_protocol.h).</summary>
public sealed record EngineStatus(
    string State, uint Session, bool Camera, bool Mic, bool WantCamera, bool WantMic, double Fps, string Device);

public sealed class EngineClient : IDisposable
{
    private Process? _proc;
    private StreamWriter? _log;
    private long _logBytes;
    private readonly object _sendLock = new();

    public event Action<EngineStatus>? StatusReceived;
    public event Action<string>? ErrorReceived;
    public event Action<int>? Exited;
    /// <summary>Plain text lines from the engine (everything that is not a JSON message), stderr included.</summary>
    public event Action<string>? LogLine;

    public bool IsRunning
    {
        get { try { return _proc is { HasExited: false }; } catch { return false; } }
    }

    /// <summary>Where the engine's text output is saved (handy when something goes wrong).</summary>
    public static string LogPath { get; set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PhoneBridge", "engine.log");

    public bool Start(string fileName, string arguments = "--ui")
    {
        if (IsRunning) return true;
        try
        {
            OpenLog();
            var psi = new ProcessStartInfo(fileName, arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(fileName)) ?? Environment.CurrentDirectory,
            };
            var p = new Process { StartInfo = psi, EnableRaisingEvents = true };
            p.OutputDataReceived += (_, e) => { if (e.Data != null) HandleLine(e.Data); };
            p.ErrorDataReceived += (_, e) => { if (e.Data != null) { WriteLog("[err] " + e.Data); LogLine?.Invoke(e.Data); } };
            p.Exited += (_, _) => { int code = -1; try { code = p.ExitCode; } catch { } Exited?.Invoke(code); };
            if (!p.Start()) return false;
            p.StandardInput.AutoFlush = true;
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
            _proc = p;
            return true;
        }
        catch (Exception ex)
        {
            WriteLog("[start failed] " + ex.Message);
            return false;
        }
    }

    public void SetStreams(bool camera, bool mic) =>
        Send("{\"cmd\":\"set\",\"camera\":" + (camera ? "true" : "false") + ",\"mic\":" + (mic ? "true" : "false") + "}");

    /// <summary>Asks the engine to quit (it releases the virtual camera) and kills it if it takes too long.</summary>
    public async Task StopAsync(int timeoutMs = 3000)
    {
        var p = _proc;
        if (p == null) return;
        try
        {
            if (!p.HasExited)
            {
                Send("{\"cmd\":\"quit\"}");
                try { p.StandardInput.Close(); } catch { }
                bool exited = await Task.Run(() => p.WaitForExit(timeoutMs));
                if (!exited) p.Kill(entireProcessTree: true);
            }
        }
        catch { /* already gone */ }
    }

    private void Send(string line)
    {
        var p = _proc;
        if (p == null) return;
        lock (_sendLock)
        {
            try { if (!p.HasExited) p.StandardInput.WriteLine(line); }
            catch (IOException) { /* engine is closing */ }
            catch (InvalidOperationException) { }
        }
    }

    private void HandleLine(string line)
    {
        WriteLog(line);
        if (line.Length == 0) return;
        if (line[0] != '{') { LogLine?.Invoke(line); return; } // plain log text from the engine
        try
        {
            using var doc = JsonDocument.Parse(line);
            var root = doc.RootElement;
            if (!root.TryGetProperty("event", out var ev)) return;
            switch (ev.GetString())
            {
                case "status":
                    StatusReceived?.Invoke(new EngineStatus(
                        Str(root, "state", "disconnected"),
                        root.TryGetProperty("session", out var se) && se.TryGetUInt32(out var sid) ? sid : 0u,
                        Bool(root, "camera"), Bool(root, "mic"),
                        Bool(root, "wantCamera"), Bool(root, "wantMic"),
                        root.TryGetProperty("fps", out var f) && f.TryGetDouble(out var fps) ? fps : 0.0,
                        Str(root, "device", "")));
                    break;
                case "error":
                    ErrorReceived?.Invoke(Str(root, "code", "unknown"));
                    break;
            }
        }
        catch (JsonException) { /* not ours */ }
    }

    private static string Str(JsonElement e, string name, string def) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? (v.GetString() ?? def) : def;

    private static bool Bool(JsonElement e, string name) =>
        e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;

    private void OpenLog()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            _log?.Dispose();
            _log = new StreamWriter(new FileStream(LogPath, FileMode.Create, FileAccess.Write, FileShare.ReadWrite)) { AutoFlush = true };
            _logBytes = 0;
        }
        catch { _log = null; }
    }

    private void WriteLog(string line)
    {
        var w = _log;
        if (w == null || _logBytes > 5_000_000) return; // stop at 5 MB so the log can never grow forever
        try { lock (w) { w.WriteLine(line); _logBytes += line.Length + 2; } } catch { }
    }

    public void Dispose()
    {
        try { if (IsRunning) _proc?.Kill(entireProcessTree: true); } catch { }
        _proc?.Dispose();
        _log?.Dispose();
    }
}
