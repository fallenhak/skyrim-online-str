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
    ("bağımsız Stock Game kopyası sürüm ve SHA-256 ile kilitlenir", StockCopyVersionLockAndIndependence),
    ("yanlış Steam oyun sürümü reddedilir", WrongSteamVersionRejected),
    ("mod kök/Data dağıtımı ve eski dosyaları geri alma", ModDeployment),
    ("plugins.txt oyun oturumunda uygulanır ve geri yüklenir", PluginProfileBackupAndRestore),
    ("hata raporu ZIP'i ve Bearer token yüklemesi", ErrorReportBundleAndUpload),
    ("zip traversal reddi", ZipTraversalRejected),
    ("Discord token talepleri ve sÃ¼re sonu", AuthTokenClaimsValidation),
    ("Discord oturumu DPAPI ile saklanÄ±r", AuthSessionDpapiRoundTrip),
    ("oyun yapÄ±landÄ±rmasÄ± token'Ä± DPAPI ile korur", NativeAuthConfigurationProtectsToken),
    ("Discord oturumu acilista yenilenir", AuthSessionRefreshReplacesToken)
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
    Assert.Equal(GameVersion.Required, GameVersion.Normalize(manifest.RequiredGameVersion));
    Assert.Equal("first", manifest.Mods[0].Id);
    var example = ManifestReader.ParseAndValidate(File.ReadAllText(Path.Combine("Tools", "Launcher", "manifest.example.json")));
    Assert.Equal(GameVersion.Required, GameVersion.Normalize(example.RequiredGameVersion));
    Assert.Equal("engine-fixes-part2", example.Mods.Single(m => m.Id == "engine-fixes-part2").Id);
    Assert.Equal("2.3.1 / Skyrim 1.7.104", example.Mods.Single(m => m.Id == "skse64-root").Version);
    return Task.CompletedTask;
}

static Task ManifestRejectsBadValues()
{
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace("https://example.com/a.zip", "file:///a.zip")));
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace(new string('a', 64), "bad")));
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace("\"target\": \"data\"", "\"target\": \"anywhere\"")));
    Assert.Throws<InvalidDataException>(() => ManifestReader.ParseAndValidate(ValidManifest().Replace("1.7.104.0", "1.6.1170.0")));
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
    Assert.Equal("1.7.104", GameVersion.Normalize("1.7.104.0"));
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
    {"schemaVersion":1,"manifestVersion":"1","requiredGameVersion":"1.7.104","mods":[{"id":"sample","name":"sample","version":"1","url":"https://example.com/sample.zip","sha256":"{{new string('0', 64)}}","size":0,"target":"data","order":0}]}
    """);
    await RunPythonScript("update_manifest_assets.py", manifestPath, "--asset-dir", assets);
    var generated = ManifestReader.ParseAndValidate(await File.ReadAllTextAsync(manifestPath));
    Assert.Equal(bytes.Length, generated.Mods[0].Size);
    Assert.Equal(Sha(bytes), generated.Mods[0].Sha256);
}

static async Task StockCopyVersionLockAndIndependence()
{
    using var temp = new TempDirectory();
    var source = Path.Combine(temp.Path, "steam");
    var target = Path.Combine(temp.Path, "launcher", "Stock Game");
    Directory.CreateDirectory(Path.Combine(source, "Data"));
    var sourceExe = Path.Combine(source, "SkyrimSE.exe");
    var targetExe = Path.Combine(target, "SkyrimSE.exe");
    await File.WriteAllTextAsync(sourceExe, "1.7.104.0 game from steam");
    await File.WriteAllTextAsync(Path.Combine(source, "Data", "ccBGSSSE001-Fish.esl"), "cc");
    await File.WriteAllTextAsync(Path.Combine(source, "Data", "Skyrim.esm"), "vanilla");
    await File.WriteAllTextAsync(Path.Combine(source, "Skyrim.ccc"), "cc index");

    var service = new StockGameService(ReadFakeExecutableVersion);
    await service.CopyFromSteamAsync(source, target, GameVersion.Required);
    Assert.Equal("1.7.104.0 game from steam", await File.ReadAllTextAsync(targetExe));
    Assert.False(File.Exists(Path.Combine(target, "Data", "ccBGSSSE001-Fish.esl")));
    Assert.True(File.Exists(Path.Combine(target, "Data", "Skyrim.esm")));
    Assert.False(File.Exists(Path.Combine(target, "Skyrim.ccc")));
    Assert.True(service.IsLockedCopyValid(target, GameVersion.Required));

    await File.WriteAllTextAsync(sourceExe, "1.6.1170.0 Steam updated");
    await service.CopyFromSteamAsync(source, target, GameVersion.Required);
    Assert.Equal("1.7.104.0 game from steam", await File.ReadAllTextAsync(targetExe));
    Assert.True(service.IsLockedCopyValid(target, GameVersion.Required));

    await File.WriteAllTextAsync(targetExe, "1.7.104.0 modified Stock Game executable");
    await Assert.ThrowsAsync<InvalidDataException>(() => Task.Run(() => service.IsLockedCopyValid(target, GameVersion.Required)));
}

static async Task WrongSteamVersionRejected()
{
    using var temp = new TempDirectory();
    var source = Path.Combine(temp.Path, "steam");
    var target = Path.Combine(temp.Path, "launcher", "Stock Game");
    Directory.CreateDirectory(source);
    await File.WriteAllTextAsync(Path.Combine(source, "SkyrimSE.exe"), "1.6.1170.0 old Steam version");
    var service = new StockGameService(ReadFakeExecutableVersion);
    await Assert.ThrowsAsync<InvalidDataException>(() => service.CopyFromSteamAsync(source, target, GameVersion.Required));
    Assert.False(File.Exists(Path.Combine(target, "SkyrimSE.exe")));
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

static Task AuthTokenClaimsValidation()
{
    var current = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var token = CreateTestAuthToken("discord:123456789", "Kerim", current + 3600);
    var session = AuthTokenClaims.Read(token, current);
    Assert.Equal("Kerim", session.DisplayName);
    Assert.Equal(current + 3600, session.ExpiresAt);
    Assert.Throws<InvalidDataException>(() => AuthTokenClaims.Read(CreateTestAuthToken("discord:123", "Kerim", current - 1), current));
    Assert.Throws<InvalidDataException>(() => AuthTokenClaims.Read(CreateTestAuthToken("not-discord", "Kerim", current + 3600), current));
    return Task.CompletedTask;
}

static Task AuthSessionDpapiRoundTrip()
{
    using var temp = new TempDirectory();
    var store = new AuthSessionStore(Path.Combine(temp.Path, "auth-session.json"));
    var token = CreateTestAuthToken("discord:123456789", "Burak", DateTimeOffset.UtcNow.ToUnixTimeSeconds() + 3600);
    store.Save(AuthTokenClaims.Read(token));
    var savedText = File.ReadAllText(Path.Combine(temp.Path, "auth-session.json"));
    Assert.False(savedText.Contains(token, StringComparison.Ordinal));
    Assert.Equal("Burak", store.Load()?.DisplayName);
    return Task.CompletedTask;
}

static Task NativeAuthConfigurationProtectsToken()
{
    using var temp = new TempDirectory();
    var store = new AuthSessionStore(Path.Combine(temp.Path, "auth-session.json"));
    var token = CreateTestAuthToken("discord:123456789", "Burak", DateTimeOffset.UtcNow.ToUnixTimeSeconds() + 3600);
    var session = AuthTokenClaims.Read(token);
    var path = Path.Combine(temp.Path, "game-auth.json");
    store.WriteRuntimeConfiguration(session, "198.51.100.7", 10578, path);
    var json = File.ReadAllText(path);
    Assert.False(json.Contains(token, StringComparison.Ordinal));
    var config = System.Text.Json.JsonSerializer.Deserialize<NativeAuthConfiguration>(json)!;
    Assert.Equal("198.51.100.7", config.ServerAddress);
    Assert.Equal(10578, config.ServerPort);
    Assert.True(Convert.FromBase64String(config.ProtectedToken).Length > token.Length / 2);
    return Task.CompletedTask;
}

static async Task AuthSessionRefreshReplacesToken()
{
    using var temp = new TempDirectory();
    var store = new AuthSessionStore(Path.Combine(temp.Path, "auth-session.json"));
    var now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
    var old = AuthTokenClaims.Read(CreateTestAuthToken("discord:123456789", "Burak", now + 3600));
    store.Save(old);
    var fresh = CreateTestAuthToken("discord:123456789", "Burak", now + 7 * 24 * 3600);
    var handler = new JsonHandler(HttpStatusCode.OK, System.Text.Json.JsonSerializer.Serialize(new { token = fresh }));
    var refreshed = await AuthSessionRefresh.TryRefreshAsync(new HttpClient(handler), "https://auth.example.test", store, old);
    Assert.True(refreshed is not null);
    Assert.Equal("Bearer " + old.Token, handler.Authorization);
    Assert.Equal("https://auth.example.test/auth/refresh", handler.Uri);
    Assert.Equal(fresh, store.Load()!.Token);

    var rejected = await AuthSessionRefresh.TryRefreshAsync(new HttpClient(new JsonHandler(HttpStatusCode.Unauthorized, "{}")),
        "https://auth.example.test", store, refreshed!);
    Assert.True(rejected is null);
    Assert.Equal(fresh, store.Load()!.Token);
}

static string CreateTestAuthToken(string subject, string name, long expires)
{
    static string Encode(byte[] data) => Convert.ToBase64String(data).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    var header = Encode(Encoding.UTF8.GetBytes("{\"alg\":\"HS256\",\"typ\":\"JWT\"}"));
    var payload = Encode(Encoding.UTF8.GetBytes(System.Text.Json.JsonSerializer.Serialize(new
    {
        iss = "sos-auth", sub = subject, name, avatar = "https://cdn.example/avatar.png", exp = expires
    })));
    return $"{header}.{payload}.{Encode(RandomNumberGenerator.GetBytes(32))}";
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
  "requiredGameVersion": "1.7.104.0",
  "mods": [{ "id": "first", "name": "First", "version": "1", "url": "https://example.com/a.zip", "sha256": "{{new string('a', 64)}}", "size": 10, "target": "data", "order": 1 }]
}
""";

static string ReadFakeExecutableVersion(string path) =>
    File.ReadAllText(path).StartsWith("1.7.104.0", StringComparison.Ordinal) ? "1.7.104.0" : "1.6.1170.0";

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

sealed class JsonHandler(HttpStatusCode status, string body) : HttpMessageHandler
{
    public string? Authorization { get; private set; }
    public string? Uri { get; private set; }
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        Authorization = request.Headers.Authorization?.ToString();
        Uri = request.RequestUri?.ToString();
        return Task.FromResult(new HttpResponseMessage(status) { Content = new StringContent(body) });
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
