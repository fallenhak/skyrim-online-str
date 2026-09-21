#!/usr/bin/env python3
"""Deterministic supervisor for the Skyrim development lanes.

Codex workers edit ordinary source, tests, and documentation only. This trusted
outer process performs validation, Git operations, CI polling, recovery bounds,
resource guards, and review checkpoints.
"""
from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import json
import os
import pwd
import re
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

SERVICE_ROOT = Path("/srv/services/skyrim-dev")
CONFIG_PATH = SERVICE_ROOT / "config" / "supervisor.json"
STATE_ROOT = Path("/var/lib/skyrim-dev")
STATE_DIR = STATE_ROOT / "state"
STATE_PATH = STATE_DIR / "state.json"
CONTROL_PATH = STATE_DIR / "control.json"
LOCK_PATH = STATE_DIR / "supervisor.lock"
REVIEW_ROOT = STATE_ROOT / "review-packets"
LOG_ROOT = Path("/var/log/skyrim-dev")
SUPERVISOR_LOG = LOG_ROOT / "supervisor.log"
UNIT_NAME = "skyrim-dev-orchestrator.service"
LANE_ORDER = ("combat", "authority", "population", "ui")
TERMINAL_STATES = {"NEEDS_SOL_REVIEW", "BLOCKED"}
RUNNABLE_STATES = {"READY", "RECOVERING"}

ANSI_RE = re.compile(r"\x1b(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")
SECRET_RE = re.compile(
    r"(?i)(?:gho_|ghp_|github_pat_|sk-[A-Za-z0-9_-]{12,}|"
    r"bearer\s+[A-Za-z0-9._~+/=-]+|"
    r"(?:token|oauth_token|access_token|password|secret)\s*[:=]\s*[^\s,;]+)"
)
PHASE_RE = re.compile(r"^([A-Z][0-9]+)\s+(.+?)\s*$")
RESULT_RE = re.compile(r"WORKER_RESULT:\s*(COMPLETE|NEEDS_SOL_REVIEW|BLOCKED)")


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def parse_time(value: str | None) -> float:
    if not value:
        return 0.0
    try:
        return dt.datetime.fromisoformat(value).timestamp()
    except (TypeError, ValueError):
        return 0.0


def redact(text: str) -> str:
    return SECRET_RE.sub("[redacted]", ANSI_RE.sub("", text))


def atomic_write_json(path: Path, data: Any, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp-{os.getpid()}-{threading.get_ident()}")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def read_json(path: Path, default: Any) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return default
    except (OSError, json.JSONDecodeError):
        return default


def tail_text(path: Path, limit: int = 12000) -> str:
    try:
        data = path.read_bytes()
    except OSError:
        return ""
    return redact(data[-limit:].decode("utf-8", errors="replace"))


class Supervisor:
    def __init__(self) -> None:
        self.config = read_json(CONFIG_PATH, {})
        if not self.config:
            raise RuntimeError(f"missing configuration: {CONFIG_PATH}")
        self.state = self._load_state()
        self.processes: dict[str, subprocess.Popen[str]] = {}
        self.output_threads: dict[str, threading.Thread] = {}
        self.stop_requested = False
        self.last_git_health_check = 0.0
        self.last_resource_guard = 0.0
        self.last_save = 0.0
        self._prepare_state()

    def _new_state(self) -> dict[str, Any]:
        return {
            "version": 1,
            "created_at": utc_now(),
            "updated_at": utc_now(),
            "global_mode": "PAUSED",
            "mode_reason": "installation default; explicit start is required",
            "lanes": {},
            "events": [],
        }

    def _load_state(self) -> dict[str, Any]:
        try:
            return json.loads(STATE_PATH.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return self._new_state()
        except (OSError, json.JSONDecodeError) as exc:
            backup = STATE_PATH.with_name(f"state.corrupt-{int(time.time())}.json")
            try:
                STATE_PATH.replace(backup)
            except OSError:
                pass
            state = self._new_state()
            state["mode_reason"] = f"state was unreadable: {type(exc).__name__}"
            return state

    def _prepare_state(self) -> None:
        self.state.setdefault("version", 1)
        self.state.setdefault("created_at", utc_now())
        self.state.setdefault("global_mode", "PAUSED")
        self.state.setdefault("mode_reason", "installation default")
        self.state.setdefault("lanes", {})
        self.state.setdefault("events", [])
        for lane_name in LANE_ORDER:
            spec = self.config["lanes"][lane_name]
            lane = self.state["lanes"].setdefault(lane_name, {
                "state": "PAUSED",
                "phase_index": 0,
                "phase_id": None,
                "phase_title": None,
                "worker_pid": None,
                "worker_log": None,
                "worker_started_at": None,
                "last_progress_at": None,
                "last_commit": None,
                "last_error": "",
                "ci": {"status": "NOT_RUN", "sha": None, "run_id": None, "url": None},
                "history": [],
                "success_since_review": 0,
                "phase_failure_count": 0,
                "recovery_attempts": 0,
                "push_attempts": 0,
                "ci_poll_failures": 0,
                "review_packet": None,
                "review_reasons": [],
            })
            lane.update({
                "branch": spec["branch"],
                "worktree": spec["worktree"],
                "issue": spec["issue"],
                "plan": spec["plan"],
            })
            lane.setdefault("history", [])
            lane.setdefault("review_reasons", [])
            self._refresh_plan(lane_name, lane)
            if lane.get("state") == "CODING":
                lane["worker_pid"] = None
                lane["state"] = "RECOVERING" if self.state.get("global_mode") == "RUNNING" else "PAUSED"
                lane["recovery_attempts"] = min(1, int(lane.get("recovery_attempts", 0)) + 1)
                lane["last_error"] = "supervisor restart interrupted worker; recovery required"
            if lane.get("last_commit") is None:
                lane["last_commit"] = self.git_head(spec["worktree"])
        self.save_state()

    def save_state(self) -> None:
        self.state["updated_at"] = utc_now()
        atomic_write_json(STATE_PATH, self.state, 0o640)
        self.last_save = time.monotonic()

    def log(self, message: str, lane: str | None = None) -> None:
        LOG_ROOT.mkdir(parents=True, exist_ok=True)
        prefix = f"{utc_now()} "
        if lane:
            prefix += f"[{lane}] "
        line = prefix + redact(message).rstrip() + "\n"
        with SUPERVISOR_LOG.open("a", encoding="utf-8") as handle:
            handle.write(line)
        try:
            max_bytes = int(self.config.get("max_log_bytes", 5242880))
            if SUPERVISOR_LOG.stat().st_size > max_bytes:
                SUPERVISOR_LOG.write_bytes(SUPERVISOR_LOG.read_bytes()[-1048576:])
        except OSError:
            pass

    def event(self, message: str, lane: str | None = None) -> None:
        self.log(message, lane)
        self.state.setdefault("events", []).append({
            "at": utc_now(),
            "lane": lane,
            "message": redact(message),
        })
        del self.state["events"][:-200]
        self.save_state()

    def _refresh_plan(self, lane_name: str, lane: dict[str, Any]) -> None:
        try:
            text = Path(lane["plan"]).read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            lane["plan_data"] = {"phases": [], "boundaries": []}
            lane["state"] = "NEEDS_SOL_REVIEW"
            lane["last_error"] = f"cannot read authoritative plan: {exc}"
            return
        phases: list[dict[str, str]] = []
        boundaries: list[str] = []
        in_queue = False
        in_boundaries = False
        for raw in text.splitlines():
            stripped = raw.strip()
            if stripped == "## Queue":
                in_queue, in_boundaries = True, False
                continue
            if stripped == "## Hard boundaries":
                in_queue, in_boundaries = False, True
                continue
            if stripped.startswith("## "):
                in_queue, in_boundaries = False, False
            if in_queue:
                match = PHASE_RE.match(stripped)
                if match:
                    phases.append({"id": match.group(1), "title": match.group(2)})
            elif in_boundaries and stripped:
                boundaries.append(stripped)
        lane["plan_data"] = {"phases": phases, "boundaries": boundaries}
        if not phases:
            lane["state"] = "NEEDS_SOL_REVIEW"
            lane["last_error"] = "authoritative plan contains no queue phases"
        index = int(lane.get("phase_index", 0))
        if index < len(phases):
            lane["phase_id"] = phases[index]["id"]
            lane["phase_title"] = phases[index]["title"]
        else:
            lane["phase_id"] = None
            lane["phase_title"] = None

    def plan_for(self, lane_name: str) -> dict[str, Any]:
        return self.state["lanes"][lane_name].get(
            "plan_data", {"phases": [], "boundaries": []}
        )

    def phase(self, lane_name: str) -> dict[str, str] | None:
        phases = self.plan_for(lane_name).get("phases", [])
        index = int(self.state["lanes"][lane_name].get("phase_index", 0))
        return phases[index] if index < len(phases) else None

    def env(self) -> dict[str, str]:
        environment = os.environ.copy()
        environment.update({
            "HOME": "/home/skyrimdev",
            "CODEX_HOME": "/home/skyrimdev/.codex",
            "GH_CONFIG_DIR": "/home/skyrimdev/.config/gh",
            "PATH": "/usr/local/bin:/usr/bin:/bin",
            "GIT_TERMINAL_PROMPT": "0",
            "CI": "1",
            "PYTHONUNBUFFERED": "1",
        })
        for key in (
            "OPENAI_API_KEY", "CODEX_API_KEY", "CODEX_ACCESS_TOKEN",
            "GH_TOKEN", "GITHUB_TOKEN", "SSH_AUTH_SOCK",
        ):
            environment.pop(key, None)
        return environment

    def command(
        self, args: list[str], cwd: str | None = None, timeout: int = 120
    ) -> tuple[int, str, str]:
        try:
            result = subprocess.run(
                args, cwd=cwd, env=self.env(), text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=timeout, check=False,
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired as exc:
            return 124, exc.stdout or "", (exc.stderr or "") + "\ncommand timed out"

    def git_head(self, worktree: str) -> str | None:
        code, out, _ = self.command(
            ["git", "-C", worktree, "rev-parse", "HEAD"], timeout=30
        )
        return out.strip() if code == 0 else None

    def git_status_files(self, worktree: str) -> list[str]:
        files: set[str] = set()
        code, out, _ = self.command(
            ["git", "-C", worktree, "diff", "--name-only",
             "--diff-filter=ACDMRTUXB"], timeout=60
        )
        if code == 0:
            files.update(line.strip() for line in out.splitlines() if line.strip())
        code, out, _ = self.command(
            ["git", "-C", worktree, "ls-files", "--others", "--exclude-standard"],
            timeout=60,
        )
        if code == 0:
            files.update(line.strip() for line in out.splitlines() if line.strip())
        return sorted(files)

    def git_dirty(self, worktree: str) -> bool:
        return bool(self.git_status_files(worktree))

    def resource_guard(self) -> tuple[bool, str]:
        usage = os.statvfs("/")
        free_bytes = usage.f_bavail * usage.f_frsize
        minimum = float(self.config.get("min_free_gb", 8.0)) * 1024**3
        if free_bytes < minimum:
            return False, "filesystem free space is below the configured 8 GiB reserve"
        values: dict[str, int] = {}
        try:
            for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
                key, value = line.split(":", 1)
                parts = value.strip().split()
                if parts:
                    values[key] = int(parts[0]) * 1024
        except (OSError, ValueError):
            return False, "cannot read /proc/meminfo"
        available = values.get("MemAvailable", 0)
        total = values.get("MemTotal", 0)
        minimum_available = float(self.config.get("min_memory_available_gb", 1.0)) * 1024**3
        fraction = float(self.config.get("min_memory_available_fraction", 0.10))
        if available < minimum_available or (total and available / total < fraction):
            return False, "memory pressure is unsafe"
        return True, ""

    def _write_control(self, mode: str, reason: str) -> None:
        atomic_write_json(CONTROL_PATH, {
            "desired_mode": mode, "reason": reason, "updated_at": utc_now()
        }, 0o640)

    def refresh_control(self) -> None:
        control = read_json(CONTROL_PATH, {})
        desired = control.get("desired_mode")
        if desired not in {"RUNNING", "PAUSED"}:
            desired = self.state.get("global_mode", "PAUSED")
        if desired == self.state.get("global_mode"):
            return
        self.state["global_mode"] = desired
        self.state["mode_reason"] = control.get("reason", "control change")
        for lane in self.state["lanes"].values():
            if desired == "PAUSED":
                if lane.get("worker_pid") is None and lane.get("state") not in TERMINAL_STATES:
                    lane["state"] = "PAUSED"
            elif lane.get("state") == "PAUSED":
                lane["state"] = "RECOVERING" if self.git_dirty(lane["worktree"]) else "READY"
        self.event(f"global mode changed to {desired}: {self.state['mode_reason']}")

    def pause_for_safety(self, reason: str) -> None:
        if self.state.get("global_mode") == "PAUSED" and self.state.get("mode_reason") == reason:
            return
        self.state["global_mode"] = "PAUSED"
        self.state["mode_reason"] = reason
        self._write_control("PAUSED", reason)
        for lane in self.state["lanes"].values():
            if lane.get("worker_pid") is None and lane.get("state") not in TERMINAL_STATES:
                lane["state"] = "PAUSED"
        self.event(f"automatic safety pause: {reason}")

    def git_health(self) -> tuple[bool, str]:
        repo = self.config["repo_root"]
        code, _, _ = self.command(
            ["git", "-C", repo, "rev-parse", "--verify", "HEAD"], timeout=30
        )
        if code != 0:
            return False, "primary repository HEAD cannot be verified"
        for lane_name in LANE_ORDER:
            worktree = self.config["lanes"][lane_name]["worktree"]
            code, _, _ = self.command(
                ["git", "-C", worktree, "rev-parse", "--verify", "HEAD"], timeout=30
            )
            if code != 0:
                return False, f"{lane_name} worktree cannot be verified"
        return True, ""

    def periodic_guards(self) -> bool:
        now = time.monotonic()
        if now - self.last_resource_guard >= 15:
            self.last_resource_guard = now
            okay, reason = self.resource_guard()
            if not okay:
                self.pause_for_safety(reason)
                return False
        if now - self.last_git_health_check >= 300:
            self.last_git_health_check = now
            okay, reason = self.git_health()
            if not okay:
                self.pause_for_safety(f"Git health check failed: {reason}")
                return False
        return self.state.get("global_mode") == "RUNNING"

    def worker_prompt(self, lane_name: str, recovery: bool = False) -> str:
        lane = self.state["lanes"][lane_name]
        phase = self.phase(lane_name)
        if phase is None:
            return ""
        boundaries = "\n".join(
            f"- {item}" for item in self.plan_for(lane_name).get("boundaries", [])
        )
        recovery_text = ""
        if recovery:
            recovery_text = (
                "\nThis is the single bounded recovery attempt. Inspect the current dirty "
                "diff and saved failure context. Complete only this same phase safely. "
                "Never discard or reset the existing diff.\n"
            )
        return f"""You are the one-phase Codex worker for lane {lane_name}.
Repository: {self.config["repo"]}
Lane branch: {lane["branch"]}
Worktree: {lane["worktree"]}
Authoritative plan: {lane["plan"]}
Phase: {phase["id"]} - {phase["title"]}
{recovery_text}
Perform exactly this one phase and then stop. Read PLAN.md and current source first.
Respect its ownership boundary and hard boundaries:
{boundaries}

Edit only ordinary source, test, and documentation files needed for this phase. Do not edit
PLAN.md, supervisor/service files, credentials, Git metadata, or unrelated lanes.
You may use read-only git status/diff/log commands, but MUST NOT run git add, commit, push,
reset, clean, merge, rebase, checkout, switch, or any command that changes Git metadata.
The trusted outer supervisor performs all Git operations after you exit.

Never make client-supplied AccountId, CharacterId, XP, reward, damage, kill attribution,
Creature/Humanoid classification, or persistent ownership authoritative. Do not add XP/reward
flow, server-side Skyrim AI, PartyService deletion, or unrelated build workarounds.
Prefer focused low-concurrency tests. Do not run a full multi-platform build.

End with a concise summary, focused tests actually run, unresolved safety questions, and exactly
one marker:
WORKER_RESULT: COMPLETE
Use WORKER_RESULT: NEEDS_SOL_REVIEW if safety cannot be proven. Use WORKER_RESULT: BLOCKED if
required evidence or tooling is unavailable.
"""

    def _bound_worker_log(self, path: Path) -> None:
        try:
            if path.stat().st_size > int(self.config.get("max_log_bytes", 5242880)):
                path.write_bytes(path.read_bytes()[-1048576:])
        except OSError:
            pass

    def _drain_worker(self, lane_name: str, stream: Any, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open("a", encoding="utf-8") as handle:
                for line in stream:
                    handle.write(redact(line))
                    handle.flush()
                    self.state["lanes"][lane_name]["last_progress_at"] = utc_now()
                    self._bound_worker_log(path)
        except OSError as exc:
            self.log(f"worker log error: {exc}", lane_name)

    def start_worker(self, lane_name: str, recovery: bool = False) -> bool:
        if lane_name in self.processes:
            return False
        lane = self.state["lanes"][lane_name]
        phase = self.phase(lane_name)
        if phase is None:
            self.review(lane_name, ["lane queue is complete; final review required"])
            return False
        if not recovery and self.git_dirty(lane["worktree"]):
            lane["state"] = "RECOVERING"
            lane["last_error"] = "unexpected dirty worktree before phase; recovery required"
            lane["recovery_attempts"] = min(1, int(lane.get("recovery_attempts", 0)) + 1)
            self.event("refusing normal worker on dirty worktree", lane_name)
            return False
        log_path = LOG_ROOT / f"{lane_name}-worker.log"
        args = [
            "codex", "exec",
            "--model", "gpt-5.6-luna",
            "--config", 'model_reasoning_effort="max"',
            "--config", 'approval_policy="never"',
            "--sandbox", "workspace-write",
            "--cd", lane["worktree"],
            "--ephemeral",
            "--color", "never",
            self.worker_prompt(lane_name, recovery),
        ]
        try:
            process = subprocess.Popen(
                args, cwd=lane["worktree"], env=self.env(), text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1,
            )
        except OSError as exc:
            lane["last_error"] = f"could not start Codex worker: {exc}"
            if int(lane.get("recovery_attempts", 0)) >= 1:
                self.review(lane_name, [lane["last_error"]])
            else:
                lane["state"] = "RECOVERING"
                lane["recovery_attempts"] = 1
            self.event(lane["last_error"], lane_name)
            return False
        self.processes[lane_name] = process
        lane.update({
            "state": "CODING",
            "phase_id": phase["id"],
            "phase_title": phase["title"],
            "worker_pid": process.pid,
            "worker_log": str(log_path),
            "worker_started_at": utc_now(),
            "last_progress_at": utc_now(),
            "worker_recovery": recovery,
            "last_error": "",
        })
        thread = threading.Thread(
            target=self._drain_worker,
            args=(lane_name, process.stdout, log_path), daemon=True
        )
        self.output_threads[lane_name] = thread
        thread.start()
        self.event(
            f"started {'recovery ' if recovery else ''}worker pid={process.pid} phase={phase['id']}",
            lane_name,
        )
        return True

    def terminate_worker(self, lane_name: str, reason: str) -> None:
        process = self.processes.get(lane_name)
        if process is None:
            return
        self.event(f"terminating worker pid={process.pid}: {reason}", lane_name)
        try:
            process.terminate()
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
        except OSError:
            pass

    def worker_result(self, lane_name: str) -> str | None:
        path = Path(self.state["lanes"][lane_name].get("worker_log") or "")
        matches = RESULT_RE.findall(tail_text(path, 30000))
        return matches[-1] if matches else None

    def worker_failure(self, lane_name: str, reason: str) -> None:
        lane = self.state["lanes"][lane_name]
        lane["last_error"] = redact(reason)
        if self.state.get("global_mode") != "RUNNING":
            lane["state"] = "PAUSED"
            self.event(f"worker stopped while paused; dirty diff preserved: {reason}", lane_name)
        elif int(lane.get("recovery_attempts", 0)) < 1:
            lane["recovery_attempts"] = 1
            lane["state"] = "RECOVERING"
            self.event(f"bounded recovery permitted: {reason}", lane_name)
        else:
            self.review(lane_name, [f"worker failure after bounded recovery: {reason}"])

    def poll_workers(self) -> None:
        now = time.time()
        for lane_name, process in list(self.processes.items()):
            lane = self.state["lanes"][lane_name]
            started = parse_time(lane.get("worker_started_at"))
            progress = parse_time(lane.get("last_progress_at")) or started
            if process.poll() is None:
                if started and now - started > int(self.config.get("worker_timeout_seconds", 5400)):
                    self.terminate_worker(lane_name, "worker timeout")
                    self.worker_failure(lane_name, "worker timeout")
                elif progress and now - progress > int(self.config.get("worker_stale_seconds", 1200)):
                    self.terminate_worker(lane_name, "worker stale/no progress")
                    self.worker_failure(lane_name, "worker stale/no progress")
                continue
            return_code = process.returncode
            thread = self.output_threads.pop(lane_name, None)
            if thread:
                thread.join(timeout=5)
            self.processes.pop(lane_name, None)
            result = self.worker_result(lane_name)
            lane["worker_pid"] = None
            lane["worker_finished_at"] = utc_now()
            lane["last_progress_at"] = utc_now()
            if return_code == 0 and result == "COMPLETE":
                lane["state"] = "LOCAL_VALIDATION"
                lane["last_error"] = ""
                self.event("worker completed; entering local validation", lane_name)
            elif result == "NEEDS_SOL_REVIEW":
                self.review(lane_name, ["worker explicitly requested human review"])
            elif result == "BLOCKED":
                self.review(lane_name, ["worker reported a blocking condition"])
            else:
                self.worker_failure(
                    lane_name,
                    f"worker exit={return_code}; marker={result or 'missing'}",
                )

    def changed_diff(self, lane_name: str) -> tuple[list[str], str, str]:
        worktree = self.state["lanes"][lane_name]["worktree"]
        files = self.git_status_files(worktree)
        _, diff, _ = self.command(
            ["git", "-C", worktree, "diff", "--no-ext-diff", "--unified=0"],
            timeout=120,
        )
        _, stat, _ = self.command(
            ["git", "-C", worktree, "diff", "--stat"], timeout=60
        )
        return files, diff[:2_000_000], stat.strip()[:12000]

    def forbidden_change_reasons(self, lane_name: str, files: list[str]) -> list[str]:
        spec = self.config["lanes"][lane_name]
        forbidden = tuple(spec.get("forbidden_prefixes", []))
        reasons: list[str] = []
        protected_names = (
            "auth.json", "hosts.yml", ".env", "id_rsa", "id_ed25519",
            "credentials", "token", "secret",
        )
        generated_parts = ("/node_modules/", "/build/", "/Build/", "/.xmake/",
                           "/obj/", "/out/", "/dist/")
        for path in files:
            normalized = path.replace("\\", "/")
            lower = normalized.lower()
            if normalized == ".codex/lane/PLAN.md" or normalized.startswith(".git/"):
                reasons.append(f"protected metadata changed: {normalized}")
            if forbidden and normalized.startswith(forbidden):
                reasons.append(f"cross-lane or forbidden path changed: {normalized}")
            if any(name in lower for name in protected_names):
                reasons.append(f"credential-like path changed: {normalized}")
            if any(part.lower() in lower for part in generated_parts):
                reasons.append(f"generated/build artifact path changed: {normalized}")
            absolute = Path(self.state["lanes"][lane_name]["worktree"]) / normalized
            try:
                if absolute.is_file() and absolute.stat().st_size > 50 * 1024 * 1024:
                    reasons.append(f"file exceeds 50 MiB safety limit: {normalized}")
            except OSError:
                pass
        return sorted(set(reasons))

    def local_validate(self, lane_name: str) -> bool:
        lane = self.state["lanes"][lane_name]
        worktree = lane["worktree"]
        code, branch, _ = self.command(
            ["git", "-C", worktree, "symbolic-ref", "--short", "HEAD"], timeout=30
        )
        if code != 0 or branch.strip() != lane["branch"]:
            self.review(lane_name, [f"worktree branch changed; expected {lane['branch']}"])
            return False
        files, diff, stat = self.changed_diff(lane_name)
        reasons = self.forbidden_change_reasons(lane_name, files)
        check_code, _, _ = self.command(
            ["git", "-C", worktree, "diff", "--check"], timeout=120
        )
        if check_code != 0:
            reasons.append("git diff --check failed")
        lane["validation"] = {
            "at": utc_now(),
            "status": "PASS" if not reasons else "FAIL",
            "tests": ["git diff --check"],
            "changed_files": files,
            "diffstat": stat,
            "diff_text": diff,
            "worker_output_tail": tail_text(Path(lane.get("worker_log") or ""), 8000),
        }
        if reasons:
            lane["last_error"] = "; ".join(reasons)
            if int(lane.get("recovery_attempts", 0)) < 1 and self.state.get("global_mode") == "RUNNING":
                lane["recovery_attempts"] = 1
                lane["state"] = "RECOVERING"
                self.event("local validation failed; one recovery allowed", lane_name)
            else:
                self.review(lane_name, reasons)
            return False
        lane["state"] = "COMMITTING"
        lane["last_error"] = ""
        self.event(f"local validation passed for {len(files)} changed files", lane_name)
        return True

    def commit_phase(self, lane_name: str) -> bool:
        lane = self.state["lanes"][lane_name]
        worktree = lane["worktree"]
        files = lane.get("validation", {}).get("changed_files", [])
        if not files:
            lane["last_commit"] = self.git_head(worktree)
            lane["history"].append({
                "phase": lane.get("phase_id"), "title": lane.get("phase_title"),
                "commit": None, "changed_files": [], "diffstat": "no source changes",
                "tests": lane.get("validation", {}).get("tests", []),
                "at": utc_now(), "ci": {"status": "NO_CHANGES"},
            })
            lane["state"] = "NEXT_PHASE"
            self.event("phase produced no changes; no commit required", lane_name)
            return True
        code, out, err = self.command(
            ["git", "-C", worktree, "add", "--"] + files,
            cwd=worktree, timeout=120,
        )
        if code != 0:
            self.review(lane_name, [f"outer git add failed: {redact(err or out)}"])
            return False
        code, out, err = self.command(
            ["git", "-C", worktree, "diff", "--cached", "--check"], timeout=120
        )
        if code != 0:
            self.review(lane_name, ["staged diff check failed"])
            return False
        stat_code, stat, _ = self.command(
            ["git", "-C", worktree, "diff", "--cached", "--stat"], timeout=60
        )
        phase = lane.get("phase_id") or "phase"
        title = lane.get("phase_title") or "phase complete"
        code, out, err = self.command([
            "git", "-C", worktree,
            "-c", "user.name=Skyrim Codex Supervisor",
            "-c", "user.email=skyrimdev@localhost",
            "commit", "-m", f"lane({lane_name}): complete {phase} {title}",
        ], cwd=worktree, timeout=180)
        if code != 0:
            self.review(lane_name, [f"outer git commit failed: {redact(err or out)}"])
            return False
        commit = self.git_head(worktree)
        lane["last_commit"] = commit
        lane["history"].append({
            "phase": phase, "title": title, "commit": commit,
            "changed_files": files,
            "diffstat": stat.strip()[:12000] if stat_code == 0 else "",
            "tests": lane.get("validation", {}).get("tests", []),
            "at": utc_now(), "ci": {"status": "PENDING"},
        })
        lane["push_attempts"] = 0
        lane["ci_poll_failures"] = 0
        lane["ci"] = {"status": "NOT_STARTED", "sha": commit, "run_id": None, "url": None}
        lane["state"] = "PUSHING"
        self.event(f"outer supervisor committed {commit}", lane_name)
        return True

    def push_phase(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        lane["push_attempts"] = int(lane.get("push_attempts", 0)) + 1
        code, out, err = self.command([
            "git", "-C", lane["worktree"],
            "-c", "credential.helper=!gh auth git-credential",
            "push", "--porcelain", "origin",
            f"{lane['branch']}:{lane['branch']}",
        ], cwd=lane["worktree"], timeout=180)
        if code == 0:
            lane["state"] = "WAITING_FOR_CI"
            lane["ci_started_at"] = utc_now()
            lane["last_error"] = ""
            self.event("branch pushed; waiting for GitHub Actions", lane_name)
        elif lane["push_attempts"] >= int(self.config.get("max_push_attempts", 3)):
            self.review(lane_name, [f"push failed after bounded retries: {redact(err or out)}"])
        else:
            lane["last_error"] = f"push attempt {lane['push_attempts']} failed"
            lane["next_retry_at"] = time.time() + 30
            self.event(lane["last_error"], lane_name)

    def ci_run_list(self, sha: str) -> tuple[int, list[dict[str, Any]], str]:
        code, out, err = self.command([
            "gh", "run", "list", "--repo", self.config["repo"],
            "--commit", sha, "--limit", "20",
            "--json", "databaseId,status,conclusion,headSha,workflowName,url,createdAt,updatedAt",
        ], timeout=120)
        if code != 0:
            return code, [], redact(err or out)
        try:
            return 0, json.loads(out or "[]"), ""
        except json.JSONDecodeError:
            return 1, [], "GitHub CLI returned invalid JSON"

    def fetch_ci_failure(self, run_id: int) -> str:
        code, out, err = self.command([
            "gh", "run", "view", str(run_id), "--repo", self.config["repo"],
            "--log-failed",
        ], timeout=180)
        return "\n".join(redact(out or err).splitlines()[-400:])

    def poll_ci(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        sha = lane.get("ci", {}).get("sha") or lane.get("last_commit")
        if not sha:
            lane["state"] = "NEXT_PHASE"
            return
        code, runs, error = self.ci_run_list(sha)
        if code != 0:
            lane["ci_poll_failures"] = int(lane.get("ci_poll_failures", 0)) + 1
            lane["last_error"] = error
            if lane["ci_poll_failures"] >= int(self.config.get("max_ci_poll_failures", 3)):
                self.review(lane_name, ["GitHub API/CI polling failed repeatedly", error])
            return
        matching = [run for run in runs if run.get("headSha") == sha]
        matching.sort(key=lambda item: item.get("createdAt", ""), reverse=True)
        if not matching:
            started = parse_time(lane.get("ci_started_at"))
            if started and time.time() - started > int(self.config.get("ci_queue_timeout_seconds", 600)):
                self.review(lane_name, ["no GitHub Actions run appeared within bounded wait"])
            return
        run = matching[0]
        lane["ci"] = {
            "status": run.get("status"), "conclusion": run.get("conclusion"),
            "sha": sha, "run_id": run.get("databaseId"),
            "url": run.get("url"), "workflow": run.get("workflowName"),
        }
        if run.get("status") != "completed":
            started = parse_time(lane.get("ci_started_at"))
            if started and time.time() - started > int(self.config.get("ci_timeout_seconds", 1800)):
                self.review(lane_name, ["GitHub Actions run timed out"])
            return
        conclusion = str(run.get("conclusion") or "").lower()
        if conclusion == "success":
            if lane.get("history"):
                lane["history"][-1]["ci"] = lane["ci"]
            self.event("GitHub Actions passed", lane_name)
            self.complete_phase(lane_name)
            return
        excerpt = self.fetch_ci_failure(int(run["databaseId"]))
        lane["ci"]["failure_excerpt"] = excerpt
        if lane.get("history"):
            lane["history"][-1]["ci"] = lane["ci"]
        lane["phase_failure_count"] = int(lane.get("phase_failure_count", 0)) + 1
        lane["last_error"] = f"CI conclusion={conclusion or 'unknown'}"
        if lane["phase_failure_count"] >= 2:
            self.review(lane_name, ["same phase failed CI twice", excerpt])
        else:
            lane["state"] = "RECOVERING"
            lane["recovery_attempts"] = 1
            self.event("CI failed; one fresh recovery worker allowed", lane_name)

    def review_reasons(self, lane_name: str) -> list[str]:
        lane = self.state["lanes"][lane_name]
        validation = lane.get("validation", {})
        files = validation.get("changed_files", [])
        diff = validation.get("diff_text", "")
        lowered = (diff + " " + " ".join(files)).lower()
        reasons: list[str] = []
        if int(lane.get("success_since_review", 0)) >= 3:
            reasons.append("three successful phases completed since the last Sol review")
        if any(re.search(r"(schema|migration|database|persistent)", path, re.I) for path in files):
            reasons.append("persistent database/schema or migration surface changed")
        if re.search(r"\b(accountid|characterid)\b", lowered):
            reasons.append("AccountId/CharacterId authority or identity surface was touched")
        if re.search(r"\b(xp|experience|reward|loot)\b", lowered):
            reasons.append("XP/reward/loot vocabulary appeared in the phase diff or worker report")
        if any(path.startswith(("Code/encoding/", "Code/protocol/")) for path in files):
            reasons.append("protocol or wire-compatibility surface changed")
        forbidden = tuple(self.config["lanes"][lane_name].get("forbidden_prefixes", []))
        if forbidden and any(path.startswith(forbidden) for path in files):
            reasons.append("cross-lane ownership boundary was crossed")
        if re.search(r"(trust boundary|relax(?:ed|ing) authority|client[- ]supplied.*authoritative)", lowered):
            reasons.append("possible trust-boundary relaxation requires review")
        return sorted(set(reasons))

    def complete_phase(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        reasons = self.review_reasons(lane_name)
        lane["success_since_review"] = int(lane.get("success_since_review", 0)) + 1
        if reasons:
            self.review(lane_name, reasons)
            return
        lane["phase_index"] = int(lane.get("phase_index", 0)) + 1
        lane.update({
            "phase_failure_count": 0, "recovery_attempts": 0,
            "push_attempts": 0, "ci_poll_failures": 0, "validation": {},
            "ci": {"status": "NOT_RUN", "sha": lane.get("last_commit"),
                   "run_id": None, "url": None},
        })
        self._refresh_plan(lane_name, lane)
        if lane.get("phase_id") is None:
            self.review(lane_name, ["authoritative lane queue completed; final review required"])
        elif self.state.get("global_mode") == "RUNNING":
            lane["state"] = "READY"
            self.event(f"phase complete; next phase {lane['phase_id']}", lane_name)
        else:
            lane["state"] = "PAUSED"
            self.event("phase complete; remaining work paused", lane_name)

    def review(self, lane_name: str, reasons: list[str]) -> None:
        lane = self.state["lanes"][lane_name]
        clean = sorted(set(redact(reason) for reason in reasons if reason))
        lane["review_reasons"] = clean or ["human review requested"]
        lane["state"] = "NEEDS_SOL_REVIEW"
        lane["worker_pid"] = None
        packet = self.make_review_packet(lane_name, lane["review_reasons"])
        lane["review_packet"] = str(packet)
        self.post_issue_update(lane_name, lane["review_reasons"])
        self.event("lane stopped for Sol review", lane_name)

    def make_review_packet(self, lane_name: str, reasons: list[str]) -> Path:
        lane = self.state["lanes"][lane_name]
        phase = lane.get("phase_id") or "FINAL"
        path = REVIEW_ROOT / f"{lane_name}-{phase}.md"
        if path.exists():
            path = REVIEW_ROOT / f"{lane_name}-{phase}-{int(time.time())}.md"
        REVIEW_ROOT.mkdir(parents=True, exist_ok=True)
        completed = "\n".join(
            f"- {item.get('phase')}: {item.get('commit') or 'no source changes'}"
            for item in lane.get("history", [])
        ) or "- none"
        changed = "\n".join(
            f"- {item}" for item in lane.get("validation", {}).get("changed_files", [])
        ) or "- none recorded"
        tests = "\n".join(
            f"- {item}" for item in lane.get("validation", {}).get("tests", [])
        ) or "- none recorded"
        ci = lane.get("ci", {})
        boundaries = "\n".join(
            f"- {item}" for item in self.plan_for(lane_name).get("boundaries", [])
        ) or "- see authoritative PLAN.md"
        next_phase = self.phase(lane_name)
        if lane.get("history") and lane["history"][-1].get("phase") == phase:
            last_ci = lane["history"][-1].get("ci", {})
            if last_ci.get("status") == "NO_CHANGES" or last_ci.get("conclusion") == "success":
                phases = self.plan_for(lane_name).get("phases", [])
                next_index = int(lane.get("phase_index", 0)) + 1
                next_phase = phases[next_index] if next_index < len(phases) else None
        worker_tail = lane.get("validation", {}).get(
            "worker_output_tail", tail_text(Path(lane.get("worker_log") or ""), 8000)
        )
        unresolved = "\n".join(f"- {item}" for item in reasons)
        body = f"""# Skyrim lane review packet: {lane_name} / {phase}

Generated: {utc_now()}
Branch: {lane['branch']}
Worktree: {lane['worktree']}
Issue: #{lane['issue']}
Supervisor mode: {self.state.get('global_mode')}
Current state: NEEDS_SOL_REVIEW

## Phases completed

{completed}

## Current meaningful diff

{changed}

Diff summary:

{lane.get('validation', {}).get('diffstat', 'not available')}

## Focused tests actually run

{tests}

Worker report tail (bounded):

---
{worker_tail}
---

## GitHub Actions result

- status: {ci.get('status', 'unknown')}
- conclusion: {ci.get('conclusion', 'unknown')}
- commit: {ci.get('sha', 'unknown')}
- run: {ci.get('url') or 'not available'}

## Security and trust decisions

Authoritative hard boundaries:

{boundaries}

The supervisor did not permit worker Git mutations. Client-supplied identity, damage,
progression, reward, kill attribution, population classification, and ownership remain
subject to the lane plan and evidence-backed review.

## Unresolved questions / why review was requested

{unresolved}

## Next planned phase

{next_phase['id'] + ' - ' + next_phase['title'] if next_phase else 'none; queue is complete'}

Autonomous work is stopped for this lane. Resume only after human review and an explicit
operator decision.
"""
        path.write_text(body, encoding="utf-8")
        os.chmod(path, 0o640)
        return path

    def post_issue_update(self, lane_name: str, reasons: list[str]) -> None:
        if not self.config.get("post_issue_updates", True):
            return
        lane = self.state["lanes"][lane_name]
        key = f"{lane_name}:{lane.get('phase_id')}:{'|'.join(reasons)}"
        if lane.get("last_issue_update_key") == key:
            return
        body = (
            f"Skyrim Codex supervisor stopped lane {lane_name} for human review at "
            f"phase {lane.get('phase_id') or 'FINAL'}.\n\n"
            + "\n".join(f"- {reason}" for reason in reasons)
            + f"\n\nBranch: {lane['branch']}"
            + f"\nLast commit: {lane.get('last_commit') or 'none'}"
            + "\nAutonomous work will not resume for this lane until explicitly reviewed."
        )
        code, out, err = self.command([
            "gh", "issue", "comment", str(lane["issue"]),
            "--repo", self.config["repo"], "--body", body,
        ], timeout=120)
        if code == 0:
            lane["last_issue_update_key"] = key
            self.event("updated mapped GitHub issue for review checkpoint", lane_name)
        else:
            lane["last_error"] = f"issue update failed: {redact(err or out)}"
            self.log(lane["last_error"], lane_name)

    def advance_lane(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        if lane_name in self.processes:
            return
        state = lane.get("state")
        if state == "LOCAL_VALIDATION":
            self.local_validate(lane_name)
        elif state == "COMMITTING":
            self.commit_phase(lane_name)
        elif state == "PUSHING":
            if time.time() >= float(lane.get("next_retry_at", 0)):
                self.push_phase(lane_name)
        elif state == "WAITING_FOR_CI":
            self.poll_ci(lane_name)
        elif state == "NEXT_PHASE":
            self.complete_phase(lane_name)
        elif state == "RECOVERING" and self.state.get("global_mode") == "RUNNING":
            self.start_worker(lane_name, recovery=True)

    def schedule(self) -> None:
        if self.state.get("global_mode") != "RUNNING":
            return
        limit = int(self.config.get("max_concurrent_workers", 2))
        for lane_name in LANE_ORDER:
            if len(self.processes) >= limit:
                break
            if self.state["lanes"][lane_name].get("state") in RUNNABLE_STATES:
                self.start_worker(
                    lane_name,
                    recovery=self.state["lanes"][lane_name].get("state") == "RECOVERING",
                )

    def run_once(self) -> None:
        self.refresh_control()
        self.poll_workers()
        self.periodic_guards()
        for lane_name in LANE_ORDER:
            self.advance_lane(lane_name)
        if self.state.get("global_mode") == "RUNNING" and self.periodic_guards():
            self.schedule()
        if time.monotonic() - self.last_save > 10:
            self.save_state()

    def shutdown(self) -> None:
        for lane_name in list(self.processes):
            self.terminate_worker(lane_name, "supervisor stopping")
        self.save_state()

    def run(self) -> int:
        self.log("supervisor started")
        try:
            while not self.stop_requested:
                self.run_once()
                time.sleep(int(self.config.get("poll_interval_seconds", 15)))
        except KeyboardInterrupt:
            pass
        finally:
            self.shutdown()
            self.log("supervisor stopped")
        return 0

    def status(self) -> int:
        print(f"GLOBAL: {self.state.get('global_mode')} ({self.state.get('mode_reason', '')})")
        code, out, err = self.command(["systemctl", "is-active", UNIT_NAME], timeout=20)
        print(f"SERVICE: {out.strip() or err.strip() or 'unknown'}")
        for lane_name in LANE_ORDER:
            lane = self.state["lanes"][lane_name]
            ci = lane.get("ci", {})
            phase = lane.get("phase_id") or "none"
            title = lane.get("phase_title") or "queue complete/review"
            print(lane_name.upper())
            print(f"branch: {lane.get('branch')}")
            print(f"current phase: {phase} - {title}")
            print(f"state: {lane.get('state')}")
            print(f"worker pid: {lane.get('worker_pid') or 'none'}")
            print(f"last commit: {lane.get('last_commit') or 'none'}")
            print(
                f"CI: {ci.get('status', 'unknown')} "
                f"{ci.get('conclusion') or ''} {ci.get('url') or ''}".rstrip()
            )
            print(f"last error: {lane.get('last_error') or 'none'}")
            if lane.get("review_packet"):
                print(f"review packet: {lane['review_packet']}")
            print()
        return 0

    def healthcheck(self) -> int:
        okay, reason = self.resource_guard()
        if not okay:
            print(f"FAIL resource guard: {reason}")
            return 1
        git_ok, git_reason = self.git_health()
        if not git_ok:
            print(f"FAIL git health: {git_reason}")
            return 1
        print("PASS resource guard")
        print("PASS git/worktree health")
        print(f"PASS state persistence: {STATE_PATH}")
        return 0

    def self_test(self) -> int:
        failures: list[str] = []

        def check(label: str, result: bool) -> None:
            print(f"{'PASS' if result else 'FAIL'} {label}")
            if not result:
                failures.append(label)

        code, out, err = self.command(["codex", "--version"], timeout=30)
        check("Codex CLI installed", code == 0 and bool(out.strip()))
        code, out, err = self.command(["codex", "login", "status"], timeout=30)
        check("Codex ChatGPT authentication", code == 0 and "logged in" in (out + err).lower())
        code, out, err = self.command(
            ["gh", "auth", "status", "--hostname", "github.com"], timeout=30
        )
        check("GitHub CLI authentication", code == 0)
        code, out, err = self.command(
            ["gh", "api", f"repos/{self.config['repo']}", "--jq", ".permissions.push"],
            timeout=60,
        )
        check("GitHub push permission", code == 0 and out.strip().lower() == "true")
        git_ok, _ = self.git_health()
        check("primary repository and four worktrees", git_ok)
        okay, _ = self.resource_guard()
        check("disk and memory guard", okay)
        for lane_name in LANE_ORDER:
            lane = self.state["lanes"][lane_name]
            check(f"{lane_name} worktree clean", not self.git_dirty(lane["worktree"]))
            code, out, err = self.command([
                "git", "-C", lane["worktree"],
                "-c", "credential.helper=!gh auth git-credential",
                "push", "--dry-run", "origin",
                f"{lane['branch']}:{lane['branch']}",
            ], timeout=180)
            check(f"{lane_name} safe push dry-run", code == 0)
        code, out, err = self.command([
            "gh", "run", "list", "--repo", self.config["repo"], "--limit", "1",
            "--json", "databaseId,status,conclusion",
        ], timeout=120)
        check("GitHub Actions polling", code == 0)
        test_path = STATE_DIR / f"self-test-{os.getpid()}.json"
        try:
            atomic_write_json(test_path, {"created": utc_now()}, 0o600)
            check("state persistence write", test_path.exists())
            test_path.unlink(missing_ok=True)
        except OSError:
            check("state persistence write", False)
        if failures:
            print("SELF_TEST_FAILED: " + "; ".join(failures))
            return 1
        print("SELF_TEST_OK")
        return 0


def write_control(mode: str, reason: str) -> int:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    atomic_write_json(CONTROL_PATH, {
        "desired_mode": mode, "reason": reason, "updated_at": utc_now()
    }, 0o640)
    try:
        account = pwd.getpwnam("skyrimdev")
        os.chown(CONTROL_PATH, account.pw_uid, account.pw_gid)
    except (KeyError, OSError):
        pass
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Skyrim deterministic Codex supervisor")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("run")
    sub.add_parser("status")
    sub.add_parser("healthcheck")
    sub.add_parser("self-test")
    control = sub.add_parser("control")
    control.add_argument("mode", choices=("start", "resume", "pause", "stop"))
    args = parser.parse_args()

    if args.command == "control":
        if args.mode in ("start", "resume"):
            write_control("RUNNING", f"operator requested {args.mode}")
            return subprocess.run(["systemctl", "start", UNIT_NAME], check=False).returncode
        if args.mode == "pause":
            return write_control("PAUSED", "operator requested pause")
        write_control("PAUSED", "operator requested stop")
        return subprocess.run(["systemctl", "stop", UNIT_NAME], check=False).returncode

    supervisor = Supervisor()
    if args.command == "status":
        return supervisor.status()
    if args.command == "healthcheck":
        return supervisor.healthcheck()
    if args.command == "self-test":
        return supervisor.self_test()

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    with LOCK_PATH.open("w", encoding="utf-8") as lock_handle:
        try:
            fcntl.flock(lock_handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("another supervisor instance is already running", file=sys.stderr)
            return 2

        def stop_handler(signum: int, frame: Any) -> None:
            supervisor.stop_requested = True

        signal.signal(signal.SIGTERM, stop_handler)
        signal.signal(signal.SIGINT, stop_handler)
        return supervisor.run()


if __name__ == "__main__":
    raise SystemExit(main())
