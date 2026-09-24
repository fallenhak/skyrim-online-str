using System.IO.Compression;
using System.Security.Cryptography;

namespace SosStr.Launcher;

public sealed class ModDeploymentService
{
    private sealed class DeploymentState
    {
        public string Fingerprint { get; set; } = "";
        public Dictionary<string, ManagedFile> Files { get; set; } = new(StringComparer.OrdinalIgnoreCase);
    }

    private sealed class ManagedFile
    {
        public string Target { get; set; } = "";
        public string RelativePath { get; set; } = "";
        public string? BackupPath { get; set; }
        public string Sha256 { get; set; } = "";
    }

    public async Task ApplyAsync(string gameDirectory, string stateDirectory, IReadOnlyList<ModPackage> packages,
        IReadOnlyDictionary<string, string> verifiedArchives, CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(stateDirectory);
        var statePath = Path.Combine(stateDirectory, "deployment-state.json");
        var pendingPath = Path.Combine(stateDirectory, "deployment-pending.json");
        var oldState = File.Exists(statePath) ? ReadState(statePath) : new DeploymentState();
        if (File.Exists(pendingPath))
        {
            Restore(gameDirectory, stateDirectory, ReadState(pendingPath).Files);
            File.Delete(pendingPath);
        }

        var fingerprint = string.Join("\n", packages.OrderBy(p => p.Order).Select(p => $"{p.Id}|{p.Version}|{p.Sha256}|{p.Target}|{p.Order}|{p.StripPrefix}"));
        if (oldState.Fingerprint == fingerprint && IsCurrent(gameDirectory, oldState.Files)) return;

        var expandedRoot = Path.Combine(stateDirectory, "expanded");
        Directory.CreateDirectory(expandedRoot);
        var desired = new Dictionary<string, (string Target, string Relative, string Source, string Sha)>(StringComparer.OrdinalIgnoreCase);
        foreach (var package in packages.OrderBy(p => p.Order))
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!verifiedArchives.TryGetValue(package.Id, out var archive))
                throw new InvalidDataException($"İndirilmiş mod paketi bulunamadı: {package.Name}");
            var expanded = Path.Combine(expandedRoot, package.Sha256.ToLowerInvariant());
            if (!Directory.Exists(expanded)) ExtractSafely(archive, expanded);
            var prefix = package.StripPrefix.Replace('\\', '/').Trim('/');
            var packageFiles = 0;
            foreach (var file in Directory.EnumerateFiles(expanded, "*", SearchOption.AllDirectories))
            {
                var relative = Path.GetRelativePath(expanded, file).Replace('\\', '/');
                if (prefix.Length > 0)
                {
                    if (!relative.StartsWith(prefix + "/", StringComparison.OrdinalIgnoreCase)) continue;
                    relative = relative[(prefix.Length + 1)..];
                }
                if (relative.Length == 0) continue;
                relative = SafePath.NormalizeRelative(relative);
                var key = package.Target + "|" + relative;
                desired[key] = (package.Target, relative, file, Hashing.Sha256File(file));
                packageFiles++;
            }
            if (packageFiles == 0) throw new InvalidDataException($"{package.Name} arşivinde kurulabilir dosya yok. stripPrefix değerini kontrol edin.");
        }

        var union = new Dictionary<string, ManagedFile>(StringComparer.OrdinalIgnoreCase);
        foreach (var (key, entry) in oldState.Files) union[key] = Clone(entry);
        foreach (var (key, value) in desired)
        {
            if (union.ContainsKey(key)) continue;
            var destination = TargetPath(gameDirectory, value.Target, value.Relative);
            string? backup = null;
            if (File.Exists(destination))
            {
                var backupFile = Path.Combine(stateDirectory, "base", Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(key))).ToLowerInvariant() + ".bak");
                Directory.CreateDirectory(Path.GetDirectoryName(backupFile)!);
                File.Copy(destination, backupFile, overwrite: true);
                backup = Path.GetRelativePath(stateDirectory, backupFile);
            }
            union[key] = new ManagedFile { Target = value.Target, RelativePath = value.Relative, BackupPath = backup };
        }

        WriteStateAtomic(pendingPath, new DeploymentState { Fingerprint = "pending", Files = union });
        Restore(gameDirectory, stateDirectory, union);
        foreach (var (key, value) in desired)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var destination = TargetPath(gameDirectory, value.Target, value.Relative);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(value.Source, destination, overwrite: true);
            union[key].Sha256 = value.Sha;
        }

        var active = new DeploymentState { Fingerprint = fingerprint, Files = desired.ToDictionary(
            p => p.Key, p => new ManagedFile
            {
                Target = p.Value.Target,
                RelativePath = p.Value.Relative,
                BackupPath = union[p.Key].BackupPath,
                Sha256 = p.Value.Sha
            }, StringComparer.OrdinalIgnoreCase) };
        WriteStateAtomic(statePath, active);
        File.Delete(pendingPath);
        PruneBackups(stateDirectory, active.Files);
    }

    public bool IsCurrentInstall(string gameDirectory, string stateDirectory, IReadOnlyList<ModPackage> packages)
    {
        var pendingPath = Path.Combine(stateDirectory, "deployment-pending.json");
        if (File.Exists(pendingPath))
        {
            Restore(gameDirectory, stateDirectory, ReadState(pendingPath).Files);
            File.Delete(pendingPath);
        }
        var statePath = Path.Combine(stateDirectory, "deployment-state.json");
        if (!File.Exists(statePath)) return false;
        var state = ReadState(statePath);
        var fingerprint = string.Join("\n", packages.OrderBy(p => p.Order).Select(p => $"{p.Id}|{p.Version}|{p.Sha256}|{p.Target}|{p.Order}|{p.StripPrefix}"));
        return state.Fingerprint == fingerprint && IsCurrent(gameDirectory, state.Files);
    }

    private static void ExtractSafely(string archive, string destination)
    {
        var temp = destination + ".extracting";
        if (Directory.Exists(temp)) Directory.Delete(temp, recursive: true);
        Directory.CreateDirectory(temp);
        try
        {
            using var zip = ZipFile.OpenRead(archive);
            foreach (var entry in zip.Entries)
            {
                if (string.IsNullOrEmpty(entry.Name)) continue;
                var relative = SafePath.NormalizeRelative(entry.FullName);
                var file = SafePath.UnderRoot(temp, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(file)!);
                using var input = entry.Open();
                using var output = new FileStream(file, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                input.CopyTo(output);
            }
            Directory.Move(temp, destination);
        }
        catch
        {
            if (Directory.Exists(temp)) Directory.Delete(temp, recursive: true);
            throw;
        }
    }

    private static bool IsCurrent(string gameDirectory, Dictionary<string, ManagedFile> files)
    {
        foreach (var item in files.Values)
        {
            var path = TargetPath(gameDirectory, item.Target, item.RelativePath);
            if (!File.Exists(path) || !string.Equals(Hashing.Sha256File(path), item.Sha256, StringComparison.OrdinalIgnoreCase)) return false;
        }
        return true;
    }

    private static void Restore(string gameDirectory, string stateDirectory, Dictionary<string, ManagedFile> files)
    {
        foreach (var item in files.Values)
        {
            var destination = TargetPath(gameDirectory, item.Target, item.RelativePath);
            if (item.BackupPath is null)
            {
                if (File.Exists(destination)) File.Delete(destination);
            }
            else
            {
                var backup = SafePath.UnderRoot(stateDirectory, item.BackupPath);
                if (!File.Exists(backup)) throw new IOException($"Yedek dosya kayıp; oyun dosyası geri alınamıyor: {item.RelativePath}");
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(backup, destination, overwrite: true);
            }
        }
    }

    private static string TargetPath(string gameDirectory, string target, string relative)
    {
        var targetRoot = target == "root" ? gameDirectory : target == "data" ? Path.Combine(gameDirectory, "Data")
            : throw new InvalidDataException($"Bilinmeyen mod hedefi: {target}");
        return SafePath.UnderRoot(targetRoot, relative);
    }

    private static DeploymentState ReadState(string path) => ManifestReader.ParseJson<DeploymentState>(File.ReadAllText(path));
    private static ManagedFile Clone(ManagedFile item) => new()
    {
        Target = item.Target, RelativePath = item.RelativePath, BackupPath = item.BackupPath, Sha256 = item.Sha256
    };

    private static void WriteStateAtomic(string path, DeploymentState state)
    {
        var temp = path + ".tmp";
        File.WriteAllText(temp, ManifestReader.Serialize(state));
        File.Move(temp, path, overwrite: true);
    }

    private static void PruneBackups(string stateDirectory, Dictionary<string, ManagedFile> active)
    {
        var keep = active.Values.Where(x => x.BackupPath is not null).Select(x => Path.GetFullPath(Path.Combine(stateDirectory, x.BackupPath!)))
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var baseDir = Path.Combine(stateDirectory, "base");
        if (!Directory.Exists(baseDir)) return;
        foreach (var backup in Directory.EnumerateFiles(baseDir, "*.bak", SearchOption.AllDirectories))
            if (!keep.Contains(Path.GetFullPath(backup))) File.Delete(backup);
    }
}
