using System.Threading;
using System.Windows;

namespace PhoneBridge.Desktop;

public partial class App : Application
{
    private Mutex? _singleInstance;

    protected override void OnStartup(StartupEventArgs e)
    {
        // Two copies would fight over the virtual camera, so only one may run.
        _singleInstance = new Mutex(true, "PhoneBridge.Desktop.SingleInstance", out bool createdNew);
        if (!createdNew)
        {
            MessageBox.Show("O PhoneBridge já está aberto.", "PhoneBridge", MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }
        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        try { _singleInstance?.ReleaseMutex(); } catch { /* not the owner */ }
        _singleInstance?.Dispose();
        base.OnExit(e);
    }
}
