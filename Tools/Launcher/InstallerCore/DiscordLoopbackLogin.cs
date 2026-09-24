using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace SosStr.Launcher;

/// <summary>Receives an OAuth token from a loopback-only launcher callback listener.</summary>
public sealed class DiscordLoopbackLogin
{
    private static readonly byte[] CallbackPage = Encoding.UTF8.GetBytes("""
        <!doctype html><meta charset="utf-8"><meta name="referrer" content="no-referrer"><title>Skyrim Online STR</title>
        <p id="status">Giriş tamamlanıyor…</p>
        <script>
        const p = new URLSearchParams(location.hash.slice(1));
        history.replaceState(null, '', location.pathname);
        fetch('/token', { method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ state: p.get('state'), token: p.get('token'), error: p.get('error') }) })
          .then(r => r.ok ? 'Giriş tamamlandı. Bu pencereyi kapatabilirsiniz.' : 'Giriş alınamadı; launcher ekranını kontrol edin.')
          .catch(() => 'Launcher bağlantısı kurulamadı.')
          .then(t => { document.getElementById('status').textContent = t; });
        </script>
        """);

    public async Task<AuthSession> SignInAsync(string authBaseUrl, AuthSessionStore store, CancellationToken cancellationToken = default)
    {
        if (!Uri.TryCreate(authBaseUrl, UriKind.Absolute, out var authBase) || authBase.Scheme != Uri.UriSchemeHttps)
            throw new InvalidOperationException("launcher.config.json içindeki authBaseUrl geçerli bir HTTPS adresi olmalı.");

        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start(4);
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var redirectUri = new Uri($"http://127.0.0.1:{port}/callback");
        var state = Base64Url(RandomNumberGenerator.GetBytes(32));
        var startUri = new UriBuilder(authBase)
        {
            Path = "/auth/discord/start",
            Query = "redirect_uri=" + Uri.EscapeDataString(redirectUri.ToString()) + "&state=" + Uri.EscapeDataString(state),
            Fragment = "",
        }.Uri;

        var completion = new TaskCompletionSource<AuthSession>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(3));
        using var registration = timeout.Token.Register(() => completion.TrySetCanceled(timeout.Token));
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(startUri.ToString()) { UseShellExecute = true });

        try
        {
            while (!completion.Task.IsCompleted)
            {
                var accept = listener.AcceptTcpClientAsync(timeout.Token).AsTask();
                var ready = await Task.WhenAny(accept, completion.Task).ConfigureAwait(false);
                if (ready == completion.Task) break;
                var client = await accept.ConfigureAwait(false);
                _ = HandleClientAsync(client, state, store, completion);
            }
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException("Discord girişi 3 dakika içinde tamamlanmadı.");
        }
        return await completion.Task.ConfigureAwait(false);
    }

    private static async Task HandleClientAsync(TcpClient client, string expectedState, AuthSessionStore store,
        TaskCompletionSource<AuthSession> completion)
    {
        using (client)
        {
            try
            {
                var stream = client.GetStream();
                var (requestLine, headers, body) = await ReadRequestAsync(stream).ConfigureAwait(false);
                var requestParts = requestLine.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (requestParts.Length != 3)
                {
                    await ReplyAsync(stream, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request"u8.ToArray()).ConfigureAwait(false);
                    return;
                }

                if (requestParts[0] == "GET" && requestParts[1] == "/callback")
                {
                    await ReplyAsync(stream, 200, "OK", "text/html; charset=utf-8", CallbackPage,
                        "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'\r\n").ConfigureAwait(false);
                    return;
                }

                if (requestParts[0] != "POST" || requestParts[1] != "/token" ||
                    !headers.TryGetValue("content-type", out var contentType) || !contentType.StartsWith("application/json", StringComparison.OrdinalIgnoreCase))
                {
                    await ReplyAsync(stream, 404, "Not Found", "text/plain; charset=utf-8", "Not Found"u8.ToArray()).ConfigureAwait(false);
                    return;
                }

                var transfer = Encoding.UTF8.GetString(body);
                // The callback page posts lower-case keys ({"state","token","error"}); System.Text.Json is
                // case-sensitive by default, which left State null and rejected every login as a mismatch.
                var payload = JsonSerializer.Deserialize<CallbackPayload>(transfer, CallbackJson);
                if (payload?.State is null || !CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes(payload.State), Encoding.UTF8.GetBytes(expectedState)))
                {
                    await ReplyAsync(stream, 403, "Forbidden", "text/plain; charset=utf-8", "State mismatch"u8.ToArray()).ConfigureAwait(false);
                    return;
                }
                if (!string.IsNullOrEmpty(payload.Error))
                {
                    completion.TrySetException(new InvalidOperationException("Discord girişi tamamlanamadı: " + SafeError(payload.Error)));
                    await ReplyAsync(stream, 200, "OK", "text/plain; charset=utf-8", "Giriş başarısız."u8.ToArray()).ConfigureAwait(false);
                    return;
                }
                if (string.IsNullOrWhiteSpace(payload.Token))
                {
                    completion.TrySetException(new InvalidDataException("Discord dönüşünde oturum token'ı bulunamadı."));
                    await ReplyAsync(stream, 400, "Bad Request", "text/plain; charset=utf-8", "Missing token"u8.ToArray()).ConfigureAwait(false);
                    return;
                }

                var session = AuthTokenClaims.Read(payload.Token);
                store.Save(session);
                completion.TrySetResult(session);
                await ReplyAsync(stream, 200, "OK", "text/plain; charset=utf-8", "Discord girişi tamamlandı. Bu pencereyi kapatabilirsiniz."u8.ToArray()).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                completion.TrySetException(ex);
                try
                {
                    await ReplyAsync(client.GetStream(), 400, "Bad Request", "text/plain; charset=utf-8", "Giriş verisi okunamadı."u8.ToArray()).ConfigureAwait(false);
                }
                catch { }
            }
        }
    }

    private static async Task<(string RequestLine, Dictionary<string, string> Headers, byte[] Body)> ReadRequestAsync(NetworkStream stream)
    {
        var headerBuffer = new List<byte>(1024);
        while (headerBuffer.Count < 8192)
        {
            var one = new byte[1];
            var count = await stream.ReadAsync(one).ConfigureAwait(false);
            if (count == 0) throw new IOException("Loopback bağlantısı beklenmedik şekilde kapandı.");
            headerBuffer.Add(one[0]);
            var n = headerBuffer.Count;
            if (n >= 4 && headerBuffer[n - 4] == 13 && headerBuffer[n - 3] == 10 && headerBuffer[n - 2] == 13 && headerBuffer[n - 1] == 10)
                break;
        }
        if (headerBuffer.Count >= 8192) throw new InvalidDataException("Loopback HTTP başlığı çok büyük.");
        var headerText = Encoding.ASCII.GetString(headerBuffer.ToArray());
        var lines = headerText.Split("\r\n", StringSplitOptions.None);
        if (lines.Length < 2) throw new InvalidDataException("Loopback HTTP isteği geçersiz.");
        var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in lines.Skip(1))
        {
            var colon = line.IndexOf(':');
            if (colon > 0) headers[line[..colon].Trim().ToLowerInvariant()] = line[(colon + 1)..].Trim();
        }
        var contentLength = 0;
        if (headers.TryGetValue("content-length", out var contentLengthText) &&
            (!int.TryParse(contentLengthText, out contentLength) || contentLength is < 0 or > 8192))
            throw new InvalidDataException("Loopback HTTP gövde boyutu geçersiz.");
        var body = new byte[contentLength];
        var offset = 0;
        while (offset < body.Length)
        {
            var read = await stream.ReadAsync(body.AsMemory(offset)).ConfigureAwait(false);
            if (read == 0) throw new IOException("Loopback gövdesi tamamlanmadı.");
            offset += read;
        }
        return (lines[0], headers, body);
    }

    private static async Task ReplyAsync(NetworkStream stream, int status, string reason, string contentType, byte[] body, string extraHeaders = "")
    {
        var head = Encoding.ASCII.GetBytes($"HTTP/1.1 {status} {reason}\r\nContent-Type: {contentType}\r\nContent-Length: {body.Length}\r\nCache-Control: no-store\r\nPragma: no-cache\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n{extraHeaders}\r\n");
        await stream.WriteAsync(head).ConfigureAwait(false);
        await stream.WriteAsync(body).ConfigureAwait(false);
        await stream.FlushAsync().ConfigureAwait(false);
    }

    private static string Base64Url(byte[] value) => Convert.ToBase64String(value).TrimEnd('=').Replace('+', '-').Replace('/', '_');
    private static string SafeError(string value) => value.Length <= 80 && value.All(ch => char.IsAsciiLetterOrDigit(ch) || ch is '_' or '-') ? value : "discord_login_failed";

    private static readonly JsonSerializerOptions CallbackJson = new() { PropertyNameCaseInsensitive = true };

    private sealed record CallbackPayload(string? State, string? Token, string? Error);
}
