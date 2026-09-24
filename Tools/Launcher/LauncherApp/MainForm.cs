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
    private readonly MonochromeProgressBar _progress = new();
    private readonly Label _status = new() { AutoSize = true, Text = "Hazır." };
    private readonly TextBox _log = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill };
    private string _manifestVersion = "yüklenmedi";
    private LauncherManifest? _manifest;
    private bool _gameRunning;
    private bool _isInstalled;
    private CancellationTokenSource? _operation;
    private bool _busy;
    private Label? _versionLabel;
    private readonly System.Drawing.Text.PrivateFontCollection _privateFonts = new();

    public MainForm()
    {
        Text = "Skyrim Online STR";
        FormBorderStyle = FormBorderStyle.None;
        MinimumSize = new Size(1100, 640);
        Size = new Size(1100, 640);
        BackColor = Color.FromArgb(5, 6, 7);
        ForeColor = Color.Gainsboro;
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
        var titleBar = new Panel { Dock = DockStyle.Top, Height = 54, BackColor = Color.FromArgb(12, 13, 15) };
        var titleText = new Label { Text = "S K Y R I M   O N L I N E", AutoSize = true, ForeColor = Color.White, Font = new Font("Segoe UI", 11, FontStyle.Bold), Location = new Point(28, 17) };
        titleText.MouseDown += DragWindow; titleBar.Controls.Add(titleText);
        var minimize = WindowButton("—", () => WindowState = FormWindowState.Minimized); minimize.Location = new Point(1014, 8);
        var close = WindowButton("×", Close); close.Location = new Point(1056, 8); titleBar.Controls.Add(minimize); titleBar.Controls.Add(close);
        titleBar.MouseDown += DragWindow; Controls.Add(titleBar);
        Controls.Add(new Label { Text = "SKYRIM\nONLINE", AutoSize = true, ForeColor = Color.White, Font = BrandFont(38), Location = new Point(58, 112) });
        _versionLabel = new Label { Text = $"Sürüm {_manifestVersion}  •  Skyrim SE {RequiredGameVersion}", AutoSize = true, ForeColor = Color.Gray, Font = new Font("Segoe UI", 10), Location = new Point(62, 220) };
        Controls.Add(_versionLabel);
        _authStatus.Location = new Point(62, 493); _authStatus.ForeColor = Color.Silver; _authStatus.Font = new Font("Segoe UI", 10); Controls.Add(_authStatus);
        _discordLoginButton.Text = "DISCORD İLE GİRİŞ"; _discordLoginButton.FlatStyle = FlatStyle.Flat; _discordLoginButton.ForeColor = Color.Gainsboro; _discordLoginButton.BackColor = Color.FromArgb(22, 24, 27); _discordLoginButton.Location = new Point(58, 525); _discordLoginButton.Size = new Size(190, 38);
        _discordLoginButton.Click += async (_, _) => { if (_authSession is null) await AuthenticateWithDiscordAsync(); else SignOut(); }; Controls.Add(_discordLoginButton);
        _steamPath.Visible = false; _findSteamButton.Visible = false; _findSteamButton.Click += (_, _) => FindSteamGame();
        var actions = new FlowLayoutPanel { Location = new Point(58, 580), AutoSize = true, BackColor = Color.Transparent };
        actions.Controls.Add(LinkLabel("Hata raporu", async () => await SendErrorReportAsync()));
        actions.Controls.Add(LinkLabel("Klas\u00f6r\u00fc a\u00e7", () => Process.Start("explorer.exe", AppContext.BaseDirectory)));
        actions.Controls.Add(LinkLabel("\u2699 Steam yolu", () => ChangeSteamGamePath())); Controls.Add(actions);
        _launchButton.Font = BrandFont(24); _launchButton.FlatStyle = FlatStyle.Flat;
        _launchButton.FlatAppearance.BorderColor = Color.FromArgb(190, 195, 200); _launchButton.FlatAppearance.BorderSize = 1;
        _launchButton.BackColor = Color.FromArgb(18, 20, 22); _launchButton.ForeColor = Color.White;
        _launchButton.Size = new Size(350, 76); _launchButton.Location = new Point(680, 400);
        _launchButton.Click += async (_, _) => await MainActionAsync(); Controls.Add(_launchButton);
        _installButton.Visible = false; _progress.Location = new Point(680, 492); _progress.Size = new Size(350, 5); Controls.Add(_progress);
        _status.Location = new Point(680, 509); _status.ForeColor = Color.Silver; _status.Font = new Font("Segoe UI", 9); Controls.Add(_status);
        _log.Visible = false; _reportButton.Visible = false;
    }

    private Font BrandFont(float size)
    {
        try
        {
            using var stream = typeof(MainForm).Assembly.GetManifestResourceStream("SkyrimOnlineSTR.font.otf");
            if (stream is not null)
            {
                using var ms = new MemoryStream(); stream.CopyTo(ms); var bytes = ms.ToArray();
                var memory = System.Runtime.InteropServices.Marshal.AllocCoTaskMem(bytes.Length);
                try { System.Runtime.InteropServices.Marshal.Copy(bytes, 0, memory, bytes.Length); _privateFonts.AddMemoryFont(memory, bytes.Length); }
                finally { System.Runtime.InteropServices.Marshal.FreeCoTaskMem(memory); }
                var family = _privateFonts.Families.FirstOrDefault(); if (family is not null) return new Font(family, size, FontStyle.Bold);
            }
        }
        catch { }
        return new Font("Segoe UI", size, FontStyle.Bold);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var bounds = new Rectangle(0, 390, Width, Height - 390);
        using var gradient = new System.Drawing.Drawing2D.LinearGradientBrush(bounds, Color.FromArgb(30, 45, 47, 50), Color.FromArgb(0, 0, 0, 0), 90f);
        e.Graphics.FillRectangle(gradient, bounds);
        foreach (var fog in new[] { new Rectangle(-90, 505, 570, 140), new Rectangle(280, 535, 660, 150), new Rectangle(690, 500, 560, 160) })
        {
            using var path = new System.Drawing.Drawing2D.GraphicsPath(); path.AddEllipse(fog);
            using var brush = new System.Drawing.Drawing2D.PathGradientBrush(path) { CenterColor = Color.FromArgb(19, 155, 160, 164), SurroundColors = [Color.FromArgb(0, 155, 160, 164)] };
            e.Graphics.FillEllipse(brush, fog);
        }
    }

    private static Button WindowButton(string text, Action action) { var b = new Button { Text = text, Size = new Size(34, 34), FlatStyle = FlatStyle.Flat, ForeColor = Color.Silver, BackColor = Color.Transparent, Font = new Font("Segoe UI", 13) }; b.FlatAppearance.BorderSize = 0; b.Click += (_, _) => action(); return b; }
    private static Label LinkLabel(string text, Action action) { var l = new Label { Text = text, AutoSize = true, ForeColor = Color.Gray, Cursor = Cursors.Hand, Font = new Font("Segoe UI", 9), Margin = new Padding(0, 0, 24, 0) }; l.Click += (_, _) => action(); return l; }
    private void DragWindow(object? sender, MouseEventArgs e) { if (e.Button != MouseButtons.Left) return; ReleaseCapture(); SendMessage(Handle, 0xA1, 0x2, 0); }
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool ReleaseCapture();
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hWnd, int msg, int wParam, int lParam);
    private async Task MainActionAsync() { if (_busy) return; if (!_isInstalled) await InstallOrUpdateAsync(); else if (_authSession is null) await AuthenticateWithDiscordAsync(); else await PlayAsync(); UpdatePrimaryAction(); }
    private void UpdatePrimaryAction() => _launchButton.Text = !_isInstalled ? "KUR / G\u00dcNCELLE" : _authSession is null ? "DISCORD \u0130LE G\u0130R\u0130\u015e" : "OYNA";
    private void SignOut()
    {
        try { File.Delete(Path.Combine(_localData, "auth-session.json")); } catch { }
        _authSession = null;
        _authStatus.Text = "Discord ile giri\u015f yap\u0131lmad\u0131.";
        _discordLoginButton.Text = _authSession is null ? "DISCORD \u0130LE G\u0130R\u0130\u015e" : "\u00c7IKI\u015e";
        UpdateAuthUi();
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

    private void ChangeSteamGamePath()
    {
        using var dialog = new FolderBrowserDialog { SelectedPath = _steamPath.Text, Description = "Steam\\steamapps\\common\\Skyrim Special Edition klasörünü seçin." };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        if (!File.Exists(Path.Combine(dialog.SelectedPath, "SkyrimSE.exe")))
        {
            MessageBox.Show(this, "Seçilen klasörde SkyrimSE.exe bulunamadı.", "Oyun bulunamadı", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        _steamPath.Text = dialog.SelectedPath;
        SaveSettings();
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
            if (_versionLabel is not null) _versionLabel.Text = $"Sürüm {_manifestVersion}  •  Skyrim SE {RequiredGameVersion}";
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
                var lastProgressAt = DateTime.UtcNow;
                long lastProgressBytes = 0;
                var progress = new Progress<DownloadProgress>(p =>
                {
                    _progress.Value = p.Total <= 0 ? 0 : (int)Math.Clamp(p.Received * 100 / p.Total, 0, 100);
                    var now = DateTime.UtcNow;
                    var seconds = Math.Max(.05, (now - lastProgressAt).TotalSeconds);
                    var speed = Math.Max(0, p.Received - lastProgressBytes) / seconds / 1024 / 1024;
                    lastProgressAt = now; lastProgressBytes = p.Received;
                    SetStatus($"İndiriliyor: {mod.Name} • {speed:0.0} MB/s • {_progress.Value}%");
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
            _launchButton.Enabled = !_busy && !_gameRunning;
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
        _busy = busy;
        UpdatePrimaryAction();
        _installButton.Enabled = !busy && !_gameRunning;
        _findSteamButton.Enabled = !busy && !_gameRunning;
        _launchButton.Enabled = !busy && !_gameRunning;
        _discordLoginButton.Enabled = !busy && !_gameRunning;
        _reportButton.Enabled = !busy;
        UseWaitCursor = busy;
        UpdatePrimaryAction();
    }

    private void UpdateAuthUi()
    {
        if (_authSession is null)
        {
            _authStatus.Text = "Discord ile giri\u015f yap\u0131lmad\u0131.";
            _discordLoginButton.Text = _authSession is null ? "DISCORD \u0130LE G\u0130R\u0130\u015e" : "\u00c7IKI\u015e";
            UpdatePrimaryAction();
            _launchButton.Enabled = !_busy && !_gameRunning;
            return;
        }

        _authStatus.Text = "Discord: " + _authSession.DisplayName;
        _discordLoginButton.Text = "\u00c7IKI\u015e";
        _launchButton.Enabled = !_gameRunning && !UseWaitCursor;
        UpdatePrimaryAction();
    }

    private void SetStatus(string message) { _status.Text = message; }
}

internal sealed class MonochromeProgressBar : Control
{
    private int _value;
    public int Maximum { get; set; } = 100;
    public int Value { get => _value; set { _value = Math.Clamp(value, 0, Maximum); Invalidate(); } }
    public MonochromeProgressBar() { SetStyle(ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true); BackColor = Color.FromArgb(34, 36, 39); }
    protected override void OnPaint(PaintEventArgs e)
    {
        using var background = new SolidBrush(Color.FromArgb(38, 40, 43)); e.Graphics.FillRectangle(background, ClientRectangle);
        var width = Maximum == 0 ? 0 : (int)(ClientSize.Width * (double)Value / Maximum);
        using var fill = new SolidBrush(Color.FromArgb(195, 198, 201)); e.Graphics.FillRectangle(fill, 0, 0, width, ClientSize.Height);
    }
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
