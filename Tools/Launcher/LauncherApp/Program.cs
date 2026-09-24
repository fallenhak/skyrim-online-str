using SosStr.Launcher;

namespace SosStr.LauncherApp;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        if (!LauncherInstallService.IsInstalled(AppContext.BaseDirectory))
        {
            using var setup = new FirstRunInstallForm();
            if (setup.ShowDialog() != DialogResult.OK) return;
            try
            {
                var executable = Environment.ProcessPath ?? throw new InvalidOperationException("Calisan exe yolu bulunamadi.");
                var assembly = typeof(Program).Assembly;
                byte[] ReadResource(string name)
                {
                    using var stream = assembly.GetManifestResourceStream(name) ?? throw new InvalidOperationException($"Gomulu kaynak bulunamadi: {name}");
                    using var buffer = new MemoryStream(); stream.CopyTo(buffer); return buffer.ToArray();
                }
                LauncherInstallService.Install(executable, setup.InstallDirectory,
                    ReadResource("SkyrimOnlineSTR.launcher.config.json"), ReadResource("SkyrimOnlineSTR.Data.renewable_encounters.txt"));
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Path.Combine(setup.InstallDirectory, "SkyrimOnlineSTR.exe")) { UseShellExecute = true });
            }
            catch (Exception ex) { MessageBox.Show(setup, ex.Message, "Kurulum hatasi", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            return;
        }
        Application.Run(new MainForm());
    }
}

internal sealed class FirstRunInstallForm : Form
{
    private readonly TextBox _path = new() { Text = @"C:\Games\SkyrimOnlineSTR", Width = 420 };
    private readonly Label _space = new() { AutoSize = true };
    public string InstallDirectory => _path.Text;

    public FirstRunInstallForm()
    {
        Text = "Skyrim Online STR \u2014 Kurulum"; Size = new Size(680, 380); StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.None; MaximizeBox = false; MinimizeBox = false;
        BackColor = Color.FromArgb(5, 6, 7); ForeColor = Color.Gainsboro;
        var titleBar = new Panel { Dock = DockStyle.Top, Height = 48, BackColor = Color.FromArgb(12, 13, 15) };
        var title = new Label { Text = "S K Y R I M   O N L I N E", AutoSize = true, ForeColor = Color.White, Font = new Font("Segoe UI", 11, FontStyle.Bold), Location = new Point(22, 15) };
        title.MouseDown += DragWindow; titleBar.MouseDown += DragWindow; titleBar.Controls.Add(title);
        var closeWindow = new Button { Text = "\u00d7", Size = new Size(38, 36), Location = new Point(634, 6), Font = new Font("Segoe UI", 12), ForeColor = Color.Silver, BackColor = Color.Transparent };
        LauncherVisualTheme.StyleButton(closeWindow); closeWindow.FlatAppearance.BorderSize = 0; closeWindow.Click += (_, _) => Close(); titleBar.Controls.Add(closeWindow);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28), RowCount = 7, ColumnCount = 1 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(new Label { Text = "SKYRIM ONLINE", Font = LauncherVisualTheme.BrandFont(28), UseCompatibleTextRendering = true, ForeColor = Color.White, AutoSize = true }, 0, 0);
        layout.Controls.Add(new Label { Text = "Kurulum klas\u00f6r\u00fc", AutoSize = true, Margin = new Padding(0, 22, 0, 5) }, 0, 1);
        var row = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        row.Controls.Add(_path);
        _path.BackColor = Color.FromArgb(18, 20, 22); _path.ForeColor = Color.Gainsboro; _path.BorderStyle = BorderStyle.FixedSingle;
        var browse = new Button { Text = "G\u00f6zat\u2026", AutoSize = true };
        LauncherVisualTheme.StyleButton(browse);
        browse.Click += (_, _) => { using var dialog = new FolderBrowserDialog { SelectedPath = _path.Text, Description = "Launcher kurulum klas\u00f6r\u00fcn\u00fc se\u00e7in." }; if (dialog.ShowDialog(this) == DialogResult.OK) { _path.Text = Path.Combine(dialog.SelectedPath, "SkyrimOnlineSTR"); UpdateSpace(); } };
        row.Controls.Add(browse); layout.Controls.Add(row, 0, 2);
        layout.Controls.Add(new Label { Text = "Stock Game i\u00e7in yakla\u015f\u0131k 15 GB bo\u015f alan gerekir. Kurulum klas\u00f6r\u00fc Steam kitapl\u0131\u011f\u0131n\u0131n i\u00e7inde olamaz.", AutoSize = true, ForeColor = Color.FromArgb(138, 138, 138), Margin = new Padding(0, 12, 0, 8) }, 0, 3);
        layout.Controls.Add(_space, 0, 4);
        var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
        var install = new Button { Text = "Kur", Width = 120, Height = 38 };
        LauncherVisualTheme.StyleButton(install);
        install.Click += (_, _) => { if (!ValidatePath()) return; DialogResult = DialogResult.OK; Close(); };
        var cancel = new Button { Text = "\u00c7\u0131k\u0131\u015f", Width = 90, Height = 38, DialogResult = DialogResult.Cancel };
        LauncherVisualTheme.StyleButton(cancel);
        buttons.Controls.Add(install); buttons.Controls.Add(cancel); layout.Controls.Add(buttons, 0, 6); Controls.Add(layout); Controls.Add(titleBar);
        _path.TextChanged += (_, _) => UpdateSpace(); Shown += (_, _) => UpdateSpace(); AcceptButton = install; CancelButton = cancel;
    }

    private void DragWindow(object? sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Left) return;
        ReleaseCapture(); SendMessage(Handle, 0xA1, 0x2, 0);
    }
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool ReleaseCapture();
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hWnd, int msg, int wParam, int lParam);

    private void UpdateSpace()
    {
        try { var root = Path.GetPathRoot(Path.GetFullPath(_path.Text)); var drive = new DriveInfo(root!); _space.Text = $"Bo\u015f alan: {drive.AvailableFreeSpace / 1024d / 1024 / 1024:0.0} GB"; }
        catch { _space.Text = "Bo\u015f alan bilgisi al\u0131namad\u0131."; }
    }

    private bool ValidatePath()
    {
        try
        {
            var full = Path.GetFullPath(_path.Text);
            if (IsInSteamLibrary(full))
                throw new InvalidOperationException("Kurulum klas\u00f6r\u00fc Steam kitapl\u0131\u011f\u0131n\u0131n i\u00e7inde olamaz. \u00d6rne\u011fin C:\\Games\\SkyrimOnlineSTR se\u00e7in.");
            var root = Path.GetPathRoot(full)!;
            if (new DriveInfo(root).AvailableFreeSpace < 15L * 1024 * 1024 * 1024)
                return MessageBox.Show(this, "Bu s\u00fcr\u00fcc\u00fcde Stock Game i\u00e7in yakla\u015f\u0131k 15 GB bo\u015f alan yok. Yine de kuruluma devam edilsin mi?", "Bo\u015f alan az", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes;
            return true;
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "Kurulum klas\u00f6r\u00fc", MessageBoxButtons.OK, MessageBoxIcon.Warning); return false; }
    }
    private static bool IsInSteamLibrary(string path)
    {
        var full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        foreach (var gameDirectory in SteamLocator.FindGameDirectories())
        {
            var library = Path.GetFullPath(Path.Combine(gameDirectory, "..", "..", ".."));
            if (string.Equals(full, library, StringComparison.OrdinalIgnoreCase) ||
                full.StartsWith(Path.TrimEndingDirectorySeparator(library) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return true;
        }
        return false;
    }

}
