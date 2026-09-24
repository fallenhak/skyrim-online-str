using Microsoft.Win32;
using System.Text.RegularExpressions;

namespace SosStr.Launcher;

public static partial class SteamLocator
{
    public const string SkyrimAppId = "489830";
    public const string GameDirectoryName = "Skyrim Special Edition";

    public static IReadOnlyList<string> FindGameDirectories(string? steamRoot = null)
    {
        var roots = new List<string>();
        if (!string.IsNullOrWhiteSpace(steamRoot)) roots.Add(steamRoot);
        else
        {
            var fromRegistry = ReadSteamRoot();
            if (fromRegistry is not null) roots.Add(fromRegistry);
            roots.AddRange([
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Steam")
            ]);
        }

        var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in roots.Where(Directory.Exists))
        {
            libraries.Add(Path.GetFullPath(root));
            var vdf = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(vdf)) continue;
            foreach (var library in ParseLibraryFolders(File.ReadAllText(vdf), root))
                libraries.Add(library);
        }

        var matches = new List<string>();
        foreach (var library in libraries)
        {
            var common = Path.Combine(library, "steamapps", "common");
            var appManifest = Path.Combine(library, "steamapps", $"appmanifest_{SkyrimAppId}.acf");
            if (!Directory.Exists(common) || !File.Exists(appManifest)) continue;
            var gamePath = Path.Combine(common, GameDirectoryName);
            if (File.Exists(Path.Combine(gamePath, "SkyrimSE.exe"))) matches.Add(gamePath);
        }
        return matches;
    }

    public static IReadOnlyList<string> ParseLibraryFolders(string vdf, string steamRoot)
    {
        var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFullPath(steamRoot) };
        var depth = 0;
        foreach (var line in vdf.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
        {
            var oldFormat = depth == 1 ? OldLibraryPathPattern().Match(line) : Match.Empty;
            var newFormat = depth >= 2 ? NewLibraryPathPattern().Match(line) : Match.Empty;
            var match = oldFormat.Success ? oldFormat : newFormat;
            if (match.Success)
            {
                var decoded = match.Groups[1].Value.Replace("\\\\", "\\").Replace("\\\"", "\"");
                if (!string.IsNullOrWhiteSpace(decoded)) result.Add(Path.GetFullPath(decoded));
            }
            depth += line.Count(ch => ch == '{') - line.Count(ch => ch == '}');
        }
        return result.ToArray();
    }

    private static string? ReadSteamRoot()
    {
        if (!OperatingSystem.IsWindows()) return null;
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            try
            {
                using var hive = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
                using var key = hive.OpenSubKey(@"SOFTWARE\Valve\Steam") ?? hive.OpenSubKey(@"SOFTWARE\WOW6432Node\Valve\Steam");
                var value = key?.GetValue("InstallPath") as string;
                if (!string.IsNullOrWhiteSpace(value)) return value;
            }
            catch (PlatformNotSupportedException) { return null; }
            catch (UnauthorizedAccessException) { }
        }
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            return key?.GetValue("SteamPath") as string ?? key?.GetValue("InstallPath") as string;
        }
        catch (PlatformNotSupportedException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    [GeneratedRegex("^\\s*\"[0-9]+\"\\s*\"((?:[^\"\\\\]|\\\\.)*)\"", RegexOptions.CultureInvariant)]
    private static partial Regex OldLibraryPathPattern();

    [GeneratedRegex("^\\s*\"path\"\\s*\"((?:[^\"\\\\]|\\\\.)*)\"", RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex NewLibraryPathPattern();
}
