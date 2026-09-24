namespace SosStr.Launcher;

public sealed class StockGameService(Func<string, string>? executableVersionReader = null)
{
    private const string IncompleteMarker = ".sos-str-copy-incomplete";
    private const string VersionLockFile = ".sos-str-stock-game.json";
    private readonly Func<string, string> _readExecutableVersion = executableVersionReader ?? GameVersion.ReadExecutableVersion;

    public async Task CopyFromSteamAsync(string steamGameDirectory, string stockGameDirectory, string requiredGameVersion,
        IProgress<(int Percent, string Message)>? progress = null, CancellationToken cancellationToken = default)
    {
        var source = Path.GetFullPath(steamGameDirectory);
        var target = Path.GetFullPath(stockGameDirectory);
        var sourceExe = Path.Combine(source, "SkyrimSE.exe");
        var targetExe = Path.Combine(target, "SkyrimSE.exe");
        var required = GameVersion.Normalize(requiredGameVersion);
        if (!File.Exists(sourceExe))
            throw new DirectoryNotFoundException("Steam klasöründe SkyrimSE.exe bulunamadı.");
        EnsureIndependentDirectories(source, target);

        var incompleteMarker = Path.Combine(target, IncompleteMarker);
        if (File.Exists(incompleteMarker))
        {
            Directory.Delete(target, recursive: true);
            Directory.CreateDirectory(target);
        }

        if (File.Exists(targetExe))
        {
            EnsureVersionLock(target, required, createWhenMissing: true);
            return;
        }

        var sourceVersion = GameVersion.Normalize(_readExecutableVersion(sourceExe));
        if (sourceVersion != required)
            throw WrongSourceVersion(sourceVersion, required);

        Directory.CreateDirectory(target);
        await File.WriteAllTextAsync(incompleteMarker, source, cancellationToken);
        var sourceExecutableHash = Hashing.Sha256File(sourceExe);
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

        var copiedVersion = GameVersion.Normalize(_readExecutableVersion(targetExe));
        if (copiedVersion != required)
            throw new IOException($"Stock Game kopyası {copiedVersion} sürümünde çıktı; gerekli Skyrim SE sürümü {required}.");
        var copiedExecutableHash = Hashing.Sha256File(targetExe);
        if (!string.Equals(sourceExecutableHash, copiedExecutableHash, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Stock Game SkyrimSE.exe kopyasının SHA-256 değeri Steam kaynağıyla eşleşmedi.");

        RemoveCreationClubContent(target);
        WriteVersionLock(target, required, copiedExecutableHash);
        File.Delete(incompleteMarker);
    }

    public bool IsLockedCopyValid(string stockGameDirectory, string requiredGameVersion)
    {
        var directory = Path.GetFullPath(stockGameDirectory);
        if (!File.Exists(Path.Combine(directory, "SkyrimSE.exe")) ||
            !File.Exists(Path.Combine(directory, VersionLockFile)))
            return false;

        EnsureVersionLock(directory, GameVersion.Normalize(requiredGameVersion), createWhenMissing: false);
        return true;
    }

    private void EnsureVersionLock(string directory, string requiredVersion, bool createWhenMissing)
    {
        var executable = Path.Combine(directory, "SkyrimSE.exe");
        var actualVersion = GameVersion.Normalize(_readExecutableVersion(executable));
        if (actualVersion != requiredVersion)
            throw new InvalidDataException($"Mevcut bağımsız Stock Game {actualVersion} sürümünde; STR için {requiredVersion} gerekli. Bu kopya otomatik değiştirilmedi. Stock Game klasörünü kaldırıp {requiredVersion} sürümlü Skyrim kopyasıyla yeniden kurun.");

        var lockPath = Path.Combine(directory, VersionLockFile);
        if (!File.Exists(lockPath))
        {
            if (!createWhenMissing) throw new InvalidDataException("Stock Game sürüm kilidi bulunamadı.");
            WriteVersionLock(directory, actualVersion, Hashing.Sha256File(executable));
            return;
        }

        StockGameVersionLock versionLock;
        try
        {
            versionLock = System.Text.Json.JsonSerializer.Deserialize<StockGameVersionLock>(File.ReadAllText(lockPath),
                new System.Text.Json.JsonSerializerOptions { PropertyNameCaseInsensitive = true })
                ?? throw new InvalidDataException("Stock Game sürüm kilidi boş.");
        }
        catch (System.Text.Json.JsonException ex)
        {
            throw new InvalidDataException("Stock Game sürüm kilidi okunamadı; Stock Game klasörünü yeniden kurun.", ex);
        }

        if (versionLock.SchemaVersion != 1 || GameVersion.Normalize(versionLock.GameVersion) != requiredVersion)
            throw new InvalidDataException("Stock Game sürüm kilidi hedef sürümle eşleşmiyor; Stock Game klasörünü yeniden kurun.");

        var executableHash = Hashing.Sha256File(executable);
        if (!string.Equals(versionLock.ExecutableSha256, executableHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Stock Game SkyrimSE.exe dosyasının SHA-256 değeri sürüm kilidiyle eşleşmiyor; oyun dosyası değişmiş veya bozulmuş. Stock Game klasörünü yeniden kurun.");
    }

    private static void WriteVersionLock(string directory, string gameVersion, string executableSha256)
    {
        var versionLock = new StockGameVersionLock(1, gameVersion, executableSha256);
        File.WriteAllText(Path.Combine(directory, VersionLockFile), ManifestReader.Serialize(versionLock));
    }

    private static InvalidDataException WrongSourceVersion(string actualVersion, string requiredVersion) =>
        new($"Seçilen SkyrimSE.exe sürümü {actualVersion}. STR istemcisi yalnız {requiredVersion} sürümünü kabul ediyor; launcher Steam oyununu güncellemez veya sürüm düşürmez. Skyrim'i {requiredVersion} sürümüne güncelleyip yeniden deneyin.");

    private static void EnsureIndependentDirectories(string source, string target)
    {
        for (var directory = new DirectoryInfo(target); directory is not null; directory = directory.Parent)
        {
            if (directory.Exists && (directory.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new InvalidOperationException("Stock Game yolu sembolik bağlantı veya junction üzerinden geçemez; Steam'den bağımsız gerçek klasör seçin.");
        }

        var sourcePrefix = source.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var targetPrefix = target.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (string.Equals(source.TrimEnd(Path.DirectorySeparatorChar), target.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase) ||
            target.StartsWith(sourcePrefix, StringComparison.OrdinalIgnoreCase) || source.StartsWith(targetPrefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Stock Game klasörü Steam oyun klasöründen bağımsız ve ayrı olmalı. Launcher'ı Steam klasörünün dışına taşıyın.");
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

    private sealed record StockGameVersionLock(int SchemaVersion, string GameVersion, string ExecutableSha256);
}
