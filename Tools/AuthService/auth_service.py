#!/usr/bin/env python3
"""Small loopback-only Discord OAuth and HS256 session-token service."""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Mapping


TOKEN_TTL_SECONDS = 12 * 60 * 60
OAUTH_STATE_TTL_SECONDS = 10 * 60
DISCORD_AUTHORIZE_URL = "https://discord.com/oauth2/authorize"
DISCORD_TOKEN_URL = "https://discord.com/api/oauth2/token"
# Discord sits behind Cloudflare, which rejects urllib's default User-Agent (HTTP 403).
DISCORD_USER_AGENT = "DiscordBot (https://github.com/fallenhak/skyrim-online-str, 1.0)"
DISCORD_USER_URL = "https://discord.com/api/users/@me"
_LOOPBACK_STATE = re.compile(r"^[A-Za-z0-9_-]{32,100}$")


class AuthConfigError(ValueError):
    pass


@dataclass(frozen=True)
class AuthConfig:
    discord_client_id: str
    discord_client_secret: str
    hmac_secret: bytes
    callback_url: str


def load_config(env: Mapping[str, str] | None = None) -> AuthConfig:
    values = os.environ if env is None else env
    required = (
        "DISCORD_CLIENT_ID",
        "DISCORD_CLIENT_SECRET",
        "SOS_AUTH_HMAC_SECRET",
        "SOS_AUTH_CALLBACK_URL",
    )
    missing = [key for key in required if not values.get(key, "").strip()]
    if missing:
        raise AuthConfigError("missing required settings: " + ", ".join(missing))

    callback = urllib.parse.urlsplit(values["SOS_AUTH_CALLBACK_URL"])
    if callback.scheme != "https" or not callback.hostname or callback.path != "/auth/discord/callback" or callback.query or callback.fragment:
        raise AuthConfigError("SOS_AUTH_CALLBACK_URL must be an HTTPS /auth/discord/callback URL")

    key = values["SOS_AUTH_HMAC_SECRET"].encode("utf-8")
    if len(key) < 32:
        raise AuthConfigError("SOS_AUTH_HMAC_SECRET must be at least 32 bytes")

    return AuthConfig(
        discord_client_id=values["DISCORD_CLIENT_ID"].strip(),
        discord_client_secret=values["DISCORD_CLIENT_SECRET"],
        hmac_secret=key,
        callback_url=values["SOS_AUTH_CALLBACK_URL"],
    )


def _b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def create_session_token(config: AuthConfig, discord_id: str, name: str, avatar_url: str, now: int | None = None) -> str:
    if not discord_id.isascii() or not discord_id.isdigit() or not (1 <= len(discord_id) <= 20):
        raise ValueError("Discord returned an invalid user ID")
    timestamp = int(time.time()) if now is None else int(now)
    name = name.encode("utf-8")[:128].decode("utf-8", errors="ignore")
    header = _b64url(json.dumps({"alg": "HS256", "typ": "JWT"}, separators=(",", ":")).encode("utf-8"))
    claims = {
        "iss": "sos-auth",
        "sub": "discord:" + discord_id,
        "name": name[:128],
        "avatar": avatar_url[:512],
        "iat": timestamp,
        "exp": timestamp + TOKEN_TTL_SECONDS,
    }
    payload = _b64url(json.dumps(claims, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    signing_input = header + "." + payload
    signature = hmac.new(config.hmac_secret, signing_input.encode("ascii"), hashlib.sha256).digest()
    return signing_input + "." + _b64url(signature)


def _loopback_redirect(value: str) -> bool:
    try:
        parsed = urllib.parse.urlsplit(value)
        return (
            parsed.scheme == "http"
            and parsed.hostname == "127.0.0.1"
            and parsed.port is not None
            and 1024 <= parsed.port <= 65535
            and parsed.path == "/callback"
            and not parsed.username
            and not parsed.password
            and not parsed.query
            and not parsed.fragment
        )
    except ValueError:
        return False


def exchange_discord_code(config: AuthConfig, code: str) -> dict:
    body = urllib.parse.urlencode({
        "client_id": config.discord_client_id,
        "client_secret": config.discord_client_secret,
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": config.callback_url,
    }).encode("ascii")
    request = urllib.request.Request(DISCORD_TOKEN_URL, data=body, headers={"Content-Type": "application/x-www-form-urlencoded", "User-Agent": DISCORD_USER_AGENT})
    with urllib.request.urlopen(request, timeout=10) as response:
        token_data = json.loads(response.read(65536))
    access_token = token_data.get("access_token")
    if not isinstance(access_token, str) or not access_token:
        raise ValueError("Discord did not return an access token")

    request = urllib.request.Request(DISCORD_USER_URL, headers={"Authorization": "Bearer " + access_token, "User-Agent": DISCORD_USER_AGENT})
    with urllib.request.urlopen(request, timeout=10) as response:
        profile = json.loads(response.read(65536))
    discord_id = str(profile.get("id", ""))
    if not discord_id.isascii() or not discord_id.isdigit() or not (1 <= len(discord_id) <= 20):
        raise ValueError("Discord returned an invalid user ID")
    name = profile.get("global_name") or profile.get("username")
    if not isinstance(name, str) or not name.strip():
        raise ValueError("Discord returned an invalid display name")
    avatar_hash = profile.get("avatar")
    if isinstance(avatar_hash, str) and avatar_hash:
        extension = "gif" if avatar_hash.startswith("a_") else "png"
        avatar = f"https://cdn.discordapp.com/avatars/{discord_id}/{avatar_hash}.{extension}?size=128"
    else:
        avatar = f"https://cdn.discordapp.com/embed/avatars/{int(discord_id) % 6}.png"
    return {"id": discord_id, "name": name, "avatar": avatar}


class AuthHandler(BaseHTTPRequestHandler):
    server: "AuthHTTPServer"

    def log_message(self, fmt: str, *args: object) -> None:
        # OAuth callback query strings carry one-time codes; never put them in logs.
        self.server.logger("%s %s" % (self.command, urllib.parse.urlsplit(self.path).path))

    def _reply(self, status: int, payload: bytes, content_type: str = "application/json; charset=utf-8", headers: Mapping[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Pragma", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(payload)

    def _json(self, status: int, value: dict, headers: Mapping[str, str] | None = None) -> None:
        self._reply(status, json.dumps(value, ensure_ascii=False).encode("utf-8"), headers=headers)

    def do_GET(self) -> None:
        route = urllib.parse.urlsplit(self.path)
        if route.path in ("/healthz", "/auth/healthz"):
            configured = self.server.config is not None
            self._json(200 if configured else 503, {"status": "ok" if configured else "not_configured"})
            return
        if route.path == "/auth/discord/start":
            self._start(urllib.parse.parse_qs(route.query, strict_parsing=False))
            return
        if route.path == "/auth/discord/callback":
            self._callback(urllib.parse.parse_qs(route.query, strict_parsing=False))
            return
        self._json(404, {"error": "not_found"})

    def _start(self, query: dict[str, list[str]]) -> None:
        config = self.server.config
        if config is None:
            self._json(503, {"error": "auth_service_not_configured"})
            return
        redirect_uri = query.get("redirect_uri", [""])[0]
        launcher_state = query.get("state", [""])[0]
        if not _loopback_redirect(redirect_uri) or not _LOOPBACK_STATE.fullmatch(launcher_state):
            self._json(400, {"error": "invalid_loopback_request"})
            return

        oauth_state = secrets.token_urlsafe(32)
        with self.server.oauth_states_lock:
            self.server.oauth_states[oauth_state] = (time.time() + OAUTH_STATE_TTL_SECONDS, redirect_uri, launcher_state)
            self.server.prune_states()
        params = urllib.parse.urlencode({
            "client_id": config.discord_client_id,
            "response_type": "code",
            "redirect_uri": config.callback_url,
            "scope": "identify",
            "state": oauth_state,
        })
        self.send_response(302)
        self.send_header("Location", DISCORD_AUTHORIZE_URL + "?" + params)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _callback(self, query: dict[str, list[str]]) -> None:
        config = self.server.config
        if config is None:
            self._json(503, {"error": "auth_service_not_configured"})
            return
        oauth_state = query.get("state", [""])[0]
        with self.server.oauth_states_lock:
            state_entry = self.server.oauth_states.pop(oauth_state, None)
        if state_entry is None or state_entry[0] < time.time():
            print("sos-auth: callback rejected: unknown or expired oauth state", flush=True)
            self._json(400, {"error": "oauth_state_invalid"})
            return
        _, redirect_uri, launcher_state = state_entry
        oauth_error = query.get("error", [""])[0]
        code = query.get("code", [""])[0]
        if oauth_error or not code:
            print(f"sos-auth: callback without code (discord error={oauth_error[:64]!r})", flush=True)
            self._redirect_loopback(redirect_uri, launcher_state, error="discord_login_failed")
            return

        try:
            profile = exchange_discord_code(config, code)
            token = create_session_token(config, profile["id"], profile["name"], profile["avatar"])
        except (urllib.error.URLError, TimeoutError, ValueError, KeyError, json.JSONDecodeError) as exc:
            detail = f"HTTP {exc.code}" if isinstance(exc, urllib.error.HTTPError) else f"{type(exc).__name__}: {str(exc)[:200]}"
            print(f"sos-auth: callback token exchange failed: {detail}", flush=True)
            self._redirect_loopback(redirect_uri, launcher_state, error="discord_auth_failed")
            return
        print("sos-auth: callback ok, session token issued", flush=True)
        self._redirect_loopback(redirect_uri, launcher_state, token=token)

    def _redirect_loopback(self, redirect_uri: str, launcher_state: str, token: str = "", error: str = "") -> None:
        # Keep the bearer token out of HTTP request lines, access logs and Referer headers.
        fragment = urllib.parse.urlencode({"state": launcher_state, "token": token, "error": error})
        self.send_response(302)
        self.send_header("Location", redirect_uri + "#" + fragment)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Length", "0")
        self.end_headers()


class AuthHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], config: AuthConfig | None, logger=print):
        super().__init__(address, AuthHandler)
        self.config = config
        self.logger = logger
        self.oauth_states: dict[str, tuple[float, str, str]] = {}
        self.oauth_states_lock = threading.Lock()

    def prune_states(self) -> None:
        current = time.time()
        self.oauth_states = {state: entry for state, entry in self.oauth_states.items() if entry[0] >= current}


def main() -> None:
    try:
        config = load_config()
    except AuthConfigError as exc:
        config = None
        print(f"sos-auth: not configured: {exc}", flush=True)
    server = AuthHTTPServer(("127.0.0.1", int(os.environ.get("SOS_AUTH_PORT", "8091"))), config)
    print("sos-auth: listening on 127.0.0.1:%d" % server.server_port, flush=True)
    server.serve_forever(poll_interval=0.5)


if __name__ == "__main__":
    main()
