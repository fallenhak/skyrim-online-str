using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text.Json;

namespace SosStr.Launcher;

public static class LauncherInstallService
{
    public const string MarkerName = ".sos-launcher-install.json";

    public static bool IsInstalled(string directory) => File.Exists(Path.Combine(directory, MarkerName));

    public static void Install(string executable, string destination, byte[] config, byte[] renewableEncounters,
        Action<string, string>? createShortcut = null, string? desktopDirectory = null, string? startMenuDirectory = null)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Launcher kurulumu Windows gerektirir.");
        var target = Path.GetFullPath(destination);
        Directory.CreateDirectory(target);
        var exeTarget = Path.Combine(target, "SkyrimOnlineSTR.exe");
        if (!string.Equals(Path.GetFullPath(executable), exeTarget, StringComparison.OrdinalIgnoreCase))
            File.Copy(executable, exeTarget, true);
        WriteIfMissing(Path.Combine(target, "launcher.config.json"), config);
        WriteIfMissing(Path.Combine(target, "Data", "renewable_encounters.txt"), renewableEncounters);
        var desktop = desktopDirectory ?? Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        var startMenu = startMenuDirectory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Skyrim Online STR");
        Directory.CreateDirectory(desktop);
        Directory.CreateDirectory(startMenu);
        var shortcut = createShortcut ?? CreateShortcut;
        shortcut(Path.Combine(desktop, "Skyrim Online STR.lnk"), exeTarget);
        shortcut(Path.Combine(startMenu, "Skyrim Online STR.lnk"), exeTarget);
        File.WriteAllText(Path.Combine(target, MarkerName), JsonSerializer.Serialize(new { version = 1, executable = "SkyrimOnlineSTR.exe" }));
    }

    private static void WriteIfMissing(string path, byte[] data)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        if (!File.Exists(path)) File.WriteAllBytes(path, data);
    }

    [SupportedOSPlatform("windows")]
    private static void CreateShortcut(string path, string target)
    {
        Type? shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType is null) throw new InvalidOperationException("Windows kısayol bileşeni bulunamadı.");
        object? shell = null;
        object? shortcut = null;
        try
        {
            shell = Activator.CreateInstance(shellType);
            shortcut = shellType.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, shell, [path]);
            var type = shortcut!.GetType();
            type.InvokeMember("TargetPath", System.Reflection.BindingFlags.SetProperty, null, shortcut, [target]);
            type.InvokeMember("WorkingDirectory", System.Reflection.BindingFlags.SetProperty, null, shortcut, [Path.GetDirectoryName(target)]);
            type.InvokeMember("Description", System.Reflection.BindingFlags.SetProperty, null, shortcut, ["Skyrim Online STR"]);
            type.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, shortcut, null);
        }
        finally
        {
            if (shortcut is not null && Marshal.IsComObject(shortcut)) Marshal.FinalReleaseComObject(shortcut);
            if (shell is not null && Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
        }
    }
}
