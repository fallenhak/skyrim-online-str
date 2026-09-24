using System.Net;
using System.Net.Http.Headers;

namespace SosStr.Launcher;

public sealed record DownloadProgress(long Received, long Total);

public sealed class DownloadService(HttpClient httpClient)
{
    public async Task<string> EnsureAssetAsync(string url, string sha256, long expectedSize, string cacheDirectory,
        IProgress<DownloadProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(cacheDirectory);
        var finalPath = Path.Combine(cacheDirectory, sha256.ToLowerInvariant() + ".asset");
        if (await IsValidAsync(finalPath, sha256, expectedSize, cancellationToken)) return finalPath;
        if (File.Exists(finalPath)) File.Delete(finalPath);

        var partialPath = finalPath + ".part";
        for (var attempt = 0; attempt < 2; attempt++)
        {
            var offset = File.Exists(partialPath) ? new FileInfo(partialPath).Length : 0;
            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            if (offset > 0) request.Headers.Range = new RangeHeaderValue(offset, null);
            using var response = await httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            if (response.StatusCode == HttpStatusCode.RequestedRangeNotSatisfiable && offset > 0)
            {
                File.Delete(partialPath);
                continue;
            }
            response.EnsureSuccessStatusCode();

            var append = offset > 0 && response.StatusCode == HttpStatusCode.PartialContent && response.Content.Headers.ContentRange?.From == offset;
            if (offset > 0 && !append) offset = 0;
            await using (var output = new FileStream(partialPath, append ? FileMode.Append : FileMode.Create,
                             FileAccess.Write, FileShare.None, 128 * 1024, useAsync: true))
            await using (var input = await response.Content.ReadAsStreamAsync(cancellationToken))
            {
                var buffer = new byte[128 * 1024];
                long received = offset;
                int count;
                while ((count = await input.ReadAsync(buffer, cancellationToken)) > 0)
                {
                    await output.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
                    received += count;
                    progress?.Report(new DownloadProgress(received, expectedSize));
                }
            }

            if (await IsValidAsync(partialPath, sha256, expectedSize, cancellationToken))
            {
                File.Move(partialPath, finalPath, overwrite: true);
                return finalPath;
            }
            File.Delete(partialPath);
            throw new InvalidDataException("İndirilen dosyanın boyutu veya SHA-256 değeri manifestle eşleşmiyor.");
        }
        throw new IOException("İndirme devam ettirilemedi. Bağlantıyı kontrol edip tekrar deneyin.");
    }

    private static async Task<bool> IsValidAsync(string path, string sha256, long expectedSize, CancellationToken ct)
    {
        if (!File.Exists(path) || new FileInfo(path).Length != expectedSize) return false;
        await using var stream = File.OpenRead(path);
        var actual = Convert.ToHexString(await System.Security.Cryptography.SHA256.HashDataAsync(stream, ct)).ToLowerInvariant();
        return string.Equals(actual, sha256, StringComparison.OrdinalIgnoreCase);
    }
}
