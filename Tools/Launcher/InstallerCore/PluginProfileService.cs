using System.Text.Json;

namespace SosStr.Launcher;

public sealed class PluginProfileService
{
    private sealed class RecoveryState
    {
        public string ProfileDirectory { get; set; } = "";
        public bool HadPlugins { get; set; }
        public bool HadLoadOrder { get; set; }
    }

    public void RecoverInterruptedSession(string profileDirectory, string stateDirectory)
    {
        Directory.CreateDirectory(stateDirectory);
        var lockPath = ProfileLockPath(stateDirectory);
        FileStream? gate;
        try { gate = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException) { return; } // Another launcher is currently using this profile.
        using (gate) RestoreIfPending(profileDirectory, stateDirectory);
    }

    public IDisposable ApplyForSession(string gameDirectory, string profileDirectory, string stateDirectory,
        IReadOnlyList<ModPackage> packages)
    {
        Directory.CreateDirectory(stateDirectory);
        Directory.CreateDirectory(profileDirectory);
        var lockPath = ProfileLockPath(stateDirectory);
        FileStream gate;
        try { gate = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException ex) { throw new InvalidOperationException("Başka bir STR oturumu oyun eklenti listesini kullanıyor.", ex); }

        try
        {
            RestoreIfPending(profileDirectory, stateDirectory);
            var data = Path.Combine(gameDirectory, "Data");
            var plugins = new List<string>();
            foreach (var name in new[] { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
                if (File.Exists(Path.Combine(data, name))) plugins.Add(name);
            foreach (var plugin in packages.OrderBy(p => p.Order).SelectMany(p => p.Plugins))
            {
                if (!File.Exists(Path.Combine(data, plugin)))
                    throw new InvalidDataException($"Manifest eklentisi Data klasöründe bulunamadı: {plugin}");
                if (!plugins.Contains(plugin, StringComparer.OrdinalIgnoreCase)) plugins.Add(plugin);
            }

            var pluginsPath = Path.Combine(profileDirectory, "plugins.txt");
            var loadOrderPath = Path.Combine(profileDirectory, "loadorder.txt");
            var backupRoot = Path.Combine(stateDirectory, "profile-backup");
            Directory.CreateDirectory(backupRoot);
            var pluginsBackup = Path.Combine(backupRoot, "plugins.txt");
            var loadOrderBackup = Path.Combine(backupRoot, "loadorder.txt");
            var hadPlugins = File.Exists(pluginsPath);
            var hadLoadOrder = File.Exists(loadOrderPath);
            if (hadPlugins) File.Copy(pluginsPath, pluginsBackup, overwrite: true);
            if (hadLoadOrder) File.Copy(loadOrderPath, loadOrderBackup, overwrite: true);
            var recoveryPath = Path.Combine(stateDirectory, "profile-recovery.json");
            WriteAtomic(recoveryPath, JsonSerializer.Serialize(new RecoveryState
            {
                ProfileDirectory = Path.GetFullPath(profileDirectory), HadPlugins = hadPlugins, HadLoadOrder = hadLoadOrder
            }));
            WriteAtomic(pluginsPath, string.Join(Environment.NewLine, plugins.Select(p => "*" + p)) + Environment.NewLine);
            WriteAtomic(loadOrderPath, string.Join(Environment.NewLine, plugins) + Environment.NewLine);
            return new ProfileLease(gate, () => RestoreIfPending(profileDirectory, stateDirectory));
        }
        catch
        {
            try { RestoreIfPending(profileDirectory, stateDirectory); } catch { }
            gate.Dispose();
            throw;
        }
    }

    private static void RestoreIfPending(string profileDirectory, string stateDirectory)
    {
        var recoveryPath = Path.Combine(stateDirectory, "profile-recovery.json");
        if (!File.Exists(recoveryPath)) return;
        var recovery = JsonSerializer.Deserialize<RecoveryState>(File.ReadAllText(recoveryPath), new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidDataException("plugins.txt geri yükleme kaydı okunamadı.");
        if (!string.Equals(Path.GetFullPath(profileDirectory), recovery.ProfileDirectory, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("plugins.txt geri yükleme klasörü beklenen konumla eşleşmiyor.");
        var backupRoot = Path.Combine(stateDirectory, "profile-backup");
        RestoreFile(Path.Combine(profileDirectory, "plugins.txt"), Path.Combine(backupRoot, "plugins.txt"), recovery.HadPlugins);
        RestoreFile(Path.Combine(profileDirectory, "loadorder.txt"), Path.Combine(backupRoot, "loadorder.txt"), recovery.HadLoadOrder);
        File.Delete(recoveryPath);
    }

    private static void RestoreFile(string destination, string backup, bool existed)
    {
        if (existed)
        {
            if (!File.Exists(backup)) throw new IOException($"Oyun profili yedeği bulunamadı: {backup}");
            var temp = destination + ".restore-" + Guid.NewGuid().ToString("N");
            File.Copy(backup, temp);
            File.Move(temp, destination, overwrite: true);
        }
        else if (File.Exists(destination)) File.Delete(destination);
    }

    private static void WriteAtomic(string path, string contents)
    {
        var temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temp, contents);
        File.Move(temp, path, overwrite: true);
    }

    private static string ProfileLockPath(string stateDirectory) =>
        Path.Combine(Directory.GetParent(Path.GetFullPath(stateDirectory))!.FullName, "profile-session.lock");

    private sealed class ProfileLease(FileStream gate, Action restore) : IDisposable
    {
        private FileStream? _gate = gate;
        public void Dispose()
        {
            var current = Interlocked.Exchange(ref _gate, null);
            if (current is null) return;
            try { restore(); }
            finally { current.Dispose(); }
        }
    }
}
