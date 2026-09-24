using System.Text.RegularExpressions;

namespace SosStr.Launcher;

public static partial class GameVersion
{
    public const string Required = "1.7.104";

    public static string Normalize(string version)
    {
        var match = VersionPattern().Match(version ?? "");
        if (!match.Success) throw new InvalidDataException($"Oyun sürümü okunamadı: {version}");
        return string.Join('.', match.Groups[1].Value, match.Groups[2].Value, match.Groups[3].Value);
    }

    public static bool TryNormalize(string? version, out string normalized)
    {
        try { normalized = Normalize(version ?? ""); return true; }
        catch { normalized = ""; return false; }
    }

    public static string ReadExecutableVersion(string executablePath)
    {
        var info = System.Diagnostics.FileVersionInfo.GetVersionInfo(executablePath);
        var fileVersion = info.ProductVersion;
        if (string.IsNullOrWhiteSpace(fileVersion)) fileVersion = info.FileVersion;
        if (string.IsNullOrWhiteSpace(fileVersion))
            throw new InvalidDataException("SkyrimSE.exe sürüm bilgisi bulunamadı.");
        return Normalize(fileVersion);
    }

    [GeneratedRegex("(\\d+)\\.(\\d+)\\.(\\d+)", RegexOptions.CultureInvariant)]
    private static partial Regex VersionPattern();
}
