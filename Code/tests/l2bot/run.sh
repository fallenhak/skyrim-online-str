#!/usr/bin/env bash
# L2 bot run (#91): start the server on the fixture, connect two bots, check the server log.
# Usage: run.sh <dir with the server binaries and L2Bot> <work dir>
set -u
BIN=$(cd "$1" && pwd)
WORK=$2
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac

rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK" || exit 1
"$BIN/L2Bot$EXE" prepare . || exit 1

export TILTED_ACCEPT_EULA=1
export SOS_AUTH_HMAC_SECRET=${SOS_AUTH_HMAC_SECRET:-l2bot-throwaway-secret-at-least-32-bytes}

if [ -n "$EXE" ]; then
  cp "$BIN/SkyrimTogetherServer.exe" "$BIN/STServer.dll" .
  ./SkyrimTogetherServer.exe > server.log 2>&1 < /dev/null &
else
  LD_LIBRARY_PATH="$BIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$BIN/SkyrimTogetherServer" > server.log 2>&1 < /dev/null &
fi
SERVER_PID=$!

for _ in $(seq 1 60); do
  grep -q "Server started" server.log 2>/dev/null && break
  sleep 0.5
done

"$BIN/L2Bot$EXE" connect 127.0.0.1:10578
RESULT=$?

kill "$SERVER_PID" 2>/dev/null
[ -n "$EXE" ] && taskkill //F //IM SkyrimTogetherServer.exe > /dev/null 2>&1
wait "$SERVER_PID" 2>/dev/null

echo "---- server.log"
cat server.log

if ! grep -q "encounter zones: 1 zone(s)" server.log; then
  echo "FAIL: the server did not load the fixture plugin"
  RESULT=1
fi
if grep -E "\[Drop\]|\[Desync\]|Couldn't parse" server.log; then
  echo "FAIL: unexpected drop/desync/parse lines in the server log"
  RESULT=1
fi
exit $RESULT
