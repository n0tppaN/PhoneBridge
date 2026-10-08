using System.IO;
using System.Text.Json;

namespace PhoneBridge.Desktop;

/// <summary>
/// Glue between the web UI (React, inside WebView2) and the engine. No WPF types on purpose, so it can be tested anywhere.
///   web -> host : {"cmd":"ready|start|stop|restart|setStreams|installDriver|uninstallDriver", ...}
///   host -> web : {"type":"state",...}  and  {"type":"logs","lines":[{"t":"HH:mm:ss.fff","text":"..."}]}
/// </summary>
public sealed class HostBridge : IDisposable
{
    private readonly Action<string> _post;        // sends one JSON message to the web page (UI thread only)
    private readonly Action<Action> _runOnUi;     // runs something on the UI thread
    private readonly EngineClient _engine;
    private readonly Settings _settings;
    private readonly IDriverInstaller _driver;
    private readonly string _enginePath;
    private readonly string _version;

    private EngineStatus? _status;
    private string? _error;
    private bool _driverInstalled;
    private bool _driverBusy;
    private bool _busy;                           // start / stop / driver operation in progress
    private volatile bool _stopRequested;         // an exit of the engine is expected
    private bool _shuttingDown;

    private readonly List<object> _pendingLogs = new();
    private readonly object _logLock = new();
    private readonly Timer _logTimer;

    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    public HostBridge(Action<string> post, Action<Action> runOnUi, EngineClient engine, Settings settings,
                      IDriverInstaller driver, string enginePath, string version)
    {
        _post = post;
        _runOnUi = runOnUi;
        _engine = engine;
        _settings = settings;
        _driver = driver;
        _enginePath = enginePath;
        _version = version;
        _driverInstalled = driver.IsInstalled();

        _engine.StatusReceived += s => _runOnUi(() => { _status = s; PostState(); });
        _engine.ErrorReceived += code => _runOnUi(() => { _error = code; PostState(); });
        _engine.Exited += _ => _runOnUi(OnEngineExited);
        _engine.LogLine += line => Log(line, fromEngine: true);
        _logTimer = new Timer(_ => FlushLogs(), null, 150, 150);
    }

    // ------------------------------------------------------------------ messages from the web page

    public void HandleMessage(string json)
    {
        string? cmd = null;
        JsonElement root;
        try
        {
            using var doc = JsonDocument.Parse(json);
            root = doc.RootElement.Clone();
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("cmd", out var c)) return;
            cmd = c.GetString();
        }
        catch (JsonException) { return; }

        switch (cmd)
        {
            case "ready": _driverInstalled = _driver.IsInstalled(); PostState(); break;
            case "start": Fire(StartAsync); break;
            case "stop": Fire(StopAsync); break;
            case "restart": Fire(RestartAsync); break;
            case "installDriver": Fire(() => DriverAsync(install: true)); break;
            case "uninstallDriver": Fire(() => DriverAsync(install: false)); break;
            case "setStreams":
                if (root.TryGetProperty("camera", out var cam) && root.TryGetProperty("mic", out var mic)
                    && cam.ValueKind is JsonValueKind.True or JsonValueKind.False
                    && mic.ValueKind is JsonValueKind.True or JsonValueKind.False)
                    SetStreams(cam.GetBoolean(), mic.GetBoolean());
                break;
        }
    }

    // ------------------------------------------------------------------ actions

    // async void on purpose: keeps the UI thread context, and nothing may crash the app from here.
    private async void Fire(Func<Task> action)
    {
        try { await action(); }
        catch (Exception ex)
        {
            _busy = false;
            _driverBusy = false;
            Log("Erro inesperado: " + ex.Message);
            PostState();
        }
    }

    private void SetStreams(bool camera, bool mic)
    {
        _settings.Camera = camera;
        _settings.Mic = mic;
        _settings.Save();
        if (_engine.IsRunning) _engine.SetStreams(camera, mic); // applied live on the phone
        PostState();
    }

    private async Task StartAsync()
    {
        if (_busy || _engine.IsRunning) return;
        _busy = true;
        try
        {
            _error = null;
            _status = null;
            if (!File.Exists(_enginePath)) { _error = "missing_engine"; Log("Falta " + _enginePath); return; }

            if (!_driver.IsInstalled())
            {
                Log("O driver da câmara virtual ainda não está instalado. A instalar (só na primeira vez)...");
                if (!await RunDriverAsync(install: true)) { _error = "driver_failed"; return; }
            }

            _stopRequested = false;
            if (!_engine.Start(_enginePath)) { _error = "start_failed"; Log("Não foi possível iniciar " + _enginePath); return; }
            _engine.SetStreams(_settings.Camera, _settings.Mic); // the engine streams nothing until told
            Log("Motor iniciado.");
        }
        finally { _busy = false; PostState(); }
    }

    private async Task StopAsync()
    {
        if (_busy || !_engine.IsRunning) return;
        _busy = true;
        try { await StopEngineCoreAsync(); }
        finally { _busy = false; PostState(); }
    }

    private async Task RestartAsync()
    {
        if (_busy) return;
        if (_engine.IsRunning)
        {
            _busy = true;
            try { await StopEngineCoreAsync(); }
            finally { _busy = false; }
        }
        await StartAsync();
    }

    private async Task StopEngineCoreAsync()
    {
        _stopRequested = true;
        await _engine.StopAsync();
        _status = null;
        Log("Motor parado.");
    }

    private async Task DriverAsync(bool install)
    {
        if (_busy) return;
        _busy = true;
        try
        {
            if (!install && _engine.IsRunning) await StopEngineCoreAsync(); // the camera must be released first
            _error = null;
            if (!await RunDriverAsync(install)) _error = "driver_failed";
        }
        finally { _busy = false; PostState(); }
    }

    private async Task<bool> RunDriverAsync(bool install)
    {
        _driverBusy = true;
        PostState();
        (bool Ok, string Message) result;
        try { result = install ? await _driver.InstallAsync(l => Log(l)) : await _driver.UninstallAsync(l => Log(l)); }
        finally { _driverBusy = false; }
        _driverInstalled = _driver.IsInstalled();
        Log((result.Ok ? "[driver] " : "[driver] ERRO: ") + result.Message);
        return result.Ok;
    }

    private void OnEngineExited()
    {
        if (_shuttingDown) return;
        _status = null;
        if (!_stopRequested && _error == null)
        {
            _error = "exited";
            Log("O motor parou inesperadamente.");
        }
        PostState();
    }

    /// <summary>Called when the window closes: let the engine release the virtual camera first.</summary>
    public async Task ShutdownAsync()
    {
        _shuttingDown = true;
        _stopRequested = true;
        await _engine.StopAsync();
    }

    // ------------------------------------------------------------------ messages to the web page

    private void PostState()
    {
        var message = new
        {
            type = "state",
            version = _version,
            engineFound = File.Exists(_enginePath),
            running = _engine.IsRunning,
            status = _status,
            error = _error,
            settings = new { camera = _settings.Camera, mic = _settings.Mic },
            driver = new { installed = _driverInstalled, busy = _driverBusy },
        };
        _post(JsonSerializer.Serialize(message, Json));
    }

    private static bool IsNoise(string line) =>
        line.Contains("Keyframe (IDR) received", StringComparison.Ordinal) ||
        line.Contains("Received packet type=", StringComparison.Ordinal);

    private void Log(string text, bool fromEngine = false)
    {
        if (fromEngine && IsNoise(text)) return;
        lock (_logLock)
        {
            _pendingLogs.Add(new { t = DateTime.Now.ToString("HH:mm:ss.fff"), text });
            if (_pendingLogs.Count > 400) _pendingLogs.RemoveRange(0, _pendingLogs.Count - 400);
        }
    }

    private void FlushLogs()
    {
        object[] batch;
        lock (_logLock)
        {
            if (_pendingLogs.Count == 0) return;
            batch = _pendingLogs.ToArray();
            _pendingLogs.Clear();
        }
        _runOnUi(() => _post(JsonSerializer.Serialize(new { type = "logs", lines = batch }, Json)));
    }

    public void Dispose()
    {
        _logTimer.Dispose();
        _engine.Dispose();
    }
}
