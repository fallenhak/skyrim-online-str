using System.Text.Json;
using System.Text.RegularExpressions;

namespace SosStr.Launcher;

public sealed class LauncherManifest
{
    public int SchemaVersion { get; set; }
    public string ManifestVersion { get; set; } = "";
    public string RequiredGameVersion { get; set; } = "";
    public List<ModPackage> Mods { get; set; } = [];
    public LauncherPackage? Launcher { get; set; }
}

public sealed class LauncherPackage
{
    public string Version { get; set; } = "";
    public string Url { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public long Size { get; set; }
}

public sealed class ModPackage
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "";
    public string Url { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public long Size { get; set; }
    public string Target { get; set; } = "data";
    public int Order { get; set; }
    public string StripPrefix { get; set; } = "";
    public List<string> Plugins { get; set; } = [];
}

public static partial class ManifestReader
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true
    };

    public static LauncherManifest ParseAndValidate(string json)
    {
        LauncherManifest manifest;
        try
        {
            manifest = JsonSerializer.Deserialize<LauncherManifest>(json, JsonOptions)
                ?? throw new InvalidDataException("Manifest boş.");
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Manifest JSON biçimi geçersiz: {ex.Message}", ex);
        }

        if (manifest.SchemaVersion != 1)
            throw new InvalidDataException("Manifest schemaVersion değeri 1 olmalı.");
        RequireText(manifest.ManifestVersion, "manifestVersion");
        if (!GameVersion.TryNormalize(manifest.RequiredGameVersion, out var requiredGameVersion))
            throw new InvalidDataException("requiredGameVersion geçerli bir oyun sürümü olmalı.");
        if (requiredGameVersion != GameVersion.Required)
            throw new InvalidDataException($"Manifest oyun sürümü {requiredGameVersion}; STR kurulumu yalnız Skyrim SE {GameVersion.Required} sürümünü kabul eder.");
        manifest.Mods ??= [];
        if (manifest.Launcher is not null)
            ValidateUrlAndHash(manifest.Launcher.Url, manifest.Launcher.Sha256, manifest.Launcher.Size, "launcher");

        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var mod in manifest.Mods)
        {
            RequireText(mod.Id, "mod.id");
            RequireText(mod.Name, $"{mod.Id}.name");
            RequireText(mod.Version, $"{mod.Id}.version");
            ValidateUrlAndHash(mod.Url, mod.Sha256, mod.Size, mod.Id);
            if (mod.Target is not ("root" or "data"))
                throw new InvalidDataException($"{mod.Id}.target yalnızca 'root' veya 'data' olabilir.");
            if (!ids.Add(mod.Id))
                throw new InvalidDataException($"Manifest'te tekrarlanan mod id: {mod.Id}");
            ValidateRelativePathPrefix(mod.StripPrefix, $"{mod.Id}.stripPrefix");
            mod.Plugins ??= [];
            foreach (var plugin in mod.Plugins)
            {
                var safePlugin = SafePath.NormalizeRelative(plugin);
                if (!string.Equals(safePlugin, Path.GetFileName(safePlugin), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"{mod.Id}.plugins yalnızca dosya adı içerebilir: {plugin}");
                var extension = Path.GetExtension(plugin);
                if (!(extension.Equals(".esm", StringComparison.OrdinalIgnoreCase) ||
                      extension.Equals(".esp", StringComparison.OrdinalIgnoreCase) ||
                      extension.Equals(".esl", StringComparison.OrdinalIgnoreCase)))
                    throw new InvalidDataException($"{mod.Id}.plugins yalnızca ESM/ESP/ESL dosyası olabilir: {plugin}");
            }
        }

        manifest.Mods = manifest.Mods.OrderBy(m => m.Order).ToList();
        return manifest;
    }

    public static T ParseJson<T>(string json) => JsonSerializer.Deserialize<T>(json, JsonOptions)
        ?? throw new InvalidDataException("JSON içeriği boş.");

    public static string Serialize<T>(T value) => JsonSerializer.Serialize(value, new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    });

    private static void ValidateUrlAndHash(string url, string sha256, long size, string name)
    {
        if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme is not ("http" or "https"))
            throw new InvalidDataException($"{name}: URL http:// veya https:// olmalı.");
        if (!Sha256Pattern().IsMatch(sha256))
            throw new InvalidDataException($"{name}: SHA-256 64 onaltılık karakter olmalı.");
        if (size < 0)
            throw new InvalidDataException($"{name}: size negatif olamaz.");
    }

    private static void ValidateRelativePathPrefix(string prefix, string property)
    {
        if (string.IsNullOrWhiteSpace(prefix)) return;
        if (Path.IsPathRooted(prefix) || prefix.Contains(':') || prefix.Split(['/', '\\']).Any(p => p == ".."))
            throw new InvalidDataException($"{property}: göreli ve güvenli bir klasör yolu olmalı.");
    }

    private static void RequireText(string value, string property)
    {
        if (string.IsNullOrWhiteSpace(value)) throw new InvalidDataException($"{property} boş bırakılamaz.");
    }

    [GeneratedRegex("^[a-fA-F0-9]{64}$", RegexOptions.CultureInvariant)]
    private static partial Regex Sha256Pattern();
}
