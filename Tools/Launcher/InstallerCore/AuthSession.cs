using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace SosStr.Launcher;

public sealed record AuthSession(string Token, string DisplayName, string AvatarUrl, long ExpiresAt);

public sealed record NativeAuthConfiguration(string ServerAddress, int ServerPort, string ProtectedToken);

public static class AuthTokenClaims
{
    public static AuthSession Read(string token, long? now = null)
    {
        var parts = token.Split('.');
        if (token.Length > 8192 || parts.Length != 3 || parts.Any(string.IsNullOrWhiteSpace))
            throw new InvalidDataException("Oturum token biçimi geçersiz.");

        try
        {
            var payloadBytes = DecodeBase64Url(parts[1]);
            using var document = JsonDocument.Parse(payloadBytes);
            var root = document.RootElement;
            if (root.GetProperty("iss").GetString() != "sos-auth")
                throw new InvalidDataException("Oturum token kaynağı geçersiz.");
            var subject = root.GetProperty("sub").GetString() ?? "";
            var name = root.GetProperty("name").GetString() ?? "";
            var avatar = root.GetProperty("avatar").GetString() ?? "";
            var expires = root.GetProperty("exp").GetInt64();
            var current = now ?? DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            if (!subject.StartsWith("discord:", StringComparison.Ordinal) ||
                subject.Length <= "discord:".Length ||
                !subject.AsSpan("discord:".Length).ToString().All(char.IsAsciiDigit) ||
                string.IsNullOrWhiteSpace(name) || expires <= current)
                throw new InvalidDataException("Oturum token süresi dolmuş veya talepleri geçersiz.");
            return new AuthSession(token, name, avatar, expires);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException("Oturum token içeriği geçersiz.", ex);
        }
        catch (KeyNotFoundException ex)
        {
            throw new InvalidDataException("Oturum token alanları eksik.", ex);
        }
        catch (FormatException ex)
        {
            throw new InvalidDataException("Oturum token kodlaması geçersiz.", ex);
        }
    }

    internal static byte[] DecodeBase64Url(string value)
    {
        var base64 = value.Replace('-', '+').Replace('_', '/');
        base64 += new string('=', (4 - base64.Length % 4) % 4);
        return Convert.FromBase64String(base64);
    }
}

/// <summary>Persists the OAuth bearer token protected with Windows CurrentUser DPAPI.</summary>
public sealed class AuthSessionStore
{
    private readonly string _sessionPath;

    public AuthSessionStore(string sessionPath) => _sessionPath = Path.GetFullPath(sessionPath);

    public AuthSession? Load()
    {
        if (!File.Exists(_sessionPath)) return null;
        try
        {
            var saved = JsonSerializer.Deserialize<StoredSession>(File.ReadAllText(_sessionPath))
                ?? throw new InvalidDataException("Oturum kaydı boş.");
            var token = Encoding.UTF8.GetString(Dpapi.Unprotect(Convert.FromBase64String(saved.ProtectedToken)));
            var session = AuthTokenClaims.Read(token);
            return session;
        }
        catch
        {
            Clear();
            return null;
        }
    }

    public void Save(AuthSession session)
    {
        var validated = AuthTokenClaims.Read(session.Token);
        Directory.CreateDirectory(Path.GetDirectoryName(_sessionPath)!);
        var protectedToken = Dpapi.Protect(Encoding.UTF8.GetBytes(validated.Token));
        var tempPath = _sessionPath + ".tmp";
        File.WriteAllText(tempPath, JsonSerializer.Serialize(new StoredSession(
            Convert.ToBase64String(protectedToken), validated.DisplayName, validated.AvatarUrl, validated.ExpiresAt)));
        File.Move(tempPath, _sessionPath, true);
    }

    public void WriteRuntimeConfiguration(AuthSession session, string serverAddress, int serverPort, string outputPath)
    {
        if (string.IsNullOrWhiteSpace(serverAddress) || serverAddress.Contains(':') || serverAddress.Contains('/') || serverAddress.Contains('\\'))
            throw new InvalidDataException("launcher.config.json içindeki serverAddress geçersiz.");
        if (serverPort is < 1 or > 65535) throw new InvalidDataException("Sunucu portu geçersiz.");
        var validated = AuthTokenClaims.Read(session.Token);
        var protectedToken = Convert.ToBase64String(Dpapi.Protect(Encoding.UTF8.GetBytes(validated.Token)));
        var fullPath = Path.GetFullPath(outputPath);
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
        File.WriteAllText(fullPath, JsonSerializer.Serialize(new NativeAuthConfiguration(serverAddress, serverPort, protectedToken)));
    }

    public void Clear()
    {
        try { if (File.Exists(_sessionPath)) File.Delete(_sessionPath); }
        catch { }
    }

    private sealed record StoredSession(string ProtectedToken, string DisplayName, string AvatarUrl, long ExpiresAt);
}

internal static class Dpapi
{
    private const uint UiForbidden = 0x1;
    [StructLayout(LayoutKind.Sequential)]
    private struct DataBlob
    {
        public int Size;
        public IntPtr Data;
    }

    [DllImport("Crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(ref DataBlob input, string? description, IntPtr entropy,
        IntPtr reserved, IntPtr prompt, uint flags, out DataBlob output);

    [DllImport("Crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref DataBlob input, IntPtr description, IntPtr entropy,
        IntPtr reserved, IntPtr prompt, uint flags, out DataBlob output);

    [DllImport("Kernel32.dll", SetLastError = true)]
    private static extern IntPtr LocalFree(IntPtr memory);

    public static byte[] Protect(byte[] value) => Transform(value, true);
    public static byte[] Unprotect(byte[] value) => Transform(value, false);

    private static byte[] Transform(byte[] value, bool protect)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Launcher auth token saklama Windows DPAPI gerektirir.");
        var input = new DataBlob { Size = value.Length, Data = Marshal.AllocHGlobal(value.Length) };
        Marshal.Copy(value, 0, input.Data, value.Length);
        DataBlob output = default;
        try
        {
            var succeeded = protect
                ? CryptProtectData(ref input, "Skyrim Online STR session", IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, UiForbidden, out output)
                : CryptUnprotectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, UiForbidden, out output);
            if (!succeeded) throw new CryptographicException(Marshal.GetLastWin32Error());
            var result = new byte[output.Size];
            Marshal.Copy(output.Data, result, 0, output.Size);
            return result;
        }
        finally
        {
            if (input.Data != IntPtr.Zero)
            {
                Marshal.Copy(new byte[value.Length], 0, input.Data, value.Length);
                Marshal.FreeHGlobal(input.Data);
            }
            if (output.Data != IntPtr.Zero) _ = LocalFree(output.Data);
        }
    }
}
