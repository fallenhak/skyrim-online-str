namespace SosStr.Launcher;

internal static class SafePath
{
    public static string NormalizeRelative(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path) || path.Contains(':'))
            throw new InvalidDataException($"Güvensiz göreli dosya yolu: {path}");
        var pieces = path.Replace('\\', '/').Split('/');
        if (pieces.Any(p => p is ".." or ""))
            throw new InvalidDataException($"Güvensiz göreli dosya yolu: {path}");
        return Path.Combine(pieces);
    }

    public static string UnderRoot(string root, string relative)
    {
        var normalized = NormalizeRelative(relative);
        var basePath = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var rootPath = basePath.TrimEnd(Path.DirectorySeparatorChar);
        if (Directory.Exists(rootPath) && (File.GetAttributes(rootPath) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException($"Hedef klasör yeniden yönlendirilmiş bir bağlantı: {root}");
        var target = Path.GetFullPath(Path.Combine(basePath, normalized));
        if (!target.StartsWith(basePath, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Yol hedef klasörün dışına çıkıyor: {relative}");
        var current = rootPath;
        foreach (var part in normalized.Split(Path.DirectorySeparatorChar))
        {
            current = Path.Combine(current, part);
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException($"Hedef yol yeniden yönlendirilmiş bir bağlantı içeriyor: {relative}");
        }
        return target;
    }
}
