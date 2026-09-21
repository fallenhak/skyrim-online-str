#!/usr/bin/env python3
"""Deterministic supervisor for the Skyrim development lanes.

Codex workers edit ordinary source, tests, and documentation only. This trusted
outer process performs validation, Git operations, CI polling, recovery bounds,
resource guards, and review checkpoints.
"""
from __future__ import annotations

import argparse
import datetime as dt
import difflib
import hashlib
import json
import os
import re
import signal
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

try:
    import fcntl
except ImportError:  # pragma: no cover - Windows-only syntax/test support
    fcntl = None  # type: ignore[assignment]
try:
    import pwd
except ImportError:  # pragma: no cover - Windows-only syntax/test support
    pwd = None  # type: ignore[assignment]

from roadmap import (
    KNOWN_LANES,
    RoadmapSnapshot,
    RoadmapValidationError,
    ScheduledTask,
    current_lane_task,
    dependency_evaluation,
    load_roadmap,
    next_round_robin_lane,
    task_definition_digest,
)

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
CONTROL_PLANE_FILES = (
    "PRODUCT_VISION.md",
    "WORLD_RULES.md",
    "MILESTONE_01_CORE_WORLD.md",
    "roadmap.json",
)
CONTROL_REVIEW_STATES = {"NEEDS_SOL_REVIEW", "INVALID", "UNINITIALIZED"}
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
CODEX_USAGE_RE = re.compile(
    r"(?i)(?:usage\s+limit|rate\s+limit|too\s+many\s+requests|quota\s+exhausted|"
    r"capacity\s+exhausted|try\s+again\s+later)"
)
CODEX_CONTEXT_RE = re.compile(r"(?i)(?:codex|chatgpt|openai|usage\s+allowance|token\s+budget|reset)")
GITHUB_LIMIT_RE = re.compile(r"(?i)(?:github|github\.com|gh\s+(?:api|run|issue))")
UNMERGED_XY = {"DD", "AU", "UD", "UA", "DU", "AA", "UU"}
GENERATED_DIRS = {"node_modules", "build", "dist", "out", ".xmake", "obj"}
MAX_UNTRACKED_REVIEW_FILE_BYTES = 64 * 1024
MAX_RECOVERY_ERROR_BYTES = 4000
MAX_RECOVERY_CI_BYTES = 8000
MAX_RECOVERY_WORKER_BYTES = 8000


def parse_porcelain_v1_z(output: bytes | str) -> list[dict[str, str | None]]:
    """Parse git status --porcelain=v1 -z without losing rename/deletion data."""
    raw = output.encode("utf-8", errors="replace") if isinstance(output, str) else output
    tokens = raw.split(b"\0")
    entries: list[dict[str, str | None]] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        index += 1
        if not token:
            continue
        text = token.decode("utf-8", errors="surrogateescape")
        if len(text) < 4 or text[2] != " ":
            entries.append({"xy": "??", "path": text, "orig_path": None, "kind": "unknown"})
            continue
        xy = text[:2]
        path = text[3:]
        orig_path: str | None = None
        if xy[0] in "RC" or xy[1] in "RC":
            if index < len(tokens) and tokens[index]:
                orig_path = tokens[index].decode("utf-8", errors="surrogateescape")
                index += 1
        if xy == "??":
            kind = "untracked"
        elif xy in UNMERGED_XY or "U" in xy:
            kind = "unmerged"
        elif "R" in xy:
            kind = "rename"
        elif "D" in xy:
            kind = "deletion"
        elif "A" in xy:
            kind = "addition"
        else:
            kind = "modified"
        entries.append({"xy": xy, "path": path, "orig_path": orig_path, "kind": kind})
    return entries


def status_paths(entries: list[dict[str, str | None]]) -> list[str]:
    paths: set[str] = set()
    for entry in entries:
        for key in ("path", "orig_path"):
            value = entry.get(key)
            if value:
                paths.add(str(value))
    return sorted(paths)


def aggregate_required_workflows(
    runs: list[dict[str, Any]], required_workflows: list[str], sha: str
) -> dict[str, Any]:
    """Reduce exact-SHA Actions runs to one newest attempt per required workflow."""
    by_name: dict[str, list[dict[str, Any]]] = {name: [] for name in required_workflows}
    for run in runs:
        if run.get("headSha") != sha:
            continue
        name = str(run.get("workflowName") or "")
        if name in by_name:
            by_name[name].append(run)

    def newest(run: dict[str, Any]) -> tuple[str, str, int, int]:
        attempt = run.get("runAttempt", run.get("attempt", run.get("run_attempt", 0)))
        try:
            attempt_number = int(attempt or 0)
        except (TypeError, ValueError):
            attempt_number = 0
        try:
            database_id = int(run.get("databaseId") or 0)
        except (TypeError, ValueError):
            database_id = 0
        return (
            str(run.get("updatedAt") or ""),
            str(run.get("createdAt") or ""),
            attempt_number,
            database_id,
        )

    selected: list[dict[str, Any]] = []
    missing: list[str] = []
    for workflow in required_workflows:
        candidates = by_name[workflow]
        if not candidates:
            missing.append(workflow)
            continue
        run = dict(max(candidates, key=newest))
        selected.append({
            "workflow": workflow,
            "run_id": run.get("databaseId"),
            "status": run.get("status"),
            "conclusion": run.get("conclusion"),
            "sha": run.get("headSha"),
            "url": run.get("url"),
            "created_at": run.get("createdAt"),
            "updated_at": run.get("updatedAt"),
            "attempt": run.get("runAttempt", run.get("attempt", run.get("run_attempt"))),
        })
    failures = [
        item for item in selected
        if str(item.get("status") or "").lower() == "completed"
        and str(item.get("conclusion") or "").lower() != "success"
    ]
    running = [
        item for item in selected
        if str(item.get("status") or "").lower() != "completed"
    ]
    if failures:
        result_status = "FAIL"
    elif missing or running:
        result_status = "WAIT"
    else:
        result_status = "PASS"
    return {
        "status": result_status,
        "sha": sha,
        "required": selected,
        "missing": missing,
        "running": running,
        "failures": failures,
    }


def classify_codex_usage_limit(return_code: int, output: str) -> bool:
    """Conservative classifier for Codex/ChatGPT allowance exhaustion."""
    text = ANSI_RE.sub("", output or "")
    if not text or not CODEX_USAGE_RE.search(text):
        return False
    if GITHUB_LIMIT_RE.search(text) and not re.search(r"(?i)(codex|chatgpt|openai)", text):
        return False
    if not CODEX_CONTEXT_RE.search(text):
        return False
    return return_code != 0


def next_codex_backoff(previous: int, base: int = 900, maximum: int = 3600) -> int:
    if previous <= 0:
        return base
    return min(maximum, previous * 2)


def checkpoint_due(success_since_review: int) -> bool:
    return success_since_review >= 3


def generated_path(path: str) -> bool:
    return any(part.lower() in GENERATED_DIRS for part in path.replace("\\", "/").split("/"))


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


def atomic_write_text(path: Path, text: str, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp-{os.getpid()}-{threading.get_ident()}")
    tmp.write_text(text, encoding="utf-8")
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
        self.roadmap_snapshot: RoadmapSnapshot | None = None
        self.processes: dict[str, subprocess.Popen[str]] = {}
        self.output_threads: dict[str, threading.Thread] = {}
        self.stop_requested = False
        self.last_git_health_check = 0.0
        self.last_resource_guard = 0.0
        self.last_save = 0.0
        self._prepare_state()

    def _new_state(self) -> dict[str, Any]:
        return {
            "version": 3,
            "created_at": utc_now(),
            "updated_at": utc_now(),
            "global_mode": "PAUSED",
            "mode_reason": "installation default; explicit start is required",
            "safety_pause": False,
            "codex_availability": {
                "status": "AVAILABLE",
                "detected_at": None,
                "next_retry_at": None,
                "backoff_seconds": 0,
                "retry_count": 0,
                "probe_started_at": None,
                "reason": "",
            },
            "lanes": {},
            "control_plane": {
                "branch": "orchestration/control-plane",
                "applied_sha": None,
                "observed_sha": None,
                "last_checked_at": None,
                "status": "UNINITIALIZED",
                "validation_error": "control plane has not been synchronized",
                "candidate_sha": None,
                "candidate_snapshot_path": None,
                "active_snapshot_path": str(
                    STATE_DIR / "control-plane" / "active" / "roadmap.json"
                ),
                "active_file_hashes": {},
            },
            "scheduler": {
                "cursor": 0,
                "last_selected_lane": None,
                "tasks": {},
                "approvals": {},
                "reviews": {},
                "workstreams": {},
                "milestones": {},
                "resolved_external_gates": [],
                "audit": [],
            },
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
        try:
            self.state["version"] = max(int(self.state.get("version", 1)), 3)
        except (TypeError, ValueError):
            self.state["version"] = 3
        self.state.setdefault("created_at", utc_now())
        self.state.setdefault("global_mode", "PAUSED")
        self.state.setdefault("mode_reason", "installation default")
        self.state.setdefault("safety_pause", False)
        self.state.setdefault("codex_availability", {
            "status": "AVAILABLE",
            "detected_at": None,
            "next_retry_at": None,
            "backoff_seconds": 0,
            "retry_count": 0,
            "probe_started_at": None,
            "reason": "",
        })
        if not isinstance(self.state.get("codex_availability"), dict):
            self.state["codex_availability"] = {}
        availability = self.state["codex_availability"]
        availability.setdefault("status", "AVAILABLE")
        availability.setdefault("detected_at", None)
        availability.setdefault("next_retry_at", None)
        availability.setdefault("backoff_seconds", 0)
        availability.setdefault("retry_count", 0)
        availability.setdefault("probe_started_at", None)
        availability.setdefault("reason", "")
        self._reconcile_startup_runtime()
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
                "review": {
                    "type": None,
                    "reviewed_phase": None,
                    "commit_sha": None,
                    "next_phase": None,
                    "created_at": None,
                },
                "history": [],
                "success_since_review": 0,
                "phase_failure_count": 0,
                "recovery_attempts": 0,
                "push_attempts": 0,
                "ci_poll_failures": 0,
                "review_packet": None,
                "review_reasons": [],
                "worker_attempt": 0,
            })
            lane.update({
                "branch": spec["branch"],
                "worktree": spec["worktree"],
                "issue": spec["issue"],
                "plan": spec["plan"],
            })
            lane.setdefault("history", [])
            lane.setdefault("review_reasons", [])
            lane.setdefault("review", {
                "type": None,
                "reviewed_phase": None,
                "commit_sha": None,
                "next_phase": None,
                "created_at": None,
            })
            if not isinstance(lane.get("review"), dict):
                lane["review"] = {
                    "type": None,
                    "reviewed_phase": None,
                    "commit_sha": None,
                    "next_phase": None,
                    "created_at": None,
                }
            lane.setdefault("worker_attempt", 0)
            self._refresh_plan(lane_name, lane)
            if lane_name not in self.processes:
                # A persisted PID is only historical evidence after a supervisor
                # restart.  Never treat it as a live worker process.
                lane["worker_pid"] = None
            if lane.get("state") == "CODING":
                lane["state"] = "RECOVERING" if self.state.get("global_mode") == "RUNNING" else "PAUSED"
                lane["recovery_attempts"] = min(1, int(lane.get("recovery_attempts", 0)) + 1)
                lane["last_error"] = "supervisor restart interrupted worker; recovery required"
            if lane.get("last_commit") is None:
                lane["last_commit"] = self.git_head(spec["worktree"])
        self._prepare_control_state()
        self._prepare_scheduler_state()
        self._load_active_roadmap()
        self._recompute_scheduler()
        self.save_state()

    def _prepare_control_state(self) -> None:
        configured = self.config.get("control_plane", {})
        if not isinstance(configured, dict):
            configured = {}
        control = self.state.setdefault("control_plane", {})
        if not isinstance(control, dict):
            control = {}
            self.state["control_plane"] = control
        defaults = {
            "branch": configured.get("branch", "orchestration/control-plane"),
            "applied_sha": None,
            "observed_sha": None,
            "last_checked_at": None,
            "status": "UNINITIALIZED",
            "validation_error": "control plane has not been synchronized",
            "candidate_sha": None,
            "candidate_snapshot_path": None,
            "active_snapshot_path": configured.get(
                "active_snapshot_path",
                str(STATE_DIR / "control-plane" / "active" / "roadmap.json"),
            ),
            "active_file_hashes": {},
        }
        for key, value in defaults.items():
            control.setdefault(key, value)
        control["branch"] = str(configured.get("branch", control["branch"]))

    def _prepare_scheduler_state(self) -> None:
        scheduler = self.state.setdefault("scheduler", {})
        if not isinstance(scheduler, dict):
            scheduler = {}
            self.state["scheduler"] = scheduler
        defaults = {
            "cursor": 0,
            "last_selected_lane": None,
            "tasks": {},
            "approvals": {},
            "reviews": {},
            "workstreams": {},
            "milestones": {},
            "resolved_external_gates": [],
            "audit": [],
        }
        for key, value in defaults.items():
            scheduler.setdefault(key, value)
        for key in ("tasks", "approvals", "reviews", "workstreams", "milestones"):
            if not isinstance(scheduler.get(key), dict):
                scheduler[key] = {}
        if not isinstance(scheduler.get("resolved_external_gates"), list):
            scheduler["resolved_external_gates"] = []
        if not isinstance(scheduler.get("audit"), list):
            scheduler["audit"] = []

    def _load_active_roadmap(self) -> None:
        control = self.state.get("control_plane", {})
        path_value = control.get("active_snapshot_path")
        if not path_value:
            self.roadmap_snapshot = None
            return
        path = Path(str(path_value))
        if not path.is_file():
            self.roadmap_snapshot = None
            if control.get("applied_sha"):
                control["status"] = "INVALID"
                control["validation_error"] = "active roadmap snapshot is missing"
            return
        try:
            self.roadmap_snapshot = load_roadmap(
                path,
                known_lanes=LANE_ORDER,
                source_sha=control.get("applied_sha"),
                file_hashes=control.get("active_file_hashes", {}),
            )
        except RoadmapValidationError as exc:
            self.roadmap_snapshot = None
            control["status"] = "INVALID"
            control["validation_error"] = redact(str(exc))

    def _control_plane_valid(self) -> bool:
        control = self.state.get("control_plane", {})
        return bool(
            self.roadmap_snapshot is not None
            and control.get("status") == "VALID"
            and control.get("applied_sha")
        )

    def _active_product_root(self) -> Path:
        control = self.state.get("control_plane", {})
        active_roadmap = control.get("active_snapshot_path")
        if active_roadmap:
            candidate = Path(str(active_roadmap)).parent
            if all((candidate / name).is_file() for name in CONTROL_PLANE_FILES if name != "roadmap.json"):
                return candidate
        return Path(self.config.get("product_root", str(SERVICE_ROOT / "product")))

    def _audit(self, event: str, **details: Any) -> None:
        scheduler = self.state.setdefault("scheduler", {})
        audit = scheduler.setdefault("audit", [])
        if not isinstance(audit, list):
            audit = []
            scheduler["audit"] = audit
        entry: dict[str, Any] = {
            "at": utc_now(),
            "event": event,
            "control_plane_sha": self.state.get("control_plane", {}).get("applied_sha"),
        }
        for key, value in details.items():
            if isinstance(value, str):
                entry[key] = redact(value)[:4000]
            elif isinstance(value, (int, float, bool)) or value is None:
                entry[key] = value
            else:
                try:
                    entry[key] = json.loads(
                        json.dumps(value, ensure_ascii=True, sort_keys=True)
                    )
                except (TypeError, ValueError):
                    entry[key] = redact(str(value))[:4000]
        audit.append(entry)
        try:
            limit = max(100, int(self.config.get("max_audit_events", 500)))
        except (TypeError, ValueError):
            limit = 500
        del audit[:-limit]

    def _control_config(self) -> dict[str, Any]:
        configured = self.config.get("control_plane", {})
        return configured if isinstance(configured, dict) else {}

    def _control_worktree_clean(self, worktree: str) -> tuple[bool, str]:
        code, out, err = self.command([
            "git", "-C", worktree, "status", "--porcelain=v1",
            "--untracked-files=all",
        ], timeout=60)
        if code != 0:
            return False, redact(err or out or "control worktree status failed")
        if out.strip():
            return False, "control-plane worktree is not clean"
        return True, ""

    def _control_git_head(self, worktree: str) -> str | None:
        return self.git_head(worktree)

    def _control_branch(self, worktree: str) -> str | None:
        return self.git_branch(worktree)

    def _control_file_hashes(self, worktree: str) -> dict[str, str]:
        root = Path(worktree).resolve()
        orchestrator_dir = (root / ".orchestrator").resolve()
        hashes: dict[str, str] = {}
        for name in CONTROL_PLANE_FILES:
            path = (orchestrator_dir / name).resolve()
            try:
                path.relative_to(orchestrator_dir)
            except ValueError as exc:
                raise RoadmapValidationError(
                    f".orchestrator/{name}: path escapes control worktree"
                ) from exc
            if not path.is_file():
                raise RoadmapValidationError(
                    f".orchestrator/{name}: required control-plane file is missing"
                )
            hashes[name] = sha256_file(path)
        return hashes

    def _cache_control_snapshot(
        self, worktree: str, target_dir: Path
    ) -> Path:
        root = Path(worktree).resolve() / ".orchestrator"
        target_dir.mkdir(parents=True, exist_ok=True)
        for name in CONTROL_PLANE_FILES:
            source = (root / name).resolve()
            text = source.read_text(encoding="utf-8")
            atomic_write_text(target_dir / name, text, 0o640)
        return target_dir / "roadmap.json"

    def _control_change_requires_review(
        self,
        old: RoadmapSnapshot | None,
        new: RoadmapSnapshot,
        old_hashes: dict[str, str],
        new_hashes: dict[str, str],
    ) -> list[str]:
        if old is None:
            return []
        reasons: list[str] = []
        scheduler = self.state.get("scheduler", {})
        persisted_tasks = scheduler.get("tasks", {}) if isinstance(scheduler, dict) else {}
        active_ids = {
            task_id for task_id, record in persisted_tasks.items()
            if isinstance(record, dict)
            and record.get("state") in {"READY", "RUNNING", "NEEDS_SOL_REVIEW"}
        }
        for lane in self.state.get("lanes", {}).values():
            if isinstance(lane, dict) and lane.get("phase_id"):
                active_ids.add(str(lane["phase_id"]))
        running_ids = {
            task_id for task_id, record in persisted_tasks.items()
            if isinstance(record, dict) and record.get("state") == "RUNNING"
        }
        completed_ids = {
            task_id for task_id, record in persisted_tasks.items()
            if isinstance(record, dict) and record.get("state") == "DONE"
        }

        def changed(task_id: str) -> bool:
            old_task = old.tasks.get(task_id)
            new_task = new.tasks.get(task_id)
            if old_task is None or new_task is None:
                return old_task is not new_task
            return old_task.definition_hash != new_task.definition_hash

        for task_id in sorted(active_ids):
            if changed(task_id):
                reasons.append(f"active task definition changed: {task_id}")
        for task_id in sorted(completed_ids):
            if changed(task_id):
                reasons.append(f"completed task semantics changed: {task_id}")

        dependency_ids: set[str] = set()
        frontier = list(running_ids)
        while frontier:
            task_id = frontier.pop()
            task = old.tasks.get(task_id) or new.tasks.get(task_id)
            if task is None:
                continue
            for dependency in (*task.dependencies, *task.blocked_by):
                if dependency not in dependency_ids:
                    dependency_ids.add(dependency)
                    frontier.append(dependency)
        for task_id in sorted(dependency_ids):
            if changed(task_id):
                reasons.append(
                    f"dependency of running work changed: {task_id}"
                )

        old_milestone = old.milestones.get(old.current_milestone, {})
        new_milestone = new.milestones.get(new.current_milestone, {})
        if tuple(old_milestone.get("acceptance", ())) != tuple(
            new_milestone.get("acceptance", ())
        ):
            reasons.append("active milestone acceptance semantics changed")
        if old_hashes.get("WORLD_RULES.md") != new_hashes.get("WORLD_RULES.md"):
            reasons.append("WORLD_RULES.md changed; future workers require Sol review")
        return sorted(set(reasons))

    def _apply_control_snapshot(
        self,
        snapshot: RoadmapSnapshot,
        worktree: str,
        file_hashes: dict[str, str],
        sha: str,
    ) -> None:
        configured = self._control_config()
        cache_root = Path(str(
            configured.get("cache_root", STATE_DIR / "control-plane")
        ))
        active_dir = cache_root / "active"
        active_path = self._cache_control_snapshot(worktree, active_dir)
        control = self.state["control_plane"]
        control.update({
            "branch": configured.get("branch", control.get("branch")),
            "applied_sha": sha,
            "observed_sha": sha,
            "last_checked_at": utc_now(),
            "status": "VALID",
            "validation_error": "",
            "candidate_sha": None,
            "candidate_snapshot_path": None,
            "active_snapshot_path": str(active_path),
            "active_file_hashes": file_hashes,
        })
        snapshot = RoadmapSnapshot(
            raw=snapshot.raw,
            version=snapshot.version,
            current_milestone=snapshot.current_milestone,
            scheduler_policy=snapshot.scheduler_policy,
            milestones=snapshot.milestones,
            workstreams=snapshot.workstreams,
            tasks=snapshot.tasks,
            sha=sha,
            file_hashes=file_hashes,
        )
        self.roadmap_snapshot = snapshot

    def sync_control_plane(self, force: bool = False) -> bool:
        """Fetch, fast-forward, validate, and possibly apply control state."""
        control = self.state.setdefault("control_plane", {})
        configured = self._control_config()
        interval = max(1, int(configured.get("sync_interval_seconds", 300)))
        last_checked = parse_time(control.get("last_checked_at"))
        if not force and last_checked and time.time() - last_checked < interval:
            return self._control_plane_valid()

        branch = str(configured.get("branch", "orchestration/control-plane"))
        remote = str(configured.get("remote", "origin"))
        repo = str(configured.get("repo_root", self.config.get("repo_root", "")))
        worktree = str(configured.get("worktree", ""))
        control["last_checked_at"] = utc_now()
        if control.get("branch") != branch or not branch or not worktree or not repo:
            control["status"] = "INVALID"
            control["validation_error"] = "control-plane branch/path configuration is invalid"
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False

        clean, clean_reason = self._control_worktree_clean(worktree)
        if not clean:
            control["status"] = "INVALID"
            control["validation_error"] = clean_reason
            self._audit("control_plane_invalid", reason=clean_reason)
            self.save_state()
            return False

        refspec = f"refs/heads/{branch}:refs/remotes/{remote}/{branch}"
        code, out, err = self.command([
            "git", "-C", repo, "-c", "credential.helper=!gh auth git-credential",
            "fetch", "--no-tags", remote, refspec,
        ], timeout=180)
        if code != 0:
            control["status"] = "INVALID"
            control["validation_error"] = redact(err or out or "control-plane fetch failed")
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False

        remote_ref = f"refs/remotes/{remote}/{branch}"
        code, out, err = self.command(
            ["git", "-C", repo, "rev-parse", remote_ref], timeout=30
        )
        if code != 0 or not out.strip():
            control["status"] = "INVALID"
            control["validation_error"] = redact(err or out or "observed control SHA is unavailable")
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False
        observed_sha = out.strip()
        control["observed_sha"] = observed_sha

        current_sha = self._control_git_head(worktree)
        if current_sha != observed_sha:
            code, out, err = self.command(
                ["git", "-C", worktree, "merge", "--ff-only", remote_ref],
                timeout=120,
            )
            if code != 0:
                control["status"] = "INVALID"
                control["validation_error"] = redact(
                    err or out or "control worktree could not fast-forward"
                )
                self._audit("control_plane_invalid", reason=control["validation_error"])
                self.save_state()
                return False
        if self._control_branch(worktree) != branch:
            control["status"] = "INVALID"
            control["validation_error"] = (
                f"control worktree branch is {self._control_branch(worktree) or 'unknown'}; "
                f"expected {branch}"
            )
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False
        clean, clean_reason = self._control_worktree_clean(worktree)
        if not clean:
            control["status"] = "INVALID"
            control["validation_error"] = clean_reason
            self._audit("control_plane_invalid", reason=clean_reason)
            self.save_state()
            return False

        candidate_sha = self._control_git_head(worktree)
        if candidate_sha != observed_sha:
            control["status"] = "INVALID"
            control["validation_error"] = "control worktree SHA did not match fetched branch"
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False
        try:
            hashes = self._control_file_hashes(worktree)
            candidate = load_roadmap(
                Path(worktree) / ".orchestrator" / "roadmap.json",
                known_lanes=LANE_ORDER,
                source_sha=candidate_sha,
                file_hashes=hashes,
            )
        except (OSError, RoadmapValidationError) as exc:
            control["status"] = "INVALID"
            control["validation_error"] = redact(str(exc))
            control["candidate_sha"] = candidate_sha
            self._audit("control_plane_invalid", reason=control["validation_error"])
            self.save_state()
            return False

        cache_root = Path(str(
            configured.get("cache_root", STATE_DIR / "control-plane")
        ))
        candidate_path = self._cache_control_snapshot(
            worktree, cache_root / "candidates" / candidate_sha
        )
        old = self.roadmap_snapshot
        old_hashes = dict(control.get("active_file_hashes", {}))
        if old is not None and control.get("applied_sha") == candidate_sha:
            control.update({
                "status": "VALID",
                "validation_error": "",
                "candidate_sha": None,
                "candidate_snapshot_path": None,
            })
            self._recompute_scheduler()
            self.save_state()
            return True

        review_reasons = self._control_change_requires_review(
            old, candidate, old_hashes, hashes
        )
        if review_reasons:
            control.update({
                "status": "NEEDS_SOL_REVIEW",
                "validation_error": "; ".join(review_reasons),
                "candidate_sha": candidate_sha,
                "candidate_snapshot_path": str(candidate_path),
                "candidate_file_hashes": hashes,
            })
            self._audit(
                "control_plane_review_requested",
                observed_sha=candidate_sha,
                reasons=review_reasons,
            )
            self.save_state()
            return False

        self._apply_control_snapshot(candidate, worktree, hashes, candidate_sha)
        self._audit("control_plane_applied", applied_sha=candidate_sha)
        self._recompute_scheduler()
        self.save_state()
        return True

    def approve_control_plane(self, sha: str) -> int:
        control = self.state.get("control_plane", {})
        if control.get("status") != "NEEDS_SOL_REVIEW":
            print("control plane is not awaiting Sol review", file=sys.stderr)
            return 1
        if str(control.get("candidate_sha")) != sha:
            print("control-plane SHA does not match the pending candidate", file=sys.stderr)
            return 1
        configured = self._control_config()
        worktree = str(configured.get("worktree", ""))
        if self._control_git_head(worktree) != sha:
            print("control worktree no longer matches the pending candidate", file=sys.stderr)
            return 1
        clean, reason = self._control_worktree_clean(worktree)
        if not clean:
            print(reason, file=sys.stderr)
            return 1
        try:
            hashes = self._control_file_hashes(worktree)
            candidate = load_roadmap(
                Path(worktree) / ".orchestrator" / "roadmap.json",
                known_lanes=LANE_ORDER,
                source_sha=sha,
                file_hashes=hashes,
            )
        except (OSError, RoadmapValidationError) as exc:
            print(f"refusing control-plane approval: {redact(str(exc))}", file=sys.stderr)
            return 1
        self._apply_control_snapshot(candidate, worktree, hashes, sha)
        self._audit("control_plane_approval_granted", applied_sha=sha)
        self._recompute_scheduler()
        self.save_state()
        print(f"control plane applied at {sha}")
        return 0

    def _reconcile_startup_runtime(self) -> None:
        """Reconcile persisted runtime markers with this supervisor instance.

        PIDs and probe timestamps describe the previous process, not this
        process.  A rate-limit probe may therefore be stale after a restart;
        clear only that marker and retain a bounded retry window without
        changing lane recovery budgets.
        """
        active_lanes = {
            lane_name for lane_name, process in self.processes.items()
            if process.poll() is None
        }
        for lane_name, lane in self.state.get("lanes", {}).items():
            if lane_name not in active_lanes:
                lane["worker_pid"] = None

        availability = self.state.get("codex_availability", {})
        if (
            availability.get("status") != "RATE_LIMITED"
            or not availability.get("probe_started_at")
            or active_lanes
        ):
            return

        try:
            previous_backoff = int(availability.get("backoff_seconds") or 0)
        except (TypeError, ValueError):
            previous_backoff = 0
        try:
            base = max(1, int(self.config.get("codex_retry_base_seconds", 900)))
            maximum = max(base, int(self.config.get("codex_retry_max_seconds", 3600)))
        except (TypeError, ValueError):
            base, maximum = 900, 3600
        bounded_backoff = min(maximum, max(base, previous_backoff))
        retry_at = parse_time(availability.get("next_retry_at"))
        if retry_at <= time.time():
            retry_at = time.time() + bounded_backoff
            availability["next_retry_at"] = dt.datetime.fromtimestamp(
                retry_at, dt.timezone.utc
            ).isoformat(timespec="seconds")
        availability["backoff_seconds"] = bounded_backoff
        availability["probe_started_at"] = None
        old_reason = str(availability.get("reason") or "").strip()
        suffix = "stale probe marker reconciled after supervisor restart"
        availability["reason"] = redact(f"{old_reason}; {suffix}" if old_reason else suffix)
        self.state.setdefault("events", []).append({
            "at": utc_now(),
            "lane": None,
            "message": availability["reason"],
        })
        del self.state["events"][:-200]

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

    def _existing_scheduled_tasks(self) -> dict[str, ScheduledTask]:
        if self.roadmap_snapshot is None:
            return {}
        result: dict[str, ScheduledTask] = {}
        control_sha = self.state.get("control_plane", {}).get("applied_sha")
        for lane_name in LANE_ORDER:
            plan = self.plan_for(lane_name)
            phases = plan.get("phases", [])
            workstream = next(
                (
                    item for item in self.roadmap_snapshot.workstreams.values()
                    if item.get("mode") == "EXISTING_PLAN"
                    and item.get("lane") == lane_name
                ),
                None,
            )
            if workstream is None:
                continue
            for index, phase in enumerate(phases):
                dependencies = (
                    (str(phases[index - 1]["id"]),) if index > 0 else ()
                )
                definition = {
                    "milestone_id": workstream["milestone_id"],
                    "workstream_id": workstream["id"],
                    "task_id": phase["id"],
                    "lane": lane_name,
                    "title": phase["title"],
                    "source": "EXISTING_PLAN",
                    "dependencies": list(dependencies),
                    "blocked_by": [],
                    "external_gates": list(workstream.get("external_gates", ())),
                    "risk": "lane-local change; preserve server authority and ownership boundaries",
                    "requires_sol_review": False,
                    "acceptance_criteria": [
                        phase["title"],
                        *plan.get("boundaries", [])[:3],
                    ],
                    "start_phase": workstream.get("start_phase"),
                }
                result[str(phase["id"])] = ScheduledTask(
                    milestone_id=str(workstream["milestone_id"]),
                    workstream_id=str(workstream["id"]),
                    task_id=str(phase["id"]),
                    lane=lane_name,
                    title=str(phase["title"]),
                    source="EXISTING_PLAN",
                    dependencies=dependencies,
                    blocked_by=(),
                    external_gates=tuple(workstream.get("external_gates", ())),
                    risk=str(definition["risk"]),
                    requires_sol_review=False,
                    acceptance_criteria=tuple(definition["acceptance_criteria"]),
                    start_phase=workstream.get("start_phase"),
                    control_plane_sha=control_sha,
                    definition_hash=task_definition_digest(definition),
                )
        return result

    def _all_scheduled_tasks(self) -> dict[str, ScheduledTask]:
        tasks = self._existing_scheduled_tasks()
        if self.roadmap_snapshot is not None:
            for task_id, task in self.roadmap_snapshot.tasks.items():
                if task_id in tasks:
                    # A duplicate between an existing PLAN phase and a roadmap
                    # task is unsafe even if the parser's roadmap IDs are unique.
                    self.state["control_plane"]["status"] = "INVALID"
                    self.state["control_plane"]["validation_error"] = (
                        f"scheduled task ID collides with existing PLAN phase: {task_id}"
                    )
                    continue
                tasks[task_id] = task
        return tasks

    def _workstream_states(
        self,
        tasks: dict[str, ScheduledTask],
        task_states: dict[str, str],
    ) -> dict[str, str]:
        result: dict[str, str] = {}
        if self.roadmap_snapshot is None:
            return result
        for workstream_id, workstream in self.roadmap_snapshot.workstreams.items():
            members = [
                task_id for task_id, task in tasks.items()
                if task.workstream_id == workstream_id
            ]
            if not members:
                result[workstream_id] = str(workstream.get("status") or "PLANNED")
                continue
            states = [task_states.get(task_id, "PAUSED") for task_id in members]
            if all(state == "DONE" for state in states):
                result[workstream_id] = "DONE"
            elif "RUNNING" in states:
                result[workstream_id] = "RUNNING"
            elif "NEEDS_SOL_REVIEW" in states:
                result[workstream_id] = "NEEDS_SOL_REVIEW"
            elif "BLOCKED_EXTERNAL_GATE" in states:
                result[workstream_id] = "BLOCKED_EXTERNAL_GATE"
            elif "BLOCKED_DEPENDENCY" in states:
                result[workstream_id] = "BLOCKED_DEPENDENCY"
            elif "READY" in states:
                result[workstream_id] = "ACTIVE"
            else:
                result[workstream_id] = "PAUSED"
        return result

    def _request_task_review(self, task: ScheduledTask, reason: str) -> None:
        scheduler = self.state["scheduler"]
        reviews = scheduler.setdefault("reviews", {})
        existing = reviews.get(task.task_id)
        if (
            isinstance(existing, dict)
            and existing.get("type") == "PRE_TASK_ROADMAP_REVIEW"
            and existing.get("control_plane_sha") == task.control_plane_sha
            and existing.get("definition_hash") == task.definition_hash
            and existing.get("decision") is None
        ):
            return
        packet = self.make_task_review_packet(task, reason)
        reviews[task.task_id] = {
            "type": "PRE_TASK_ROADMAP_REVIEW",
            "task_id": task.task_id,
            "title": task.title,
            "dependencies": list(task.dependencies),
            "risk": task.risk,
            "acceptance_criteria": list(task.acceptance_criteria),
            "control_plane_sha": task.control_plane_sha,
            "definition_hash": task.definition_hash,
            "reason": redact(reason),
            "packet": str(packet),
            "decision": None,
            "created_at": utc_now(),
        }
        self._audit(
            "task_review_requested",
            task_id=task.task_id,
            review_type="PRE_TASK_ROADMAP_REVIEW",
            reason=reason,
        )

    def _recompute_scheduler(self) -> None:
        """Persist every dependency decision and derive milestone states."""
        if not hasattr(self, "state"):
            return
        self._prepare_scheduler_state()
        scheduler = self.state["scheduler"]
        tasks = self._all_scheduled_tasks()
        old_records = scheduler.get("tasks", {})
        if not isinstance(old_records, dict):
            old_records = {}
        base_states: dict[str, str] = {}
        for task_id, task in tasks.items():
            old_record = old_records.get(task_id, {})
            old_definition = old_record.get("definition_hash") if isinstance(old_record, dict) else None
            if task.source == "EXISTING_PLAN":
                lane = self.state["lanes"].get(task.lane, {})
                phase_index = int(lane.get("phase_index", 0))
                phases = self.plan_for(task.lane).get("phases", [])
                phase_position = next(
                    (index for index, phase in enumerate(phases) if phase.get("id") == task.task_id),
                    None,
                )
                if phase_position is not None and phase_position < phase_index:
                    base_states[task_id] = "DONE"
                    continue
                if (
                    phase_position is not None
                    and phase_position == phase_index
                    and lane.get("state") in {
                        "CODING", "LOCAL_VALIDATION", "COMMITTING", "PUSHING",
                        "WAITING_FOR_CI", "NEXT_PHASE",
                    }
                ):
                    base_states[task_id] = "RUNNING"
                    continue
            if (
                isinstance(old_record, dict)
                and old_record.get("state") == "DONE"
                and old_definition == task.definition_hash
            ):
                base_states[task_id] = "DONE"
            else:
                base_states[task_id] = "PAUSED"

        approvals = scheduler.setdefault("approvals", {})
        for task_id, approval in list(approvals.items()):
            task = tasks.get(task_id)
            if task is not None and (
                approval.get("control_plane_sha") != task.control_plane_sha
                or approval.get("definition_hash") != task.definition_hash
            ):
                approval["invalidated_at"] = utc_now()
                approval["invalidated_reason"] = "task definition or control-plane SHA changed"
                self._audit("task_approval_invalidated", task_id=task_id)
                del approvals[task_id]

        task_states = dict(base_states)
        records: dict[str, dict[str, Any]] = {}
        for _ in range(max(1, len(tasks) + 1)):
            changed = False
            workstream_states = self._workstream_states(tasks, task_states)
            for task_id in sorted(tasks):
                task = tasks[task_id]
                old_record = old_records.get(task_id, {})
                lane = self.state["lanes"].get(task.lane, {})
                if task_states.get(task_id) == "DONE":
                    new_state, reason = "DONE", "task completion is persisted"
                elif task.source == "EXISTING_PLAN" and lane.get("phase_id") == task_id:
                    review = lane.get("review", {})
                    if lane.get("state") == "NEEDS_SOL_REVIEW":
                        new_state, reason = (
                            "NEEDS_SOL_REVIEW",
                            f"lane review pending: {review.get('type') or 'operator review'}",
                        )
                    else:
                        new_state, reason = dependency_evaluation(
                            task,
                            task_states=task_states,
                            workstream_states=workstream_states,
                            milestone_status=self.roadmap_snapshot.milestones.get(
                                task.milestone_id, {}
                            ).get("status", "ACTIVE") if self.roadmap_snapshot else "ACTIVE",
                            milestone_executable=self.roadmap_snapshot.milestones.get(
                                task.milestone_id, {}
                            ).get("executable", False) if self.roadmap_snapshot else False,
                            lane_available=task.lane in self.config.get("lanes", {}),
                            lane_busy=task.lane in self.processes,
                            global_mode=self.state.get("global_mode", "PAUSED"),
                            control_plane_valid=self._control_plane_valid(),
                            resolved_external_gates=scheduler.get("resolved_external_gates", []),
                            approval=approvals.get(task_id),
                        )
                else:
                    milestone = self.roadmap_snapshot.milestones.get(task.milestone_id, {}) if self.roadmap_snapshot else {}
                    workstream_states = self._workstream_states(tasks, task_states)
                    new_state, reason = dependency_evaluation(
                        task,
                        task_states=task_states,
                        workstream_states=workstream_states,
                        milestone_status=milestone.get("status", "ACTIVE"),
                        milestone_executable=bool(milestone.get("executable", False)),
                        lane_available=task.lane in self.config.get("lanes", {}),
                        lane_busy=task.lane in self.processes,
                        global_mode=self.state.get("global_mode", "PAUSED"),
                        control_plane_valid=self._control_plane_valid(),
                        resolved_external_gates=scheduler.get("resolved_external_gates", []),
                        approval=approvals.get(task_id),
                    )
                    if task.source == "ROADMAP" and task_id in self.processes:
                        new_state, reason = "RUNNING", "worker process is active"
                if task_states.get(task_id) != new_state:
                    task_states[task_id] = new_state
                    changed = True
                previous = old_records.get(task_id, {})
                record = dict(task.to_dict())
                for key in ("started_at", "completed_at", "worker_pid", "commit_sha"):
                    if isinstance(previous, dict) and previous.get(key) is not None:
                        record[key] = previous[key]
                record.update({
                    "state": new_state,
                    "reason": redact(reason),
                    "evaluated_at": utc_now(),
                })
                records[task_id] = record
            if not changed:
                break

        for task_id, record in records.items():
            previous = old_records.get(task_id, {})
            if (
                not isinstance(previous, dict)
                or previous.get("state") != record.get("state")
                or previous.get("reason") != record.get("reason")
            ):
                self._audit(
                    "dependency_evaluated",
                    task_id=task_id,
                    state=record.get("state"),
                    reason=record.get("reason"),
                )
            task = tasks[task_id]
            if task.source == "ROADMAP" and record["state"] == "NEEDS_SOL_REVIEW":
                self._request_task_review(task, record["reason"])
        scheduler["tasks"] = records
        scheduler["workstreams"] = self._workstream_states(tasks, task_states)

        milestone_states = scheduler.setdefault("milestones", {})
        for milestone_id, milestone in (self.roadmap_snapshot.milestones.items() if self.roadmap_snapshot else []):
            member_ids = [
                task_id for task_id, task in tasks.items()
                if task.milestone_id == milestone_id
            ]
            previous = milestone_states.get(milestone_id, {})
            previous_state = previous.get("state") if isinstance(previous, dict) else None
            if previous_state == "ACCEPTED":
                milestone_state = "ACCEPTED"
            elif (
                bool(milestone.get("executable"))
                and member_ids
                and all(records[task_id]["state"] == "DONE" for task_id in member_ids)
            ):
                milestone_state = "WAITING_RUNTIME_ACCEPTANCE"
            else:
                milestone_state = str(milestone.get("status", "PLANNED"))
                if milestone_state == "ENGINEERING_COMPLETE":
                    milestone_state = "ACTIVE"
            milestone_record = dict(previous) if isinstance(previous, dict) else {}
            milestone_record.update({
                "id": milestone_id,
                "title": milestone.get("title"),
                "state": milestone_state,
                "acceptance": list(milestone.get("acceptance", ())),
                "updated_at": utc_now(),
            })
            milestone_states[milestone_id] = milestone_record
            if previous_state and previous_state != milestone_state:
                self._audit(
                    "milestone_transition",
                    milestone_id=milestone_id,
                    from_state=previous_state,
                    to_state=milestone_state,
                )

        for lane_name in LANE_ORDER:
            lane = self.state["lanes"].get(lane_name)
            if not isinstance(lane, dict):
                continue
            current_id = lane.get("phase_id")
            record = records.get(current_id) if current_id else None
            if record:
                lane["scheduler_task_id"] = current_id
                lane["scheduler_state"] = record.get("state")
                lane["scheduler_reason"] = record.get("reason")
                lane["scheduler_dependencies"] = record.get("dependencies", [])
                lane["scheduler_source"] = record.get("source")
            else:
                lane["scheduler_task_id"] = None
                lane["scheduler_state"] = "DONE" if current_id is None else "PAUSED"
                lane["scheduler_reason"] = "lane queue complete" if current_id is None else "no current scheduler task"
        scheduler["last_recomputed_at"] = utc_now()

    def _scheduled_task_for_lane(self, lane_name: str) -> dict[str, Any] | None:
        tasks = self.state.get("scheduler", {}).get("tasks", {})
        lane = self.state.get("lanes", {}).get(lane_name, {})
        phase_id = lane.get("phase_id")
        if phase_id and isinstance(tasks.get(phase_id), dict):
            return tasks[phase_id]
        candidates = [
            record for record in tasks.values()
            if isinstance(record, dict)
            and record.get("lane") == lane_name
            and record.get("state") in {"READY", "RUNNING"}
        ]
        return sorted(candidates, key=lambda item: str(item.get("task_id")))[0] if candidates else None

    def _mark_scheduled_task_started(self, lane_name: str, task_id: str, pid: int) -> None:
        record = self.state.get("scheduler", {}).get("tasks", {}).get(task_id)
        if not isinstance(record, dict):
            return
        record.update({"state": "RUNNING", "started_at": utc_now(), "worker_pid": pid})
        self.state["scheduler"]["last_selected_lane"] = lane_name
        self._audit("task_started", task_id=task_id, lane=lane_name)

    def _mark_scheduled_task_done(self, task_id: str, commit_sha: str | None) -> None:
        record = self.state.get("scheduler", {}).get("tasks", {}).get(task_id)
        if not isinstance(record, dict):
            return
        record.update({
            "state": "DONE",
            "completed_at": utc_now(),
            "commit_sha": commit_sha,
            "reason": "required validation and CI completed",
        })
        self._audit("task_completed", task_id=task_id, commit_sha=commit_sha)

    def plan_for(self, lane_name: str) -> dict[str, Any]:
        return self.state["lanes"][lane_name].get(
            "plan_data", {"phases": [], "boundaries": []}
        )

    def phase(self, lane_name: str) -> dict[str, str] | None:
        phases = self.plan_for(lane_name).get("phases", [])
        index = int(self.state["lanes"][lane_name].get("phase_index", 0))
        return phases[index] if index < len(phases) else None

    def roadmap_task(self, lane_name: str) -> Any:
        phase = self.phase(lane_name)
        if phase is None:
            return None
        workstream_id = None
        milestone_id = "M01"
        if self.roadmap_snapshot is not None:
            for workstream in self.roadmap_snapshot.workstreams.values():
                if (
                    workstream.get("mode") == "EXISTING_PLAN"
                    and workstream.get("lane") == lane_name
                ):
                    workstream_id = str(workstream.get("id"))
                    milestone_id = str(workstream.get("milestone_id", milestone_id))
                    break
        return current_lane_task(
            lane_name,
            phase["id"],
            phase["title"],
            self.plan_for(lane_name).get("boundaries", []),
            milestone=milestone_id,
            workstream_id=workstream_id,
            control_plane_sha=self.state.get("control_plane", {}).get("applied_sha"),
        )

    def product_context(self, lane_name: str) -> str:
        """Return a small, phase-relevant context section for a worker prompt."""
        product_root = self._active_product_root()
        canonical = (
            product_root / "PRODUCT_VISION.md",
            product_root / "WORLD_RULES.md",
            product_root / "MILESTONE_01_CORE_WORLD.md",
        )
        available = all(path.exists() for path in canonical)
        relevance = {
            "combat": "Lifecycle/incarnation work matters because renewable dungeon respawns must be fresh lifecycles and stale packets must not affect them.",
            "authority": "Authority work matters because the server owns identity, lifecycle, persistence, attribution, and trust decisions; PvP/self-damage is not PvE evidence.",
            "population": "Population work matters because humanoids are suppressed, trusted creatures remain, and unknown actor classification must not be guessed into Creature.",
            "ui": "Character UI work matters because players enter through server Character Select and a server-owned character, not an authoritative local save.",
        }
        product_state = "canonical product files available" if available else "canonical product files unavailable; use these bounded rules"
        control_sha = self.state.get("control_plane", {}).get("applied_sha") or "none"
        return "\n".join([
            "PRODUCT CONTEXT (bounded; canonical files are installed under " + str(product_root) + "):",
            "- Applied control-plane SHA: " + str(control_sha) + ".",
            "- Vision: transform Skyrim Together Reborn into a persistent multiplayer RP/MMO platform; keep its synchronization foundations while replacing the single-player world/character model with server-owned persistent multiplayer systems.",
            "- Immutable rules: Character, AccountId, CharacterId, lifecycle, persistence, and trust decisions are server-owned; local saves are bootstrap/shell state; never invent server-side Skyrim AI; security/trust correctness beats feature velocity.",
            "- Milestone: MILESTONE_01_CORE_WORLD — authenticate, select a server-owned character, enter a world without ordinary humanoid NPC population, coexist with trusted creatures, complete renewable encounters, and reconnect with the character. Queue completion alone is not acceptance; multi-client runtime evidence is required.",
            "- Lane relevance: " + relevance.get(lane_name, "Preserve the immutable world rules while completing this lane phase."),
            "- Product files: " + product_state + ".",
        ])

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
            "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN",
            "GITHUB_ENTERPRISE_TOKEN", "SSH_AUTH_SOCK", "SSH_AGENT_PID",
        ):
            environment.pop(key, None)
        return environment

    def worker_env(self) -> dict[str, str]:
        """Build the least-privileged environment needed by a Codex worker.

        Codex authentication remains available through CODEX_HOME, but GitHub
        CLI credentials are hidden behind a dedicated empty config directory.
        The worker never receives GitHub token variables or an SSH agent.
        """
        environment = self.env()
        environment["GH_CONFIG_DIR"] = str(
            self.config.get("worker_gh_config_dir", "/var/lib/skyrim-dev/worker-gh-config")
        )
        for key in (
            "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN",
            "GITHUB_ENTERPRISE_TOKEN", "SSH_AUTH_SOCK", "SSH_AGENT_PID",
        ):
            environment.pop(key, None)
        return environment

    def command(
        self, args: list[str], cwd: str | None = None, timeout: int = 120
    ) -> tuple[int, str, str]:
        try:
            result = subprocess.run(
                args, cwd=cwd, env=self.env(), text=True,
                encoding="utf-8", errors="replace",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=timeout, check=False,
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired as exc:
            stdout = exc.stdout or ""
            stderr = exc.stderr or ""
            if isinstance(stdout, bytes):
                stdout = stdout.decode("utf-8", errors="replace")
            if isinstance(stderr, bytes):
                stderr = stderr.decode("utf-8", errors="replace")
            return 124, str(stdout), str(stderr) + "\ncommand timed out"

    def git_head(self, worktree: str) -> str | None:
        code, out, _ = self.command(
            ["git", "-C", worktree, "rev-parse", "HEAD"], timeout=30
        )
        return out.strip() if code == 0 else None

    def git_status_details(self, worktree: str) -> tuple[bool, list[dict[str, str | None]], str]:
        code, out, err = self.command(
            ["git", "-C", worktree, "status", "--porcelain=v1", "-z",
             "--untracked-files=all"], timeout=60,
        )
        if code != 0:
            return False, [], redact(err or out or "git status failed")
        return True, parse_porcelain_v1_z(out), ""

    def git_status_files(self, worktree: str) -> list[str] | None:
        okay, entries, _ = self.git_status_details(worktree)
        return status_paths(entries) if okay else None

    def git_dirty(self, worktree: str) -> bool:
        okay, entries, _ = self.git_status_details(worktree)
        return (not okay) or bool(entries)

    def git_status_is_conflicted(self, worktree: str) -> bool:
        okay, entries, _ = self.git_status_details(worktree)
        return (not okay) or any(entry.get("kind") == "unmerged" for entry in entries)

    def git_branch(self, worktree: str) -> str | None:
        code, out, _ = self.command(
            ["git", "-C", worktree, "symbolic-ref", "--short", "HEAD"], timeout=30
        )
        return out.strip() if code == 0 else None

    def verify_worker_worktree(self, lane_name: str, recovery: bool) -> tuple[bool, str]:
        lane = self.state["lanes"][lane_name]
        worktree = lane["worktree"]
        expected = lane["branch"]
        actual = self.git_branch(worktree)
        if actual != expected:
            return False, f"worktree branch is {actual or 'unknown'}; expected {expected}"
        okay, entries, error = self.git_status_details(worktree)
        if not okay:
            return False, error
        if any(entry.get("kind") == "unknown" for entry in entries):
            return False, "Git status contained an unrecognized record"
        if any(entry.get("kind") == "unmerged" for entry in entries):
            return False, "worktree has merge/conflict entries"
        if entries and not recovery:
            return False, "normal worker requires a clean worktree"
        return True, ""

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

    def _stop_active_workers(self, reason: str) -> None:
        for lane_name in list(self.processes):
            self.terminate_worker(lane_name, reason)

    def _pause_lanes(self) -> None:
        for lane in self.state["lanes"].values():
            if lane.get("state") not in TERMINAL_STATES:
                if lane.get("state") != "PAUSED":
                    lane["paused_from_state"] = lane.get("state")
                lane["state"] = "PAUSED"
                lane["worker_pid"] = None

    def refresh_control(self) -> None:
        control = read_json(CONTROL_PATH, {})
        desired = control.get("desired_mode")
        if desired not in {"RUNNING", "PAUSED"}:
            desired = self.state.get("global_mode", "PAUSED")
        if desired == "RUNNING" and self.state.get("safety_pause"):
            resource_ok, resource_reason = self.resource_guard()
            git_ok, git_reason = self.git_health() if resource_ok else (False, "resource guard still unsafe")
            if not resource_ok or not git_ok:
                reason = resource_reason or git_reason
                self._write_control("PAUSED", f"safety condition remains: {reason}")
                if self.state.get("global_mode") != "PAUSED":
                    self.state["global_mode"] = "PAUSED"
                self._pause_lanes()
                return
            self.state["safety_pause"] = False
        if desired == self.state.get("global_mode"):
            if desired == "PAUSED":
                self._stop_active_workers("global pause; dirty diff preserved")
                self._pause_lanes()
            return
        self.state["global_mode"] = desired
        self.state["mode_reason"] = control.get("reason", "control change")
        if desired == "PAUSED":
            self._stop_active_workers("operator pause; dirty diff preserved")
            self._pause_lanes()
        else:
            for lane in self.state["lanes"].values():
                if lane.get("state") == "PAUSED":
                    previous = lane.pop("paused_from_state", None)
                    if previous in {"WAITING_FOR_CI", "LOCAL_VALIDATION", "COMMITTING", "PUSHING", "NEXT_PHASE"}:
                        lane["state"] = previous
                    elif previous == "RECOVERING":
                        lane["state"] = "RECOVERING"
                    else:
                        lane["state"] = "RECOVERING" if self.git_dirty(lane["worktree"]) else "READY"
        self.event(f"global mode changed to {desired}: {self.state['mode_reason']}")

    def pause_for_safety(self, reason: str) -> None:
        if (
            self.state.get("global_mode") == "PAUSED"
            and self.state.get("mode_reason") == reason
            and self.state.get("safety_pause")
        ):
            return
        self.state["global_mode"] = "PAUSED"
        self.state["mode_reason"] = reason
        self.state["safety_pause"] = True
        self._write_control("PAUSED", reason)
        self._stop_active_workers(f"automatic safety pause: {reason}")
        self._pause_lanes()
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
            branch = self.git_branch(worktree)
            expected = self.config["lanes"][lane_name]["branch"]
            if branch != expected:
                return False, f"{lane_name} worktree branch is {branch or 'unknown'}; expected {expected}"
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
        scheduled = self._scheduled_task_for_lane(lane_name)
        if phase is None and scheduled is None:
            return ""
        task = self.roadmap_task(lane_name)
        task_id = scheduled.get("task_id") if scheduled else (phase["id"] if phase else "unknown")
        task_title = scheduled.get("title") if scheduled else (phase["title"] if phase else "roadmap task")
        task_milestone = scheduled.get("milestone_id") if scheduled else (task.milestone if task else "M01")
        task_workstream = scheduled.get("workstream_id") if scheduled else (task.workstream_id if task else "")
        task_source = scheduled.get("source") if scheduled else (task.source if task else "EXISTING_PLAN")
        task_risk = scheduled.get("risk") if scheduled else (task.risk if task else "lane-local")
        task_dependencies = scheduled.get("dependencies", []) if scheduled else list(task.dependencies if task else ())
        task_acceptance = scheduled.get("acceptance_criteria", []) if scheduled else list(task.acceptance_criteria if task else ())
        control_sha = self.state.get("control_plane", {}).get("applied_sha") or "none"
        boundaries = "\n".join(
            f"- {item}" for item in self.plan_for(lane_name).get("boundaries", [])
        )
        recovery_text = ""
        if recovery:
            recovery_text = (
                "\nThis is the single bounded recovery attempt correcting the same failed "
                "phase. Inspect the current diff and the bounded evidence below. Complete "
                "only this phase safely; do not advance to another phase. Never discard or "
                "reset the existing diff. Do not query GitHub and do not use credentials; "
                "the supervisor has supplied the observed failure context.\n\n"
                + self._bounded_recovery_context(lane_name)
                + "\n"
            )
        return f"""You are the one-phase Codex worker for lane {lane_name}.
Repository: {self.config["repo"]}
Lane branch: {lane["branch"]}
Worktree: {lane["worktree"]}
Authoritative plan: {lane["plan"]}
Control-plane SHA: {control_sha}
Task identity: milestone_id={task_milestone}, workstream_id={task_workstream}, task_id={task_id}, source={task_source}
Task: {task_title}
Risk: {task_risk}
Dependencies: {json.dumps(task_dependencies, sort_keys=True)}
Acceptance criteria: {json.dumps(task_acceptance, sort_keys=True)}
{recovery_text}
{self.product_context(lane_name)}

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
            with path.open("w", encoding="utf-8") as handle:
                for line in stream:
                    handle.write(redact(line))
                    handle.flush()
                    self.state["lanes"][lane_name]["last_progress_at"] = utc_now()
                    self._bound_worker_log(path)
        except OSError as exc:
            self.log(f"worker log error: {exc}", lane_name)

    def _prune_worker_logs(self, lane_name: str) -> None:
        keep = int(self.config.get("max_worker_logs_per_lane", 20))
        paths = sorted(
            LOG_ROOT.glob(f"{lane_name}-*-attempt-*.log"),
            key=lambda path: path.stat().st_mtime if path.exists() else 0,
            reverse=True,
        )
        for path in paths[keep:]:
            try:
                path.unlink()
            except OSError:
                pass

    def _new_worker_log(self, lane_name: str, phase_id: str, attempt: int) -> Path:
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        safe_phase = re.sub(r"[^A-Za-z0-9_.-]+", "-", phase_id)
        return LOG_ROOT / f"{lane_name}-{safe_phase}-attempt-{attempt}-{stamp}.log"

    def _bounded_recovery_context(self, lane_name: str) -> str:
        """Build bounded, redacted evidence for a same-phase recovery worker."""
        lane = self.state["lanes"][lane_name]
        stored = lane.get("recovery_context", {})
        if not isinstance(stored, dict):
            stored = {}

        def bounded(value: Any, limit: int) -> str:
            text = redact(str(value or ""))
            if len(text) <= limit:
                return text
            return text[:limit] + "\n[context truncated]"

        previous_failure = stored.get("last_error") or lane.get("last_error")
        review_reasons = stored.get("review_reasons") or lane.get("review_reasons", [])
        if isinstance(review_reasons, list):
            review_text = "\n".join(
                bounded(item, MAX_RECOVERY_ERROR_BYTES) for item in review_reasons if item
            )
        else:
            review_text = bounded(review_reasons, MAX_RECOVERY_ERROR_BYTES)
        ci = lane.get("ci", {})
        ci_excerpt = ci.get("failure_excerpt") if isinstance(ci, dict) else ""
        ci_excerpt = ci_excerpt or stored.get("ci_failure_excerpt")
        worker_evidence = ""
        worker_log = lane.get("worker_log")
        if worker_log:
            worker_evidence = tail_text(Path(str(worker_log)), MAX_RECOVERY_WORKER_BYTES)
        if not worker_evidence:
            validation = lane.get("validation", {})
            if isinstance(validation, dict):
                worker_evidence = validation.get("worker_output_tail", "")
        worker_evidence = worker_evidence or stored.get("worker_evidence")
        sections = [
            "RECOVERY CONTEXT",
            "Previous failure:",
            bounded(previous_failure, MAX_RECOVERY_ERROR_BYTES) or "(no persisted failure text)",
            "",
            "Required CI failure excerpt:",
            bounded(ci_excerpt, MAX_RECOVERY_CI_BYTES) or "(no persisted CI failure excerpt)",
            "",
            "Previous worker evidence:",
            bounded(worker_evidence, MAX_RECOVERY_WORKER_BYTES) or "(no persisted worker evidence)",
        ]
        if review_text:
            sections[4:4] = ["", "Review observations:", review_text]
        return "\n".join(sections)

    def _capture_recovery_context(self, lane_name: str) -> dict[str, Any]:
        lane = self.state["lanes"][lane_name]
        ci = lane.get("ci", {})
        validation = lane.get("validation", {})
        worker_evidence = ""
        if lane.get("worker_log"):
            worker_evidence = tail_text(
                Path(str(lane["worker_log"])), MAX_RECOVERY_WORKER_BYTES
            )
        if not worker_evidence and isinstance(validation, dict):
            worker_evidence = str(validation.get("worker_output_tail") or "")
        return {
            "last_error": redact(str(lane.get("last_error") or ""))[:MAX_RECOVERY_ERROR_BYTES],
            "review_reasons": [
                redact(str(item))[:MAX_RECOVERY_ERROR_BYTES]
                for item in lane.get("review_reasons", [])
                if item
            ][:8],
            "ci_failure_excerpt": redact(
                str(ci.get("failure_excerpt") if isinstance(ci, dict) else "")
            )[:MAX_RECOVERY_CI_BYTES],
            "worker_evidence": redact(worker_evidence)[:MAX_RECOVERY_WORKER_BYTES],
        }

    def start_worker(self, lane_name: str, recovery: bool = False) -> bool:
        if lane_name in self.processes:
            return False
        availability = self.state.get("codex_availability", {})
        if availability.get("status") == "RATE_LIMITED":
            retry_at = parse_time(availability.get("next_retry_at"))
            if retry_at and time.time() < retry_at:
                return False
            if availability.get("probe_started_at"):
                return False
        lane = self.state["lanes"][lane_name]
        scheduled = None
        if "scheduler" in self.state:
            self._recompute_scheduler()
            scheduled = self._scheduled_task_for_lane(lane_name)
            if scheduled is None:
                return False
            allowed_states = {"READY", "RUNNING"}
            if scheduled.get("state") not in allowed_states:
                return False
        phase = self.phase(lane_name)
        if phase is None and scheduled and scheduled.get("source") == "ROADMAP":
            phase = {
                "id": str(scheduled.get("task_id")),
                "title": str(scheduled.get("title")),
            }
        if phase is None:
            self.review(lane_name, ["lane queue is complete; final review required"])
            return False
        okay, reason = self.verify_worker_worktree(lane_name, recovery)
        if not okay:
            if (
                recovery
                or "branch" in reason
                or "conflict" in reason
                or "unrecognized" in reason
                or "status" in reason.lower()
            ):
                self.review(lane_name, [reason])
            else:
                lane["state"] = "RECOVERING"
                lane["last_error"] = reason
                lane["recovery_attempts"] = min(1, int(lane.get("recovery_attempts", 0)) + 1)
                self.event("refusing normal worker; bounded recovery required", lane_name)
            return False
        attempt = int(lane.get("worker_attempt", 0)) + 1
        lane["worker_attempt"] = attempt
        log_path = self._new_worker_log(lane_name, phase["id"], attempt)
        self._prune_worker_logs(lane_name)
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
                args, cwd=lane["worktree"], env=self.worker_env(), text=True,
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
        if availability.get("status") == "RATE_LIMITED":
            availability["probe_started_at"] = utc_now()
        lane.update({
            "state": "CODING",
            "phase_id": phase["id"],
            "phase_title": phase["title"],
            "scheduler_task_id": scheduled.get("task_id") if scheduled else phase["id"],
            "scheduler_source": scheduled.get("source") if scheduled else "EXISTING_PLAN",
            "control_plane_sha": self.state.get("control_plane", {}).get("applied_sha"),
            "worker_pid": process.pid,
            "worker_log": str(log_path),
            "worker_started_at": utc_now(),
            "last_progress_at": utc_now(),
            "worker_recovery": recovery,
            "worker_attempt": attempt,
            "last_error": "",
        })
        thread = threading.Thread(
            target=self._drain_worker,
            args=(lane_name, process.stdout, log_path), daemon=True
        )
        self.output_threads[lane_name] = thread
        thread.start()
        if scheduled:
            self._mark_scheduled_task_started(
                lane_name, str(scheduled.get("task_id")), process.pid
            )
        self.event(
            f"started {'recovery ' if recovery else ''}worker pid={process.pid} phase={phase['id']} attempt={attempt} log={log_path}",
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

    def handle_codex_rate_limited(self, lane_name: str, output: str) -> None:
        availability = self.state.setdefault("codex_availability", {})
        previous = int(availability.get("backoff_seconds") or 0)
        backoff = next_codex_backoff(
            previous,
            int(self.config.get("codex_retry_base_seconds", 900)),
            int(self.config.get("codex_retry_max_seconds", 3600)),
        )
        retry_count = int(availability.get("retry_count") or 0) + 1
        detected = time.time()
        retry_at = detected + backoff
        availability.update({
            "status": "RATE_LIMITED",
            "detected_at": utc_now(),
            "next_retry_at": dt.datetime.fromtimestamp(
                retry_at, dt.timezone.utc
            ).isoformat(timespec="seconds"),
            "backoff_seconds": backoff,
            "retry_count": retry_count,
            "probe_started_at": None,
            "reason": redact("Codex usage allowance/rate limit detected: " + tail_text(
                Path(self.state["lanes"][lane_name].get("worker_log") or ""), 1000
            )),
        })
        lane = self.state["lanes"][lane_name]
        lane["last_error"] = "Codex availability is rate limited; dirty diff preserved"
        lane["state"] = "RECOVERING" if self.git_dirty(lane["worktree"]) else "READY"
        self.event(
            f"Codex RATE_LIMITED; no new workers until {availability['next_retry_at']} "
            f"(backoff {backoff}s)", lane_name,
        )

    def mark_codex_available(self) -> None:
        availability = self.state.setdefault("codex_availability", {})
        if availability.get("status") != "RATE_LIMITED":
            return
        availability.update({
            "status": "AVAILABLE",
            "detected_at": None,
            "next_retry_at": None,
            "backoff_seconds": 0,
            "retry_count": 0,
            "probe_started_at": None,
            "reason": "Codex worker became available",
        })
        self.event("Codex availability restored; normal scheduling may resume")

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
            worker_output = tail_text(Path(lane.get("worker_log") or ""), 40000)
            lane["worker_pid"] = None
            lane["worker_finished_at"] = utc_now()
            lane["last_progress_at"] = utc_now()
            if classify_codex_usage_limit(return_code, worker_output):
                self.handle_codex_rate_limited(lane_name, worker_output)
                continue
            self.mark_codex_available()
            if return_code == 0 and result == "COMPLETE":
                lane["state"] = (
                    "LOCAL_VALIDATION"
                    if self.state.get("global_mode") == "RUNNING"
                    else "PAUSED"
                )
                lane["last_error"] = ""
                self.event(
                    "worker completed; entering local validation"
                    if lane["state"] == "LOCAL_VALIDATION"
                    else "worker completed while paused; diff preserved",
                    lane_name,
                )
            elif result == "NEEDS_SOL_REVIEW":
                self.review(lane_name, ["worker explicitly requested human review"])
            elif result == "BLOCKED":
                self.review(lane_name, ["worker reported a blocking condition"])
            else:
                self.worker_failure(
                    lane_name,
                    f"worker exit={return_code}; marker={result or 'missing'}",
                )

    def changed_diff(self, lane_name: str) -> tuple[list[str], str, str, dict[str, Any]]:
        worktree = self.state["lanes"][lane_name]["worktree"]
        okay, entries, status_error = self.git_status_details(worktree)
        files = status_paths(entries) if okay else []
        diff_parts: list[str] = []
        stat_parts: list[str] = []
        tracked_diff_bytes = 0
        for label, extra in (("unstaged", []), ("staged", ["--cached"])):
            code, diff, err = self.command(
                ["git", "-C", worktree, "diff", "--no-ext-diff", "--binary",
                 "--unified=0", *extra], timeout=120,
            )
            if code == 0:
                tracked_diff_bytes += len(diff.encode("utf-8", errors="replace"))
                if diff:
                    diff_parts.append(f"### {label}\n{diff}")
            elif err:
                diff_parts.append(f"### {label} diff error\n{redact(err)}")
            stat_code, stat, stat_err = self.command(
                ["git", "-C", worktree, "diff", "--stat", *extra], timeout=60
            )
            if stat_code == 0 and stat.strip():
                stat_parts.append(f"{label}:\n{stat.strip()}")
            elif stat_err:
                stat_parts.append(f"{label}: {redact(stat_err)}")
        untracked_bytes = 0
        untracked_review_bytes = 0
        untracked_review_issues: list[str] = []
        try:
            review_limit = max(1, int(self.config.get("max_total_diff_bytes", 524288)))
        except (TypeError, ValueError):
            review_limit = 524288
        max_untracked_file = min(MAX_UNTRACKED_REVIEW_FILE_BYTES, review_limit)
        worktree_root = Path(worktree).resolve()
        for entry in entries:
            if entry.get("kind") != "untracked" or not entry.get("path"):
                continue
            relative = str(entry["path"])
            path = worktree_root / relative
            try:
                path = path.resolve()
                path.relative_to(worktree_root)
            except (OSError, ValueError):
                untracked_review_issues.append(
                    f"untracked path could not be safely resolved for bounded review: {relative}"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\ncontent: omitted (unsafe path resolution)"
                )
                continue
            try:
                size = path.stat().st_size
            except OSError as exc:
                untracked_review_issues.append(
                    f"untracked file could not be read for bounded review: {relative} ({exc})"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\ncontent: omitted (stat failed)"
                )
                continue
            untracked_bytes += size
            if not path.is_file():
                reason = "not a regular file"
                untracked_review_issues.append(
                    f"untracked file requires review: {relative} ({reason})"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {size}\ncontent: omitted ({reason})"
                )
                continue
            if size > max_untracked_file:
                reason = f"{size} bytes exceeds bounded text review limit {max_untracked_file}"
                untracked_review_issues.append(
                    f"untracked file content omitted from bounded review: {relative} ({reason})"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {size}\ncontent: omitted ({reason})"
                )
                stat_parts.append(f"untracked: {relative} ({size} bytes; content omitted)")
                continue
            try:
                raw = path.read_bytes()
            except OSError as exc:
                untracked_review_issues.append(
                    f"untracked file could not be read for bounded review: {relative} ({exc})"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {size}\ncontent: omitted (read failed)"
                )
                continue
            if len(raw) > max_untracked_file:
                reason = f"{len(raw)} bytes exceeds bounded text review limit {max_untracked_file}"
                untracked_review_issues.append(
                    f"untracked file content omitted from bounded review: {relative} ({reason})"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {len(raw)}\ncontent: omitted ({reason})"
                )
                continue
            if b"\0" in raw:
                reason = "binary content detected"
                untracked_review_issues.append(
                    f"untracked binary file requires review: {relative} ({size} bytes)"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {size}\ncontent: omitted ({reason})"
                )
                stat_parts.append(f"untracked: {relative} ({size} bytes; binary omitted)")
                continue
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                reason = "non-UTF-8/binary content detected"
                untracked_review_issues.append(
                    f"untracked binary file requires review: {relative} ({size} bytes)"
                )
                diff_parts.append(
                    "### untracked file review metadata\n"
                    f"path: {relative}\nsize_bytes: {size}\ncontent: omitted ({reason})"
                )
                stat_parts.append(f"untracked: {relative} ({size} bytes; binary omitted)")
                continue
            text = redact(text)
            rendered = "".join(difflib.unified_diff(
                [], text.splitlines(keepends=True),
                fromfile="/dev/null", tofile=f"b/{relative}", n=0,
            ))
            diff_parts.append(f"### untracked text file: {relative}\n{rendered}")
            untracked_review_bytes += len(rendered.encode("utf-8", errors="replace"))
            stat_parts.append(f"untracked: {relative} ({size} bytes; text included)")
        metrics = {
            "changed_files": len(files),
            "tracked_diff_bytes": tracked_diff_bytes,
            "untracked_bytes": untracked_bytes,
            "total_diff_bytes": tracked_diff_bytes + untracked_bytes,
            "untracked_review_bytes": untracked_review_bytes,
            "untracked_review_issues": untracked_review_issues,
        }
        if status_error:
            diff_parts.append(f"### status error\n{status_error}")
        diff_text = "\n\n".join(diff_parts)
        diff_bytes = diff_text.encode("utf-8", errors="replace")
        if len(diff_bytes) > review_limit:
            marker = b"\n[review diff text truncated to configured bound]\n"
            keep = max(0, review_limit - len(marker))
            first = keep // 2
            last = keep - first
            diff_bytes = diff_bytes[:first] + marker + diff_bytes[-last:] if last else diff_bytes[:first] + marker
        return files, diff_bytes.decode("utf-8", errors="replace"), "\n\n".join(stat_parts)[:12000], metrics

    def forbidden_change_reasons(self, lane_name: str, files: list[str]) -> list[str]:
        spec = self.config["lanes"][lane_name]
        forbidden = tuple(spec.get("forbidden_prefixes", []))
        reasons: list[str] = []
        protected_names = (
            "auth.json", "hosts.yml", ".env", "id_rsa", "id_ed25519",
            "credentials", "token", "secret",
        )
        for path in files:
            normalized = path.replace("\\", "/")
            lower = normalized.lower()
            if normalized == ".codex/lane/PLAN.md" or normalized.startswith(".git/"):
                reasons.append(f"protected metadata changed: {normalized}")
            if forbidden and normalized.startswith(forbidden):
                reasons.append(f"cross-lane or forbidden path changed: {normalized}")
            if any(name in lower for name in protected_names):
                reasons.append(f"credential-like path changed: {normalized}")
            if generated_path(normalized):
                reasons.append(f"generated/build artifact path changed: {normalized}")
            absolute = Path(self.state["lanes"][lane_name]["worktree"]) / normalized
            try:
                if absolute.is_file() and absolute.stat().st_size > 50 * 1024 * 1024:
                    reasons.append(f"file exceeds 50 MiB safety limit: {normalized}")
            except OSError:
                pass
        return sorted(set(reasons))

    def focused_validation_commands(self, lane_name: str, phase_id: str) -> list[list[str]]:
        configured = self.config.get("focused_validation_commands", {})
        lane_config = configured.get(lane_name, {}) if isinstance(configured, dict) else {}
        commands = lane_config.get(phase_id, []) if isinstance(lane_config, dict) else []
        if not isinstance(commands, list):
            return []
        result: list[list[str]] = []
        for command in commands:
            if isinstance(command, list) and command and all(isinstance(item, str) for item in command):
                result.append(command)
        return result

    def run_focused_validation(self, lane_name: str, phase_id: str) -> dict[str, Any]:
        commands = self.focused_validation_commands(lane_name, phase_id)
        if not commands:
            return {
                "status": "NOT_CONFIGURED",
                "commands": [],
                "message": "focused tests: not independently verified by supervisor",
            }
        results: list[dict[str, Any]] = []
        passed = True
        for command in commands:
            code, out, err = self.command(
                command,
                cwd=self.state["lanes"][lane_name]["worktree"],
                timeout=int(self.config.get("focused_validation_timeout_seconds", 900)),
            )
            passed = passed and code == 0
            results.append({
                "command": command,
                "exit_code": code,
                "output_tail": redact((out + "\n" + err)[-4000:]),
            })
        return {
            "status": "PASS" if passed else "FAIL",
            "commands": results,
            "message": "focused tests observed by supervisor",
        }

    def local_validate(self, lane_name: str) -> bool:
        lane = self.state["lanes"][lane_name]
        worktree = lane["worktree"]
        branch = self.git_branch(worktree)
        if branch != lane["branch"]:
            self.review(lane_name, [f"worktree branch changed; expected {lane['branch']}"])
            return False
        status_ok, entries, status_error = self.git_status_details(worktree)
        files, diff, stat, metrics = self.changed_diff(lane_name)
        reasons = self.forbidden_change_reasons(lane_name, files)
        structural_checks = [
            "expected lane branch",
            "porcelain Git status understood",
            "unmerged/conflict status absent",
            "unstaged and staged diff checks",
        ]
        if not status_ok:
            reasons.append("Git status failure: " + (status_error or "git status failed"))
        if any(entry.get("kind") == "unmerged" for entry in entries):
            reasons.append("Git merge/conflict state is not safe to review automatically")
        unstaged_check, _, unstaged_err = self.command(
            ["git", "-C", worktree, "diff", "--check"], timeout=120
        )
        staged_check, _, staged_err = self.command(
            ["git", "-C", worktree, "diff", "--cached", "--check"], timeout=120
        )
        if unstaged_check != 0 or staged_check != 0:
            reasons.append("staged or unstaged git diff --check failed")
        if metrics["changed_files"] > int(self.config.get("max_changed_files", 40)):
            reasons.append(
                f"changed file count {metrics['changed_files']} exceeds configured limit "
                f"{self.config.get('max_changed_files', 40)}"
            )
        if metrics["total_diff_bytes"] > int(self.config.get("max_total_diff_bytes", 524288)):
            reasons.append(
                f"total diff size {metrics['total_diff_bytes']} bytes exceeds configured limit "
                f"{self.config.get('max_total_diff_bytes', 524288)}"
            )
        reasons.extend(str(item) for item in metrics.get("untracked_review_issues", []))
        phase_id = str(lane.get("phase_id") or "")
        phase_config = self.config.get("phase_completion", {}).get(phase_id, {})
        allow_no_change = bool(
            isinstance(phase_config, dict) and phase_config.get("completion_mode") == "evidence-only"
        )
        if not files and not allow_no_change:
            reasons.append("worker reported phase complete but produced no reviewable changes")
        structural_failed = bool(reasons)
        focused = self.run_focused_validation(lane_name, phase_id) if not reasons else {
            "status": "NOT_RUN",
            "commands": [],
            "message": "focused tests: not run because structural validation failed",
        }
        if focused.get("status") == "FAIL":
            reasons.append("configured focused validation failed")
        lane["validation"] = {
            "at": utc_now(),
            "status": "PASS" if not reasons else "FAIL",
            "structural": {
                "status": "PASS" if not structural_failed else "FAIL",
                "checks": structural_checks,
                "unstaged_diff_check": unstaged_check == 0,
                "staged_diff_check": staged_check == 0,
                "unstaged_diff_error": redact(unstaged_err),
                "staged_diff_error": redact(staged_err),
            },
            "focused_tests": focused,
            "tests": [
                item.get("command") for item in focused.get("commands", [])
                if isinstance(item, dict) and item.get("command")
            ],
            "changed_files": files,
            "diffstat": stat,
            "diff_text": diff,
            "diff_metrics": metrics,
            "worker_output_tail": tail_text(Path(lane.get("worker_log") or ""), 8000),
        }
        if reasons:
            lane["last_error"] = "; ".join(reasons)
            hard_review = any(
                "no reviewable changes" in reason
                or "exceeds configured limit" in reason
                or "merge/conflict" in reason
                or "forbidden" in reason
                or "generated/build" in reason
                or "untracked" in reason
                or "git status" in reason.lower()
                for reason in reasons
            )
            if hard_review:
                self.review(lane_name, reasons)
            elif int(lane.get("recovery_attempts", 0)) < 1 and self.state.get("global_mode") == "RUNNING":
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
        if self.state.get("global_mode") != "RUNNING":
            lane["state"] = "PAUSED"
            self.event("commit prevented while global mode is PAUSED", lane_name)
            return False
        files = lane.get("validation", {}).get("changed_files", [])
        if not files:
            self.review(
                lane_name,
                ["worker reported phase complete but produced no reviewable changes"],
            )
            return False
        current_files = self.git_status_files(worktree)
        if current_files is None:
            self.review(lane_name, [
                "Git status failed after validation; automatic commit and push are forbidden",
            ])
            return False
        if sorted(current_files) != sorted(files):
            self.review(lane_name, [
                "worktree changed after validation; validated explicit file set no longer matches",
            ])
            return False
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
        if self.git_dirty(worktree):
            self.review(lane_name, [
                "worktree remained dirty after outer-supervisor commit; automatic push is forbidden",
            ])
            return False
        lane["state"] = "PUSHING"
        self.event(f"outer supervisor committed {commit}", lane_name)
        return True

    def push_phase(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        if self.state.get("global_mode") != "RUNNING":
            lane["state"] = "PAUSED"
            self.event("push prevented while global mode is PAUSED", lane_name)
            return
        if self.git_dirty(lane["worktree"]):
            self.review(lane_name, ["push blocked because worktree is not clean"])
            return
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
            "--commit", sha, "--limit", "100",
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
        required = [
            str(item) for item in self.config.get("required_workflows", [])
            if str(item).strip()
        ]
        aggregate = aggregate_required_workflows(runs, required, sha)
        lane["ci"] = {
            "status": aggregate["status"],
            "conclusion": "success" if aggregate["status"] == "PASS" else None,
            "sha": sha,
            "run_id": None,
            "url": None,
            "required_workflows": aggregate["required"],
            "missing_workflows": aggregate["missing"],
            "running_workflows": aggregate["running"],
            "failure_workflows": aggregate["failures"],
            "observed_at": utc_now(),
        }
        if aggregate["status"] == "WAIT":
            started = parse_time(lane.get("ci_started_at"))
            elapsed = time.time() - started if started else 0
            if aggregate["missing"] and elapsed > int(self.config.get("ci_queue_timeout_seconds", 600)):
                self.review(lane_name, [
                    "one or more required GitHub Actions workflows did not appear within bounded wait",
                    "missing workflows: " + ", ".join(aggregate["missing"]),
                ])
            elif elapsed > int(self.config.get("ci_timeout_seconds", 1800)):
                self.review(lane_name, [
                    "required GitHub Actions workflows did not all complete within bounded wait",
                ])
            return
        if aggregate["status"] == "PASS":
            if lane.get("history"):
                lane["history"][-1]["ci"] = lane["ci"]
            self.event(
                "all required GitHub Actions workflows passed: "
                + ", ".join(required), lane_name,
            )
            self.complete_phase(lane_name)
            return
        excerpts: list[str] = []
        for failure in aggregate["failures"]:
            run_id = failure.get("run_id")
            if run_id is not None:
                excerpts.append(
                    f"{failure.get('workflow')}:\n{self.fetch_ci_failure(int(run_id))}"
                )
        excerpt = "\n\n".join(excerpts)[-12000:]
        lane["ci"]["failure_excerpt"] = excerpt
        if lane.get("history"):
            lane["history"][-1]["ci"] = lane["ci"]
        lane["phase_failure_count"] = int(lane.get("phase_failure_count", 0)) + 1
        failed_names = ", ".join(
            str(item.get("workflow")) for item in aggregate["failures"]
        )
        lane["last_error"] = f"required CI workflow failure: {failed_names}"
        if lane["phase_failure_count"] >= 2:
            self.review(lane_name, ["same phase failed required CI twice", excerpt])
        else:
            lane["state"] = "RECOVERING"
            lane["recovery_attempts"] = 1
            self.event("required CI failed; one fresh recovery worker allowed", lane_name)

    def review_reasons(self, lane_name: str, include_checkpoint: bool = False) -> list[str]:
        lane = self.state["lanes"][lane_name]
        validation = lane.get("validation", {})
        files = validation.get("changed_files", [])
        diff = validation.get("diff_text", "")
        lowered = (diff + " " + " ".join(files)).lower()
        reasons: list[str] = []
        if include_checkpoint and checkpoint_due(int(lane.get("success_since_review", 0))):
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

    def planned_next_phase(self, lane_name: str, index: int | None = None) -> dict[str, str] | None:
        phases = self.plan_for(lane_name).get("phases", [])
        if index is None:
            index = int(self.state["lanes"][lane_name].get("phase_index", 0))
        return phases[index] if 0 <= index < len(phases) else None

    def complete_phase(self, lane_name: str) -> None:
        lane = self.state["lanes"][lane_name]
        completed_phase = {
            "id": lane.get("phase_id"),
            "title": lane.get("phase_title"),
        }
        if "scheduler" in self.state and completed_phase.get("id"):
            self._mark_scheduled_task_done(
                str(completed_phase["id"]), lane.get("last_commit")
            )
        lane["success_since_review"] = int(lane.get("success_since_review", 0)) + 1
        reasons = self.review_reasons(lane_name)
        if checkpoint_due(int(lane.get("success_since_review", 0))):
            reasons.append("three successful phases completed since the last Sol review")
        next_phase = self.planned_next_phase(lane_name, int(lane.get("phase_index", 0)) + 1)
        if reasons and next_phase is not None:
            self.review(
                lane_name,
                reasons,
                review_type="POST_PHASE_CHECKPOINT",
                reviewed_phase=completed_phase,
                commit_sha=lane.get("last_commit"),
                next_phase=next_phase,
            )
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
            self.review(
                lane_name,
                ["authoritative lane queue completed; final milestone/queue review required"],
                review_type="FINAL_MILESTONE_OR_QUEUE_REVIEW",
                reviewed_phase=completed_phase,
                commit_sha=lane.get("last_commit"),
                next_phase=None,
            )
        elif self.state.get("global_mode") == "RUNNING":
            lane["state"] = "READY"
            self.event(f"phase complete; next phase {lane['phase_id']}", lane_name)
        else:
            lane["state"] = "PAUSED"
            self.event("phase complete; remaining work paused", lane_name)
        if "scheduler" in self.state:
            self._recompute_scheduler()

    def review(
        self,
        lane_name: str,
        reasons: list[str],
        review_type: str | None = None,
        reviewed_phase: dict[str, Any] | None = None,
        commit_sha: str | None = None,
        next_phase: dict[str, Any] | None = None,
    ) -> None:
        lane = self.state["lanes"][lane_name]
        clean = sorted(set(redact(reason) for reason in reasons if reason))
        lane["review_reasons"] = clean or ["human review requested"]
        if review_type is None:
            if any("final milestone" in reason or "queue completed" in reason for reason in lane["review_reasons"]):
                review_type = "FINAL_MILESTONE_OR_QUEUE_REVIEW"
            else:
                review_type = "CURRENT_PHASE_REVIEW"
        current_phase = reviewed_phase or {
            "id": lane.get("phase_id"),
            "title": lane.get("phase_title"),
        }
        if next_phase is None and review_type == "CURRENT_PHASE_REVIEW":
            next_phase = {
                "id": lane.get("phase_id"),
                "title": lane.get("phase_title"),
                "action": "retry_current_phase",
            }
        lane["review"] = {
            "type": review_type,
            "reviewed_phase": current_phase,
            "commit_sha": commit_sha if commit_sha is not None else lane.get("last_commit"),
            "next_phase": next_phase,
            "created_at": utc_now(),
            "decision": None,
        }
        lane["state"] = "NEEDS_SOL_REVIEW"
        lane["worker_pid"] = None
        if "scheduler" in self.state and lane.get("scheduler_task_id"):
            record = self.state["scheduler"].get("tasks", {}).get(
                lane.get("scheduler_task_id")
            )
            if isinstance(record, dict):
                record["state"] = "NEEDS_SOL_REVIEW"
                record["reason"] = "; ".join(lane["review_reasons"])
                record["review_type"] = review_type
                self._audit(
                    "review_requested",
                    task_id=lane.get("scheduler_task_id"),
                    review_type=review_type,
                    reasons=lane["review_reasons"],
                )
        packet = self.make_review_packet(lane_name, lane["review_reasons"])
        lane["review_packet"] = str(packet)
        self.post_issue_update(lane_name, lane["review_reasons"])
        self.event("lane stopped for Sol review", lane_name)

    def approve_review(self, lane_name: str) -> int:
        lane = self.state["lanes"].get(lane_name)
        if lane is None:
            print(f"unknown lane: {lane_name}", file=sys.stderr)
            return 2
        if lane.get("state") != "NEEDS_SOL_REVIEW":
            print(f"{lane_name} is not awaiting Sol review", file=sys.stderr)
            return 1
        review = lane.get("review", {})
        review_type = review.get("type")
        if review_type == "CURRENT_PHASE_REVIEW":
            print(
                f"refusing approve for {lane_name}: current phase is not complete; use retry {lane_name}",
                file=sys.stderr,
            )
            return 1
        if review_type == "FINAL_MILESTONE_OR_QUEUE_REVIEW":
            review["decision"] = "APPROVED_NO_AUTOMATIC_NEXT_WORK"
            review["decided_at"] = utc_now()
            lane["state"] = "BLOCKED"
            self.event("final/queue review approved; no automatic next work exists", lane_name)
            return 0
        if review_type != "POST_PHASE_CHECKPOINT":
            print(f"refusing approve for {lane_name}: unknown review type {review_type}", file=sys.stderr)
            return 1
        reviewed_phase = review.get("reviewed_phase") or {}
        if reviewed_phase.get("id") != lane.get("phase_id"):
            print(f"refusing approve for {lane_name}: review phase no longer matches", file=sys.stderr)
            return 1
        lane["phase_index"] = int(lane.get("phase_index", 0)) + 1
        lane["success_since_review"] = 0
        lane["phase_failure_count"] = 0
        lane["recovery_attempts"] = 0
        lane["review_reasons"] = []
        review["decision"] = "APPROVED_ADVANCE_ONCE"
        review["decided_at"] = utc_now()
        self._refresh_plan(lane_name, lane)
        if lane.get("phase_id") is None:
            lane["state"] = "BLOCKED"
        elif self.state.get("global_mode") == "RUNNING":
            lane["state"] = "READY"
        else:
            lane["state"] = "PAUSED"
        self.event(
            f"POST_PHASE_CHECKPOINT approved; advanced once to {lane.get('phase_id') or 'queue complete'}",
            lane_name,
        )
        return 0

    def retry_review(self, lane_name: str) -> int:
        lane = self.state["lanes"].get(lane_name)
        if lane is None:
            print(f"unknown lane: {lane_name}", file=sys.stderr)
            return 2
        if lane.get("state") != "NEEDS_SOL_REVIEW":
            print(f"{lane_name} is not awaiting Sol review", file=sys.stderr)
            return 1
        review = lane.get("review", {})
        if review.get("type") != "CURRENT_PHASE_REVIEW":
            print(f"retry is only valid for CURRENT_PHASE_REVIEW ({lane_name})", file=sys.stderr)
            return 1
        review["decision"] = "RETRY_CURRENT_PHASE"
        review["decided_at"] = utc_now()
        lane["recovery_context"] = self._capture_recovery_context(lane_name)
        lane["review_reasons"] = []
        lane["review_packet"] = None
        lane["recovery_attempts"] = 0
        lane["last_error"] = "operator requested explicit retry of current phase"
        if self.state.get("global_mode") == "RUNNING":
            lane["state"] = "RECOVERING"
        else:
            lane["paused_from_state"] = "RECOVERING"
            lane["state"] = "PAUSED"
        self.event("operator authorized retry of current phase", lane_name)
        return 0

    def block_lane(self, lane_name: str) -> int:
        lane = self.state["lanes"].get(lane_name)
        if lane is None:
            print(f"unknown lane: {lane_name}", file=sys.stderr)
            return 2
        lane["state"] = "BLOCKED"
        lane.setdefault("review", {})["decision"] = "BLOCKED_BY_OPERATOR"
        lane["review"]["decided_at"] = utc_now()
        self.event("lane blocked by operator", lane_name)
        return 0

    def approve_task(self, task_id: str) -> int:
        scheduler = self.state.get("scheduler", {})
        record = scheduler.get("tasks", {}).get(task_id)
        if not isinstance(record, dict) or record.get("source") != "ROADMAP":
            print(f"unknown roadmap task: {task_id}", file=sys.stderr)
            return 2
        review = scheduler.get("reviews", {}).get(task_id, {})
        if record.get("state") != "NEEDS_SOL_REVIEW" or review.get("type") != "PRE_TASK_ROADMAP_REVIEW":
            print(f"{task_id} is not awaiting PRE_TASK_ROADMAP_REVIEW", file=sys.stderr)
            return 1
        if not self._control_plane_valid():
            print("control-plane snapshot is not valid", file=sys.stderr)
            return 1
        task = self.roadmap_snapshot.tasks.get(task_id) if self.roadmap_snapshot else None
        if task is None:
            print(f"task definition is unavailable: {task_id}", file=sys.stderr)
            return 1
        approval = {
            "task_id": task.task_id,
            "control_plane_sha": task.control_plane_sha,
            "definition_hash": task.definition_hash,
            "approved_at": utc_now(),
            "decision": "APPROVED_EXACT_TASK_DEFINITION",
        }
        scheduler.setdefault("approvals", {})[task_id] = approval
        review["decision"] = approval["decision"]
        review["decided_at"] = approval["approved_at"]
        self._audit(
            "task_approval_granted",
            task_id=task_id,
            control_plane_sha=task.control_plane_sha,
            definition_hash=task.definition_hash,
        )
        self._recompute_scheduler()
        self.save_state()
        print(f"approved {task_id} at control-plane SHA {task.control_plane_sha}")
        return 0

    def accept_milestone(self, milestone_id: str, evidence: Mapping[str, Any] | None = None) -> int:
        self._recompute_scheduler()
        milestone = self.state.get("scheduler", {}).get("milestones", {}).get(milestone_id)
        if not isinstance(milestone, dict):
            print(f"unknown milestone: {milestone_id}", file=sys.stderr)
            return 2
        if milestone.get("state") != "WAITING_RUNTIME_ACCEPTANCE":
            print(
                f"{milestone_id} is {milestone.get('state')}; explicit runtime acceptance is not available",
                file=sys.stderr,
            )
            return 1
        evidence = dict(evidence or {})
        platform = str(evidence.get("runtime_platform", "")).lower()
        required_flags = (
            "character_select",
            "humanoid_suppression",
            "synchronized_creature_encounter",
            "encounter_clear_reset_reentry",
            "stale_incarnation_rejection",
            "reconnect_server_character",
        )
        missing = [key for key in required_flags if not evidence.get(key)]
        try:
            clients = int(evidence.get("clients", 0))
        except (TypeError, ValueError):
            clients = 0
        if "windows" not in platform or "skyrim" not in platform:
            missing.append("runtime_platform=Windows Skyrim")
        if clients < 2:
            missing.append("clients>=2")
        if not evidence.get("human_reviewed") or not str(evidence.get("reviewed_by", "")).strip():
            missing.append("human_reviewed and reviewed_by")
        if missing:
            print("runtime acceptance evidence is incomplete: " + ", ".join(missing), file=sys.stderr)
            return 1
        milestone.update({
            "state": "ACCEPTED",
            "accepted_at": utc_now(),
            "accepted_by": redact(str(evidence.get("reviewed_by")))[:200],
            "runtime_evidence": {
                key: (bool(value) if key in required_flags + ("human_reviewed",) else value)
                for key, value in evidence.items()
                if key not in {"logs", "raw_log", "massive_output"}
            },
        })
        self._audit(
            "milestone_transition",
            milestone_id=milestone_id,
            from_state="WAITING_RUNTIME_ACCEPTANCE",
            to_state="ACCEPTED",
        )
        self.save_state()
        print(f"accepted milestone {milestone_id} with reviewed Windows Skyrim evidence")
        return 0

    def make_task_review_packet(self, task: ScheduledTask, reason: str) -> Path:
        """Write bounded metadata for a roadmap-native pre-task Sol gate."""
        REVIEW_ROOT.mkdir(parents=True, exist_ok=True)
        safe_id = re.sub(r"[^A-Za-z0-9_.-]+", "-", task.task_id)
        sha = task.control_plane_sha or "unknown"
        path = REVIEW_ROOT / f"roadmap-{safe_id}-{sha[:12]}.md"
        body = f"""# Roadmap pre-task review: {task.task_id}

Generated: {utc_now()}
Review type: PRE_TASK_ROADMAP_REVIEW
Control-plane SHA: {sha}
Definition hash: {task.definition_hash}

## Task

- milestone: {task.milestone_id}
- workstream: {task.workstream_id}
- lane: {task.lane}
- source: {task.source}
- title: {task.title}
- risk: {task.risk}
- dependencies: {json.dumps(task.dependencies, sort_keys=True)}
- external gates: {json.dumps(task.external_gates, sort_keys=True)}

## Acceptance criteria

{chr(10).join('- ' + item for item in task.acceptance_criteria) or '- none recorded'}

## Why pre-review is required

{redact(reason)}

The approval is valid only for task {task.task_id} at control-plane SHA {sha}
and definition hash {task.definition_hash}.  A later control-plane change
invalidates the approval before any worker can start.
"""
        atomic_write_text(path, body, 0o640)
        return path

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
        validation = lane.get("validation", {})
        structural = validation.get("structural", {})
        focused = validation.get("focused_tests", {})
        structural_text = "\n".join([
            f"- structural status: {structural.get('status', 'not recorded')}",
            f"- unstaged diff check observed: {structural.get('unstaged_diff_check', 'not recorded')}",
            f"- staged diff check observed: {structural.get('staged_diff_check', 'not recorded')}",
        ])
        if focused.get("commands"):
            focused_text = "\n".join(
                f"- {item.get('command')} -> exit {item.get('exit_code')}"
                for item in focused.get("commands", [])
            )
        else:
            focused_text = f"- {focused.get('message', 'focused tests: not independently verified by supervisor')}"
        ci = lane.get("ci", {})
        required_ci = ci.get("required_workflows", [])
        ci_text = "\n".join(
            f"- {item.get('workflow')}: run={item.get('run_id') or 'none'} "
            f"status={item.get('status') or 'unknown'} "
            f"conclusion={item.get('conclusion') or 'unknown'} "
            f"sha={item.get('sha') or 'unknown'} url={item.get('url') or 'none'}"
            for item in required_ci
        ) or "- no required workflow result recorded"
        boundaries = "\n".join(
            f"- {item}" for item in self.plan_for(lane_name).get("boundaries", [])
        ) or "- see authoritative PLAN.md"
        review_info = lane.get("review", {})
        next_phase = review_info.get("next_phase")
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
Applied control-plane SHA: {self.state.get('control_plane', {}).get('applied_sha') or 'none'}
Scheduler task identity: {lane.get('scheduler_task_id') or phase}

## Explicit review metadata

- review type: {review_info.get('type') or 'unknown'}
- reviewed phase: {json.dumps(review_info.get('reviewed_phase'), sort_keys=True)}
- commit SHA: {review_info.get('commit_sha') or 'none'}
- next phase/action: {json.dumps(next_phase, sort_keys=True) if next_phase else 'none'}

## Phases completed

{completed}

## Current meaningful diff

{changed}

Diff summary:

{lane.get('validation', {}).get('diffstat', 'not available')}

## Structural validation observed

{structural_text}

## Focused tests actually run

{focused_text}

Worker report tail (bounded):

---
{worker_tail}
---

## GitHub Actions result

- status: {ci.get('status', 'unknown')}
- conclusion: {ci.get('conclusion', 'unknown')}
- commit: {ci.get('sha', 'unknown')}
- run: {ci.get('url') or 'not available'}

Required workflow aggregation (exact commit SHA):

{ci_text}

## Security and trust decisions

Authoritative hard boundaries:

{boundaries}

The supervisor did not permit worker Git mutations. Client-supplied identity, damage,
progression, reward, kill attribution, population classification, and ownership remain
subject to the lane plan and evidence-backed review.

## Unresolved questions / why review was requested

{unresolved}

## Next planned phase

{next_phase.get('id') + ' - ' + next_phase.get('title', '') if isinstance(next_phase, dict) and next_phase.get('id') else 'none; queue is complete or retry current phase'}

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
        # Re-read operator control immediately before any state transition that
        # could reach validation, commit, push, CI completion, or next phase.
        self.refresh_control()
        lane = self.state["lanes"][lane_name]
        if lane_name in self.processes:
            return
        state = lane.get("state")
        if self.state.get("global_mode") != "RUNNING":
            if state in {"LOCAL_VALIDATION", "COMMITTING", "PUSHING", "NEXT_PHASE", "RECOVERING", "READY"}:
                lane["state"] = "PAUSED"
            return
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
        availability = self.state.get("codex_availability", {})
        if "scheduler" in self.state:
            self._recompute_scheduler()
            if not self._control_plane_valid():
                return
            scheduler = self.state["scheduler"]
            attempted: set[str] = set()

            def choose() -> str | None:
                ready = []
                for lane_name in LANE_ORDER:
                    lane = self.state["lanes"].get(lane_name, {})
                    record = self._scheduled_task_for_lane(lane_name)
                    if (
                        lane.get("state") in RUNNABLE_STATES
                        and record
                        and record.get("state") == "READY"
                        and lane_name not in self.processes
                        and lane_name not in attempted
                    ):
                        ready.append(lane_name)
                lane_name, cursor = next_round_robin_lane(
                    ready, LANE_ORDER, int(scheduler.get("cursor", 0))
                )
                if lane_name is not None:
                    scheduler["cursor"] = cursor
                return lane_name

            if availability.get("status") == "RATE_LIMITED":
                retry_at = parse_time(availability.get("next_retry_at"))
                if retry_at and time.time() < retry_at:
                    return
                if len(self.processes) >= limit:
                    return
                lane_name = choose()
                if lane_name is not None:
                    self._audit("task_selected", lane=lane_name, reason="rate-limit probe")
                    self.start_worker(
                        lane_name,
                        recovery=self.state["lanes"][lane_name].get("state") == "RECOVERING",
                    )
                return

            while len(self.processes) < limit:
                lane_name = choose()
                if lane_name is None:
                    break
                attempted.add(lane_name)
                record = self._scheduled_task_for_lane(lane_name)
                self._audit(
                    "task_selected",
                    task_id=record.get("task_id") if record else None,
                    lane=lane_name,
                    reason="round-robin READY task",
                )
                if not self.start_worker(
                    lane_name,
                    recovery=self.state["lanes"][lane_name].get("state") == "RECOVERING",
                ):
                    self._recompute_scheduler()
            return

        # V2.1 compatibility path used only by isolated legacy tests/state.
        if availability.get("status") == "RATE_LIMITED":
            retry_at = parse_time(availability.get("next_retry_at"))
            if retry_at and time.time() < retry_at:
                return
            if len(self.processes) >= limit:
                return
            for lane_name in LANE_ORDER:
                state = self.state["lanes"][lane_name].get("state")
                if state in RUNNABLE_STATES:
                    # A single retry probe is allowed after the persisted backoff.
                    self.start_worker(
                        lane_name,
                        recovery=state == "RECOVERING",
                    )
                    return
            return
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
        self.sync_control_plane()
        self.periodic_guards()
        self._recompute_scheduler()
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
        self._recompute_scheduler()
        print(f"GLOBAL: {self.state.get('global_mode')} ({self.state.get('mode_reason', '')})")
        availability = self.state.get("codex_availability", {})
        print(
            f"CODEX: {availability.get('status', 'UNKNOWN')} "
            f"next retry: {availability.get('next_retry_at') or 'none'} "
            f"backoff: {availability.get('backoff_seconds', 0)}s"
        )
        code, out, err = self.command(["systemctl", "is-active", UNIT_NAME], timeout=20)
        print(f"SERVICE: {out.strip() or err.strip() or 'unknown'}")
        control = self.state.get("control_plane", {})
        print("CONTROL PLANE")
        print(f"branch: {control.get('branch') or 'unknown'}")
        print(f"applied SHA: {control.get('applied_sha') or 'none'}")
        print(f"observed SHA: {control.get('observed_sha') or 'none'}")
        print(f"status: {control.get('status') or 'unknown'}")
        print(f"last sync: {control.get('last_checked_at') or 'never'}")
        print(f"validation error: {control.get('validation_error') or 'none'}")
        milestone_id = self.roadmap_snapshot.current_milestone if self.roadmap_snapshot else "M01"
        milestone = self.state.get("scheduler", {}).get("milestones", {}).get(milestone_id, {})
        print("MILESTONE")
        print(f"{milestone_id} {milestone.get('title') or 'Core World'}")
        print(f"state: {milestone.get('state') or 'BLOCKED'}")
        print()
        for lane_name in LANE_ORDER:
            lane = self.state["lanes"][lane_name]
            ci = lane.get("ci", {})
            phase = lane.get("phase_id") or "none"
            title = lane.get("phase_title") or "queue complete/review"
            print(lane_name.upper())
            print(f"branch: {lane.get('branch')}")
            print(f"current phase: {phase} - {title}")
            print(f"state: {lane.get('state')}")
            print(
                f"scheduler: {lane.get('scheduler_state') or 'unknown'} "
                f"task={lane.get('scheduler_task_id') or 'none'} "
                f"reason={lane.get('scheduler_reason') or 'none'}"
            )
            print(f"worker pid: {lane.get('worker_pid') or 'none'}")
            print(f"last commit: {lane.get('last_commit') or 'none'}")
            print(
                f"CI: {ci.get('status', 'unknown')} "
                f"{ci.get('conclusion') or ''} {ci.get('url') or ''}".rstrip()
            )
            for workflow in ci.get("required_workflows", []):
                print(
                    f"  CI workflow {workflow.get('workflow')}: "
                    f"run={workflow.get('run_id') or 'none'} "
                    f"status={workflow.get('status') or 'unknown'} "
                    f"conclusion={workflow.get('conclusion') or 'unknown'}"
                )
            print(f"last error: {lane.get('last_error') or 'none'}")
            review = lane.get("review", {})
            if review.get("type"):
                print(
                    f"review: {review.get('type')} phase={review.get('reviewed_phase')} "
                    f"next={review.get('next_phase') or 'none'}"
                )
            if lane.get("review_packet"):
                print(f"review packet: {lane['review_packet']}")
            print()
        print("FUTURE BLOCKED WORK")
        for task_id, record in self.state.get("scheduler", {}).get("tasks", {}).items():
            if isinstance(record, dict) and record.get("source") == "ROADMAP" and record.get("state") != "DONE":
                print(f"{task_id} {record.get('state')}: {record.get('reason') or 'none'}")
        return 0

    def roadmap_status(self) -> int:
        self.sync_control_plane(force=True)
        self._recompute_scheduler()
        control = self.state.get("control_plane", {})
        print("CONTROL PLANE")
        print(f"branch: {control.get('branch') or 'unknown'}")
        print(f"applied SHA: {control.get('applied_sha') or 'none'}")
        print(f"observed SHA: {control.get('observed_sha') or 'none'}")
        print(f"valid: {'yes' if self._control_plane_valid() else 'no'}")
        print(f"status: {control.get('status') or 'unknown'}")
        print(f"last sync: {control.get('last_checked_at') or 'never'}")
        print(f"validation error: {control.get('validation_error') or 'none'}")
        if self.roadmap_snapshot is None:
            return 1
        print(f"CURRENT MILESTONE: {self.roadmap_snapshot.current_milestone}")
        for milestone_id, milestone in self.roadmap_snapshot.milestones.items():
            state = self.state.get("scheduler", {}).get("milestones", {}).get(milestone_id, {}).get(
                "state", milestone.get("status")
            )
            print(
                f"MILESTONE {milestone_id}: {state} "
                f"executable={milestone.get('executable')} title={milestone.get('title')}"
            )
            for workstream_id, workstream in self.roadmap_snapshot.workstreams.items():
                if workstream.get("milestone_id") != milestone_id:
                    continue
                ws_state = self.state.get("scheduler", {}).get("workstreams", {}).get(
                    workstream_id, workstream.get("status")
                )
                print(
                    f"  {workstream_id}: lane={workstream.get('lane')} "
                    f"mode={workstream.get('mode')} state={ws_state} "
                    f"start_phase={workstream.get('start_phase') or 'none'}"
                )
        print("TASKS")
        for task_id, record in sorted(self.state.get("scheduler", {}).get("tasks", {}).items()):
            if not isinstance(record, dict):
                continue
            print(
                f"{task_id} {record.get('state')}: lane={record.get('lane')} "
                f"source={record.get('source')} reason={record.get('reason') or 'none'}"
            )
        return 0

    def milestone_status(self) -> int:
        self._recompute_scheduler()
        if self.roadmap_snapshot is None:
            print("MILESTONE: unavailable; control plane is not valid")
            return 1
        for milestone_id, milestone in self.roadmap_snapshot.milestones.items():
            record = self.state.get("scheduler", {}).get("milestones", {}).get(milestone_id, {})
            print(f"{milestone_id} {milestone.get('title')}: {record.get('state', milestone.get('status'))}")
            if milestone.get("acceptance"):
                print("  acceptance criteria:")
                for criterion in milestone["acceptance"]:
                    print(f"    - {criterion}")
            if record.get("state") == "WAITING_RUNTIME_ACCEPTANCE":
                print("  runtime evidence: required; no Linux evidence is implied")
            if record.get("state") == "ACCEPTED":
                print(f"  accepted at: {record.get('accepted_at') or 'recorded'}")
        return 0

    def review_status(self) -> int:
        print(f"GLOBAL: {self.state.get('global_mode')}")
        for lane_name in LANE_ORDER:
            lane = self.state["lanes"][lane_name]
            review = lane.get("review", {})
            print(f"{lane_name}: state={lane.get('state')} review_type={review.get('type') or 'none'}")
            if review.get("type"):
                print(f"  reviewed_phase: {json.dumps(review.get('reviewed_phase'), sort_keys=True)}")
                print(f"  commit_sha: {review.get('commit_sha') or 'none'}")
                print(f"  next_phase: {json.dumps(review.get('next_phase'), sort_keys=True) if review.get('next_phase') else 'none'}")
                print(f"  reasons: {'; '.join(lane.get('review_reasons', [])) or 'none'}")
                print(f"  decision: {review.get('decision') or 'pending'}")
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
        control_ok = self.sync_control_plane(force=True)
        if not control_ok:
            control = self.state.get("control_plane", {})
            print(
                "FAIL control plane: "
                + str(control.get("validation_error") or control.get("status") or "invalid")
            )
            return 1
        print("PASS resource guard")
        print("PASS git/worktree health")
        print("PASS control plane")
        print(f"PASS state persistence: {STATE_PATH}")
        product_root = self._active_product_root()
        missing_product = [
            name for name in (
                "PRODUCT_VISION.md", "WORLD_RULES.md", "MILESTONE_01_CORE_WORLD.md"
            ) if not (product_root / name).is_file()
        ]
        if missing_product:
            print("FAIL product context: " + ", ".join(missing_product))
            return 1
        print("PASS product context")
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
        control_ok = self.sync_control_plane(force=True)
        check("control-plane branch, worktree, and roadmap validation", control_ok)
        okay, _ = self.resource_guard()
        check("disk and memory guard", okay)
        worker_environment = self.worker_env()
        check(
            "worker GitHub credential isolation",
            worker_environment.get("GH_CONFIG_DIR") == self.config.get("worker_gh_config_dir")
            and not any(key in worker_environment for key in (
                "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN",
                "GITHUB_ENTERPRISE_TOKEN", "SSH_AUTH_SOCK",
            )),
        )
        required_workflows = self.config.get("required_workflows", [])
        check(
            "required CI workflow configuration",
            required_workflows == ["Build linux", "Build windows"],
        )
        worker_config_dir = Path(str(self.config.get("worker_gh_config_dir", "")))
        check(
            "empty worker GitHub config directory",
            worker_config_dir.is_dir()
            and not any(worker_config_dir.iterdir()),
        )
        check(
            "Codex usage-limit classifier safety",
            classify_codex_usage_limit(1, "Codex: usage limit reached; resets in 15 minutes")
            and not classify_codex_usage_limit(1, "GitHub API rate limit exceeded"),
        )
        product_root = self._active_product_root()
        check(
            "product context files",
            all((product_root / name).is_file() for name in (
                "PRODUCT_VISION.md", "WORLD_RULES.md", "MILESTONE_01_CORE_WORLD.md"
            )),
        )
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
    if pwd is not None:
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
    sub.add_parser("roadmap-status")
    sub.add_parser("milestone-status")
    sub.add_parser("sync-control-plane")
    sub.add_parser("review-status")
    sub.add_parser("healthcheck")
    sub.add_parser("self-test")
    for command_name in ("approve", "retry", "block"):
        command_parser = sub.add_parser(command_name)
        command_parser.add_argument("lane", choices=LANE_ORDER)
    approve_task_parser = sub.add_parser("approve-task")
    approve_task_parser.add_argument("task_id")
    approve_control_parser = sub.add_parser("approve-control-plane")
    approve_control_parser.add_argument("sha")
    accept_parser = sub.add_parser("accept-milestone")
    accept_parser.add_argument("milestone_id")
    accept_parser.add_argument("--evidence-file")
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
    if args.command == "roadmap-status":
        return supervisor.roadmap_status()
    if args.command == "milestone-status":
        return supervisor.milestone_status()
    if args.command == "sync-control-plane":
        return 0 if supervisor.sync_control_plane(force=True) else 1
    if args.command == "review-status":
        return supervisor.review_status()
    if args.command == "healthcheck":
        return supervisor.healthcheck()
    if args.command == "self-test":
        return supervisor.self_test()
    if args.command == "approve":
        result = supervisor.approve_review(args.lane)
        supervisor.save_state()
        return result
    if args.command == "retry":
        result = supervisor.retry_review(args.lane)
        supervisor.save_state()
        return result
    if args.command == "block":
        result = supervisor.block_lane(args.lane)
        supervisor.save_state()
        return result
    if args.command == "approve-task":
        return supervisor.approve_task(args.task_id)
    if args.command == "approve-control-plane":
        return supervisor.approve_control_plane(args.sha)
    if args.command == "accept-milestone":
        evidence: dict[str, Any] = {}
        if args.evidence_file:
            try:
                evidence = json.loads(Path(args.evidence_file).read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as exc:
                print(f"cannot read evidence file: {exc}", file=sys.stderr)
                return 2
        return supervisor.accept_milestone(args.milestone_id, evidence)

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    with LOCK_PATH.open("w", encoding="utf-8") as lock_handle:
        if fcntl is not None:
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
