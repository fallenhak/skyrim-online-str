using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;
using SosStr.Launcher;

var tests = new (string Name, Func<Task> Run)[]
{
    ("manifest okuma ve doğrulama", ManifestValidation),
    ("manifest hatalı değerleri reddeder", ManifestRejectsBadValues),
    ("Steam VDF yeni ve eski kitaplık biçimleri", SteamVdfParsing),
    ("oyun sürümünü normalleştirme", VersionNormalization),
    ("indirmeyi yarım dosyadan sürdürme ve sha256 doğrulama", ResumableDownload),
    ("hatalı indirmeyi silme", InvalidDownloadRejected),
    ("manifest varlık üreticisi boyut ve sha yazar", ManifestAssetGenerator),
    ("Steam kopyası oluşturma ve CC temizliği", StockCopyAndCreationClubCleanup),
    ("delta patch kopyala/ekle işlemleri", DeltaPatchApplication),
    ("delta üreticisi patch uygulayıcıyla uyumlu", DeltaBuilderRoundTrip),
    ("mod kök/Data dağıtımı ve eski dosyaları geri alma", ModDeployment),
    ("plugins.txt oyun oturumunda uygulanır ve geri yüklenir", PluginProfileBackupAndRestore),
    ("hata raporu ZIP'i ve Bearer token yüklemesi", ErrorReportBundleAndUpload),
    ("zip traversal reddi", ZipTraversalRejected)
};

var failures = new List<string>();
foreach (var test in tests)
{
    try { await test.Run(); Console.WriteLine($"PASS {test.Name}"); }
    catch (Exception ex) { failures.Add($"FAIL {test.Name}: {ex}"); Console.WriteLine(failures[^1]); }
}
Console.WriteLine($"{tests.Length - failures.Count}/{tests.Length} test geçti.");
return failures.Count == 0 ? 0 : 1;

static Task ManifestValidation()
{
    var manifest = ManifestReader.ParseAndValidate(ValidManifest());
    Assert.Equal(1, manifest.SchemaVersion);
    Assert.Equal("1.6.1170", GameVersion.Normalize(manifest.RequiredGameVersion));
    Assert.Equal("first", manifest.Mods[0].Id);
    var example = ManifestReader.ParseAndValidate(File.ReadAllText(Path.Combine("Tools", "Launcher", "manifest.example.json")));
    Assert.Equal("1.6.1170", GameVersion.Normalize(example.RequiredGameVersion));
    Assert.Equal("engine-fixes-part2", example.Mods.Single(m => m.Id == "engine-fixes-part2").Id);
    return Task.CompletedTask;
}

static Task ManifestRejectsBadValues()
{
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace("https://example.com/a.zip", "file:///a.zip")));
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace(new string('a', 64), "bad")));
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace("\"target\": \"data\"", "\"target\": \"anywhere\"")));
    return Task.CompletedTask;
}

static Task SteamVdfParsing()
{
    const string vdf = "\"libraryfolders\"\n{\n \"0\" \"C:\\\\Steam\"\n \"1\"\n {\n  \"path\" \"D:\\\\Games\\\\SteamLibrary\"\n  \"apps\"\n  {\n   \"489830\" \"1\"\n  }\n }\n}";
    var roots = SteamLocator.ParseLibraryFolders(vdf, @"C:\Steam");
    Assert.True(roots.Any(x => x.EndsWith(@"SteamLibrary", StringComparison.OrdinalIgnoreCase)));
    Assert.True(roots.Any(x => x.EndsWith(@"Steam", StringComparison.OrdinalIgnoreCase)));
    Assert.Equal(2, roots.Count);
    return Task.CompletedTask;
}

static Task VersionNormalization()
{
    Assert.Equal("1.6.1170", GameVersion.Normalize("1.6.1170.0"));
    Assert.Equal("1.6.640", GameVersion.Normalize("Skyrim SE 1.6.640.0 (Steam)"));
    Assert.False(GameVersion.TryNormalize("unknown", out _));
    return Task.CompletedTask;
}

static async Task ResumableDownload()
{
    using var temp = new TempDirectory();
    var content = Encoding.UTF8.GetBytes("verified download payload");
    var first = content[..8];
    var cache = Path.Combine(temp.Path, "cache");
    Directory.CreateDirectory(cache);
    var hash = Sha(content);
    await File.WriteAllBytesAsync(Path.Combine(cache, hash + ".asset.part"), first);
    var handler = new RangeHandler(content, first.Length);
    var downloader = new DownloadService(new HttpClient(handler));
    var result = await downloader.EnsureAssetAsync("https://example.com/mod.zip", hash, content.Length, cache);
    Assert.Bytes(content, await File.ReadAllBytesAsync(result));
    Assert.Equal(first.Length, handler.RequestedRangeFrom);
}

static async Task InvalidDownloadRejected()
{
    using var temp = new TempDirectory();
    var handler = new FixedHandler(Encoding.UTF8.GetBytes("wrong bytes"));
    var downloader = new DownloadService(new HttpClient(handler));
    await Assert.ThrowsAsync<InvalidDataException>(() => downloader.EnsureAssetAsync("https://example.com/mod.zip", new string('0', 64), 11, temp.Path));
    Assert.False(File.Exists(Path.Combine(temp.Path, new string('0', 64) + ".asset.part")));
}

static async Task ManifestAssetGenerator()
{
    using var temp = new TempDirectory();
    var assets = Path.Combine(temp.Path, "assets");
    Directory.CreateDirectory(assets);
    var bytes = Encoding.UTF8.GetBytes("server package bytes");
    await File.WriteAllBytesAsync(Path.Combine(assets, "sample.zip"), bytes);
    var manifestPath = Path.Combine(temp.Path, "manifest.json");
    await File.WriteAllTextAsync(manifestPath, $$"""
    {"schemaVersion":1,"manifestVersion":"1","requiredGameVersion":"1.6.1170","patches":[],"mods":[{"id":"sample","name":"sample","version":"1","url":"https://example.com/sample.zip","sha256":"{{new string('0', 64)}}","size":0,"target":"data","order":0}]}
    """);
    await RunPythonScript("update_manifest_assets.py", manifestPath, "--asset-dir", assets);
    var generated = ManifestReader.ParseAndValidate(await File.ReadAllTextAsync(manifestPath));
    Assert.Equal(bytes.Length, generated.Mods[0].Size);
    Assert.Equal(Sha(bytes), generated.Mods[0].Sha256);
}

static async Task StockCopyAndCreationClubCleanup()
{
    using var temp = new TempDirectory();
    var source = Path.Combine(temp.Path, "steam");
    var target = Path.Combine(temp.Path, "launcher", "Stock Game");
    Directory.CreateDirectory(Path.Combine(source, "Data"));
    await File.WriteAllTextAsync(Path.Combine(source, "SkyrimSE.exe"), "game from steam");
    await File.WriteAllTextAsync(Path.Combine(source, "Data", "ccBGSSSE001-Fish.esl"), "cc");
    await File.WriteAllTextAsync(Path.Combine(source, "Data", "Skyrim.esm"), "vanilla");
    await File.WriteAllTextAsync(Path.Combine(source, "Skyrim.ccc"), "cc index");

    await new StockGameService().CopyFromSteamAsync(source, target);
    Assert.Equal("game from steam", await File.ReadAllTextAsync(Path.Combine(target, "SkyrimSE.exe")));
    Assert.False(File.Exists(Path.Combine(target, "Data", "ccBGSSSE001-Fish.esl")));
    Assert.True(File.Exists(Path.Combine(target, "Data", "Skyrim.esm")));
    Assert.False(File.Exists(Path.Combine(target, "Skyrim.ccc")));
    await File.WriteAllTextAsync(Path.Combine(source, "SkyrimSE.exe"), "Steam updated");
    await new StockGameService().CopyFromSteamAsync(source, target);
    Assert.Equal("game from steam", await File.ReadAllTextAsync(Path.Combine(target, "SkyrimSE.exe")));
}

static async Task DeltaPatchApplication()
{
    using var temp = new TempDirectory();
    var game = Path.Combine(temp.Path, "Stock Game");
    Directory.CreateDirectory(game);
    var exe = Path.Combine(game, "SkyrimSE.exe");
    var original = Encoding.UTF8.GetBytes("HELLO world");
    var final = Encoding.UTF8.GetBytes("HELLO STR!");
    await File.WriteAllBytesAsync(exe, original);
    var archive = Path.Combine(temp.Path, "patch.zip");
    using (var zip = ZipFile.Open(archive, ZipArchiveMode.Create))
    {
        var doc = new DeltaPatchDocument
        {
            Format = "sostr-delta-v1", FromVersion = "1.6.640", ToVersion = "1.6.1170",
            Files = [new DeltaFile
            {
                Path = "SkyrimSE.exe", BaseSha256 = Sha(original), Sha256 = Sha(final), Size = final.Length,
                Payload = "payload/exe.bin", Operations = [new DeltaOperation { Kind = "copy", Offset = 0, Length = 6 }, new DeltaOperation { Kind = "insert", Offset = 0, Length = 4 }]
            }]
        };
        var jsonEntry = zip.CreateEntry("patch.json");
        await using (var writer = new StreamWriter(jsonEntry.Open())) await writer.WriteAsync(ManifestReader.Serialize(doc));
        var dataEntry = zip.CreateEntry("payload/exe.bin");
        await using (var stream = dataEntry.Open()) await stream.WriteAsync(Encoding.UTF8.GetBytes("STR!"));
    }
    await new DeltaPatchApplier().ApplyAsync(game, archive, "1.6.640.0", "1.6.1170.0");
    Assert.Bytes(final, await File.ReadAllBytesAsync(exe));
}

static async Task DeltaBuilderRoundTrip()
{
    using var temp = new TempDirectory();
    var source = Path.Combine(temp.Path, "source");
    var target = Path.Combine(temp.Path, "target");
    var game = Path.Combine(temp.Path, "game");
    Directory.CreateDirectory(source);
    Directory.CreateDirectory(target);
    Directory.CreateDirectory(game);
    var original = Enumerable.Range(0, 32_768).Select(x => (byte)(x % 251)).ToArray();
    var updated = original.ToArray();
    updated[8_100] = 99;
    updated = [.. updated, 1, 2, 3, 4];
    await File.WriteAllBytesAsync(Path.Combine(source, "SkyrimSE.exe"), original);
    await File.WriteAllBytesAsync(Path.Combine(target, "SkyrimSE.exe"), updated);
    await File.WriteAllTextAsync(Path.Combine(source, "removed.txt"), "old");
    await File.WriteAllTextAsync(Path.Combine(target, "new.txt"), "new file");
    foreach (var file in Directory.EnumerateFiles(source)) File.Copy(file, Path.Combine(game, Path.GetFileName(file)));
    var patch = Path.Combine(temp.Path, "patch.zip");
    await RunPythonScript("make_delta_patch.py", source, target, patch, "--from-version", "1.6.640");
    await new DeltaPatchApplier().ApplyAsync(game, patch, "1.6.640", "1.6.1170");
    var expected = Directory.EnumerateFiles(target).Select(Path.GetFileName).OrderBy(x => x).ToArray();
    var actual = Directory.EnumerateFiles(game).Select(Path.GetFileName).OrderBy(x => x).ToArray();
    Assert.True(expected.SequenceEqual(actual));
    foreach (var filename in expected) Assert.Bytes(await File.ReadAllBytesAsync(Path.Combine(target, filename!)), await File.ReadAllBytesAsync(Path.Combine(game, filename!)));
}

static async Task ModDeployment()
{
    using var temp = new TempDirectory();
    var game = Path.Combine(temp.Path, "Stock Game");
    var state = Path.Combine(temp.Path, "state");
    Directory.CreateDirectory(Path.Combine(game, "Data", "SKSE", "Plugins"));
    await File.WriteAllTextAsync(Path.Combine(game, "loader.dll"), "stock loader");
    await File.WriteAllTextAsync(Path.Combine(game, "Data", "SKSE", "Plugins", "str.dll"), "old version");
    var rootArchive = CreateZip(temp.Path, "root-v1.zip", ("loader.dll", "root mod"), ("old.dll", "obsolete"));
    var dataArchive = CreateZip(temp.Path, "data-v1.zip", ("Data/SKSE/Plugins/str.dll", "STR mod"));
    var root = Mod("root", "root", 0, rootArchive, "");
    var data = Mod("str", "data", 1, dataArchive, "Data");
    await new ModDeploymentService().ApplyAsync(game, state, [root, data], new Dictionary<string, string> { ["root"] = rootArchive, ["str"] = dataArchive });
    Assert.Equal("root mod", await File.ReadAllTextAsync(Path.Combine(game, "loader.dll")));
    Assert.Equal("STR mod", await File.ReadAllTextAsync(Path.Combine(game, "Data", "SKSE", "Plugins", "str.dll")));

    var updated = CreateZip(temp.Path, "root-v2.zip", ("loader.dll", "updated root"));
    root = Mod("root", "root", 0, updated, "");
    await new ModDeploymentService().ApplyAsync(game, state, [root], new Dictionary<string, string> { ["root"] = updated });
    Assert.Equal("updated root", await File.ReadAllTextAsync(Path.Combine(game, "loader.dll")));
    Assert.False(File.Exists(Path.Combine(game, "old.dll")));
    Assert.Equal("old version", await File.ReadAllTextAsync(Path.Combine(game, "Data", "SKSE", "Plugins", "str.dll")));
    await new ModDeploymentService().ApplyAsync(game, state, [], new Dictionary<string, string>());
    Assert.Equal("stock loader", await File.ReadAllTextAsync(Path.Combine(game, "loader.dll")));
}

static async Task ZipTraversalRejected()
{
    using var temp = new TempDirectory();
    var game = Path.Combine(temp.Path, "game");
    Directory.CreateDirectory(game);
    var archive = CreateZip(temp.Path, "bad.zip", ("../escape.txt", "bad"));
    var mod = Mod("bad", "data", 0, archive, "");
    await Assert.ThrowsAsync<InvalidDataException>(() => new ModDeploymentService().ApplyAsync(game, Path.Combine(temp.Path, "state"), [mod], new Dictionary<string, string> { ["bad"] = archive }));
    Assert.False(File.Exists(Path.Combine(temp.Path, "escape.txt")));
}

static async Task PluginProfileBackupAndRestore()
{
    using var temp = new TempDirectory();
    var game = Path.Combine(temp.Path, "Stock Game");
    var profile = Path.Combine(temp.Path, "profile");
    var state = Path.Combine(temp.Path, "state");
    Directory.CreateDirectory(Path.Combine(game, "Data"));
    Directory.CreateDirectory(profile);
    await File.WriteAllTextAsync(Path.Combine(game, "Data", "Skyrim.esm"), "master");
    await File.WriteAllTextAsync(Path.Combine(game, "Data", "TestMod.esp"), "plugin");
    await File.WriteAllTextAsync(Path.Combine(profile, "plugins.txt"), "*Original.esp\n");
    await File.WriteAllTextAsync(Path.Combine(profile, "loadorder.txt"), "Original.esp\n");
    var package = new ModPackage { Id = "test", Name = "test", Version = "1", Url = "https://example.com/test.zip", Sha256 = new string('0', 64), Size = 0, Target = "data", Plugins = ["TestMod.esp"] };
    using (new PluginProfileService().ApplyForSession(game, profile, state, [package]))
    {
        Assert.Equal("*Skyrim.esm" + Environment.NewLine + "*TestMod.esp" + Environment.NewLine, await File.ReadAllTextAsync(Path.Combine(profile, "plugins.txt")));
        Assert.Equal("Skyrim.esm" + Environment.NewLine + "TestMod.esp" + Environment.NewLine, await File.ReadAllTextAsync(Path.Combine(profile, "loadorder.txt")));
    }
    Assert.Equal("*Original.esp\n", await File.ReadAllTextAsync(Path.Combine(profile, "plugins.txt")));
    Assert.Equal("Original.esp\n", await File.ReadAllTextAsync(Path.Combine(profile, "loadorder.txt")));
}

static async Task ErrorReportBundleAndUpload()
{
    using var temp = new TempDirectory();
    var log = Path.Combine(temp.Path, "launcher.log");
    var game = Path.Combine(temp.Path, "game");
    var report = Path.Combine(temp.Path, "report.zip");
    Directory.CreateDirectory(game);
    await File.WriteAllTextAsync(log, "launcher details");
    await File.WriteAllTextAsync(Path.Combine(game, "SkyrimTogether.log"), "STR details");
    await new ErrorReportService(new HttpClient(new FixedHandler([]))).CreateReportZipAsync(report, "crashes on start", "test-3", log, game);
    using (var zip = ZipFile.OpenRead(report))
    {
        Assert.True(zip.GetEntry("report-description.txt") is not null);
        Assert.True(zip.GetEntry("manifest-version.txt") is not null);
        Assert.True(zip.GetEntry("launcher/launcher.log") is not null);
        Assert.True(zip.GetEntry("logs/stock-game/SkyrimTogether.log") is not null);
    }
    var capture = new CaptureHandler();
    await new ErrorReportService(new HttpClient(capture)).SendAsync("https://reports.example.test/api", "secret-token", report);
    Assert.Equal("Bearer secret-token", capture.Authorization);
    Assert.True(capture.BodyContainsZip);
    await Assert.ThrowsAsync<InvalidOperationException>(() => new ErrorReportService(new HttpClient(new FixedHandler([]))).SendAsync("https://reports.example.test/api", "", report));
}

static string ValidManifest() => $$"""
{
  "schemaVersion": 1,
  "manifestVersion": "test-1",
  "requiredGameVersion": "1.6.1170.0",
  "patches": [],
  "mods": [{ "id": "first", "name": "First", "version": "1", "url": "https://example.com/a.zip", "sha256": "{{new string('a', 64)}}", "size": 10, "target": "data", "order": 1 }]
}
""";

static ModPackage Mod(string id, string target, int order, string archive, string stripPrefix) => new()
{
    Id = id, Name = id, Version = "1", Url = "https://example.com/" + id + ".zip", Sha256 = Hashing.Sha256File(archive),
    Size = new FileInfo(archive).Length, Target = target, Order = order, StripPrefix = stripPrefix
};

static string CreateZip(string directory, string filename, params (string Name, string Content)[] files)
{
    var path = Path.Combine(directory, filename);
    using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
    foreach (var (name, content) in files)
    {
        using var writer = new StreamWriter(zip.CreateEntry(name).Open());
        writer.Write(content);
    }
    return path;
}

static string Sha(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

static async Task RunPythonScript(string scriptName, params string[] args)
{
    var script = Path.GetFullPath(Path.Combine("Tools", "Launcher", "Scripts", scriptName));
    if (!File.Exists(script)) throw new FileNotFoundException("Python helper script not found from repository root.", script);
    var start = new System.Diagnostics.ProcessStartInfo("python") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
    start.ArgumentList.Add(script);
    foreach (var arg in args) start.ArgumentList.Add(arg);
    using var process = System.Diagnostics.Process.Start(start) ?? throw new InvalidOperationException("Python script process failed to start.");
    var stdoutTask = process.StandardOutput.ReadToEndAsync();
    var stderrTask = process.StandardError.ReadToEndAsync();
    await process.WaitForExitAsync();
    if (process.ExitCode != 0) throw new Exception($"Python script failed ({process.ExitCode}): {await stderrTask}\n{await stdoutTask}");
}

static class Assert
{
    public static void True(bool value) { if (!value) throw new Exception("Expected true."); }
    public static void False(bool value) { if (value) throw new Exception("Expected false."); }
    public static void Equal<T>(T expected, T actual) { if (!EqualityComparer<T>.Default.Equals(expected, actual)) throw new Exception($"Expected <{expected}>, got <{actual}>."); }
    public static void Bytes(byte[] expected, byte[] actual) { if (!expected.SequenceEqual(actual)) throw new Exception("Byte arrays do not match."); }
    public static void Throws<T>(Action action) where T : Exception { try { action(); } catch (T) { return; } throw new Exception($"Expected exception {typeof(T).Name}."); }
    public static async Task ThrowsAsync<T>(Func<Task> action) where T : Exception { try { await action(); } catch (T) { return; } throw new Exception($"Expected exception {typeof(T).Name}."); }
}

sealed class TempDirectory : IDisposable
{
    public string Path { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "sostr-tests-" + Guid.NewGuid().ToString("N"));
    public TempDirectory() => Directory.CreateDirectory(Path);
    public void Dispose() { if (Directory.Exists(Path)) Directory.Delete(Path, true); }
}

sealed class FixedHandler(byte[] bytes) : HttpMessageHandler
{
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) =>
        Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) });
}

sealed class RangeHandler(byte[] bytes, int startingOffset) : HttpMessageHandler
{
    public long? RequestedRangeFrom { get; private set; }
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        RequestedRangeFrom = request.Headers.Range?.Ranges.FirstOrDefault()?.From;
        var content = new ByteArrayContent(bytes[startingOffset..]);
        var response = new HttpResponseMessage(HttpStatusCode.PartialContent) { Content = content };
        response.Content.Headers.ContentRange = new ContentRangeHeaderValue(startingOffset, bytes.Length - 1, bytes.Length);
        return Task.FromResult(response);
    }
}

sealed class CaptureHandler : HttpMessageHandler
{
    public string? Authorization { get; private set; }
    public bool BodyContainsZip { get; private set; }
    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        Authorization = request.Headers.Authorization?.ToString();
        var body = await request.Content!.ReadAsByteArrayAsync(cancellationToken);
        BodyContainsZip = request.Content.Headers.ContentType?.MediaType == "multipart/form-data" &&
            body.AsSpan().IndexOf(new byte[] { 0x50, 0x4b, 0x03, 0x04 }) >= 0;
        return new HttpResponseMessage(HttpStatusCode.OK);
    }
}
