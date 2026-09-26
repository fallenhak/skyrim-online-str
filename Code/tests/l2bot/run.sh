#!/usr/bin/env bash
# L2 bot run (#91): start the server on the fixture, run the bots, restart the server, check
# persistence, and check the server log.
# Usage: run.sh <dir with the server binaries and L2Bot> <work dir>
set -u
BIN=$(cd "$1" && pwd)
WORK=$2
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac

rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK" || exit 1
"$BIN/L2Bot$EXE" prepare . || exit 1
[ -n "$EXE" ] && cp "$BIN/SkyrimTogetherServer.exe" "$BIN/STServer.dll" .

export TILTED_ACCEPT_EULA=1
export SOS_AUTH_HMAC_SECRET=${SOS_AUTH_HMAC_SECRET:-l2bot-throwaway-secret-at-least-32-bytes}

start_server() {
  if [ -n "$EXE" ]; then
    ./SkyrimTogetherServer.exe >> server.log 2>&1 < /dev/null &
  else
    LD_LIBRARY_PATH="$BIN${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$BIN/SkyrimTogetherServer" >> server.log 2>&1 < /dev/null &
  fi
  SERVER_PID=$!
  local started_before=$1
  for _ in $(seq 1 60); do
    [ "$(grep -c "Server started" server.log 2>/dev/null)" -gt "$started_before" ] && return 0
    sleep 0.5
  done
  echo "FAIL: the server did not start"
  return 1
}

# A hard kill, like a crash: whatever was not persisted yet is lost.
stop_server() {
  kill -9 "$SERVER_PID" 2>/dev/null
  [ -n "$EXE" ] && taskkill //F //IM SkyrimTogetherServer.exe > /dev/null 2>&1
  wait "$SERVER_PID" 2>/dev/null
}

RESULT=0
start_server 0 || RESULT=1
if [ $RESULT -eq 0 ]; then
  "$BIN/L2Bot$EXE" connect 127.0.0.1:10578 || RESULT=1
fi
stop_server

if [ $RESULT -eq 0 ]; then
  echo "---- restart" >> server.log
  start_server 1 || RESULT=1
  if [ $RESULT -eq 0 ]; then
    "$BIN/L2Bot$EXE" after-restart 127.0.0.1:10578 || RESULT=1
  fi
  stop_server
fi

echo "---- server.log"
cat server.log

if ! grep -q "encounter zones: 1 zone(s)" server.log; then
  echo "FAIL: the server did not load the fixture plugin"
  RESULT=1
fi
# The scenario sends these on purpose: each must be dropped exactly once, and nothing else may be.
EXPECTED_DROPS=(
  "\[Drop\] projectile launch: invalid parameters"     # step 7, malformed spell
  "\[Drop\] death state: actor not found or not owner" # ownership, stale epoch
)
for DROP in "${EXPECTED_DROPS[@]}"; do
  if [ "$(grep -cE "$DROP" server.log)" != 1 ]; then
    echo "FAIL: expected exactly one line matching: $DROP"
    RESULT=1
  fi
done
EXPECTED_DROP=$(IFS='|'; echo "${EXPECTED_DROPS[*]}")
if grep -E "\[Drop\]|\[Desync\]|Couldn't parse" server.log | grep -vE "$EXPECTED_DROP"; then
  echo "FAIL: unexpected drop/desync/parse lines in the server log"
  RESULT=1
fi
[ $RESULT -eq 0 ] && echo "L2 BOT: PASS" || echo "L2 BOT: FAIL"
exit $RESULT
