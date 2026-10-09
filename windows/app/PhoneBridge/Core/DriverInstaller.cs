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
/// Installs / removes the separate Media Foundation and DirectShow camera COM servers.
/// Requires Administrator; DirectShow clients must be closed before replacing DLLs.
/// </summary>
public sealed class DriverInstaller : IDriverInstaller
{
    private const string Clsid = "{E6B65C58-4D2A-4C20-9F16-368798135CC4}";
    private const string DirectShowClsid = "{497D53E0-1D46-4E4B-A959-88A413139466}";
    private const string VideoInputCategory = "{860BB310-5D01-11D0-BD3B-00A0C911CE86}";
    private readonly string _sourceDll;
    private readonly string _directShowDll;

    public DriverInstaller(string sourceDll)
    {
        _sourceDll = sourceDll;
        _directShowDll = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(sourceDll))!, "phonebridge_directshow.dll");
    }

    public static string InstallDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "PhoneBridge");
    public static string InstalledDll => Path.Combine(InstallDir, "phonebridge_mediasource.dll");
    public static string InstalledDirectShowDll => Path.Combine(InstallDir, "phonebridge_directshow.dll");
    public static string LogDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "PhoneBridge", "logs");
    private static string Regsvr32 => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),
        Environment.Is64BitProcess ? "System32" : "Sysnative", "regsvr32.exe");

    public bool IsInstalled()
    {
        try
        {
            using var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var key = hklm.OpenSubKey($@"SOFTWARE\Classes\CLSID\{Clsid}\InprocServer32");
            using var directShow = hklm.OpenSubKey($@"SOFTWARE\Classes\CLSID\{DirectShowClsid}\InprocServer32");
            using var category = hklm.OpenSubKey($@"SOFTWARE\Classes\CLSID\{VideoInputCategory}\Instance\{DirectShowClsid}");
            return key != null && directShow != null && category != null &&
                File.Exists(InstalledDll) && File.Exists(InstalledDirectShowDll);
        }
        catch { return false; }
    }

    public Task<(bool Ok, string Message)> InstallAsync(Action<string> log) => Task.Run(async () =>
    {
        if (!File.Exists(_sourceDll))
            return (false, "Falta o ficheiro phonebridge_mediasource.dll junto do PhoneBridge.exe.");
        if (!File.Exists(_directShowDll))
            return (false, "Falta phonebridge_directshow.dll. Compile com Microsoft DirectShow BaseClasses; veja docs/directshow-compatibility.md.");
        try
        {
            log("[camera] Feche OBS e outras aplicações de vídeo antes de instalar.");
            log("[driver] A parar o serviço de câmaras do Windows (Frame Server)...");
            await StopFrameServerAsync();

            Directory.CreateDirectory(InstallDir);
            try
            {
                if (!string.Equals(Path.GetFullPath(_sourceDll), InstalledDll, StringComparison.OrdinalIgnoreCase))
                    File.Copy(_sourceDll, InstalledDll, overwrite: true);
                if (!string.Equals(Path.GetFullPath(_directShowDll), InstalledDirectShowDll, StringComparison.OrdinalIgnoreCase))
                    File.Copy(_directShowDll, InstalledDirectShowDll, overwrite: true);
            }
            catch (IOException)
            {
                return (false, "O ficheiro está em uso. Feche as aplicações que usam a câmara (Câmara, Teams, Discord, OBS...) e tente de novo.");
            }
            log("[driver] Ficheiros copiados para " + InstallDir);

            // The Frame Server runs as LOCAL SERVICE (S-1-5-19): it must be able to read the DLL and write its log.
            if (await RunAsync("icacls.exe", $"\"{InstallDir}\" /grant *S-1-5-19:(OI)(CI)(RX) /T") != 0)
                return (false, "Falhou a permissão de leitura do Frame Server.");
            Directory.CreateDirectory(LogDir);
            if (await RunAsync("icacls.exe", $"\"{LogDir}\" /grant *S-1-5-19:(OI)(CI)(M)") != 0)
                return (false, "Falhou a permissão da pasta de logs.");

            var reg = await RunAsync(Regsvr32, $"/s \"{InstalledDll}\"");
            if (reg != 0) return (false, $"O registo da DLL falhou (regsvr32 código {reg}).");
            reg = await RunAsync(Regsvr32, $"/s \"{InstalledDirectShowDll}\"");
            if (reg != 0) return (false, $"Falhou registo DirectShow ({reg}); instalação parcial. Verifique VC++ Runtime x64, exports e feche OBS.");
            log("[camera] DLLs MF/DirectShow registadas (HKLM x64). Reinicie o serviço para atualizar a ACL do shared memory.");
            bool installed = IsInstalled();
            return (installed, installed ? "Fontes Media Foundation e DirectShow instaladas. Reinicie serviço e OBS x64."
                                       : "O registo das duas fontes não ficou visível no Windows.");
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
            foreach (string dll in new[] { InstalledDirectShowDll, InstalledDll })
            {
                if (File.Exists(dll) && await RunAsync(Regsvr32, $"/u /s \"{dll}\"") != 0)
                    return (false, "Falhou a remoção do registo de " + Path.GetFileName(dll) + "; ficheiros preservados.");
            }
            try
            {
                if (File.Exists(InstalledDirectShowDll)) File.Delete(InstalledDirectShowDll);
                if (File.Exists(InstalledDll)) File.Delete(InstalledDll);
            }
            catch (IOException) { return (false, "Não foi possível apagar os ficheiros (em uso). Feche as aplicações de vídeo e tente de novo."); }
            log("[camera] Fontes MF/DirectShow removidas; aplicação, serviço e logs preservados.");
            return (!IsInstalled(), "Fontes Media Foundation e DirectShow removidas.");
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
