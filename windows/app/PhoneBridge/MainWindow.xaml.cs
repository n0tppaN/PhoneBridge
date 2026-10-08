using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using Microsoft.Web.WebView2.Core;

namespace PhoneBridge.Desktop;

/// <summary>The window is just a frame: the screen is the React app shown in WebView2.</summary>
public partial class MainWindow : Window
{
    private const string VirtualHost = "phonebridge.app";
    private HostBridge? _bridge;
    private bool _shuttingDown;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += async (_, _) => await InitWebAsync();
    }

    private async Task InitWebAsync()
    {
        try
        {
            // The app runs as Administrator, so WebView2 must keep its data in a writable folder.
            string userData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PhoneBridge", "WebView2");
            var env = await CoreWebView2Environment.CreateAsync(null, userData);
            await Web.EnsureCoreWebView2Async(env);
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "O PhoneBridge precisa do \"Microsoft Edge WebView2 Runtime\" (já vem com o Windows 11).\n" +
                "Instale-o em https://go.microsoft.com/fwlink/p/?LinkId=2124703 e abra a app de novo.\n\n" + ex.Message,
                "PhoneBridge", MessageBoxButton.OK, MessageBoxImage.Warning);
            Close();
            return;
        }

        string www = Path.Combine(AppContext.BaseDirectory, "wwwroot");
        if (!File.Exists(Path.Combine(www, "index.html")))
        {
            MessageBox.Show("Faltam os ficheiros do ecrã (pasta wwwroot) junto do PhoneBridge.exe.",
                "PhoneBridge", MessageBoxButton.OK, MessageBoxImage.Error);
            Close();
            return;
        }

        var core = Web.CoreWebView2;
        core.Settings.AreDefaultContextMenusEnabled = false;
        core.Settings.AreBrowserAcceleratorKeysEnabled = false;   // no F5 / Ctrl+R / Ctrl+P
        core.Settings.IsStatusBarEnabled = false;
        core.Settings.IsZoomControlEnabled = false;
        core.Settings.IsBuiltInErrorPageEnabled = false;
#if !DEBUG
        core.Settings.AreDevToolsEnabled = false;
#endif
        Web.DefaultBackgroundColor = System.Drawing.Color.FromArgb(0xFF, 0x0A, 0x0C, 0x10);
        core.SetVirtualHostNameToFolderMapping(VirtualHost, www, CoreWebView2HostResourceAccessKind.Allow);

        // The page may only be our own files, and may not open other windows.
        core.NavigationStarting += (_, e) => { if (!e.Uri.StartsWith($"https://{VirtualHost}/", StringComparison.OrdinalIgnoreCase)) e.Cancel = true; };
        core.NewWindowRequested += (_, e) => e.Handled = true;
        core.ProcessFailed += (_, _) => Dispatcher.InvokeAsync(() => Web.Reload());

        string baseDir = AppContext.BaseDirectory;
        _bridge = new HostBridge(
            post: json => core.PostWebMessageAsJson(json),
            runOnUi: action => Dispatcher.InvokeAsync(action),
            engine: new EngineClient(),
            settings: Settings.Load(),
            driver: new DriverInstaller(Path.Combine(baseDir, "phonebridge_mediasource.dll")),
            enginePath: Path.Combine(baseDir, "phonebridge_service.exe"),
            version: typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "0.2.0");

        core.WebMessageReceived += (_, e) => _bridge?.HandleMessage(e.WebMessageAsJson);
        Web.Source = new Uri($"https://{VirtualHost}/index.html");
    }

    protected override async void OnClosing(CancelEventArgs e)
    {
        if (_bridge != null && !_shuttingDown)
        {
            e.Cancel = true;          // let the engine release the virtual camera first
            _shuttingDown = true;
            IsEnabled = false;
            await _bridge.ShutdownAsync();
            Close();
            return;
        }
        base.OnClosing(e);
    }

    protected override void OnClosed(EventArgs e)
    {
        _bridge?.Dispose();
        base.OnClosed(e);
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        ApplyDarkTitleBar();
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    /// <summary>Dark native title bar in the app colours (Windows 11; harmlessly ignored on older builds).</summary>
    private void ApplyDarkTitleBar()
    {
        IntPtr hwnd = new WindowInteropHelper(this).Handle;
        int dark = 1;
        DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int));   // DWMWA_USE_IMMERSIVE_DARK_MODE
        int bg = 0x00100C0A;                                      // #0A0C10 as 0x00BBGGRR
        DwmSetWindowAttribute(hwnd, 35, ref bg, sizeof(int));     // DWMWA_CAPTION_COLOR
        DwmSetWindowAttribute(hwnd, 34, ref bg, sizeof(int));     // DWMWA_BORDER_COLOR
        int text = 0x00FAF6F3;                                    // #F3F6FA
        DwmSetWindowAttribute(hwnd, 36, ref text, sizeof(int));   // DWMWA_TEXT_COLOR
    }
}
