namespace SosStr.Launcher;

// The launcher cannot update what it does not know about, so it replaces its own exe whenever
// the manifest publishes a different build. Identity is the file's SHA-256, not a version string.
public sealed class SelfUpdateService(HttpClient httpClient)
{
    public const string NewSuffix = ".new";
    public const string OldSuffix = ".old";

    public static bool NeedsUpdate(string executablePath, LauncherPackage? launcher) =>
        launcher is not null &&
        !string.Equals(Hashing.Sha256File(executablePath), launcher.Sha256, StringComparison.OrdinalIgnoreCase);

    // Downloads and verifies the new exe, then swaps it in. A running exe cannot be overwritten on
    // Windows but it can be renamed, so the current file moves aside to .old for the next start to delete.
    public async Task<bool> TryUpdateAsync(string executablePath, LauncherPackage? launcher, CancellationToken cancellationToken)
    {
        if (!NeedsUpdate(executablePath, launcher)) return false;
        var cache = Path.Combine(Path.GetDirectoryName(executablePath)!, ".launcher-update");
        var asset = await new DownloadService(httpClient).EnsureAssetAsync(launcher!.Url, launcher.Sha256, launcher.Size, cache,
            cancellationToken: cancellationToken);
        var staged = executablePath + NewSuffix;
        File.Copy(asset, staged, overwrite: true);
        Swap(executablePath, staged);
        return true;
    }

    public static void Swap(string executablePath, string stagedPath)
    {
        var old = executablePath + OldSuffix;
        if (File.Exists(old)) File.Delete(old);
        File.Move(executablePath, old);
        try { File.Move(stagedPath, executablePath); }
        catch
        {
            File.Move(old, executablePath);
            throw;
        }
    }

    public static void CleanupPrevious(string executablePath)
    {
        foreach (var leftover in new[] { executablePath + OldSuffix, executablePath + NewSuffix })
        {
            try { if (File.Exists(leftover)) File.Delete(leftover); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
