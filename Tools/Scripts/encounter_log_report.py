#!/usr/bin/env python3
"""Summarise renewable encounter activity from a server log (runtime test plan).

Reads the [World] lines RenewableEncounterService prints and reports, per
encounter, the order of clears, blocked resets and resets, plus spawns, stale
removals, persistence and startup problems. It checks what the log alone can
prove; it does not replace watching the game.

usage: encounter_log_report.py <server.log>
exit code: 0 no problems found, 1 problems found, 2 bad usage
"""

import re
import sys
from collections import defaultdict

LOADED = re.compile(r"\[World\] renewable encounters loaded encounters=(\d+) cells=(\d+) slots=(\d+) errors=(\d+)")
NO_CONFIG = re.compile(r"\[World\] no renewable encounter config at (.+?), none configured")
RESTORED = re.compile(r"\[World\] snapshot restored encounters=(\d+)")
REJECTED = re.compile(r"\[World\] snapshot rejected")
SKIPPED = re.compile(r"\[World\] snapshot skipped (\d+)")
SAVED = re.compile(r"\[World\] snapshot saved encounters=(\d+)")
SAVE_FAILED = re.compile(r"\[World\] snapshot save failed")
CLEARED = re.compile(r"\[World\] encounter cleared ([0-9a-f]+/\d+) tick=(\d+)")
BLOCKED = re.compile(r"\[World\] reset blocked ([0-9a-f]+/\d+) reason=(\w+) tick=(\d+)")
RESET = re.compile(r"\[World\] encounter reset ([0-9a-f]+/\d+) epoch=(\d+)->(\d+) tick=(\d+)")
SPAWNED = re.compile(r"\[World\] spawn completed slot=([0-9a-f]+) incarnation=([0-9a-f]+):(\d+)")
SPAWN_REJECTED = re.compile(r"\[World\] spawn rejected slot=([0-9a-f]+)")
STALE = re.compile(r"\[World\] stale actor removed incarnation=([0-9a-f]+):(\d+)")


def analyse(lines):
    problems = []
    notes = []
    startups = 0
    history = defaultdict(list)  # encounter -> [(event, detail)]
    spawned = saved = stale = rejected_spawns = 0
    restored = None

    for number, line in enumerate(lines, 1):
        if m := LOADED.search(line):
            startups += 1
            encounters, cells, slots, errors = map(int, m.groups())
            notes.append(f"line {number}: config loaded encounters={encounters} cells={cells} slots={slots} errors={errors}")
            if encounters == 0:
                problems.append(f"line {number}: config loaded with no encounters")
            if errors:
                problems.append(f"line {number}: config has {errors} bad line(s)")
        elif m := NO_CONFIG.search(line):
            startups += 1
            problems.append(f"line {number}: no encounter config at {m.group(1)}; the run tests nothing")
        elif m := RESTORED.search(line):
            restored = int(m.group(1))
        elif REJECTED.search(line):
            problems.append(f"line {number}: stored snapshot rejected, encounters started fresh")
        elif m := SKIPPED.search(line):
            notes.append(f"line {number}: snapshot skipped {m.group(1)} unconfigured encounter(s)")
        elif SAVE_FAILED.search(line):
            problems.append(f"line {number}: snapshot save failed")
        elif SAVED.search(line):
            saved += 1
        elif m := CLEARED.search(line):
            history[m.group(1)].append(("cleared", f"tick={m.group(2)}", number))
        elif m := BLOCKED.search(line):
            history[m.group(1)].append(("blocked", f"{m.group(2)} tick={m.group(3)}", number))
        elif m := RESET.search(line):
            history[m.group(1)].append(("reset", f"epoch {m.group(2)}->{m.group(3)} tick={m.group(4)}", number))
        elif SPAWNED.search(line):
            spawned += 1
        elif SPAWN_REJECTED.search(line):
            rejected_spawns += 1
        elif STALE.search(line):
            stale += 1

    if startups == 0:
        problems.append("no startup line: is this a server log at info level?")

    for encounter, events in sorted(history.items()):
        cleared = False
        for event, _, number in events:
            if event == "cleared":
                if cleared:
                    problems.append(f"line {number}: {encounter} cleared twice without a reset in between")
                cleared = True
            elif event == "reset":
                if not cleared and startups <= 1:
                    problems.append(f"line {number}: {encounter} reset without being cleared first")
                cleared = False

    report = ["== startup"]
    report += notes or ["(nothing)"]
    report.append(f"snapshot restored: {restored if restored is not None else 'no'}; saves: {saved}")
    report.append(f"spawns bound: {spawned}; spawns rejected: {rejected_spawns}; stale actors removed: {stale}")
    for encounter, events in sorted(history.items()):
        report.append(f"== encounter {encounter}")
        report += [f"  line {number}: {event} {detail}" for event, detail, number in events]
    report.append("== problems")
    report += problems or ["none"]
    return report, problems


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-2], file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8", errors="replace") as log:
        report, problems = analyse(log)
    print("\n".join(report))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
