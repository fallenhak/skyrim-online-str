using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;
using SosStr.Launcher;

namespace SosStr.LauncherApp;

internal sealed class LauncherConfig
{
    public string ManifestUrl { get; set; } = "";
    public string StrLauncherPath { get; set; } = "SkyrimTogether.exe";
    public string ErrorReportEndpoint { get; set; } = "";
    public string ErrorReportToken { get; set; } = "";
    public string AuthBaseUrl { get; set; } = "";
    public string ServerAddress { get; set; } = "";
    public int ServerPort { get; set; } = 10578;
}

internal sealed class UserSettings
{
    public string SteamGameDirectory { get; set; } = "";
}

internal sealed class MainForm : Form
{
    private const string RequiredGameVersion = GameVersion.Required;
    private static readonly HttpClient Http = new() { Timeout = Timeout.InfiniteTimeSpan };
    private readonly LauncherConfig _config;
    private readonly string _localData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SkyrimOnlineSTR");
    private readonly string _stockGame;
    private readonly string _stateDirectory;
    private readonly string _profileDirectory;
    private readonly string _logPath;
    private readonly AuthSessionStore _authSessionStore;
    private AuthSession? _authSession;
    private readonly TextBox _steamPath = new() { ReadOnly = true, Anchor = AnchorStyles.Left | AnchorStyles.Right };
    private readonly Button _discordLoginButton = new() { Text = "Discord ile giris", Width = 145, Height = 32 };
    private readonly Label _authStatus = new() { AutoSize = true, Text = "Discord girisi yapilmadi." };
    private readonly Button _installButton = new() { Text = "Kur / Güncelle", Width = 140, Height = 36 };
    private readonly Button _launchButton = new() { Text = "Oyunu başlat", Width = 140, Height = 36, Enabled = false };
    private readonly Button _reportButton = new() { Text = "Hata raporu gönder", Width = 170, Height = 36 };
    private readonly Button _findSteamButton = new() { Text = "Steam oyununu bul", Width = 150, Height = 31 };
    private readonly ProgressBar _progress = new() { Dock = DockStyle.Fill, Minimum = 0, Maximum = 100 };
    private readonly Label _status = new() { AutoSize = true, Text = "Hazır." };
    private readonly TextBox _log = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };
    private string _manifestVersion = "yüklenmedi";
    private LauncherManifest? _manifest;
    private bool _gameRunning;
    private bool _isInstalled;
    private CancellationTokenSource? _operation;

    public MainForm()
    {
        Text = "Skyrim Online STR — Kurulum Aracı";
        MinimumSize = new Size(720, 520);
        Size = new Size(820, 620);
        StartPosition = FormStartPosition.CenterScreen;
        _config = LoadConfig();
        _stockGame = Path.Combine(AppContext.BaseDirectory, "Stock Game");
        var installKey = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(AppContext.BaseDirectory))).ToLowerInvariant()[..16];
        _stateDirectory = Path.Combine(_localData, "state-" + installKey);
        _profileDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Skyrim Special Edition");
        _logPath = Path.Combine(_localData, "launcher.log");
        Directory.CreateDirectory(_localData);
        _authSessionStore = new AuthSessionStore(Path.Combine(_localData, "auth-session.json"));
        _authSession = _authSessionStore.Load();
        new PluginProfileService().RecoverInterruptedSession(_profileDirectory, _stateDirectory);
        LoadCachedManifest();
        try
        {
            _isInstalled = _manifest is not null && new StockGameService().IsLockedCopyValid(_stockGame, RequiredGameVersion) &&
                new ModDeploymentService().IsCurrentInstall(_stockGame, _stateDirectory, _manifest.Mods);
        }
        catch (Exception ex) { AppendLog("Önceki kurulum doğrulanamadı; Kur / Güncelle çalıştırın. " + ex.Message); }
        BuildUi();
        LoadSettings();
        AppendLog("Skyrim Online STR launcher hazır.");
        AppendLog($"Hedef Skyrim SE sürümü: {RequiredGameVersion}; Stock Game kopyası sürüm ve SkyrimSE.exe SHA-256 değeriyle kilitlenir.");
        UpdateAuthUi();
        FormClosing += (_, e) =>
        {
            if (!_gameRunning) return;
            e.Cancel = true;
            WindowState = FormWindowState.Minimized;
            SetStatus("Oyun açıkken launcher arka planda kalır; kapanınca kullanıcı eklenti listesi geri yüklenir.");
        };
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(14), ColumnCount = 1, RowCount = 7 };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        Controls.Add(root);

        var title = new Label { Text = "Skyrim Online STR", Font = new Font(Font, FontStyle.Bold), AutoSize = true };
        root.Controls.Add(title, 0, 0);
        var version = new Label { Text = $"Skyrim SE {RequiredGameVersion} • Steam'den bağımsız Stock Game kopyası", AutoSize = true, Margin = new Padding(0, 5, 0, 10) };
        root.Controls.Add(version, 0, 1);

        var authRow = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.LeftToRight, Margin = new Padding(0, 0, 0, 8) };
        _discordLoginButton.Click += async (_, _) => await AuthenticateWithDiscordAsync();
        authRow.Controls.Add(_discordLoginButton);
        authRow.Controls.Add(_authStatus);
        root.Controls.Add(authRow, 0, 2);

        var pathRow = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true };
        pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        _steamPath.Dock = DockStyle.Fill;
        _findSteamButton.Click += (_, _) => FindSteamGame();
        pathRow.Controls.Add(_steamPath, 0, 0);
        pathRow.Controls.Add(_findSteamButton, 1, 0);
        root.Controls.Add(pathRow, 0, 3);

        root.Controls.Add(_progress, 0, 4);
        root.Controls.Add(_status, 0, 5);
        root.Controls.Add(_log, 0, 6);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 48, FlowDirection = FlowDirection.LeftToRight, Padding = new Padding(0, 4, 0, 0) };
        _installButton.Click += async (_, _) => await InstallOrUpdateAsync();
        _launchButton.Text = "Oyna";
        _launchButton.Click += async (_, _) => await PlayAsync();
        _reportButton.Click += async (_, _) => await SendErrorReportAsync();
        buttons.Controls.AddRange([_installButton, _launchButton, _reportButton]);
        root.Controls.Add(buttons, 0, 6);
        root.SetRow(_log, 6);
        root.Controls.Remove(buttons);
        Controls.Add(buttons);
        buttons.BringToFront();
    }

    private void FindSteamGame()
    {
        var found = SteamLocator.FindGameDirectories().FirstOrDefault();
        if (found is null)
        {
            using var dialog = new FolderBrowserDialog { Description = @"Steam\steamapps\common\Skyrim Special Edition klasörünü seçin." };
            if (dialog.ShowDialog(this) != DialogResult.OK) return;
            found = dialog.SelectedPath;
        }
        if (!File.Exists(Path.Combine(found, "SkyrimSE.exe")))
        {
            MessageBox.Show(this, "Seçilen klasörde SkyrimSE.exe bulunamadı.", "Oyun bulunamadı", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        _steamPath.Text = found;
        SaveSettings();
        AppendLog($"Steam oyunu: {found}");
        _launchButton.Enabled = _isInstalled;
    }

    private async Task InstallOrUpdateAsync()
    {
        if (string.IsNullOrWhiteSpace(_steamPath.Text) || !File.Exists(Path.Combine(_steamPath.Text, "SkyrimSE.exe")))
        {
            MessageBox.Show(this, "Önce Steam oyun klasörünüzü bulun.", "Kurulum", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        var wasInstalled = _isInstalled;
        var previousManifestVersion = _manifestVersion;
        _isInstalled = false;
        SetBusy(true);
        _operation = new CancellationTokenSource();
        try
        {
            SetStatus("Manifest indiriliyor…");
            using var response = await Http.GetAsync(_config.ManifestUrl, _operation.Token);
            response.EnsureSuccessStatusCode();
            var manifest = ManifestReader.ParseAndValidate(await response.Content.ReadAsStringAsync(_operation.Token));
            _manifestVersion = manifest.ManifestVersion;
            _manifest = manifest;
            File.WriteAllText(Path.Combine(_localData, "manifest.json"), ManifestReader.Serialize(manifest));
            if (GameVersion.Normalize(manifest.RequiredGameVersion) != RequiredGameVersion)
                throw new InvalidDataException($"Sunucu manifesti {RequiredGameVersion} hedefinden farklı. Kurulum durduruldu.");
            AppendLog($"Manifest {manifest.ManifestVersion} alındı.");

            if (wasInstalled && string.Equals(previousManifestVersion, manifest.ManifestVersion, StringComparison.Ordinal) &&
                new StockGameService().IsLockedCopyValid(_stockGame, RequiredGameVersion) &&
                new ModDeploymentService().IsCurrentInstall(_stockGame, _stateDirectory, manifest.Mods))
            {
                _isInstalled = true;
                _progress.Value = 100;
                SetStatus("Kurulum güncel; oyun başlatılmaya hazır.");
                UpdateAuthUi();
                return;
            }

            SetStatus("Steam oyunu Stock Game klasörüne kopyalanıyor…");
            var copyProgress = new Progress<(int Percent, string Message)>(x => { _progress.Value = x.Percent; SetStatus(x.Message); });
            var stockGame = new StockGameService();
            await stockGame.CopyFromSteamAsync(_steamPath.Text, _stockGame, RequiredGameVersion, copyProgress, _operation.Token);
            if (!stockGame.IsLockedCopyValid(_stockGame, RequiredGameVersion))
                throw new InvalidDataException("Stock Game kopyası için sürüm kilidi oluşturulamadı.");

            var removed = StockGameService.RemoveCreationClubContent(_stockGame);
            if (removed > 0) AppendLog($"CC içeriği temizlendi: {removed} dosya.");

            var downloader = new DownloadService(Http);
            var archives = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var mod in manifest.Mods)
            {
                SetStatus($"İndiriliyor: {mod.Name}");
                var progress = new Progress<DownloadProgress>(p =>
                {
                    _progress.Value = p.Total <= 0 ? 0 : (int)Math.Clamp(p.Received * 100 / p.Total, 0, 100);
                    SetStatus($"İndiriliyor: {mod.Name} ({_progress.Value}%)");
                });
                archives[mod.Id] = await downloader.EnsureAssetAsync(mod.Url, mod.Sha256, mod.Size,
                    Path.Combine(_localData, "downloads"), progress, _operation.Token);
            }
            SetStatus("Modlar yerleştiriliyor…");
            _isInstalled = false;
            await new ModDeploymentService().ApplyAsync(_stockGame, _stateDirectory, manifest.Mods, archives, _operation.Token);
            if (!stockGame.IsLockedCopyValid(_stockGame, RequiredGameVersion))
                throw new InvalidDataException("Mod kurulumu SkyrimSE.exe sürüm kilidini değiştirdi. Stock Game kurulumu doğrulanamadı.");
            _progress.Value = 100;
            _isInstalled = true;
            UpdateAuthUi();
            SetStatus("Kurulum tamamlandı.");
            AppendLog($"Stock Game hazır: {RequiredGameVersion}; {manifest.Mods.Count} paket uygulandı.");
        }
        catch (OperationCanceledException) { SetStatus("İşlem iptal edildi."); AppendLog("İşlem iptal edildi."); }
        catch (Exception ex) { SetStatus("Kurulum tamamlanamadı."); AppendLog("HATA: " + ex.Message); MessageBox.Show(this, ex.Message, "Kurulum hatası", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        finally { _operation?.Dispose(); _operation = null; SetBusy(false); }
    }

    private async Task LaunchStrAsync()
    {
        var launcher = Path.IsPathRooted(_config.StrLauncherPath) ? _config.StrLauncherPath : Path.Combine(AppContext.BaseDirectory, _config.StrLauncherPath);
        if (!File.Exists(launcher))
        {
            MessageBox.Show(this, $"STR başlatıcısı bulunamadı:\n{launcher}\n\nDerlenmiş SkyrimTogether.exe dosyasını bu aracın yanına koyun veya launcher.config.json içindeki strLauncherPath değerini düzenleyin.", "STR başlatıcısı bulunamadı", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        IDisposable? profile = null;
        string? authConfigPath = null;
        try
        {
            var session = _authSessionStore.Load() ?? throw new InvalidOperationException("Discord ile giris yapin.");
            if (!new StockGameService().IsLockedCopyValid(_stockGame, RequiredGameVersion))
                throw new InvalidDataException($"Stock Game sürüm veya SHA-256 doğrulaması başarısız. Gerekli Skyrim SE sürümü: {RequiredGameVersion}. Kur / Güncelle adımını yeniden çalıştırın.");
            var manifest = _manifest ?? throw new InvalidOperationException("Önce Kur / Güncelle adımını tamamlayın.");
            profile = new PluginProfileService().ApplyForSession(_stockGame, _profileDirectory, _stateDirectory, manifest.Mods);
            var info = new ProcessStartInfo(launcher) { WorkingDirectory = _stockGame, UseShellExecute = false };
            authConfigPath = Path.Combine(_localData, "runtime-auth-" + Guid.NewGuid().ToString("N") + ".json");
            _authSessionStore.WriteRuntimeConfiguration(session, _config.ServerAddress, _config.ServerPort, authConfigPath);
            info.Environment["SOS_AUTH_CONFIG_PATH"] = authConfigPath;
            info.ArgumentList.Add("--exePath");
            info.ArgumentList.Add(Path.Combine(_stockGame, "SkyrimSE.exe"));
            using var process = Process.Start(info) ?? throw new InvalidOperationException("STR başlatıcısı başlatılamadı.");
            _gameRunning = true;
            _installButton.Enabled = false;
            _findSteamButton.Enabled = false;
            _launchButton.Enabled = false;
            SetStatus("Oyun açık. Kapanınca önceki plugins.txt geri yüklenecek.");
            AppendLog("Mevcut STR başlatıcısı çalıştırıldı.");
            await process.WaitForExitAsync();
            SetStatus("Oyun kapandı.");
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "Başlatma hatası", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        finally
        {
            try { profile?.Dispose(); }
            catch (Exception ex) { AppendLog("UYARI: Kullanıcının plugins.txt geri yüklenemedi: " + ex.Message); }
            if (authConfigPath is not null)
            {
                try { File.Delete(authConfigPath); }
                catch (Exception ex) { AppendLog("Runtime auth config silinemedi: " + ex.Message); }
            }
            _gameRunning = false;
            _installButton.Enabled = true;
            _findSteamButton.Enabled = true;
            UpdateAuthUi();
        }
    }

    private async Task PlayAsync()
    {
        _authSession = _authSessionStore.Load();
        if (_authSession is null)
        {
            UpdateAuthUi();
            MessageBox.Show(this, "Oyuna baslamak icin once Discord ile giris yapin.", "Discord girisi gerekli", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        await InstallOrUpdateAsync();
        if (!_isInstalled) return;
        await LaunchStrAsync();
    }

    private async Task AuthenticateWithDiscordAsync()
    {
        _discordLoginButton.Enabled = false;
        SetStatus("Discord servisi denetleniyor...");
        try
        {
            if (!Uri.TryCreate(_config.AuthBaseUrl, UriKind.Absolute, out var authBase) || authBase.Scheme != Uri.UriSchemeHttps)
                throw new InvalidOperationException("launcher.config.json icindeki authBaseUrl HTTPS olmali.");
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            using var health = await Http.GetAsync(new Uri(authBase, "/auth/healthz"), timeout.Token);
            if (!health.IsSuccessStatusCode)
                throw new InvalidOperationException("Discord kimlik servisi hazir degil. VDS'te /etc/sos-auth.env ve Discord uygulama ayarlari yapilandirilmali.");

            SetStatus("Tarayicida Discord girisi bekleniyor...");
            _authSession = await new DiscordLoopbackLogin().SignInAsync(_config.AuthBaseUrl, _authSessionStore);
            _authStatus.Text = "Discord: " + _authSession.DisplayName;
            SetStatus("Discord girisi tamamlandi.");
            AppendLog("Discord girisi tamamlandi: " + _authSession.DisplayName);
        }
        catch (Exception ex)
        {
            SetStatus("Discord girisi tamamlanamadi.");
            AppendLog("Discord auth hatasi: " + ex.Message);
            MessageBox.Show(this, ex.Message, "Discord girisi", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            _discordLoginButton.Enabled = true;
            UpdateAuthUi();
        }
    }

    private async Task SendErrorReportAsync()
    {
        using var dialog = new ErrorReportDialog();
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        try
        {
            var zipPath = Path.Combine(_localData, "reports", $"report-{DateTime.Now:yyyyMMdd-HHmmss}.zip");
            SetBusy(true);
            SetStatus("Hata raporu hazırlanıyor…");
            await new ErrorReportService(Http).CreateReportZipAsync(zipPath, dialog.Description,
                _manifestVersion, _logPath, _stockGame);
            if (string.IsNullOrWhiteSpace(_config.ErrorReportEndpoint) || _config.ErrorReportEndpoint.Contains("example.invalid", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException($"Rapor ZIP'i hazırlandı: {zipPath}\n\nGönderim için launcher.config.json içindeki errorReportEndpoint ve errorReportToken alanlarını sunucu değerleriyle doldurun.");
            SetStatus("Hata raporu gönderiliyor…");
            await new ErrorReportService(Http).SendAsync(_config.ErrorReportEndpoint, _config.ErrorReportToken, zipPath);
            SetStatus("Hata raporu gönderildi.");
            AppendLog("Hata raporu sunucuya gönderildi.");
            MessageBox.Show(this, "Hata raporunuz gönderildi. Teşekkürler.", "Rapor gönderildi", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            AppendLog("Rapor hatası: " + ex.Message);
            MessageBox.Show(this, ex.Message, "Hata raporu", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally { SetBusy(false); }
    }

    private LauncherConfig LoadConfig()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "launcher.config.json");
        LauncherConfig config;
        if (!File.Exists(path)) config = new LauncherConfig();
        else
        {
            try { config = JsonSerializer.Deserialize<LauncherConfig>(File.ReadAllText(path), new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? new LauncherConfig(); }
            catch { config = new LauncherConfig(); }
        }
        config.ErrorReportToken = Environment.GetEnvironmentVariable("SOS_STR_REPORT_TOKEN") ?? config.ErrorReportToken;
        return config;
    }

    private void LoadSettings()
    {
        var settingsPath = Path.Combine(_localData, "settings.json");
        if (File.Exists(settingsPath))
        {
            try { _steamPath.Text = JsonSerializer.Deserialize<UserSettings>(File.ReadAllText(settingsPath))?.SteamGameDirectory ?? ""; }
            catch { }
        }
        if (string.IsNullOrWhiteSpace(_steamPath.Text))
        {
            var found = SteamLocator.FindGameDirectories().FirstOrDefault();
            if (found is not null) _steamPath.Text = found;
        }
    }

    private void LoadCachedManifest()
    {
        var path = Path.Combine(_localData, "manifest.json");
        if (!File.Exists(path)) return;
        try
        {
            _manifest = ManifestReader.ParseAndValidate(File.ReadAllText(path));
            if (GameVersion.Normalize(_manifest.RequiredGameVersion) != RequiredGameVersion)
                throw new InvalidDataException($"Önbellekteki manifest {RequiredGameVersion} hedef sürümüyle eşleşmiyor.");
            _manifestVersion = _manifest.ManifestVersion;
        }
        catch (Exception ex) { AppendLog("Önceki manifest okunamadı; tekrar Kur / Güncelle yapın. " + ex.Message); }
    }

    private void SaveSettings()
    {
        File.WriteAllText(Path.Combine(_localData, "settings.json"), JsonSerializer.Serialize(new UserSettings { SteamGameDirectory = _steamPath.Text }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private void AppendLog(string message)
    {
        var line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {message}";
        _log.AppendText(line + Environment.NewLine);
        File.AppendAllText(_logPath, line + Environment.NewLine);
    }

    private void SetBusy(bool busy)
    {
        _installButton.Enabled = !busy && !_gameRunning;
        _findSteamButton.Enabled = !busy && !_gameRunning;
        _launchButton.Enabled = !busy && !_gameRunning && _authSession is not null;
        _discordLoginButton.Enabled = !busy && !_gameRunning;
        _reportButton.Enabled = !busy;
        UseWaitCursor = busy;
    }

    private void UpdateAuthUi()
    {
        if (_authSession is null)
        {
            _authStatus.Text = "Discord girisi yapilmadi.";
            _launchButton.Enabled = false;
            return;
        }

        _authStatus.Text = "Discord: " + _authSession.DisplayName;
        _launchButton.Enabled = !_gameRunning && !UseWaitCursor;
    }

    private void SetStatus(string message) { _status.Text = message; }
}

internal sealed class ErrorReportDialog : Form
{
    private readonly TextBox _description = new() { Multiline = true, MaxLength = 2000, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };
    public string Description => _description.Text;

    public ErrorReportDialog()
    {
        Text = "Hata raporu";
        Size = new Size(500, 300);
        StartPosition = FormStartPosition.CenterParent;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), RowCount = 3, ColumnCount = 1 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(new Label { Text = "Sorunu kısaca anlatın (isteğe bağlı):", AutoSize = true }, 0, 0);
        layout.Controls.Add(_description, 0, 1);
        var buttons = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
        var send = new Button { Text = "ZIP oluştur ve gönder", DialogResult = DialogResult.OK, AutoSize = true };
        var cancel = new Button { Text = "Vazgeç", DialogResult = DialogResult.Cancel, AutoSize = true };
        buttons.Controls.Add(send); buttons.Controls.Add(cancel);
        layout.Controls.Add(buttons, 0, 2);
        Controls.Add(layout);
        AcceptButton = send; CancelButton = cancel;
    }
}
