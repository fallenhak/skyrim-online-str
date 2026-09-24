# Discord OAuth service

Python 3.12+ standard-library service. It binds only to `127.0.0.1:8091`; nginx
must expose `/auth/` only from the site's HTTPS virtual host, using
`nginx-auth-location.conf` and include `nginx-auth-rate-limit.conf` at HTTP scope. OAuth tokens and authorization codes are excluded
from service and nginx access logs. The OAuth callback returns the 12-hour
session token in the loopback URL fragment; launcher JavaScript posts it to the
local launcher listener, so the bearer token is absent from HTTP request lines.

Required root-owned `/etc/sos-auth.env` values (mode `0600`; systemd loads them
before dropping privileges to the `sosauth` account):

```sh
DISCORD_CLIENT_ID=...
DISCORD_CLIENT_SECRET=...
SOS_AUTH_HMAC_SECRET=<at least 32 random bytes>
SOS_AUTH_CALLBACK_URL=https://<auth-host>/auth/discord/callback
```

The game server reads only `SOS_AUTH_HMAC_SECRET` from
`/etc/sos-server-auth.env` via `20-sos-auth-server.conf`; set the same value in
both files so the Discord client secret stays out of the game-server process.
Keep `/etc/sos-server-auth.env` root-owned with mode `0600` as well.
Do not use a placeholder key in production. Without any
required Discord setting the service stays up but `/healthz` and
`/auth/discord/start` return `503` with a machine-readable configuration error.

Set the exact `SOS_AUTH_CALLBACK_URL` value in Discord Developer Portal as the
OAuth2 redirect URL. Enable the OAuth2 authorization-code flow and request only
the `identify` scope. No client secret or HMAC key belongs in the launcher.

Run tests with `python3 -m unittest -v test_auth_service.py` from this directory.
