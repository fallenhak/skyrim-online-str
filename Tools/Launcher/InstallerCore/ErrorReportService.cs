using System.IO.Compression;
using System.Net.Http.Headers;
using System.Text;

namespace SosStr.Launcher;

public sealed class ErrorReportService(HttpClient httpClient)
{
    public Task<string> CreateReportZipAsync(string destination, string description, string manifestVersion,
        string launcherLogPath, string stockGameDirectory, CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if (File.Exists(destination)) File.Delete(destination);
        using var zip = ZipFile.Open(destination, ZipArchiveMode.Create);
        AddText(zip, "report-description.txt", description);
        AddText(zip, "manifest-version.txt", manifestVersion);

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        AddFileIfExists(zip, launcherLogPath, "launcher/launcher.log", seen);
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var userDocs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        AddFileIfExists(zip, Path.Combine(local, "Skyrim Special Edition", "plugins.txt"), "game/plugins.txt", seen);
        AddFileIfExists(zip, Path.Combine(local, "Skyrim Special Edition", "loadorder.txt"), "game/loadorder.txt", seen);
        AddFilesByPattern(zip, Path.Combine(local, "Skyrim Special Edition"), "*.log", "logs/game-profile", seen);
        AddFilesByPattern(zip, Path.Combine(local, "SkyrimOnlineSTR", "logs"), "*.log", "logs/str", seen);
        AddFilesByPattern(zip, Path.Combine(userDocs, "My Games", "Skyrim Special Edition"), "*.log", "logs/game", seen);
        AddFilesByPattern(zip, Path.Combine(userDocs, "My Games", "Skyrim Special Edition", "SKSE"), "skse/*.log", "logs/skse", seen);
        AddFilesByPattern(zip, Path.Combine(userDocs, "My Games", "Skyrim Special Edition", "SKSE"), "skse/*.dmp", "logs/skse", seen);
        AddFilesByPattern(zip, Path.Combine(userDocs, "My Games", "Skyrim Special Edition", "SKSE", "Crashlogs"), "*.log", "logs/crash", seen);
        AddFilesByPattern(zip, Path.Combine(userDocs, "My Games", "Skyrim Special Edition"), "crash-*.log", "logs/crash", seen);
        AddFilesByPattern(zip, stockGameDirectory, "*.log", "logs/stock-game", seen);
        AddFilesByPattern(zip, Path.Combine(stockGameDirectory, "Data", "SKSE", "Plugins"), "*.log", "logs/skse-plugins", seen);
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(destination);
    }

    public async Task SendAsync(string endpoint, string token, string zipPath, CancellationToken cancellationToken = default)
    {
        if (!Uri.TryCreate(endpoint, UriKind.Absolute, out var uri) || uri.Scheme is not ("http" or "https"))
            throw new InvalidDataException("Hata raporu endpoint'i http:// veya https:// olmalı.");
        if (string.IsNullOrWhiteSpace(token))
            throw new InvalidOperationException("Hata raporu gönderimi için errorReportToken veya SOS_STR_REPORT_TOKEN ayarlanmalı.");
        using var form = new MultipartFormDataContent();
        await using var file = File.OpenRead(zipPath);
        var content = new StreamContent(file);
        content.Headers.ContentType = new MediaTypeHeaderValue("application/zip");
        form.Add(content, "file", Path.GetFileName(zipPath));
        using var request = new HttpRequestMessage(HttpMethod.Post, uri) { Content = form };
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        using var response = await httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    private static void AddFilesByPattern(ZipArchive zip, string directory, string pattern, string entryPrefix, HashSet<string> seen)
    {
        if (!Directory.Exists(directory)) return;
        foreach (var file in Directory.EnumerateFiles(directory, pattern.Contains('/') ? pattern[(pattern.LastIndexOf('/') + 1)..] : pattern))
            AddFileIfExists(zip, file, $"{entryPrefix}/{Path.GetFileName(file)}", seen);
    }

    private static void AddFileIfExists(ZipArchive zip, string path, string entry, HashSet<string> seen)
    {
        if (!File.Exists(path) || !seen.Add(Path.GetFullPath(path))) return;
        zip.CreateEntryFromFile(path, entry.Replace('\\', '/'), CompressionLevel.Optimal);
    }

    private static void AddText(ZipArchive zip, string name, string content)
    {
        var entry = zip.CreateEntry(name, CompressionLevel.Optimal);
        using var writer = new StreamWriter(entry.Open(), new UTF8Encoding(false));
        writer.Write(content);
    }
}
