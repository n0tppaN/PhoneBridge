using System.IO;
using System.Text.Json;

namespace PhoneBridge.Desktop;

/// <summary>User choices kept between runs (%AppData%\PhoneBridge\settings.json).</summary>
public sealed class Settings
{
    public bool Camera { get; set; } = true;
    public bool Mic { get; set; } = true;

    public static string FilePath { get; set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PhoneBridge", "settings.json");

    public static Settings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings();
        }
        catch { /* corrupted file: fall back to defaults */ }
        return new Settings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this));
        }
        catch { /* not critical */ }
    }
}
