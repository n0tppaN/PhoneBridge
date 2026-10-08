using System.IO;
using System.Diagnostics;
using Microsoft.Win32;

namespace PhoneBridge.Desktop;

public interface IDriverInstaller
{
    bool IsInstalled();
    Task<(bool Ok, string Message)> InstallAsync(Action<string> log);
    Task<(bool Ok, string Message)> UninstallAsync(Action<string> log);
}

/// <summary>
/// Installs / removes the Media Foundation virtual camera source (phonebridge_mediasource.dll) from inside the app - the same steps
/// the old install_camera.ps1 did, so no PowerShell is needed. Requires the app to run as Administrator.
/// </summary>
public sealed class DriverInstaller : IDriverInstaller
{
    private const string Clsid = "{E6B65C58-4D2A-4C20-9F16-368798135CC4}";
    private readonly string _sourceDll;

    public DriverInstaller(string sourceDll) => _sourceDll = sourceDll;

    public static string InstallDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "PhoneBridge");
    public static string InstalledDll => Path.Combine(InstallDir, "phonebridge_mediasource.dll");
    public static string LogDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "PhoneBridge", "logs");

    public bool IsInstalled()
    {
        try
        {
            using var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var key = hklm.OpenSubKey($@"SOFTWARE\Classes\CLSID\{Clsid}\InprocServer32");
            return key != null && File.Exists(InstalledDll);
        }
        catch { return false; }
    }

    public Task<(bool Ok, string Message)> InstallAsync(Action<string> log) => Task.Run(async () =>
    {
        if (!File.Exists(_sourceDll))
            return (false, "Falta o ficheiro phonebridge_mediasource.dll junto do PhoneBridge.exe.");
        try
        {
            log("[driver] A parar o serviço de câmaras do Windows (Frame Server)...");
            await StopFrameServerAsync();

            Directory.CreateDirectory(InstallDir);
            try { File.Copy(_sourceDll, InstalledDll, overwrite: true); }
            catch (IOException)
            {
                return (false, "O ficheiro está em uso. Feche as aplicações que usam a câmara (Câmara, Teams, Discord, OBS...) e tente de novo.");
            }
            log("[driver] Ficheiros copiados para " + InstallDir);

            // The Frame Server runs as LOCAL SERVICE (S-1-5-19): it must be able to read the DLL and write its log.
            await RunAsync("icacls.exe", $"\"{InstallDir}\" /grant *S-1-5-19:(OI)(CI)(RX) /T");
            Directory.CreateDirectory(LogDir);
            await RunAsync("icacls.exe", $"\"{LogDir}\" /grant *S-1-5-19:(OI)(CI)(M)");

            var reg = await RunAsync("regsvr32.exe", $"/s \"{InstalledDll}\"");
            if (reg != 0) return (false, $"O registo da DLL falhou (regsvr32 código {reg}).");
            log("[driver] DLL registada (HKLM).");
            return (IsInstalled(), IsInstalled() ? "Fonte Media Foundation instalada." : "O registo não ficou visível no Windows.");
        }
        catch (Exception ex) { return (false, "Falha ao instalar: " + ex.Message); }
        finally { await StartFrameServerAsync(); log("[driver] Frame Server reiniciado."); }
    });

    public Task<(bool Ok, string Message)> UninstallAsync(Action<string> log) => Task.Run(async () =>
    {
        try
        {
            log("[driver] A parar o serviço de câmaras do Windows (Frame Server)...");
            await StopFrameServerAsync();
            if (File.Exists(InstalledDll)) await RunAsync("regsvr32.exe", $"/u /s \"{InstalledDll}\"");
            try { if (Directory.Exists(InstallDir)) Directory.Delete(InstallDir, recursive: true); }
            catch (IOException) { return (false, "Não foi possível apagar os ficheiros (em uso). Feche as aplicações de vídeo e tente de novo."); }
            log("[camera] Fonte Media Foundation removida.");
            return (!IsInstalled(), "Fonte Media Foundation removida.");
        }
        catch (Exception ex) { return (false, "Falha ao remover: " + ex.Message); }
        finally { await StartFrameServerAsync(); }
    });

    private static async Task StopFrameServerAsync()
    {
        await RunAsync("net.exe", "stop FrameServerMonitor /y");
        await RunAsync("net.exe", "stop FrameServer /y");
    }

    private static Task StartFrameServerAsync() => RunAsync("net.exe", "start FrameServer");

    private static async Task<int> RunAsync(string file, string args)
    {
        var psi = new ProcessStartInfo(file, args)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        using var p = Process.Start(psi) ?? throw new InvalidOperationException("Não foi possível executar " + file);
        var stdout = p.StandardOutput.ReadToEndAsync();
        var stderr = p.StandardError.ReadToEndAsync();
        await p.WaitForExitAsync();
        await Task.WhenAll(stdout, stderr);
        return p.ExitCode;
    }
}
