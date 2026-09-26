import json
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from http.server import ThreadingHTTPServer
from unittest.mock import patch

import auth_service


ENV = {
    "DISCORD_CLIENT_ID": "1234567890",
    "DISCORD_CLIENT_SECRET": "discord-client-secret-for-test",
    "SOS_AUTH_HMAC_SECRET": "0123456789abcdef0123456789abcdef",
    "SOS_AUTH_CALLBACK_URL": "https://auth.example.test/auth/discord/callback",
}


class AuthServiceTests(unittest.TestCase):
    def setUp(self):
        self.config = auth_service.load_config(ENV)

    def test_missing_environment_is_reported(self):
        with self.assertRaisesRegex(auth_service.AuthConfigError, "DISCORD_CLIENT_ID"):
            auth_service.load_config({})

    def test_callback_url_requires_https_and_expected_path(self):
        with self.assertRaisesRegex(auth_service.AuthConfigError, "HTTPS"):
            auth_service.load_config({**ENV, "SOS_AUTH_CALLBACK_URL": "http://auth.example.test/auth/discord/callback"})

    def test_token_contains_discord_subject_and_expires_after_seven_days(self):
        token = auth_service.create_session_token(self.config, "123456789012345678", "Kerim Öztürk", "https://cdn.example/avatar.png", now=1_000)
        header, payload, signature = token.split(".")
        claims = json.loads(auth_service.base64.urlsafe_b64decode(payload + "=="))
        self.assertEqual(claims["sub"], "discord:123456789012345678")
        self.assertEqual(claims["name"], "Kerim Öztürk")
        self.assertEqual(claims["avatar"], "https://cdn.example/avatar.png")
        self.assertEqual(claims["exp"], 1_000 + 7 * 24 * 60 * 60)
        self.assertEqual(claims["auth_time"], 1_000)
        self.assertEqual(json.loads(auth_service.base64.urlsafe_b64decode(header + "=="))["alg"], "HS256")
        self.assertEqual(len(auth_service.base64.urlsafe_b64decode(signature + "==")), 32)

    def test_discord_user_id_must_be_numeric(self):
        with self.assertRaisesRegex(ValueError, "user ID"):
            auth_service.create_session_token(self.config, "not-an-id", "Kerim", "", now=1_000)

    def test_loopback_redirect_rejects_remote_or_non_callback_urls(self):
        self.assertTrue(auth_service._loopback_redirect("http://127.0.0.1:43123/callback"))
        for value in (
            "http://localhost:43123/callback",
            "http://127.0.0.1:80/callback",
            "http://127.0.0.1:43123/other",
            "http://127.0.0.1.evil.test:43123/callback",
            "https://127.0.0.1:43123/callback",
        ):
            self.assertFalse(auth_service._loopback_redirect(value), value)

    def test_start_requires_config_and_validated_loopback_and_uses_identify_scope(self):
        server, thread = self._serve(self.config)
        try:
            invalid = self._get(server, "/auth/discord/start?redirect_uri=" + urllib.parse.quote("http://example.com:43123/callback") + "&state=" + "x" * 40)
            self.assertEqual(invalid[0], 400)
            good_url = "/auth/discord/start?" + urllib.parse.urlencode({
                "redirect_uri": "http://127.0.0.1:43123/callback",
                "state": "a" * 40,
            })
            status, headers, _ = self._get(server, good_url, follow=False)
            self.assertEqual(status, 302)
            target = urllib.parse.urlsplit(headers["Location"])
            params = urllib.parse.parse_qs(target.query)
            self.assertEqual(params["scope"], ["identify"])
            self.assertEqual(params["redirect_uri"], [ENV["SOS_AUTH_CALLBACK_URL"]])
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def test_health_reports_missing_server_credentials(self):
        server, thread = self._serve(None)
        try:
            status, _, body = self._get(server, "/auth/healthz")
            self.assertEqual(status, 503)
            self.assertEqual(json.loads(body)["status"], "not_configured")
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def test_callback_issues_token_and_returns_it_only_in_fragment(self):
        server, thread = self._serve(self.config)
        server.oauth_states["oauth-state"] = (time.time() + 60, "http://127.0.0.1:43123/callback", "b" * 40)
        try:
            with patch.object(auth_service, "exchange_discord_code", return_value={
                "id": "42", "name": "Burak", "avatar": "https://cdn.example/avatar.png",
            }):
                callback = "/auth/discord/callback?" + urllib.parse.urlencode({"state": "oauth-state", "code": "one-time-code"})
                status, headers, _ = self._get(server, callback, follow=False)
            self.assertEqual(status, 302)
            self.assertNotIn("token=", headers["Location"].split("#", 1)[0])
            fragment = urllib.parse.parse_qs(urllib.parse.urlsplit(headers["Location"]).fragment)
            self.assertEqual(fragment["state"], ["b" * 40])
            claims = json.loads(auth_service.base64.urlsafe_b64decode(fragment["token"][0].split(".")[1] + "=="))
            self.assertEqual(claims["sub"], "discord:42")
            self.assertEqual(claims["name"], "Burak")
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def test_oauth_state_is_single_use(self):
        server, thread = self._serve(self.config)
        server.oauth_states["once"] = (time.time() + 60, "http://127.0.0.1:43123/callback", "c" * 40)
        try:
            first = self._get(server, "/auth/discord/callback?state=once&error=access_denied", follow=False)
            second = self._get(server, "/auth/discord/callback?state=once&error=access_denied", follow=False)
            self.assertEqual(first[0], 302)
            self.assertEqual(second[0], 400)
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def _claims(self, token):
        return json.loads(auth_service._b64url_decode(token.split(".")[1]))

    def test_refresh_extends_expiry_and_keeps_login_time(self):
        token = auth_service.create_session_token(self.config, "42", "Burak", "https://cdn.example/a.png", now=1_000)
        claims = self._claims(auth_service.refresh_session_token(self.config, token, now=5_000))
        self.assertEqual(claims["sub"], "discord:42")
        self.assertEqual(claims["name"], "Burak")
        self.assertEqual(claims["avatar"], "https://cdn.example/a.png")
        self.assertEqual(claims["exp"], 5_000 + auth_service.TOKEN_TTL_SECONDS)
        self.assertEqual(claims["auth_time"], 1_000)

    def test_refresh_rejects_expired_forged_and_too_old_sessions(self):
        token = auth_service.create_session_token(self.config, "42", "Burak", "", now=1_000)
        with self.assertRaisesRegex(ValueError, "expired"):
            auth_service.refresh_session_token(self.config, token, now=1_000 + auth_service.TOKEN_TTL_SECONDS)
        other = auth_service.load_config({**ENV, "SOS_AUTH_HMAC_SECRET": "f" * 32})
        with self.assertRaisesRegex(ValueError, "signature"):
            auth_service.refresh_session_token(other, token, now=2_000)
        old = auth_service.create_session_token(self.config, "42", "Burak", "", now=1_000 + auth_service.MAX_SESSION_AGE_SECONDS - 60, auth_time=1_000)
        with self.assertRaisesRegex(ValueError, "too old"):
            auth_service.refresh_session_token(self.config, old, now=1_000 + auth_service.MAX_SESSION_AGE_SECONDS + 1)

    def test_refresh_endpoint_requires_valid_bearer_token(self):
        server, thread = self._serve(self.config)
        url = "http://127.0.0.1:%d/auth/refresh" % server.server_port

        def post(headers):
            request = urllib.request.Request(url, data=b"", headers=headers, method="POST")
            try:
                with urllib.request.urlopen(request, timeout=3) as response:
                    return response.status, json.loads(response.read())
            except urllib.error.HTTPError as error:
                with error:
                    return error.code, json.loads(error.read())

        try:
            self.assertEqual(post({})[0], 401)
            self.assertEqual(post({"Authorization": "Bearer nope"})[0], 401)
            token = auth_service.create_session_token(self.config, "42", "Burak", "")
            status, body = post({"Authorization": "Bearer " + token})
            self.assertEqual(status, 200)
            self.assertEqual(self._claims(body["token"])["sub"], "discord:42")
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def _serve(self, config):
        server = auth_service.AuthHTTPServer(("127.0.0.1", 0), config, logger=lambda _: None)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        return server, thread

    def _get(self, server, path, follow=True):
        request = urllib.request.Request("http://127.0.0.1:%d%s" % (server.server_port, path))
        opener = urllib.request.build_opener() if follow else urllib.request.build_opener(_NoRedirect())
        try:
            with opener.open(request, timeout=3) as response:
                return response.status, dict(response.headers), response.read()
        except urllib.error.HTTPError as error:
            result = error.code, dict(error.headers), error.read()
            error.close()
            return result


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


if __name__ == "__main__":
    unittest.main()
