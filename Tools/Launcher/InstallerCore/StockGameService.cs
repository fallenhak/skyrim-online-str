namespace SosStr.Launcher;

public sealed class StockGameService
{
    private const string IncompleteMarker = ".sos-str-copy-incomplete";

    public async Task CopyFromSteamAsync(string steamGameDirectory, string stockGameDirectory,
        IProgress<(int Percent, string Message)>? progress = null, CancellationToken cancellationToken = default)
    {
        var source = Path.GetFullPath(steamGameDirectory);
        var target = Path.GetFullPath(stockGameDirectory);
        if (!File.Exists(Path.Combine(source, "SkyrimSE.exe")))
            throw new DirectoryNotFoundException("Steam klasöründe SkyrimSE.exe bulunamadı.");
        var sourcePrefix = source.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var targetPrefix = target.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (string.Equals(source.TrimEnd(Path.DirectorySeparatorChar), target.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase) ||
            target.StartsWith(sourcePrefix, StringComparison.OrdinalIgnoreCase) || source.StartsWith(targetPrefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Stock Game ve Steam klasörleri aynı konumda veya birbirinin içinde olamaz.");

        Directory.CreateDirectory(target);
        var marker = Path.Combine(target, IncompleteMarker);
        if (File.Exists(marker))
        {
            Directory.Delete(target, recursive: true);
            Directory.CreateDirectory(target);
        }
        if (File.Exists(Path.Combine(target, "SkyrimSE.exe"))) return;

        await File.WriteAllTextAsync(marker, source, cancellationToken);
        var files = Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories).ToArray();
        var completed = 0;
        foreach (var from in files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relative = Path.GetRelativePath(source, from);
            if (string.Equals(relative, IncompleteMarker, StringComparison.OrdinalIgnoreCase)) continue;
            var to = Path.Combine(target, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            await using var input = new FileStream(from, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, true);
            await using var output = new FileStream(to, FileMode.Create, FileAccess.Write, FileShare.None, 128 * 1024, true);
            await input.CopyToAsync(output, cancellationToken);
            completed++;
            progress?.Report((files.Length == 0 ? 100 : completed * 100 / files.Length, $"Kopyalanıyor: {relative}"));
        }

        if (!File.Exists(Path.Combine(target, "SkyrimSE.exe")))
            throw new IOException("Oyun kopyalanamadı: SkyrimSE.exe bulunamadı.");
        File.Delete(marker);
        RemoveCreationClubContent(target);
    }

    public static int RemoveCreationClubContent(string stockGameDirectory)
    {
        var removed = 0;
        var data = Path.Combine(stockGameDirectory, "Data");
        if (Directory.Exists(data))
        {
            foreach (var file in Directory.EnumerateFiles(data, "*", SearchOption.TopDirectoryOnly))
            {
                var name = Path.GetFileName(file);
                var extension = Path.GetExtension(file);
                if (name.StartsWith("cc", StringComparison.OrdinalIgnoreCase) &&
                    (extension.Equals(".esl", StringComparison.OrdinalIgnoreCase) ||
                     extension.Equals(".esm", StringComparison.OrdinalIgnoreCase) ||
                     extension.Equals(".bsa", StringComparison.OrdinalIgnoreCase)))
                {
                    File.Delete(file);
                    removed++;
                }
            }
        }
        var ccc = Path.Combine(stockGameDirectory, "Skyrim.ccc");
        if (File.Exists(ccc)) { File.Delete(ccc); removed++; }
        return removed;
    }
}
