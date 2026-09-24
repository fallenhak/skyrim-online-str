using System.IO.Compression;
using System.Security.Cryptography;

namespace SosStr.Launcher;

public sealed class DeltaPatchApplier
{
    public async Task ApplyAsync(string gameDirectory, string patchArchive, string currentVersion, string requiredVersion,
        CancellationToken cancellationToken = default)
    {
        using var zip = ZipFile.OpenRead(patchArchive);
        var metadata = zip.GetEntry("patch.json") ?? throw new InvalidDataException("Delta paketi patch.json içermiyor.");
        DeltaPatchDocument patch;
        using (var reader = new StreamReader(metadata.Open()))
            patch = ManifestReader.ParseJson<DeltaPatchDocument>(await reader.ReadToEndAsync(cancellationToken));

        if (patch.Format != "sostr-delta-v1") throw new InvalidDataException("Delta paketi biçimi desteklenmiyor.");
        if (GameVersion.Normalize(patch.FromVersion) != GameVersion.Normalize(currentVersion) ||
            GameVersion.Normalize(patch.ToVersion) != GameVersion.Normalize(requiredVersion))
            throw new InvalidDataException("Delta paketi oyun sürümüyle eşleşmiyor.");

        var staged = new List<(string Temp, string Destination)>();
        var patchTemp = Path.Combine(gameDirectory, ".sos-str-patch-work-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(patchTemp);
        try
        {
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var index = 0;
            foreach (var file in patch.Files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var relative = SafePath.NormalizeRelative(file.Path);
                if (!seen.Add(relative)) throw new InvalidDataException($"Patch'te tekrarlanan dosya yolu: {relative}");
                if (file.Size < 0 || (file.Operations.Count == 0 && file.Size != 0))
                    throw new InvalidDataException($"Patch dosya kaydı geçersiz: {relative}");
                var destination = SafePath.UnderRoot(gameDirectory, relative);
                var originalExists = File.Exists(destination);
                if (string.IsNullOrWhiteSpace(file.BaseSha256) != !originalExists)
                    throw new InvalidDataException($"Patch önkoşulu karşılanmıyor: {relative}");
                if (originalExists && !string.Equals(Hashing.Sha256File(destination), file.BaseSha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"Kaynak oyun dosyası beklenen SHA-256 ile eşleşmiyor: {relative}");

                var payload = file.Operations.Any(op => op.Kind == "insert")
                    ? zip.GetEntry(file.Payload) ?? throw new InvalidDataException($"Patch verisi bulunamadı: {file.Payload}")
                    : null;
                var temp = Path.Combine(patchTemp, $"{index++}.new");
                await using (var output = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 128 * 1024, true))
                await using (var original = originalExists ? new FileStream(destination, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, true) : null)
                {
                    long written = 0;
                    foreach (var operation in file.Operations)
                    {
                        if (operation.Offset < 0 || operation.Length < 0) throw new InvalidDataException("Patch işlemi negatif aralık içeriyor.");
                        if (operation.Kind == "copy")
                        {
                            if (original is null || operation.Offset + operation.Length > original.Length)
                                throw new InvalidDataException($"Patch kaynak aralığı geçersiz: {relative}");
                            original.Position = operation.Offset;
                            await CopyExactlyAsync(original, output, operation.Length, cancellationToken);
                        }
                        else if (operation.Kind == "insert")
                        {
                            await using var data = payload!.Open();
                            await SkipExactlyAsync(data, operation.Offset, cancellationToken);
                            await CopyExactlyAsync(data, output, operation.Length, cancellationToken);
                        }
                        else throw new InvalidDataException($"Bilinmeyen delta işlemi: {operation.Kind}");
                        written = checked(written + operation.Length);
                    }
                    if (written != file.Size) throw new InvalidDataException($"Patch hedef boyutu tutmuyor: {relative}");
                }
                if (!string.Equals(Hashing.Sha256File(temp), file.Sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"Patch sonucu SHA-256 doğrulamasından geçmedi: {relative}");
                staged.Add((temp, destination));
            }

            foreach (var deleted in patch.Delete)
            {
                var relative = SafePath.NormalizeRelative(deleted);
                if (!seen.Add(relative)) throw new InvalidDataException($"Patch yolunu hem yazıp hem siliyor: {relative}");
                _ = SafePath.UnderRoot(gameDirectory, relative);
            }

            foreach (var (temp, destination) in staged)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Move(temp, destination, overwrite: true);
            }
            foreach (var deleted in patch.Delete)
            {
                var destination = SafePath.UnderRoot(gameDirectory, deleted);
                if (File.Exists(destination)) File.Delete(destination);
            }
        }
        finally
        {
            if (Directory.Exists(patchTemp)) Directory.Delete(patchTemp, recursive: true);
        }
    }

    private static async Task CopyExactlyAsync(Stream from, Stream to, long bytes, CancellationToken ct)
    {
        var buffer = new byte[64 * 1024];
        while (bytes > 0)
        {
            var read = await from.ReadAsync(buffer.AsMemory(0, (int)Math.Min(buffer.Length, bytes)), ct);
            if (read == 0) throw new EndOfStreamException("Delta paketi beklenenden erken bitti.");
            await to.WriteAsync(buffer.AsMemory(0, read), ct);
            bytes -= read;
        }
    }

    private static async Task SkipExactlyAsync(Stream stream, long bytes, CancellationToken ct)
    {
        var buffer = new byte[64 * 1024];
        while (bytes > 0)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(0, (int)Math.Min(buffer.Length, bytes)), ct);
            if (read == 0) throw new EndOfStreamException("Delta paketi ek verisi beklenenden erken bitti.");
            bytes -= read;
        }
    }
}
