#!/usr/bin/env python3
"""Acceptance check for a test server's startup (M01 test build).

Looks at the server's install directory and its log and fails if the server
did not come up the way the test build requires. Only the most recent startup
in the log counts: each check uses the last matching line.

Checks:
  - the log file exists
  - "Server <commit> started on port <port>" with the expected port
  - Data/renewable_encounters.txt exists under the server root
  - "[World] renewable encounters loaded ... errors=0" with at least one
    encounter, and no "no renewable encounter config" warning after it
  - every extra --require pattern (other lanes' acceptance lines)

usage: server_startup_check.py [--root DIR] [--log FILE] [--port N] [--require REGEX ...]
exit code: 0 all checks pass, 1 a check failed, 2 bad usage
"""

import argparse
import re
import sys
from pathlib import Path

DEFAULT_ROOT = "/var/lib/sos-server"
DEFAULT_LOG = "logs/STServerOut.log"
DEFAULT_PORT = 10578

STARTED = re.compile(r"Server (\S+) started on port (\d+)")
LOADED = re.compile(r"\[World\] renewable encounters loaded encounters=(\d+) cells=(\d+) slots=(\d+) errors=(\d+)")
NO_CONFIG = re.compile(r"\[World\] no renewable encounter config at (.+?), none configured")


def last_match(lines, pattern):
    """Returns (line number, match) of the last line matching pattern, or (0, None)."""
    found = (0, None)
    for number, line in enumerate(lines, 1):
        if m := pattern.search(line):
            found = (number, m)
    return found


def check(root, log_path, port, required):
    results = []

    def record(ok, text):
        results.append((ok, text))

    config = root / "Data" / "renewable_encounters.txt"
    record(config.is_file(), f"encounter config present: {config}")

    if not log_path.is_file():
        record(False, f"server log present: {log_path}")
        return results
    record(True, f"server log present: {log_path}")

    with open(log_path, encoding="utf-8", errors="replace") as log:
        lines = log.readlines()

    number, m = last_match(lines, STARTED)
    if m is None:
        record(False, "server started: no 'started on port' line")
    else:
        record(int(m.group(2)) == port, f"server started (line {number}): commit {m.group(1)} port {m.group(2)}, expected {port}")

    loaded_at, loaded = last_match(lines, LOADED)
    missing_at, missing = last_match(lines, NO_CONFIG)
    if missing and missing_at > loaded_at:
        record(False, f"encounters loaded (line {missing_at}): server found no config at {missing.group(1)}")
    elif loaded is None:
        record(False, "encounters loaded: no 'renewable encounters loaded' line")
    else:
        encounters, cells, slots, errors = map(int, loaded.groups())
        record(encounters > 0 and errors == 0,
               f"encounters loaded (line {loaded_at}): encounters={encounters} cells={cells} slots={slots} errors={errors}")

    for pattern in required:
        number, m = last_match(lines, re.compile(pattern))
        record(m is not None, f"required line /{pattern}/" + (f" (line {number})" if m else ": not found"))

    return results


def main(argv):
    parser = argparse.ArgumentParser(description="M01 test server startup acceptance check")
    parser.add_argument("--root", default=DEFAULT_ROOT, help=f"server install directory (default {DEFAULT_ROOT})")
    parser.add_argument("--log", help=f"server log (default <root>/{DEFAULT_LOG})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"expected game port (default {DEFAULT_PORT})")
    parser.add_argument("--require", action="append", default=[], metavar="REGEX", help="another line the log must contain; repeatable")
    args = parser.parse_args(argv[1:])

    root = Path(args.root)
    log_path = Path(args.log) if args.log else root / DEFAULT_LOG
    results = check(root, log_path, args.port, args.require)

    for ok, text in results:
        print(("PASS  " if ok else "FAIL  ") + text)
    failed = sum(1 for ok, _ in results if not ok)
    print(f"== {len(results) - failed}/{len(results)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
