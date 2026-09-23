#!/usr/bin/env python3
"""Deterministic supervisor for the Skyrim development lanes.

Codex workers edit ordinary source, tests, and documentation only. This trusted
outer process performs validation, Git operations, CI polling, recovery bounds,
resource guards, and review checkpoints.
"""
from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import difflib
import hashlib
import io
import json
import os
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from copy import deepcopy
from pathlib import Path
from typing import Any, Mapping

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
from architect_review import (
    ArchitectReviewMixin,
    REVIEW_OUTPUT_SCHEMA,
    ReviewOutputError,
    ReviewEvidenceError,
    build_bwrap_command,
    canonical_json,
    normalize_review_evidence,
    redact_secrets,
    review_failure_fingerprint,
    review_identity,
    review_prompt,
    sha256_json,
    should_recheck_legacy_blocked_retry,
    stable_review_state_sha256,
    validate_review_output,
)

SERVICE_ROOT = Path("/srv/services/skyrim-dev")
CONFIG_PATH = SERVICE_ROOT / "config" / "supervisor.json"
STATE_ROOT = Path("/var/lib/skyrim-dev")
STATE_DIR = STATE_ROOT / "state"
STATE_PATH = STATE_DIR / "state.json"
CONTROL_PATH = STATE_DIR / "control.json"
LOCK_PATH = STATE_DIR / "supervisor.lock"
OPERATOR_REQUEST_ROOT = STATE_ROOT / "operator-requests"
OPERATOR_REQUEST_INBOX = OPERATOR_REQUEST_ROOT / "inbox"
OPERATOR_REQUEST_RECEIPTS = OPERATOR_REQUEST_ROOT / "receipts"
OPERATOR_REQUEST_ARCHIVE = OPERATOR_REQUEST_ROOT / "archive"
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
TERMINAL_STATES = {
    "NEEDS_SOL_REVIEW", "BLOCKED",
    "WAITING_FOR_ARCHITECT_MODEL", "WAITING_FOR_LUNA_MODEL",
}
RUNNABLE_STATES = {"READY", "RECOVERING"}
REVIEW_TIERS = {"normal", "architect"}
REVIEW_WAITING_STATES = {
    "NEEDS_SOL_REVIEW",
    "WAITING_FOR_ARCHITECT_MODEL",
    "WAITING_FOR_LUNA_MODEL",
}

ANSI_RE = re.compile(r"\x1b(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")
SECRET_RE = re.compile(
    r"(?i)(?:gho_|ghp_|github_pat_|sk-[A-Za-z0-9_-]{12,}|"
    r"bearer\s+[A-Za-z0-9._~+/=-]+|"
    r"(?:token|oauth_token|access_token|password|secret)\s*[:=]\s*[^\s,;]+)"
)
PHASE_RE = re.compile(r"^([A-Z][0-9]+)\s+(.+?)\s*$")
RESULT_RE = re.compile(
    r"WORKER_RESULT:\s*(COMPLETE_WITH_VALIDATION_GAP|COMPLETE|NEEDS_SOL_REVIEW|BLOCKED)"
)
VALIDATION_GAP_RE = re.compile(r"VALIDATION_GAP:\s*(.+)")
CODEX_USAGE_RE = re.compile(
    r"(?i)(?:usage\s+limit|rate\s+limit|too\s+many\s+requests|quota\s+exhausted|"
    r"capacity\s+exhausted|try\s+again\s+later)"
)
CODEX_CONTEXT_RE = re.compile(r"(?i)(?:codex|chatgpt|openai|usage\s+allowance|token\s+budget|reset)")
GITHUB_LIMIT_RE = re.compile(r"(?i)(?:github|github\.com|gh\s+(?:api|run|issue))")
MODEL_UNAVAILABLE_RE = re.compile(
    r"(?i)(?:model\s+(?:is\s+)?(?:not\s+found|unavailable|not\s+available|unsupported|does\s+not\s+exist)|"
    r"unknown\s+model|invalid\s+model|model.*(?:access|permission).*(?:denied|forbidden)|"
    r"no\s+such\s+model)"
)
UNMERGED_XY = {"DD", "AU", "UD", "UA", "DU", "AA", "UU"}
GENERATED_DIRS = {"node_modules", "build", "dist", "out", ".xmake", "obj"}
MAX_UNTRACKED_REVIEW_FILE_BYTES = 64 * 1024
MAX_RECOVERY_ERROR_BYTES = 4000
MAX_RECOVERY_CI_BYTES = 8000
MAX_RECOVERY_WORKER_BYTES = 8000
MAX_ARCHITECT_REVIEW_FAILURES = 3
MAX_ARCHITECT_REVIEW_LOG_BYTES = 256 * 1024
MAX_ARCHITECT_REVIEW_BUNDLE_BYTES = 2 * 1024 * 1024
ARCHITECT_REVIEW_ROOT = Path("/var/lib/skyrim-review")
OPERATOR_REQUEST_VERSION = 1
OPERATOR_MUTATION_COMMANDS = {
    "approve",
    "retry",
    "block",
    "approve-task",
    "approve-control-plane",
    "accept-milestone",
    "sync-control-plane",
    "re-review-v3",
}
OPERATOR_REQUEST_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")


def classify_codex_model_unavailable(return_code: int, output: str) -> bool:
    """Classify an unavailable model separately from engineering failures."""
    if return_code == 0:
        return False
    return bool(MODEL_UNAVAILABLE_RE.search(ANSI_RE.sub("", output or "")))


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
    return redact_secrets(SECRET_RE.sub("[redacted]", ANSI_RE.sub("", text)))


def redact_value(value: Any) -> Any:
    if isinstance(value, str):
        return redact(value)
    if isinstance(value, list):
        return [redact_value(item) for item in value]
    if isinstance(value, dict):
        return {str(key): redact_value(item) for key, item in value.items()}
    return value


def atomic_write_json(path: Path, data: Any, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp-{os.getpid()}-{threading.get_ident()}")
    try:
        with tmp.open("w", encoding="utf-8") as handle:
            json.dump(data, handle, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except OSError:
            pass
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass


def atomic_write_text(path: Path, text: str, mode: int = 0o640) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp-{os.getpid()}-{threading.get_ident()}")
    try:
        with tmp.open("w", encoding="utf-8") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except OSError:
            pass
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass


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


def operator_request_paths(config: Mapping[str, Any] | None = None) -> dict[str, Path]:
    configured = config.get("operator_request_root") if isinstance(config, dict) else None
    root = Path(str(configured)) if configured else OPERATOR_REQUEST_ROOT
    if not root.is_absolute():
        root = OPERATOR_REQUEST_ROOT
    return {
        "root": root,
        "inbox": root / "inbox",
        "receipts": root / "receipts",
        "archive": root / "archive",
    }


def ensure_operator_request_dirs(config: Mapping[str, Any] | None = None) -> dict[str, Path]:
    paths = operator_request_paths(config)
    for path in paths.values():
        path.mkdir(parents=True, exist_ok=True)
        try:
            os.chmod(path, 0o700)
        except OSError:
            pass
    return paths


def _valid_operator_request_id(value: Any) -> bool:
    return isinstance(value, str) and bool(OPERATOR_REQUEST_ID_RE.fullmatch(value))


def validate_operator_request(
    payload: Any,
    *,
    now: float | None = None,
    max_age_seconds: int = 900,
) -> tuple[bool, str, str]:
    """Validate the durable request envelope before it reaches Supervisor state."""
    if not isinstance(payload, dict):
        return False, "", "request envelope is not an object"
    request_id = payload.get("request_id")
    if not _valid_operator_request_id(request_id):
        return False, str(request_id or ""), "request_id is missing or unsafe"
    if payload.get("version") != OPERATOR_REQUEST_VERSION:
        return False, request_id, "unsupported request version"
    command = payload.get("command")
    if command not in OPERATOR_MUTATION_COMMANDS:
        return False, request_id, "unsupported operator command"
    args = payload.get("args")
    if not isinstance(args, dict):
        return False, request_id, "request args must be an object"
    created_at = payload.get("created_at")
    created_timestamp = parse_time(created_at if isinstance(created_at, str) else None)
    if not created_timestamp:
        return False, request_id, "created_at is missing or invalid"
    current_time = time.time() if now is None else now
    if created_timestamp > current_time + 60:
        return False, request_id, "request timestamp is too far in the future"
    if current_time - created_timestamp > max(1, int(max_age_seconds)):
        return False, request_id, "request is stale"
    if command in {"approve", "retry", "block"}:
        if set(args) != {"lane"} or args.get("lane") not in LANE_ORDER:
            return False, request_id, "lane command requires one known lane"
    elif command == "approve-task":
        if set(args) != {"task_id"} or not isinstance(args.get("task_id"), str) or not args["task_id"].strip():
            return False, request_id, "approve-task requires a task_id"
    elif command == "approve-control-plane":
        sha = args.get("sha")
        if set(args) != {"sha"} or not isinstance(sha, str) or not re.fullmatch(r"[0-9a-fA-F]{7,64}", sha):
            return False, request_id, "approve-control-plane requires a Git SHA"
    elif command == "accept-milestone":
        if set(args) != {"milestone_id", "evidence"}:
            return False, request_id, "accept-milestone requires milestone_id and evidence"
        if not isinstance(args.get("milestone_id"), str) or not args["milestone_id"].strip():
            return False, request_id, "accept-milestone requires a milestone_id"
        if not isinstance(args.get("evidence"), dict):
            return False, request_id, "milestone evidence must be an object"
    elif command == "sync-control-plane":
        if set(args) != {"force"} or not isinstance(args.get("force"), bool):
            return False, request_id, "sync-control-plane requires a boolean force flag"
    elif command == "re-review-v3":
        targets = args.get("targets")
        if set(args) != {"targets"} or not isinstance(targets, list) or len(targets) != len(LANE_ORDER):
            return False, request_id, "re-review-v3 requires four exact lane/phase/SHA targets"
        seen = set()
        for target in targets:
            if not isinstance(target, dict) or set(target) != {"lane", "phase", "sha"}:
                return False, request_id, "re-review-v3 target schema is invalid"
            if target.get("lane") not in LANE_ORDER or target.get("lane") in seen:
                return False, request_id, "re-review-v3 lane target is unknown or duplicated"
            if not isinstance(target.get("phase"), str) or not target["phase"].strip():
                return False, request_id, "re-review-v3 phase is required"
            if not isinstance(target.get("sha"), str) or not re.fullmatch(r"[0-9a-f]{40,64}", target["sha"]):
                return False, request_id, "re-review-v3 requires a full Git SHA"
            seen.add(target["lane"])
        if seen != set(LANE_ORDER):
            return False, request_id, "re-review-v3 must cover all four active lanes"
    return True, request_id, ""


def daemon_is_active() -> tuple[bool, str]:
    try:
        result = subprocess.run(
            ["systemctl", "is-active", "--quiet", UNIT_NAME],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, f"cannot verify supervisor service: {exc}"
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "inactive").strip()
        return False, f"supervisor service is not active ({detail})"
    return True, ""


def submit_operator_request(
    command: str,
    args: dict[str, Any],
    *,
    request_id: str | None = None,
    wait_seconds: int | None = None,
    config: Mapping[str, Any] | None = None,
) -> int:
    """Submit a mutation and wait only for the daemon's durable receipt."""
    if command not in OPERATOR_MUTATION_COMMANDS:
        print(f"unsupported operator command: {command}", file=sys.stderr)
        return 2
    active, reason = daemon_is_active()
    if not active:
        print(f"operator mutation refused: {reason}; no offline state write is performed", file=sys.stderr)
        return 1
    request_id = request_id or uuid.uuid4().hex
    payload = {
        "version": OPERATOR_REQUEST_VERSION,
        "request_id": request_id,
        "command": command,
        "args": args,
        "created_at": utc_now(),
        "submitted_by": str(os.getuid()) if hasattr(os, "getuid") else "operator",
    }
    valid, _, validation_error = validate_operator_request(payload, max_age_seconds=10**9)
    if not valid:
        print(f"operator request rejected locally: {validation_error}", file=sys.stderr)
        return 2
    paths = ensure_operator_request_dirs(config)
    receipt_path = paths["receipts"] / f"{request_id}.json"
    existing_receipt = read_json(receipt_path, None)
    if isinstance(existing_receipt, dict):
        receipt = existing_receipt
    else:
        request_path = paths["inbox"] / f"{request_id}.json"
        if not request_path.exists():
            atomic_write_json(request_path, payload, 0o600)
        timeout = wait_seconds
        if timeout is None:
            configured = (config or {}).get("operator_request_timeout_seconds", 45)
            try:
                timeout = max(1, int(configured))
            except (TypeError, ValueError):
                timeout = 45
        deadline = time.monotonic() + timeout
        receipt = None
        while time.monotonic() < deadline:
            candidate = read_json(receipt_path, None)
            if isinstance(candidate, dict):
                receipt = candidate
                break
            time.sleep(0.1)
        if receipt is None:
            print(
                f"operator request {request_id} timed out waiting for daemon receipt; "
                "the durable request was not replayed through an offline path",
                file=sys.stderr,
            )
            return 1
    if receipt.get("status") == "SUCCEEDED" and receipt.get("return_code") == 0:
        output = str(receipt.get("stdout") or "").rstrip()
        if output:
            print(output)
        else:
            print(f"operator request {request_id} succeeded")
        return 0
    error = str(receipt.get("stderr") or receipt.get("stdout") or "operator request failed")
    print(error.rstrip(), file=sys.stderr)
    return int(receipt.get("return_code") or 1)


def tail_text(path: Path, limit: int = 12000) -> str:
    try:
        data = path.read_bytes()
    except OSError:
        return ""
    return redact(data[-limit:].decode("utf-8", errors="replace"))


class Supervisor(ArchitectReviewMixin):
    def __init__(self, runtime_owner: bool = False) -> None:
        # Only the daemon that owns the process lock may reconcile persisted
        # runtime markers. CLI/healthcheck instances are observers by default.
        self.runtime_owner = bool(runtime_owner)
        self._state_write_enabled = self.runtime_owner
        self._deferred_save_depth = 0
        self._save_deferred = False
        self.config = read_json(CONFIG_PATH, {})
        if not self.config:
            raise RuntimeError(f"missing configuration: {CONFIG_PATH}")
        self.state = self._load_state()
        self.roadmap_snapshot: RoadmapSnapshot | None = None
        self.processes: dict[str, subprocess.Popen[str]] = {}
        self.output_threads: dict[str, threading.Thread] = {}
        self.review_process: subprocess.Popen[str] | None = None
        self.review_output_thread: threading.Thread | None = None
        self.active_review_id: str | None = None
        self.review_log_path: Path | None = None
        self.stop_requested = False
        self.last_git_health_check = 0.0
        self.last_resource_guard = 0.0
        self.last_save = 0.0
        self._prepare_state()

    @staticmethod
    def _availability_record() -> dict[str, Any]:
        return {
            "status": "AVAILABLE",
            "detected_at": None,
            "next_retry_at": None,
            "backoff_seconds": 0,
            "retry_count": 0,
            "probe_started_at": None,
            "reason": "",
            "configured_model": None,
            "selected_model": None,
            "selected_reasoning_effort": None,
            "fallback_used": False,
        }

    @staticmethod
    def _infer_model_family(model: str) -> str:
        lowered = model.strip().lower()
        if "luna" in lowered:
            return "luna"
        if "sol" in lowered:
            return "sol"
        return "unknown"

    def _development_route(self) -> dict[str, str]:
        routing = self.config.get("model_routing", {})
        configured = routing.get("development", {}) if isinstance(routing, dict) else {}
        if not isinstance(configured, dict):
            configured = {}
        model = str(
            configured.get("model")
            or self.config.get("development_model")
            or "gpt-6-luna"
        ).strip()
        effort = str(
            configured.get("reasoning_effort")
            or self.config.get("development_reasoning_effort")
            or "max"
        ).strip()
        family = str(configured.get("family") or self._infer_model_family(model)).strip().lower()
        if family != "luna":
            raise RuntimeError("development route must remain in the configured Luna model family")
        return {"model": model, "reasoning_effort": effort, "family": family}

    def _review_route(self, tier: str) -> dict[str, str]:
        if tier not in REVIEW_TIERS:
            raise ValueError(f"unknown review tier: {tier}")
        routing = self.config.get("model_routing", {})
        review_routing = routing.get("review", {}) if isinstance(routing, dict) else {}
        if not isinstance(review_routing, dict):
            review_routing = {}
        if tier == "normal":
            configured = review_routing.get("normal", {})
            if not isinstance(configured, dict):
                configured = {}
            legacy = self.config.get("architect_review", {})
            if not isinstance(legacy, dict):
                legacy = {}
            model = str(
                configured.get("model")
                or legacy.get("normal_model")
                or "gpt-6-luna"
            ).strip()
            family = str(
                configured.get("family") or self._infer_model_family(model)
            ).strip().lower()
            if family != "luna":
                raise RuntimeError("normal review route must remain in the configured Luna model family")
            effort = str(
                configured.get("reasoning_effort")
                or legacy.get("normal_reasoning_effort")
                or "high"
            ).strip()
            return {"model": model, "reasoning_effort": effort, "family": family}

        configured = review_routing.get("architect", {})
        if not isinstance(configured, dict):
            configured = {}
        legacy = self.config.get("architect_review", {})
        if not isinstance(legacy, dict):
            legacy = {}
        model = str(
            configured.get("model")
            or legacy.get("model")
            or "gpt-6-sol"
        ).strip()
        family = str(
            configured.get("family") or self._infer_model_family(model)
        ).strip().lower()
        if family != "sol":
            raise RuntimeError("architect review route must remain in the configured Sol model family")
        # Sol/max is forbidden for automatic review, including stale legacy config.
        return {"model": model, "reasoning_effort": "medium", "family": family}

    def _luna_fallback_routes(self, purpose: str) -> list[dict[str, str]]:
        routing = self.config.get("model_routing", {})
        configured = routing.get("luna_fallback_models", []) if isinstance(routing, dict) else []
        if not isinstance(configured, list):
            return []
        effort = (
            "max"
            if purpose == "development"
            else self._review_route("normal")["reasoning_effort"]
        )
        routes: list[dict[str, str]] = []
        for candidate in configured:
            if isinstance(candidate, str):
                model = candidate.strip()
                family = self._infer_model_family(model)
            elif isinstance(candidate, dict):
                model = str(candidate.get("model") or "").strip()
                family = str(candidate.get("family") or self._infer_model_family(model)).strip().lower()
            else:
                continue
            if model and family == "luna":
                routes.append({"model": model, "reasoning_effort": effort, "family": family})
        return routes

    def _review_availability(self, tier: str) -> dict[str, Any]:
        if tier not in REVIEW_TIERS:
            raise ValueError(f"unknown review tier: {tier}")
        review_state = self.state.setdefault("architect_review", {})
        legacy = review_state.get("availability")
        by_tier = review_state.setdefault("tier_availability", {})
        if not isinstance(by_tier, dict):
            by_tier = {}
            review_state["tier_availability"] = by_tier
        record = by_tier.get(tier)
        if not isinstance(record, dict):
            record = self._availability_record()
            if tier == "architect" and isinstance(legacy, dict):
                record.update(legacy)
            by_tier[tier] = record
        defaults = self._availability_record()
        for key, value in defaults.items():
            record.setdefault(key, value)
        # Keep the legacy flat field as an architect-only compatibility view.
        if tier == "architect":
            review_state["availability"] = record
        return record

    def _reviewer_limit(self) -> int:
        configured = self._review_cfg()
        try:
            return max(1, int(configured.get("max_concurrent_reviewers", 1)))
        except (TypeError, ValueError):
            return 1

    def _review_route_for_item(self, item: Mapping[str, Any]) -> dict[str, str]:
        tier = str(item.get("review_tier") or "architect")
        route = self._review_route(tier)
        selected_model = str(item.get("selected_model") or "").strip()
        if selected_model:
            family = self._infer_model_family(selected_model)
            configured_luna_models = {route["model"]}
            if tier == "normal":
                configured_luna_models.update(
                    candidate["model"] for candidate in self._luna_fallback_routes("review")
                )
            if family != "luna" or tier != "normal" or selected_model not in configured_luna_models:
                # Architect reviews never inherit an arbitrary persisted model;
                # normal reviews may inherit only the configured Luna model or
                # an explicit fallback that was previously probe-selected.
                selected_model = ""
            else:
                route["model"] = selected_model
                route["family"] = family
        return route

    def _probe_model_route(self, route: Mapping[str, str], purpose: str) -> tuple[bool, str]:
        """Run a harmless read-only probe; its result is the availability source of truth."""
        model = str(route.get("model") or "").strip()
        effort = str(route.get("reasoning_effort") or "").strip()
        if not model or str(route.get("family") or "") != "luna":
            return False, "candidate is not an explicit Luna route"
        with tempfile.TemporaryDirectory(prefix="skyrim-model-probe-") as probe_root:
            command = [
                "codex", "exec", "--model", model,
                "--config", f'model_reasoning_effort="{effort}"',
                "--config", 'approval_policy="never"',
                "--sandbox", "read-only", "--cd", probe_root,
                "--ephemeral", "--color", "never", "--skip-git-repo-check", "-",
            ]
            try:
                result = subprocess.run(
                    command,
                    cwd=probe_root,
                    env=self.worker_env(),
                    input="Reply with exactly MODEL_OK and do not use tools.",
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=max(10, int(self.config.get("model_probe_timeout_seconds", 60))),
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                return False, f"{purpose} Luna read-only probe failed: {type(exc).__name__}"
        output = redact(result.stdout or "")[-2000:]
        if result.returncode == 0 and "MODEL_OK" in output:
            return True, output
        return False, output or f"probe exited {result.returncode}"

    def _resolve_luna_route(self, purpose: str, availability: dict[str, Any]) -> dict[str, str] | None:
        configured_primary = self._development_route() if purpose == "development" else self._review_route("normal")
        selected = str(availability.get("selected_model") or "").strip()
        candidates = [configured_primary, *self._luna_fallback_routes(purpose)]
        if selected:
            selected_route = next((route for route in candidates if route["model"] == selected), None)
            if selected_route is not None:
                candidates.remove(selected_route)
                candidates.insert(0, selected_route)
        unique: list[dict[str, str]] = []
        seen: set[tuple[str, str]] = set()
        for route in candidates:
            identity = (route["model"], route["reasoning_effort"])
            if identity not in seen:
                seen.add(identity)
                unique.append(route)
        candidates = unique
        for route in candidates:
            ok, evidence = self._probe_model_route(route, purpose)
            if ok:
                availability.update({
                    "status": "AVAILABLE",
                    "next_retry_at": None,
                    "backoff_seconds": 0,
                    "retry_count": 0,
                    "probe_started_at": None,
                    "reason": "Luna capability probe succeeded",
                    "selected_model": route["model"],
                    "selected_reasoning_effort": route["reasoning_effort"],
                    "configured_model": configured_primary["model"],
                    "fallback_used": route["model"] != configured_primary["model"],
                })
                return route
            availability["reason"] = redact(evidence)[:1000]
        return None

    def _review_tier_for_request(
        self,
        lane_name: str,
        review_type: str,
        reasons: list[str],
        explicit_tier: str | None = None,
        explicit_reason: str | None = None,
    ) -> tuple[str, str | None]:
        if explicit_tier is not None:
            if explicit_tier not in REVIEW_TIERS:
                raise ValueError(f"unknown review tier: {explicit_tier}")
            return explicit_tier, redact(explicit_reason or "explicit architect escalation") if explicit_tier == "architect" else None
        if review_type == "FINAL_MILESTONE_OR_QUEUE_REVIEW":
            return "architect", "final integration/milestone architecture checkpoint"
        material = " ".join(reasons).lower()
        direct_patterns = (
            ("persistent database/schema or migration surface changed", "persistence schema or protocol authority boundary change"),
            ("protocol or wire-compatibility surface changed", "persistence schema or protocol authority boundary change"),
            ("cross-lane ownership boundary was crossed", "cross-lane integration architecture decision"),
            ("cross-lane integration", "cross-lane integration architecture decision"),
            ("cross-lane or forbidden path changed", "cross-lane integration architecture decision"),
        )
        for marker, reason in direct_patterns:
            if marker in material:
                return "architect", reason
        scheduler = self.state.get("scheduler", {})
        task_id = self.state.get("lanes", {}).get(lane_name, {}).get("scheduler_task_id")
        task_record = scheduler.get("tasks", {}).get(task_id, {}) if isinstance(scheduler, dict) else {}
        if isinstance(task_record, dict) and task_record.get("requires_sol_review"):
            return "architect", "roadmap explicitly requires stronger architectural review"
        # Security/trust/authority findings are initially given to Luna. They
        # escalate only when Luna reports that architecture judgement is needed.
        return "normal", None

    def _new_state(self) -> dict[str, Any]:
        return {
            "version": 6,
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
                "configured_model": None,
                "selected_model": None,
                "selected_reasoning_effort": None,
                "fallback_used": False,
            },
            "architect_review": {
                "enabled": True,
                "queue": [],
                "items": {},
                "phase_counters": {},
                "normal_retry_counters": {},
                "active_review_id": None,
                "availability": self._availability_record(),
                "tier_availability": {
                    "normal": self._availability_record(),
                    "architect": self._availability_record(),
                },
                "last_progress_at": utc_now(),
                "last_worker_completion_at": None,
                "last_review_completion_at": None,
                "last_idle_warning_at": None,
                "idle_reason": None,
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
            "processed_operator_requests": {},
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
            self.state["version"] = max(int(self.state.get("version", 1)), 6)
        except (TypeError, ValueError):
            self.state["version"] = 6
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
        availability.setdefault("configured_model", None)
        availability.setdefault("selected_model", None)
        availability.setdefault("selected_reasoning_effort", None)
        availability.setdefault("fallback_used", False)
        if self.runtime_owner:
            self._reconcile_startup_runtime()
        self.state.setdefault("lanes", {})
        if not isinstance(self.state.get("processed_operator_requests"), dict):
            self.state["processed_operator_requests"] = {}
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
                    "review_tier": None,
                    "selected_model": None,
                    "selected_reasoning_effort": None,
                    "escalation_reason": None,
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
                "worker_result": None,
                "worker_phase_id": None,
                "worker_start_head": None,
                "worker_exit_code": None,
                "worker_interrupted": False,
                "validation_gap": {"status": "NONE", "reason": "", "at": None},
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
                "review_tier": None,
                "selected_model": None,
                "selected_reasoning_effort": None,
                "escalation_reason": None,
            })
            if not isinstance(lane.get("review"), dict):
                lane["review"] = {
                    "type": None,
                    "reviewed_phase": None,
                    "commit_sha": None,
                    "next_phase": None,
                    "created_at": None,
                    "review_tier": None,
                    "selected_model": None,
                    "selected_reasoning_effort": None,
                    "escalation_reason": None,
                }
            review_metadata = lane["review"]
            review_metadata.setdefault("review_tier", None)
            review_metadata.setdefault("selected_model", None)
            review_metadata.setdefault("selected_reasoning_effort", None)
            review_metadata.setdefault("escalation_reason", None)
            if (
                review_metadata.get("type")
                and review_metadata.get("review_tier") is None
                and lane.get("state") not in REVIEW_WAITING_STATES
            ):
                # Preserve historical decisions outside a pending V3.3 review
                # gate. Pending legacy reviews are classified below.
                review_metadata["review_tier"] = "architect"
            lane.setdefault("worker_attempt", 0)
            lane.setdefault("worker_result", None)
            lane.setdefault("worker_phase_id", None)
            lane.setdefault("worker_start_head", None)
            lane.setdefault("worker_exit_code", None)
            lane.setdefault("worker_interrupted", False)
            if not isinstance(lane.get("validation_gap"), dict):
                lane["validation_gap"] = {"status": "NONE", "reason": "", "at": None}
            self._refresh_plan(lane_name, lane)
            if self.runtime_owner and lane_name not in self.processes:
                # A persisted PID is only historical evidence after a supervisor
                # restart.  Never treat it as a live worker process.
                lane["worker_pid"] = None
            if self.runtime_owner and lane.get("state") == "CODING":
                self._reconcile_interrupted_worker(lane_name, lane)
            if lane.get("last_commit") is None:
                lane["last_commit"] = self.git_head(spec["worktree"])
        self._prepare_control_state()
        self._prepare_scheduler_state()
        self._prepare_architect_review_state()
        self._load_active_roadmap()
        if self.runtime_owner:
            self.reconcile_invalid_empty_current_phase_reviews()
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

    def _prepare_architect_review_state(self) -> None:
        configured = self.config.get("architect_review", {})
        if not isinstance(configured, dict):
            configured = {}
        review = self.state.setdefault("architect_review", {})
        if not isinstance(review, dict):
            review = {}
            self.state["architect_review"] = review
        defaults = {
            "enabled": bool(configured.get("enabled", True)),
            "queue": [],
            "items": {},
            "phase_counters": {},
            "normal_retry_counters": {},
            "active_review_id": None,
            "availability": self._availability_record(),
            "tier_availability": {
                "normal": self._availability_record(),
                "architect": self._availability_record(),
            },
            "last_progress_at": utc_now(),
            "last_worker_completion_at": None,
            "last_review_completion_at": None,
            "last_idle_warning_at": None,
            "idle_reason": None,
        }
        for key, value in defaults.items():
            review.setdefault(key, value)
        if not isinstance(review.get("queue"), list):
            review["queue"] = []
        if not isinstance(review.get("items"), dict):
            review["items"] = {}
        if not isinstance(review.get("phase_counters"), dict):
            review["phase_counters"] = {}
        if not isinstance(review.get("normal_retry_counters"), dict):
            review["normal_retry_counters"] = {}
        if not isinstance(review.get("availability"), dict):
            review["availability"] = defaults["availability"].copy()
        availability = review["availability"]
        for key, value in defaults["availability"].items():
            availability.setdefault(key, value)
        if not isinstance(review.get("tier_availability"), dict):
            review["tier_availability"] = {}
        for tier in REVIEW_TIERS:
            record = review["tier_availability"].get(tier)
            if not isinstance(record, dict):
                record = self._availability_record()
                if tier == "architect":
                    record.update(availability)
                review["tier_availability"][tier] = record
            for key, value in self._availability_record().items():
                record.setdefault(key, value)
        # V3.3 stored a single Sol availability record. Preserve it as the
        # architect view while giving normal Luna reviews an independent lane.
        review["availability"] = review["tier_availability"]["architect"]
        for review_id, item in review["items"].items():
            if not isinstance(item, dict):
                continue
            lane_name = str(item.get("lane") or "")
            lane = self.state.get("lanes", {}).get(lane_name, {})
            lane_review = lane.setdefault("review", {}) if isinstance(lane, dict) else {}
            status = str(item.get("status") or "")
            pending = status in {
                "QUEUED", "WAITING_FOR_ARCHITECT_MODEL", "WAITING_FOR_LUNA_MODEL",
            }
            item_tier = item.get("review_tier")
            legacy_unclassified = item_tier not in REVIEW_TIERS
            tier_reason = item.get("escalation_reason")
            if item_tier not in REVIEW_TIERS:
                lane_tier = lane_review.get("review_tier") if isinstance(lane_review, dict) else None
                if lane_tier in REVIEW_TIERS:
                    item_tier = lane_tier
                elif status == "WAITING_FOR_ARCHITECT_MODEL":
                    item_tier, tier_reason = "architect", "architect reviewer availability backoff"
                elif status == "WAITING_FOR_LUNA_MODEL":
                    item_tier, tier_reason = "normal", None
                elif pending and isinstance(lane, dict):
                    item_tier, tier_reason = self._review_tier_for_request(
                        lane_name, str(item.get("review_type") or ""),
                        lane.get("review_reasons", []) if isinstance(lane.get("review_reasons", []), list) else [],
                    )
                else:
                    # A running or completed V3.3 item was actually reviewed by
                    # Sol; preserve its historical decision semantics.
                    item_tier = "architect"
            item["review_tier"] = item_tier
            route = self._review_route(str(item_tier))
            if pending and isinstance(lane_review, dict):
                selected = str(item.get("selected_model") or "")
                if item_tier != "normal" or self._infer_model_family(selected) != "luna":
                    selected = route["model"]
                item.update({
                    "selected_model": selected,
                    "selected_reasoning_effort": route["reasoning_effort"],
                })
                lane_review.update({
                    "review_tier": item_tier,
                    "selected_model": selected,
                    "selected_reasoning_effort": route["reasoning_effort"],
                    "escalation_reason": tier_reason or lane_review.get("escalation_reason"),
                })
                if tier_reason:
                    item["escalation_reason"] = tier_reason
                # V3.3 queued bundles did not bind a review tier. Never launch
                # them under a newly classified route; the current exact-state
                # bundle will be rebuilt by queue_sol_reviews().
                if status == "QUEUED" and legacy_unclassified and self.runtime_owner:
                    item.update({
                        "status": "STALE",
                        "last_error": "legacy queued bundle superseded by V3.4 tier-bound evidence",
                    })
                    review["queue"][:] = [queued for queued in review["queue"] if str(queued) != str(review_id)]
            elif legacy_unclassified:
                # These are completed/in-flight V3.3 decisions and therefore
                # reflect the former Sol/max route, not the new Sol/medium one.
                item["selected_model"] = "gpt-6-sol"
                item["selected_reasoning_effort"] = "max"
            else:
                item.setdefault("selected_model", route["model"])
                item.setdefault("selected_reasoning_effort", route["reasoning_effort"])
            item.setdefault("selected_model", None)
            item.setdefault("selected_reasoning_effort", None)
            item.setdefault("escalation_reason", None)
            item.setdefault("prior_decisions", [])
        for lane_name, lane in self.state.get("lanes", {}).items():
            if not isinstance(lane, dict) or lane.get("state") not in REVIEW_WAITING_STATES:
                continue
            lane_review = lane.setdefault("review", {})
            if lane_review.get("review_tier") in REVIEW_TIERS:
                continue
            if lane.get("state") == "WAITING_FOR_ARCHITECT_MODEL":
                tier, reason = "architect", "architect reviewer availability backoff"
            elif lane.get("state") == "WAITING_FOR_LUNA_MODEL":
                tier, reason = "normal", None
            else:
                tier, reason = self._review_tier_for_request(
                    str(lane_name), str(lane_review.get("type") or ""),
                    lane.get("review_reasons", []) if isinstance(lane.get("review_reasons", []), list) else [],
                )
            route = self._review_route(tier)
            lane_review.update({
                "review_tier": tier,
                "selected_model": route["model"],
                "selected_reasoning_effort": route["reasoning_effort"],
                "escalation_reason": reason,
            })
        review["enabled"] = bool(configured.get("enabled", review.get("enabled", True)))
        if self.runtime_owner:
            self._reconcile_architect_review_runtime()

    def _architect_review_root(self) -> Path:
        configured = self.config.get("architect_review", {})
        if isinstance(configured, dict) and configured.get("root"):
            return Path(str(configured["root"]))
        return ARCHITECT_REVIEW_ROOT

    def _review_item_path(self, item: Mapping[str, Any], key: str) -> Path | None:
        value = item.get(key)
        return Path(str(value)) if value else None

    def _review_process_matches(self, pid: int, item: Mapping[str, Any]) -> bool:
        marker = str(item.get("bundle_dir") or "").encode("utf-8")
        if pid <= 0 or not marker:
            return False
        try:
            cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
            os.kill(pid, 0)
        except (OSError, ProcessLookupError):
            return False
        return marker in cmdline and b"bwrap" in cmdline

    def _find_review_process(self, item: Mapping[str, Any]) -> int | None:
        marker = str(item.get("bundle_dir") or "").encode("utf-8")
        if not marker:
            return None
        proc_root = Path("/proc")
        try:
            candidates = list(proc_root.iterdir())
        except OSError:
            return None
        for candidate in candidates:
            if not candidate.name.isdigit():
                continue
            try:
                cmdline = (candidate / "cmdline").read_bytes()
            except OSError:
                continue
            if marker in cmdline and b"bwrap" in cmdline:
                return int(candidate.name)
        return None

    def _reconcile_architect_review_runtime(self) -> None:
        review = self.state.get("architect_review", {})
        items = review.get("items", {}) if isinstance(review, dict) else {}
        queue = review.get("queue", []) if isinstance(review, dict) else []
        if not isinstance(items, dict) or not isinstance(queue, list):
            return
        pending: list[str] = []
        active: str | None = None
        for review_id, item in items.items():
            if not isinstance(item, dict) or item.get("status") not in {"STARTING", "RUNNING"}:
                continue
            result_path = self._review_item_path(item, "result_path")
            pid = int(item.get("pid") or 0)
            if not pid:
                found = self._find_review_process(item)
                pid = int(found or 0)
            if pid and self._review_process_matches(pid, item) and active is None:
                item["pid"] = pid
                item["status"] = "RUNNING"
                active = str(review_id)
                continue
            if result_path and result_path.is_file():
                item["status"] = "DECISION_PENDING"
                item["pid"] = None
                continue
            item["status"] = "QUEUED"
            item["pid"] = None
            item["last_error"] = "review process interrupted or absent after supervisor restart"
            pending.append(str(review_id))
        for review_id, item in items.items():
            if (
                isinstance(item, dict)
                and item.get("status") == "QUEUED"
                and review_id not in pending
                and review_id not in queue
            ):
                pending.append(str(review_id))
        review["queue"] = list(dict.fromkeys(pending + [str(item) for item in queue if str(item) in items and items[str(item)].get("status") == "QUEUED"]))
        review["active_review_id"] = active
        self.active_review_id = active

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
        if not self.runtime_owner:
            return
        active_lanes = {
            lane_name for lane_name, process in self.processes.items()
            if process.poll() is None
        }
        for lane_name, lane in self.state.get("lanes", {}).items():
            if lane_name not in active_lanes:
                lane["worker_pid"] = None

        availability = self.state.get("codex_availability", {})
        if (
            availability.get("status") not in {"RATE_LIMITED", "MODEL_UNAVAILABLE"}
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

    def save_state(self) -> bool:
        if not getattr(self, "_state_write_enabled", True):
            return False
        if getattr(self, "_deferred_save_depth", 0) > 0:
            self._save_deferred = True
            return True
        self.state["updated_at"] = utc_now()
        atomic_write_json(STATE_PATH, self.state, 0o640)
        self.last_save = time.monotonic()
        self._save_deferred = False
        return True

    def _operator_paths(self) -> dict[str, Path]:
        return ensure_operator_request_dirs(self.config)

    def _remember_operator_request(self, request_id: str, receipt: dict[str, Any]) -> None:
        processed = self.state.setdefault("processed_operator_requests", {})
        if not isinstance(processed, dict):
            processed = {}
            self.state["processed_operator_requests"] = processed
        processed[request_id] = dict(receipt)
        try:
            limit = max(16, int(self.config.get("max_processed_operator_requests", 256)))
        except (TypeError, ValueError):
            limit = 256
        ordered = sorted(
            processed.items(),
            key=lambda item: parse_time(
                str(item[1].get("processed_at") or "")
                if isinstance(item[1], dict) else ""
            ),
            reverse=True,
        )
        self.state["processed_operator_requests"] = dict(ordered[:limit])

    def _restore_operator_request_snapshot(
        self,
        state_snapshot: dict[str, Any],
        roadmap_snapshot: RoadmapSnapshot | None,
        save_deferred: bool,
        last_save: float,
    ) -> None:
        """Discard an uncommitted request after its durable save failed."""
        self.state = state_snapshot
        self.roadmap_snapshot = roadmap_snapshot
        self._save_deferred = save_deferred
        self.last_save = last_save

    def _operator_receipt(
        self,
        request_id: str,
        command: str,
        return_code: int,
        stdout: str = "",
        stderr: str = "",
    ) -> dict[str, Any]:
        return {
            "version": OPERATOR_REQUEST_VERSION,
            "request_id": request_id,
            "command": command,
            "status": "SUCCEEDED" if return_code == 0 else "FAILED",
            "return_code": int(return_code),
            "stdout": redact(stdout)[-8000:],
            "stderr": redact(stderr)[-8000:],
            "processed_at": utc_now(),
            "daemon_pid": os.getpid(),
        }

    def _write_operator_receipt(self, receipt: dict[str, Any]) -> None:
        request_id = str(receipt.get("request_id") or "")
        if not _valid_operator_request_id(request_id):
            return
        path = self._operator_paths()["receipts"] / f"{request_id}.json"
        atomic_write_json(path, receipt, 0o600)

    def _archive_operator_request(self, path: Path) -> None:
        paths = self._operator_paths()
        try:
            if path.is_symlink() or path.resolve().parent != paths["inbox"].resolve():
                return
            target = paths["archive"] / path.name
            if target.exists():
                target = paths["archive"] / (
                    f"{path.stem}-archived-{uuid.uuid4().hex[:12]}{path.suffix}"
                )
            path.replace(target)
        except OSError as exc:
            self.log(f"operator request archive failed: {exc}")

    def _prune_operator_artifacts(self) -> None:
        paths = self._operator_paths()
        try:
            keep = max(16, int(self.config.get("max_processed_operator_requests", 256)))
        except (TypeError, ValueError):
            keep = 256
        for key in ("receipts", "archive"):
            files = sorted(
                paths[key].glob("*.json"),
                key=lambda item: item.stat().st_mtime if item.exists() else 0,
                reverse=True,
            )
            for path in files[keep:]:
                try:
                    path.unlink()
                except OSError:
                    pass

    def _dispatch_operator_request(self, payload: dict[str, Any]) -> tuple[int, str, str]:
        command = str(payload["command"])
        args = payload["args"]
        stdout = io.StringIO()
        stderr = io.StringIO()
        try:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                if command == "approve":
                    result = self.approve_review(str(args["lane"]))
                elif command == "retry":
                    result = self.retry_review(str(args["lane"]))
                elif command == "block":
                    result = self.block_lane(str(args["lane"]))
                elif command == "approve-task":
                    result = self.approve_task(str(args["task_id"]))
                elif command == "approve-control-plane":
                    result = self.approve_control_plane(str(args["sha"]))
                elif command == "accept-milestone":
                    result = self.accept_milestone(
                        str(args["milestone_id"]), dict(args["evidence"])
                    )
                elif command == "sync-control-plane":
                    result = 0 if self.sync_control_plane(bool(args["force"])) else 1
                elif command == "re-review-v3":
                    result = self.request_fresh_reviews_v3(list(args["targets"]))
                else:  # validate_operator_request should make this unreachable
                    result = 2
                    print("unsupported operator command", file=sys.stderr)
        except Exception as exc:  # fail closed and retain the request for audit
            result = 1
            print(f"operator request raised {type(exc).__name__}: {redact(str(exc))}", file=sys.stderr)
        return int(result), stdout.getvalue(), stderr.getvalue()

    def process_operator_requests(self) -> int:
        """Execute bounded operator mutations against this daemon's live state."""
        if not self.runtime_owner:
            return 0
        paths = self._operator_paths()
        try:
            maximum = max(1, int(self.config.get("max_operator_requests_per_tick", 16)))
        except (TypeError, ValueError):
            maximum = 16
        try:
            max_age = max(1, int(self.config.get("operator_request_stale_seconds", 900)))
        except (TypeError, ValueError):
            max_age = 900
        processed_count = 0
        for path in sorted(paths["inbox"].glob("*.json"))[:maximum]:
            if path.is_symlink() or path.resolve().parent != paths["inbox"].resolve():
                self.log(f"rejecting operator request with unsafe path: {path.name}")
                continue
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
                payload = None
                parse_error = f"request JSON is unreadable: {type(exc).__name__}"
            else:
                parse_error = ""
            valid, request_id, validation_error = validate_operator_request(
                payload, max_age_seconds=max_age
            )
            if parse_error:
                valid = False
                validation_error = parse_error
            if valid:
                processed = self.state.get("processed_operator_requests", {})
                previous = processed.get(request_id) if isinstance(processed, dict) else None
                if isinstance(previous, dict):
                    # The state record is authoritative after restart.  Re-emitting the
                    # receipt is safe and never dispatches the logical command twice.
                    self._write_operator_receipt(previous)
                    self._archive_operator_request(path)
                    processed_count += 1
                    continue
                state_snapshot = deepcopy(self.state)
                roadmap_snapshot = deepcopy(getattr(self, "roadmap_snapshot", None))
                save_deferred = self._save_deferred
                last_save = self.last_save
                self._deferred_save_depth += 1
                try:
                    return_code, stdout, stderr = self._dispatch_operator_request(payload)
                finally:
                    self._deferred_save_depth = max(0, self._deferred_save_depth - 1)
                receipt = self._operator_receipt(
                    request_id, str(payload["command"]), return_code, stdout, stderr
                )
                self._remember_operator_request(request_id, receipt)
                try:
                    if not self.save_state():
                        self._restore_operator_request_snapshot(
                            state_snapshot, roadmap_snapshot, save_deferred, last_save
                        )
                        self.log("operator request state save was refused; request retained")
                        break
                except Exception as exc:
                    self._restore_operator_request_snapshot(
                        state_snapshot, roadmap_snapshot, save_deferred, last_save
                    )
                    self.log(f"operator request state save failed; request retained: {exc}")
                    break
                self._write_operator_receipt(receipt)
                self._archive_operator_request(path)
                processed_count += 1
                continue

            safe_id = request_id if _valid_operator_request_id(request_id) else re.sub(
                r"[^A-Za-z0-9_.:-]+", "-", path.stem
            )[:96] or uuid.uuid4().hex
            receipt = self._operator_receipt(
                safe_id,
                str(payload.get("command") if isinstance(payload, dict) else "malformed"),
                2,
                stderr=f"operator request rejected: {validation_error}",
            )
            if _valid_operator_request_id(request_id):
                state_snapshot = deepcopy(self.state)
                roadmap_snapshot = deepcopy(getattr(self, "roadmap_snapshot", None))
                save_deferred = self._save_deferred
                last_save = self.last_save
                self._remember_operator_request(request_id, receipt)
                try:
                    if not self.save_state():
                        self._restore_operator_request_snapshot(
                            state_snapshot, roadmap_snapshot, save_deferred, last_save
                        )
                        self.log("malformed operator request state save was refused; request retained")
                        break
                except Exception as exc:
                    self._restore_operator_request_snapshot(
                        state_snapshot, roadmap_snapshot, save_deferred, last_save
                    )
                    self.log(f"malformed operator request state save failed: {exc}")
                    break
            self._write_operator_receipt(receipt)
            self._archive_operator_request(path)
            processed_count += 1
        self._prune_operator_artifacts()
        return processed_count

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
            elif "BLOCKED_REVIEW" in states:
                result[workstream_id] = "BLOCKED_REVIEW"
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
        if not getattr(self, "_state_write_enabled", True):
            return
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
                elif (
                    task.source == "EXISTING_PLAN" and lane.get("phase_id") == task_id
                    and lane.get("state") == "BLOCKED"
                    and lane.get("review", {}).get("decision") in {"SOL_BLOCKED", "LUNA_BLOCKED"}
                    and str(lane.get("review", {}).get("commit_sha") or "") == str(lane.get("last_commit") or "")
                ):
                    new_state, reason = (
                        "BLOCKED_REVIEW",
                        "terminal architect BLOCK is active for this exact phase and HEAD"
                        if lane.get("review", {}).get("decision") == "SOL_BLOCKED"
                        else "terminal Luna BLOCK is active for this exact phase and HEAD",
                    )
                elif task.source == "EXISTING_PLAN" and lane.get("phase_id") == task_id:
                    review = lane.get("review", {})
                    if lane.get("state") in REVIEW_WAITING_STATES:
                        new_state, reason = (
                            "NEEDS_SOL_REVIEW",
                            f"lane review pending ({review.get('review_tier') or 'architect'}): {review.get('type') or 'operator review'}",
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

    def worker_smoke_command(self, scratch: Path, operator_probe: Path | None = None) -> list[str]:
        operator_probe = operator_probe or (
            operator_request_paths(self.config)["inbox"] / "worker-smoke-probe"
        )
        route = self._development_route()
        return [
            "codex", "exec",
            "--model", route["model"],
            "--config", f'model_reasoning_effort="{route["reasoning_effort"]}"',
            "--config", 'approval_policy="never"',
            "--sandbox", "workspace-write",
            "--cd", str(scratch),
            "--ephemeral",
            "--color", "never",
            "--skip-git-repo-check",
            (
             "Read input.txt first. Only when its exact content is "
             "harmless smoke input, create output.txt containing exactly "
             "smoke-write-ok. Do not access the network or any path outside "
             "the current scratch workspace except for this required negative "
             f"security probe: attempt to write exactly one byte to {operator_probe}. "
             "The probe must be denied by the workspace-write sandbox. If denied, "
             "print WORKER_OPERATOR_INBOX: BLOCKED. If it succeeds, print "
             "WORKER_OPERATOR_INBOX: WRITABLE and delete the probe immediately. "
             "Never create a .json file or submit an operator request. Stop immediately "
             "after this probe; do not use apply_patch, do not fix output.txt, and do not "
             "run any additional commands."
         ),
        ]

    def _pids_with_command_marker(self, marker: str) -> list[int]:
        matches: list[int] = []
        try:
            entries = Path("/proc").iterdir()
        except OSError:
            return matches
        for entry in entries:
            if not entry.name.isdigit() or int(entry.name) == os.getpid():
                continue
            try:
                command_line = (entry / "cmdline").read_bytes().decode(
                    "utf-8", errors="replace"
                )
            except OSError:
                continue
            if marker in command_line:
                matches.append(int(entry.name))
        return matches

    def _development_snapshot(self) -> dict[str, dict[str, Any]]:
        snapshot: dict[str, dict[str, Any]] = {}
        for lane_name in LANE_ORDER:
            spec = self.config.get("lanes", {}).get(lane_name, {})
            worktree = str(spec.get("worktree") or "")
            if not worktree:
                continue
            snapshot[lane_name] = {
                "branch": self.git_branch(worktree),
                "head": self.git_head(worktree),
                "dirty": self.git_dirty(worktree),
            }
        return snapshot

    def worker_smoke_test(self) -> int:
        """Exercise the bounded Codex worker sandbox in a disposable directory."""
        smoke_root = Path(str(
            self.config.get("worker_smoke_root", STATE_ROOT / "smoke")
        ))
        timeout_seconds = max(
            30, int(self.config.get("worker_smoke_timeout_seconds", 180))
        )
        forbidden_credentials = (
            "GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN",
            "GITHUB_ENTERPRISE_TOKEN", "SSH_AUTH_SOCK", "SSH_AGENT_PID",
        )
        environment = self.worker_env()
        credentials_exposed = any(key in environment for key in forbidden_credentials[:-2])
        ssh_exposed = any(key in environment for key in forbidden_credentials[-2:])
        before = self._development_snapshot()
        scratch: Path | None = None
        output = ""
        return_code: int | None = None
        timed_out = False
        cleanup_error = ""
        try:
            smoke_root.mkdir(parents=True, exist_ok=True)
            os.chmod(smoke_root, 0o750)
            scratch = Path(tempfile.mkdtemp(prefix="codex-worker-", dir=str(smoke_root)))
            os.chmod(scratch, 0o700)
            atomic_write_text(scratch / "input.txt", "harmless smoke input", 0o600)
            operator_probe = (
                operator_request_paths(self.config)["inbox"] / f"worker-smoke-{os.getpid()}-probe"
            )
            ensure_operator_request_dirs(self.config)
            process = subprocess.Popen(
                self.worker_smoke_command(scratch, operator_probe),
                cwd=str(scratch),
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                start_new_session=True,
            )
            try:
                output, _ = process.communicate(timeout=timeout_seconds)
                return_code = process.returncode
            except subprocess.TimeoutExpired as exc:
                timed_out = True
                output = exc.output or ""
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except OSError:
                    pass
                try:
                    tail, _ = process.communicate(timeout=10)
                    output += tail or ""
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except OSError:
                        pass
                    tail, _ = process.communicate(timeout=10)
                    output += tail or ""
                return_code = process.returncode if process.returncode is not None else 124
        except (OSError, ValueError) as exc:
            output = f"worker smoke process failed: {exc}"
            return_code = 127

        after = self._development_snapshot()
        output = redact(output or "")
        output_lower = output.lower()
        output_path = scratch / "output.txt" if scratch else None
        operator_probe = (
            operator_request_paths(self.config)["inbox"] / f"worker-smoke-{os.getpid()}-probe"
        )
        operator_probe_exists = operator_probe.exists()
        probe_marker = re.search(
            r"(?m)^\s*(?:\|\s*)?WORKER_OPERATOR_INBOX:\s*(WRITABLE|BLOCKED)\s*$",
            output,
        )
        operator_probe_blocked = (
            bool(probe_marker and probe_marker.group(1) == "BLOCKED")
            or bool(re.search(
                r"(?:operator request inbox|inbox|probe).{0,100}(?:blocked|denied|not permitted)",
                output,
                re.IGNORECASE | re.DOTALL,
            ))
        )
        operator_probe_writable = bool(
            (probe_marker and probe_marker.group(1) == "WRITABLE") or operator_probe_exists
        )
        local_write_ok = False
        if output_path is not None:
            try:
                local_write_ok = output_path.read_text(encoding="utf-8") in {
                    "smoke-write-ok",
                    "smoke-write-ok\n",
                }
            except OSError:
                local_write_ok = False
        local_read_ok = local_write_ok
        rtm_error = bool(re.search(r"RTM_NEWADDR", output, re.IGNORECASE))
        sandbox_failure = bool(re.search(
            r"bwrap|loopback|workspace command runner .*fail|sandbox .*fail",
            output_lower,
        ))
        persistent_processes = self._pids_with_command_marker(str(scratch)) if scratch else []
        worktree_modified = before != after
        branch_changed = any(
            before.get(name, {}).get("branch") != after.get(name, {}).get("branch")
            or before.get(name, {}).get("head") != after.get(name, {}).get("head")
            for name in set(before) | set(after)
        )
        if scratch is not None:
            try:
                shutil.rmtree(scratch)
            except OSError as exc:
                cleanup_error = str(exc)

        print(f"worker local filesystem read: {'PASS' if local_read_ok else 'FAIL'}")
        print(f"worker local allowed write: {'PASS' if local_write_ok else 'FAIL'}")
        print(f"bwrap RTM_NEWADDR error: {'PRESENT' if rtm_error else 'ABSENT'}")
        print(f"sandbox failure signal: {'PRESENT' if sandbox_failure else 'ABSENT'}")
        print(f"GitHub credentials exposed: {'YES' if credentials_exposed else 'NO'}")
        print(f"SSH agent exposed: {'YES' if ssh_exposed else 'NO'}")
        print(
            "operator request inbox from worker: "
            + ("BLOCKED" if operator_probe_blocked and not operator_probe_writable else "WRITABLE/UNPROVEN")
        )
        print(f"development worktree modified: {'YES' if worktree_modified else 'NO'}")
        print(f"development branch changed: {'YES' if branch_changed else 'NO'}")
        print(f"persistent worker process: {'YES' if persistent_processes else 'NO'}")

        reasons: list[str] = []
        if return_code != 0:
            reasons.append(f"codex exit={return_code}")
        if timed_out:
            reasons.append("smoke command timed out")
        if not local_read_ok or not local_write_ok:
            reasons.append("scratch read/write proof failed")
        if sandbox_failure:
            reasons.append("Codex workspace-write sandbox reported a bwrap/loopback failure")
        if credentials_exposed:
            reasons.append("worker environment exposed GitHub credentials")
        if ssh_exposed:
            reasons.append("worker environment exposed SSH agent variables")
        if not operator_probe_blocked or operator_probe_writable:
            reasons.append("workspace-write worker could write the operator request inbox")
        if worktree_modified:
            reasons.append("development worktree changed")
        if branch_changed:
            reasons.append("development branch changed")
        if persistent_processes:
            reasons.append(f"persistent worker process(es): {persistent_processes}")
        if cleanup_error:
            reasons.append(f"scratch cleanup failed: {cleanup_error}")
        if reasons:
            failure_prefix = "SANDBOX_INFRA_BLOCKED: " if sandbox_failure else ""
            print("WORKER_SMOKE_FAILED: " + failure_prefix + redact("; ".join(reasons)))
            if output:
                excerpt = output[-1200:].strip().replace("\n", " | ")
                print("worker evidence: " + redact(excerpt))
            return 1
        print("WORKER_SMOKE_OK")
        return 0

    def command(
        self,
        args: list[str],
        cwd: str | None = None,
        timeout: int = 120,
        env: Mapping[str, str] | None = None,
    ) -> tuple[int, str, str]:
        try:
            result = subprocess.run(
                args, cwd=cwd, env=dict(env) if env is not None else self.env(), text=True,
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

    def _current_phase_review_evidence(self, lane_name: str) -> dict[str, Any]:
        """Classify current-phase review evidence from trusted state and Git only."""
        lane = self.state.get("lanes", {}).get(lane_name, {})
        phase_id = str(lane.get("phase_id") or "")
        lane_config = self.config.get("lanes", {}).get(lane_name, {})
        worktree = str(lane.get("worktree") or lane_config.get("worktree") or "")
        if not phase_id or not worktree:
            return {"kind": "unknown", "reason_code": "PHASE_OR_WORKTREE_MISSING"}

        head = self.git_head(worktree)
        expected_branch = str(lane.get("branch") or lane_config.get("branch") or "")
        actual_branch = self.git_branch(worktree)
        okay, entries, _ = self.git_status_details(worktree)
        if not head or not okay or actual_branch != expected_branch:
            return {"kind": "unknown", "reason_code": "GIT_STATE_UNAVAILABLE", "head": head}
        if any(entry.get("kind") in {"unknown", "unmerged"} for entry in entries):
            return {"kind": "unknown", "reason_code": "GIT_STATUS_UNSAFE", "head": head}

        dirty = bool(entries)
        worker_is_current = lane.get("worker_phase_id") == phase_id
        worker_result = lane.get("worker_result") if worker_is_current else None
        worker_interrupted = bool(lane.get("worker_interrupted"))

        base = self._trusted_previous_accepted_head(lane, phase_id)
        committed_diff = False
        if base and re.fullmatch(r"[0-9a-f]{40,64}", base):
            if base != head:
                ancestor, _, _ = self.command(
                    ["git", "-C", worktree, "merge-base", "--is-ancestor", base, head],
                    cwd=worktree, timeout=30,
                )
                if ancestor != 0:
                    return {"kind": "unknown", "reason_code": "TRUSTED_BASE_NOT_ANCESTOR", "head": head}
                diff_code, _, _ = self.command(
                    ["git", "-C", worktree, "diff", "--quiet", base, head],
                    cwd=worktree, timeout=90,
                )
                if diff_code == 1:
                    committed_diff = True
                elif diff_code != 0:
                    return {"kind": "unknown", "reason_code": "COMMITTED_DIFF_UNAVAILABLE", "head": head}
            elif str(lane.get("last_commit") or head) != head:
                return {"kind": "unknown", "reason_code": "LANE_HEAD_MISMATCH", "head": head}
        else:
            return {"kind": "unknown", "reason_code": "TRUSTED_PHASE_BASE_MISSING", "head": head}

        if dirty:
            safe, reason = self.verify_worker_worktree(lane_name, recovery=True)
            if not safe:
                return {"kind": "unknown", "reason_code": "RECOVERY_WORKTREE_UNSAFE", "head": head,
                        "detail": reason}
            for extra in ([], ["--cached"]):
                diff_check, _, _ = self.command(
                    ["git", "-C", worktree, "diff", "--quiet", *extra],
                    cwd=worktree, timeout=90,
                )
                if diff_check not in {0, 1}:
                    return {"kind": "unknown", "reason_code": "WORKTREE_DIFF_UNAVAILABLE", "head": head}
            files, _diff, _stat, metrics = self.changed_diff(lane_name)
            if (
                metrics.get("changed_files", len(files)) > int(self.config.get("max_changed_files", 40))
                or metrics.get("total_diff_bytes", 0) > int(self.config.get("max_total_diff_bytes", 524288))
                or metrics.get("untracked_review_issues")
                or self.forbidden_change_reasons(lane_name, files)
            ):
                return {"kind": "unknown", "reason_code": "RECOVERY_DIFF_UNSAFE", "head": head}
            if committed_diff:
                return {"kind": "reviewable", "head": head, "dirty": True,
                        "reason_code": "CURRENT_PHASE_COMMITTED_DIFF"}
            if worker_is_current and not worker_interrupted and worker_result in {"NEEDS_SOL_REVIEW", "BLOCKED"}:
                return {"kind": "reviewable", "head": head, "dirty": True,
                        "reason_code": "CURRENT_PHASE_WORKER_RESULT"}
            if (
                worker_is_current
                and not worker_interrupted
                and isinstance(lane.get("worker_exit_code"), int)
            ):
                return {"kind": "reviewable", "head": head, "dirty": True,
                        "reason_code": "CURRENT_PHASE_FAILURE_WITH_DIFF"}
            return {"kind": "recoverable", "head": head, "dirty": True,
                    "reason_code": "PRESERVED_CURRENT_PHASE_WORK"}

        if committed_diff:
            return {"kind": "reviewable", "head": head, "dirty": False,
                    "reason_code": "CURRENT_PHASE_COMMITTED_DIFF"}
        if worker_is_current and not worker_interrupted and worker_result in {"NEEDS_SOL_REVIEW", "BLOCKED"}:
            return {"kind": "reviewable", "head": head, "dirty": False,
                    "reason_code": "CURRENT_PHASE_WORKER_RESULT"}
        if worker_is_current and worker_result in {"COMPLETE", "COMPLETE_WITH_VALIDATION_GAP"}:
            return {"kind": "empty", "head": head, "dirty": False,
                    "reason_code": "COMPLETION_WITHOUT_CURRENT_PHASE_DIFF"}
        return {"kind": "empty", "head": head, "dirty": False,
                "reason_code": "NO_CURRENT_PHASE_EVIDENCE"}

    def _reset_phase_transient_metadata(self, lane: dict[str, Any], head: str | None) -> None:
        lane.update({
            "worker_pid": None,
            "worker_started_at": None,
            "worker_finished_at": None,
            "last_progress_at": None,
            "worker_phase_id": None,
            "worker_start_head": None,
            "worker_exit_code": None,
            "worker_interrupted": False,
            "worker_recovery": False,
            "worker_result": None,
            "validation": {},
            "validation_gap": {"status": "NONE", "reason": "", "at": None},
            "ci": {"status": "NOT_RUN", "sha": head, "run_id": None, "url": None},
            "ci_started_at": None,
            "ci_poll_failures": 0,
            "push_attempts": 0,
            "review_reasons": [],
            "review_packet": None,
            "recovery_context": {},
            "last_error": "",
        })

    def _reconcile_interrupted_worker(self, lane_name: str, lane: dict[str, Any]) -> None:
        """Recover an interrupted worker from exact Git state without dropping its diff."""
        evidence = self._current_phase_review_evidence(lane_name)
        if evidence.get("kind") == "unknown":
            lane["worker_pid"] = None
            lane["state"] = "BLOCKED"
            lane["last_error"] = "interrupted worker state could not be reconciled safely"
            lane["worker_interrupted"] = True
            self.event(
                f"interrupted worker reconciliation failed closed ({evidence.get('reason_code')})",
                lane_name,
            )
            return

        interrupted = {
            "phase_id": lane.get("worker_phase_id") or lane.get("phase_id"),
            "attempt": lane.get("worker_attempt"),
            "worker_log": lane.get("worker_log"),
            "worker_start_head": lane.get("worker_start_head"),
            "worker_exit_code": lane.get("worker_exit_code"),
            "worker_result": lane.get("worker_result"),
            "worker_recovery": lane.get("worker_recovery"),
            "interrupted_at": utc_now(),
        }
        target = "RECOVERING" if evidence.get("dirty") or evidence.get("kind") == "reviewable" else "READY"
        recovery_context = self._capture_recovery_context(lane_name) if target == "RECOVERING" else {}
        self._reset_phase_transient_metadata(lane, evidence.get("head"))
        lane["last_interrupted_worker"] = interrupted
        self._mark_worker_task_stopped(lane_name)
        if target == "RECOVERING":
            lane["recovery_context"] = recovery_context
            lane["recovery_attempts"] = 1
            lane["last_error"] = "supervisor restart interrupted current-phase work; bounded recovery required"
        else:
            lane["recovery_attempts"] = 0
            lane["recovery_context"] = {}
            lane["last_error"] = ""
        if self.state.get("global_mode") == "RUNNING":
            lane["state"] = target
        else:
            lane["paused_from_state"] = target
            lane["state"] = "PAUSED"
        self.event(f"interrupted worker reconciled to {target}; worktree bytes preserved", lane_name)

    def _mark_worker_task_stopped(self, lane_name: str) -> None:
        scheduler = self.state.get("scheduler", {})
        tasks = scheduler.get("tasks", {}) if isinstance(scheduler, dict) else {}
        lane = self.state.get("lanes", {}).get(lane_name, {})
        task_id = lane.get("scheduler_task_id") or lane.get("phase_id")
        record = tasks.get(task_id) if isinstance(tasks, dict) else None
        if isinstance(record, dict):
            record["worker_pid"] = None

    def _finish_unreviewable_current_phase(
        self, lane_name: str, evidence: dict[str, Any], reasons: list[str], *,
        existing_review: bool,
    ) -> None:
        lane = self.state["lanes"][lane_name]
        review_state = self.state.setdefault("architect_review", {})
        phase_id = str(lane.get("phase_id") or "")
        head = evidence.get("head")
        old_review = lane.get("review", {})
        old_review = old_review if isinstance(old_review, dict) else {}
        record = {
            "lane": lane_name,
            "phase": phase_id,
            "reviewed_sha": old_review.get("commit_sha") or lane.get("last_commit"),
            "head": head,
            "review": dict(old_review) if isinstance(old_review, dict) else {},
            "review_reasons": list(lane.get("review_reasons") or reasons),
            "suppressed_reasons": list(reasons),
            "review_packet": lane.get("review_packet"),
            "existing_review": existing_review,
            "worker_phase_id": lane.get("worker_phase_id"),
            "worker_result": lane.get("worker_result"),
            "worker_attempt": lane.get("worker_attempt"),
            "classification": evidence.get("kind"),
            "reason_code": evidence.get("reason_code"),
            "retired_at": utc_now(),
        }
        items = review_state.setdefault("items", {})
        active_ids = {
            str(value) for value in (
                review_state.get("active_review_id"), getattr(self, "active_review_id", None)
            ) if value
        }
        active_items = [items.get(review_id) for review_id in active_ids] if isinstance(items, dict) else []
        active_for_lane = any(
            isinstance(item, dict) and item.get("lane") == lane_name and item.get("phase") == phase_id
            for item in active_items
        )
        reviewer_process = getattr(self, "review_process", None)
        active_state_resolves = any(isinstance(item, dict) for item in active_items)
        if active_for_lane or (reviewer_process is not None and not active_state_resolves):
            # Leave an in-flight reviewer and its exact evidence untouched.
            lane["last_error"] = "active reviewer prevents stale review reconciliation"
            self.event("empty review reconciliation failed closed with active reviewer", lane_name)
            return

        queue = review_state.get("queue", [])
        evidence_errors = review_state.get("evidence_errors", {})
        bundle_failures = review_state.get("bundle_failures", {})
        if (
            not isinstance(items, dict) or not isinstance(queue, list)
            or not isinstance(evidence_errors, dict) or not isinstance(bundle_failures, dict)
        ):
            lane["state"] = "BLOCKED"
            lane["last_error"] = "review queue state is malformed; reconciliation failed closed"
            self.event("empty review reconciliation failed closed with malformed queue state", lane_name)
            return

        stale_reviews = review_state.setdefault("stale_current_phase_reviews", [])
        if not isinstance(stale_reviews, list):
            lane["state"] = "BLOCKED"
            lane["last_error"] = "stale review archive is malformed; reconciliation failed closed"
            self.event("empty review reconciliation failed closed with malformed archive", lane_name)
            return
        if not any(
            item.get("lane") == lane_name and item.get("phase") == phase_id
            and item.get("reviewed_sha") == record["reviewed_sha"]
            and item.get("reason_code") == record["reason_code"]
            for item in stale_reviews if isinstance(item, dict)
        ):
            stale_reviews.append(record)

        for error in evidence_errors.values():
            if not isinstance(error, dict):
                continue
            if (
                error.get("lane") == lane_name and error.get("phase") == phase_id
                and error.get("review_type") == "CURRENT_PHASE_REVIEW"
                and error.get("reviewed_sha") in {record["reviewed_sha"], head}
                and error.get("status") in {"EVIDENCE_ERROR", "REQUIRES_INFRA_REVIEW"}
            ):
                error.update({
                    "status": "STALE",
                    "stale_at": utc_now(),
                    "stale_reason_code": evidence.get("reason_code"),
                })

        if isinstance(items, dict):
            for review_id, item in items.items():
                if not isinstance(item, dict) or item.get("lane") != lane_name or item.get("phase") != phase_id:
                    continue
                if item.get("reviewed_sha") not in {record["reviewed_sha"], head}:
                    continue
                if item.get("status") in {
                    "QUEUED", "WAITING_FOR_ARCHITECT_MODEL", "WAITING_FOR_LUNA_MODEL",
                    "STARTING", "RUNNING", "EVIDENCE_ERROR",
                }:
                    item.update({"status": "STALE", "stale_at": utc_now(),
                                 "stale_reason_code": evidence.get("reason_code")})
                    queue[:] = [queued for queued in queue if str(queued) != str(review_id)]

        bundle_failure = bundle_failures.get(lane_name)
        if isinstance(bundle_failure, dict) and bundle_failure.get("phase") == phase_id:
            bundle_failure.update({"status": "STALE", "stale_at": utc_now(),
                                   "stale_reason_code": evidence.get("reason_code")})

        dirty = evidence.get("kind") == "recoverable"
        recovery_context = self._capture_recovery_context(lane_name) if dirty else {}
        self._reset_phase_transient_metadata(lane, head if isinstance(head, str) else None)
        if existing_review or (isinstance(old_review, dict) and old_review.get("type") == "CURRENT_PHASE_REVIEW"):
            lane["review"] = {
                "type": None, "reviewed_phase": None, "commit_sha": None, "next_phase": None,
                "created_at": None, "decision": None, "review_tier": None, "selected_model": None,
                "selected_reasoning_effort": None, "escalation_reason": None,
            }
        lane["recovery_context"] = recovery_context
        if dirty:
            lane["recovery_attempts"] = 1
            lane["last_error"] = "preserved current-phase work requires one bounded recovery attempt"
            target = "RECOVERING"
        else:
            lane["recovery_attempts"] = 0
            lane["last_error"] = ""
            target = "READY"
        if self.state.get("global_mode") == "RUNNING":
            lane["state"] = target
        else:
            lane["paused_from_state"] = target
            lane["state"] = "PAUSED"
        self._mark_worker_task_stopped(lane_name)
        self.event(
            f"retired empty CURRENT_PHASE_REVIEW as stale infrastructure state; lane restored to {target}",
            lane_name,
        )
        self._audit(
            "empty_current_phase_review_retired",
            task_id=lane.get("scheduler_task_id") or phase_id,
            lane=lane_name,
            phase=phase_id,
            classification=evidence.get("kind"),
            reason_code=evidence.get("reason_code"),
        )

    def reconcile_invalid_empty_current_phase_reviews(self) -> int:
        """Retire only current-phase gates that Git and structured worker state prove empty."""
        if not self.runtime_owner:
            return 0
        changed = 0
        waiting_states = {"NEEDS_SOL_REVIEW", "WAITING_FOR_ARCHITECT_MODEL", "WAITING_FOR_LUNA_MODEL"}
        for lane_name, lane in self.state.get("lanes", {}).items():
            if not isinstance(lane, dict) or lane.get("state") not in waiting_states:
                continue
            review = lane.get("review", {})
            if not isinstance(review, dict) or review.get("type") != "CURRENT_PHASE_REVIEW":
                continue
            phase_id = str(lane.get("phase_id") or "")
            reviewed_phase = review.get("reviewed_phase") or {}
            if reviewed_phase and reviewed_phase.get("id") not in (None, phase_id):
                evidence = {"kind": "unknown", "reason_code": "REVIEW_PHASE_MISMATCH"}
            else:
                evidence = self._current_phase_review_evidence(lane_name)
                reviewed_sha = str(review.get("commit_sha") or lane.get("last_commit") or "")
                if reviewed_sha and reviewed_sha != evidence.get("head"):
                    evidence = {"kind": "unknown", "reason_code": "REVIEW_HEAD_MISMATCH",
                                "head": evidence.get("head")}

            if evidence.get("kind") == "reviewable":
                continue
            if evidence.get("kind") == "unknown":
                lane["state"] = "BLOCKED"
                lane["last_error"] = "current-phase review evidence reconciliation failed closed"
                review_state = self.state.setdefault("architect_review", {})
                failures = review_state.setdefault("reconciliation_errors", {})
                key = f"{lane_name}:{phase_id}:{evidence.get('reason_code')}"
                failures[key] = {
                    "lane": lane_name, "phase": phase_id,
                    "status": "REQUIRES_INFRA_REVIEW",
                    "reason_code": evidence.get("reason_code"), "recorded_at": utc_now(),
                }
                self.event(
                    f"current-phase review reconciliation failed closed ({evidence.get('reason_code')})",
                    lane_name,
                )
                changed += 1
                continue

            self._finish_unreviewable_current_phase(
                lane_name, evidence, list(lane.get("review_reasons") or []), existing_review=True,
            )
            changed += 1
        if changed and "scheduler" in self.state and getattr(self, "roadmap_snapshot", None) is not None:
            self._recompute_scheduler()
        return changed

    def _suppress_unreviewable_current_phase_request(
        self, lane_name: str, reasons: list[str], evidence: dict[str, Any],
    ) -> None:
        if evidence.get("kind") in {"empty", "recoverable"}:
            self._finish_unreviewable_current_phase(
                lane_name, evidence, reasons, existing_review=False,
            )
            if "scheduler" in self.state and getattr(self, "roadmap_snapshot", None) is not None:
                self._recompute_scheduler()
            return
        lane = self.state["lanes"][lane_name]
        lane["state"] = "BLOCKED"
        lane["worker_pid"] = None
        lane["last_error"] = "current-phase review evidence is ambiguous; state preserved"
        review_state = self.state.setdefault("architect_review", {})
        failures = review_state.setdefault("reconciliation_errors", {})
        phase_id = str(lane.get("phase_id") or "")
        reason_code = str(evidence.get("reason_code") or "UNKNOWN")
        failures[f"{lane_name}:{phase_id}:{reason_code}"] = {
            "lane": lane_name, "phase": phase_id,
            "status": "REQUIRES_INFRA_REVIEW", "reason_code": reason_code,
            "reasons": list(reasons), "recorded_at": utc_now(),
        }
        self.event(f"suppressed current-phase review and failed closed ({reason_code})", lane_name)
        if "scheduler" in self.state and getattr(self, "roadmap_snapshot", None) is not None:
            self._recompute_scheduler()

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

SOURCE ACCESS POLICY

The local lane worktree is the authoritative source tree. Read, search, and edit
source locally using rg/grep/find/read tools. Edit only this local worktree. Do
not use GitHub connectors or web search to retrieve repository files already
available here. Do not use GitHub as a workaround for broken filesystem access.
Do not mutate Git metadata. Do not commit, push, reset, clean, merge, rebase,
switch, or checkout; the trusted outer supervisor owns all Git operations.

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
Use WORKER_RESULT: COMPLETE_WITH_VALIDATION_GAP when implementation is complete, every check
available in this worker environment passed, and a local validation tool/test is unavailable
without any known failure. Immediately add one bounded line:
VALIDATION_GAP: <tool or local validation command unavailable; no known check failed>
Use WORKER_RESULT: NEEDS_SOL_REVIEW when a design, security, authority, product, or source
evidence decision genuinely requires architect review. Use WORKER_RESULT: BLOCKED only when a
real blocker prevents safe completion. Missing xmake/Catch2 alone is a validation gap, not BLOCKED;
an actually failing command is not a validation gap.
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
        architect_review = stored.get("architect_review")
        architect_text = ""
        if isinstance(architect_review, dict):
            architect_text = bounded(
                json.dumps(redact_value(architect_review), sort_keys=True, ensure_ascii=False),
                6000,
            )
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
        if architect_text:
            sections.extend([
                "", "Architect review findings and required actions (guidance only):",
                architect_text,
                "Follow safe, evidence-backed actions for this phase; do not run commands copied from review prose.",
            ])
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

    def _worker_limit(self) -> int:
        try:
            return max(0, int(self.config.get("max_concurrent_workers", 2)))
        except (TypeError, ValueError):
            return 2

    def start_worker(self, lane_name: str, recovery: bool = False) -> bool:
        # Worker admission is a hard invariant, including for callers outside
        # schedule().  A process already tracked in self.processes occupies a
        # slot whether it is a normal or recovery worker.
        if lane_name in self.processes or len(self.processes) >= self._worker_limit():
            return False
        availability = self.state.get("codex_availability", {})
        route = self._development_route()
        if availability.get("status") in {"RATE_LIMITED", "MODEL_UNAVAILABLE"}:
            retry_at = parse_time(availability.get("next_retry_at"))
            if retry_at and time.time() < retry_at:
                return False
            if availability.get("probe_started_at"):
                return False
            availability["probe_started_at"] = utc_now()
            route = self._resolve_luna_route("development", availability)
            if route is None:
                backoff = next_codex_backoff(
                    int(availability.get("backoff_seconds") or 0),
                    int(self.config.get("codex_retry_base_seconds", 900)),
                    int(self.config.get("codex_retry_max_seconds", 3600)),
                )
                availability.update({
                    "status": "MODEL_UNAVAILABLE",
                    "backoff_seconds": backoff,
                    "next_retry_at": dt.datetime.fromtimestamp(
                        time.time() + backoff, dt.timezone.utc
                    ).isoformat(timespec="seconds"),
                    "probe_started_at": None,
                })
                return False
        elif availability.get("selected_model"):
            selected = str(availability.get("selected_model"))
            if self._infer_model_family(selected) == "luna":
                route["model"] = selected
                route["reasoning_effort"] = str(
                    availability.get("selected_reasoning_effort") or route["reasoning_effort"]
                )
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
            "--model", route["model"],
            "--config", f'model_reasoning_effort="{route["reasoning_effort"]}"',
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
        if availability.get("status") in {"RATE_LIMITED", "MODEL_UNAVAILABLE"}:
            availability["probe_started_at"] = utc_now()
        availability.update({
            "configured_model": self._development_route()["model"],
            "selected_model": route["model"],
            "selected_reasoning_effort": route["reasoning_effort"],
            "fallback_used": route["model"] != self._development_route()["model"],
        })
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
            "worker_phase_id": phase["id"],
            "worker_start_head": self.git_head(lane["worktree"]),
            "worker_exit_code": None,
            "worker_interrupted": False,
            "worker_result": None,
            "validation_gap": {"status": "NONE", "reason": "", "at": None},
            "last_error": "",
        })
        architect_state = self.state.setdefault("architect_review", {})
        architect_state["last_progress_at"] = utc_now()
        architect_state["last_worker_start_at"] = utc_now()
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

    def worker_validation_gap_reason(self, lane_name: str) -> str | None:
        path = Path(self.state["lanes"][lane_name].get("worker_log") or "")
        matches = VALIDATION_GAP_RE.findall(tail_text(path, 30000))
        if not matches:
            return None
        reason = redact(matches[-1].strip())
        return reason[:1000] if reason else None

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

    def handle_codex_model_unavailable(self, lane_name: str, output: str) -> None:
        availability = self.state.setdefault("codex_availability", {})
        previous = int(availability.get("backoff_seconds") or 0)
        backoff = next_codex_backoff(
            previous,
            int(self.config.get("codex_retry_base_seconds", 900)),
            int(self.config.get("codex_retry_max_seconds", 3600)),
        )
        retry_count = int(availability.get("retry_count") or 0) + 1
        retry_at = time.time() + backoff
        availability.update({
            "status": "MODEL_UNAVAILABLE",
            "detected_at": utc_now(),
            "next_retry_at": dt.datetime.fromtimestamp(
                retry_at, dt.timezone.utc
            ).isoformat(timespec="seconds"),
            "backoff_seconds": backoff,
            "retry_count": retry_count,
            "probe_started_at": None,
            "reason": redact("configured Luna model unavailable: " + (output or "")[-1000:]),
        })
        lane = self.state["lanes"][lane_name]
        lane["last_error"] = "configured Luna model is unavailable; dirty diff preserved"
        lane["state"] = "RECOVERING" if self.git_dirty(lane["worktree"]) else "READY"
        self.event(
            f"configured Luna model unavailable; no new workers until {availability['next_retry_at']} "
            f"(backoff {backoff}s)", lane_name,
        )

    def mark_codex_available(self) -> None:
        availability = self.state.setdefault("codex_availability", {})
        if availability.get("status") not in {"RATE_LIMITED", "MODEL_UNAVAILABLE"}:
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
            validation_gap_reason = self.worker_validation_gap_reason(lane_name)
            worker_output = tail_text(Path(lane.get("worker_log") or ""), 40000)
            lane["worker_pid"] = None
            lane["worker_finished_at"] = utc_now()
            lane["last_progress_at"] = utc_now()
            lane["worker_exit_code"] = return_code
            lane["worker_interrupted"] = return_code in {-15, 143}
            if result in {"COMPLETE", "COMPLETE_WITH_VALIDATION_GAP", "NEEDS_SOL_REVIEW", "BLOCKED"}:
                lane["worker_phase_id"] = lane.get("worker_phase_id") or lane.get("phase_id")
                lane["worker_result"] = result
            architect_state = self.state.setdefault("architect_review", {})
            architect_state["last_progress_at"] = utc_now()
            architect_state["last_worker_completion_at"] = utc_now()
            if classify_codex_usage_limit(return_code, worker_output):
                self.handle_codex_rate_limited(lane_name, worker_output)
                continue
            if classify_codex_model_unavailable(return_code, worker_output):
                self.handle_codex_model_unavailable(lane_name, worker_output)
                continue
            self.mark_codex_available()
            if return_code == 0 and result in {"COMPLETE", "COMPLETE_WITH_VALIDATION_GAP"}:
                if result == "COMPLETE_WITH_VALIDATION_GAP" and not validation_gap_reason:
                    self.worker_failure(
                        lane_name,
                        "worker reported COMPLETE_WITH_VALIDATION_GAP without a bounded VALIDATION_GAP reason",
                    )
                    continue
                lane["worker_result"] = result
                lane["validation_gap"] = (
                    {
                        "status": "PRESENT",
                        "reason": validation_gap_reason,
                        "at": utc_now(),
                    }
                    if result == "COMPLETE_WITH_VALIDATION_GAP"
                    else {"status": "NONE", "reason": "", "at": utc_now()}
                )
                lane["state"] = (
                    "LOCAL_VALIDATION"
                    if self.state.get("global_mode") == "RUNNING"
                    else "PAUSED"
                )
                lane["last_error"] = ""
                self.event(
                    (
                        "worker completed with local validation gap; entering local validation"
                        if result == "COMPLETE_WITH_VALIDATION_GAP"
                        else "worker completed; entering local validation"
                    )
                    if lane["state"] == "LOCAL_VALIDATION"
                    else (
                        "worker completed with validation gap while paused; diff preserved"
                        if result == "COMPLETE_WITH_VALIDATION_GAP"
                        else "worker completed while paused; diff preserved"
                    ),
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

    def _git_index_logical_snapshot(self, worktree: str) -> tuple[str | None, str]:
        code, output, error = self.command(
            ["git", "-C", worktree, "ls-files", "--stage", "-z"], timeout=120
        )
        if code != 0:
            return None, redact(error or output or "git index snapshot failed")
        return hashlib.sha256(output.encode("utf-8", errors="surrogateescape")).hexdigest(), ""

    def prospective_commit_diff_check(
        self, worktree: str, files: list[str]
    ) -> dict[str, Any]:
        """Check the prospective HEAD-to-working-tree commit in an isolated index."""
        before, before_error = self._git_index_logical_snapshot(worktree)
        result: dict[str, Any] = {
            "status": "FAIL",
            "mechanism": "temporary Git index populated from HEAD with explicit validated paths",
            "files": list(files),
            "real_index_before": before,
            "real_index_after": None,
            "real_index_unchanged": False,
            "diff_check": False,
            "output_tail": "",
        }
        if before is None:
            result["output_tail"] = before_error
            return result
        if not files:
            after, after_error = self._git_index_logical_snapshot(worktree)
            result.update({
                "status": "PASS",
                "real_index_after": after,
                "real_index_unchanged": after == before,
                "diff_check": True,
                "output_tail": after_error,
            })
            if after is None or after != before:
                result["status"] = "FAIL"
                result["output_tail"] = after_error or "real Git index changed during prospective check"
            return result

        descriptor, temporary_index_name = tempfile.mkstemp(prefix="skyrim-supervisor-index-")
        os.close(descriptor)
        temporary_index = Path(temporary_index_name)
        try:
            temporary_index.unlink(missing_ok=True)
            isolated_environment = self.env()
            isolated_environment["GIT_INDEX_FILE"] = str(temporary_index)
            code, output, error = self.command(
                ["git", "-C", worktree, "read-tree", "HEAD"],
                timeout=120,
                env=isolated_environment,
            )
            if code == 0:
                code, output, error = self.command(
                    ["git", "-C", worktree, "add", "--"] + list(files),
                    cwd=worktree,
                    timeout=120,
                    env=isolated_environment,
                )
            if code == 0:
                code, output, error = self.command(
                    ["git", "-C", worktree, "diff", "--cached", "--check"],
                    timeout=120,
                    env=isolated_environment,
                )
            result["diff_check"] = code == 0
            result["output_tail"] = redact((output or "") + "\n" + (error or ""))[-4000:]
            result["status"] = "PASS" if code == 0 else "FAIL"
        finally:
            try:
                temporary_index.unlink(missing_ok=True)
            except OSError:
                pass
            after, after_error = self._git_index_logical_snapshot(worktree)
            result["real_index_after"] = after
            result["real_index_unchanged"] = after is not None and after == before
            if after is None:
                result["status"] = "FAIL"
                result["output_tail"] = (result.get("output_tail") or "") + "\n" + after_error
            elif not result["real_index_unchanged"]:
                result["status"] = "FAIL"
                result["output_tail"] = (result.get("output_tail") or "") + (
                    "\nreal Git index changed during prospective check"
                )
        return result

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
            "prospective temporary-index commit diff check",
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
        prospective = self.prospective_commit_diff_check(worktree, files) if status_ok else {
            "status": "NOT_RUN",
            "mechanism": "temporary Git index populated from HEAD with explicit validated paths",
            "files": list(files),
            "real_index_unchanged": None,
            "diff_check": None,
            "output_tail": "prospective check not run because Git status failed",
        }
        if prospective.get("status") != "PASS":
            reasons.append(
                "prospective commit diff check failed before real index mutation"
                + (
                    ": " + str(prospective.get("output_tail"))
                    if prospective.get("output_tail") else ""
                )
            )
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
                "prospective_commit": prospective,
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
            "worker_result": lane.get("worker_result") or "COMPLETE",
            "validation_gap": dict(lane.get("validation_gap") or {
                "status": "NONE", "reason": "", "at": None,
            }),
            "worker_output_tail": tail_text(Path(lane.get("worker_log") or ""), 8000),
        }
        if reasons and lane["validation"].get("validation_gap", {}).get("status") == "PRESENT":
            lane["validation"]["validation_gap"]["status"] = "INVALIDATED"
            lane["validation"]["validation_gap"]["invalidated_reason"] = (
                "a structural or configured validation check failed"
            )
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
            "worker_result": lane.get("validation", {}).get("worker_result"),
            "worker_phase_id": lane.get("worker_phase_id"),
            "worker_attempt": lane.get("worker_attempt"),
            "worker_log": lane.get("worker_log"),
            "worker_exit_code": lane.get("worker_exit_code"),
            "validation_gap": lane.get("validation", {}).get("validation_gap"),
            "prospective_commit": lane.get("validation", {}).get("structural", {}).get(
                "prospective_commit"
            ),
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

    def complete_phase(self, lane_name: str, notify_review: bool = True) -> None:
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
                notify_external=notify_review,
            )
            return
        lane["phase_index"] = int(lane.get("phase_index", 0)) + 1
        lane.update({
            "phase_failure_count": 0, "recovery_attempts": 0,
            "push_attempts": 0,
        })
        self._reset_phase_transient_metadata(lane, lane.get("last_commit"))
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

    def repair_population_l05_checkpoint(self, phase_id: str, reviewed_sha: str) -> bool:
        """Convert only the clean-commit empty-diff artifact into the normal checkpoint state."""
        lane_name = "population"
        lane = self.state.get("lanes", {}).get(lane_name)
        if not isinstance(lane, dict) or phase_id != "L05":
            return False
        review = lane.get("review", {})
        if (lane.get("state") != "BLOCKED" or lane.get("phase_id") != phase_id
                or review.get("type") != "CURRENT_PHASE_REVIEW"
                or review.get("decision") != "SOL_BLOCKED"
                or str(lane.get("last_commit") or "") != reviewed_sha
                or str(review.get("commit_sha") or "") != reviewed_sha
                or self.git_head(str(lane.get("worktree") or "")) != reviewed_sha):
            return False
        if lane.get("worker_result") != "COMPLETE_WITH_VALIDATION_GAP":
            return False
        validation = lane.get("validation", {})
        structural = validation.get("structural", {}) if isinstance(validation, dict) else {}
        prospective = structural.get("prospective_commit", {}) if isinstance(structural, dict) else {}
        if (structural.get("status") != "FAIL" or validation.get("changed_files")
                or prospective.get("files") != [] or prospective.get("status") != "PASS"
                or prospective.get("diff_check") is not True
                or prospective.get("real_index_unchanged") is not True
                or structural.get("unstaged_diff_check") is not True
                or structural.get("staged_diff_check") is not True
                or lane.get("success_since_review") != 2
                or lane.get("validation_gap", {}).get("status") != "PRESENT"):
            return False
        tree = str(lane.get("worktree") or self.config["lanes"][lane_name]["worktree"])
        expected_branch = str(lane.get("branch") or self.config["lanes"][lane_name].get("branch") or "")
        if expected_branch and self.git_branch(tree) != expected_branch:
            return False
        status_ok, entries, _ = self.git_status_details(tree)
        if not status_ok or entries or any(entry.get("kind") == "unmerged" for entry in entries):
            return False
        base = self._trusted_previous_accepted_head(lane, phase_id)
        if not base:
            return False
        try:
            evidence = self._committed_phase_evidence(lane_name, lane, "POST_PHASE_CHECKPOINT", phase_id, tree, reviewed_sha)
        except ReviewEvidenceError:
            return False
        files = evidence.get("committed_phase_files", [])
        diff = str(evidence.get("committed_phase_diff") or "")
        if not files or not diff:
            return False
        if len(files) > int(self.config.get("max_changed_files", 40)):
            return False
        if len(diff.encode("utf-8", errors="replace")) > int(self.config.get("max_total_diff_bytes", 524288)):
            return False
        forbidden = self.forbidden_change_reasons(lane_name, [str(row.get("path") or "") for row in files])
        if forbidden:
            return False
        check, _, _ = self.command(["git", "-C", tree, "diff", "--check", base, reviewed_sha], cwd=tree, timeout=90)
        if check != 0 or not self._review_ci_is_exact_pass(lane, reviewed_sha)[0]:
            return False
        if self.state.get("control_plane", {}).get("status") != "VALID" or not self._control_plane_valid():
            return False
        snapshot = json.loads(json.dumps(lane))
        lane_validation = lane.setdefault("validation", {})
        lane_validation["status"] = "PASS"
        lane_validation["changed_files"] = [str(row.get("path") or "") for row in files]
        lane_validation["diffstat"] = evidence.get("committed_phase_stat", "")
        lane_validation["diff_text"] = diff
        lane_validation["diff_metrics"] = {
            "changed_files": len(files), "total_diff_bytes": len(diff.encode("utf-8", errors="replace")),
            "untracked_bytes": 0, "untracked_review_bytes": 0, "untracked_review_issues": [],
        }
        lane_validation.setdefault("structural", {}).update({
            "status": "PASS", "committed_phase_recovery": {
                "base_previous_accepted_head": base, "reviewed_sha": reviewed_sha,
                "committed_phase_diff_sha256": evidence["committed_phase_diff_sha256"],
                "committed_phase_files": files, "phase_commits": evidence["phase_commits"],
                "reason": "prior structural failure was the clean-worktree empty-diff check; committed phase invariants revalidated",
            },
        })
        self.event("reconciled Population L05 committed completion with normal checkpoint semantics", lane_name)
        self.complete_phase(lane_name, notify_review=False)
        self._recompute_scheduler()
        current = self.state["lanes"][lane_name]
        repaired = (
            current.get("state") == "NEEDS_SOL_REVIEW"
            and current.get("review", {}).get("type") == "POST_PHASE_CHECKPOINT"
            and current.get("review", {}).get("commit_sha") == reviewed_sha
            and (current.get("review", {}).get("reviewed_phase") or {}).get("id") == phase_id
            and current.get("phase_index") == lane.get("phase_index")
        )
        if not repaired:
            self.state["lanes"][lane_name] = snapshot
            self._recompute_scheduler()
            return False
        return True

    def review(
        self,
        lane_name: str,
        reasons: list[str],
        review_type: str | None = None,
        reviewed_phase: dict[str, Any] | None = None,
        commit_sha: str | None = None,
        next_phase: dict[str, Any] | None = None,
        notify_external: bool = True,
        review_tier: str | None = None,
        escalation_reason: str | None = None,
    ) -> None:
        lane = self.state["lanes"][lane_name]
        clean = sorted(set(redact(reason) for reason in reasons if reason))
        lane["review_reasons"] = clean or ["human review requested"]
        if review_type is None:
            if any("final milestone" in reason or "queue completed" in reason for reason in lane["review_reasons"]):
                review_type = "FINAL_MILESTONE_OR_QUEUE_REVIEW"
            else:
                review_type = "CURRENT_PHASE_REVIEW"
        if review_type == "CURRENT_PHASE_REVIEW":
            evidence = self._current_phase_review_evidence(lane_name)
            if evidence.get("kind") != "reviewable":
                self._suppress_unreviewable_current_phase_request(
                    lane_name, lane["review_reasons"], evidence,
                )
                return
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
        selected_tier, selected_escalation_reason = self._review_tier_for_request(
            lane_name,
            review_type,
            lane["review_reasons"],
            explicit_tier=review_tier,
            explicit_reason=escalation_reason,
        )
        route = self._review_route(selected_tier)
        lane["review"] = {
            "type": review_type,
            "reviewed_phase": current_phase,
            "commit_sha": commit_sha if commit_sha is not None else lane.get("last_commit"),
            "next_phase": next_phase,
            "created_at": utc_now(),
            "decision": None,
            "review_tier": selected_tier,
            "selected_model": route["model"],
            "selected_reasoning_effort": route["reasoning_effort"],
            "escalation_reason": selected_escalation_reason,
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
        if notify_external:
            self.post_issue_update(lane_name, lane["review_reasons"])
        self.event(
            f"lane stopped for {selected_tier} review using {route['model']}/{route['reasoning_effort']}",
            lane_name,
        )

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
        review["decision"] = "APPROVED_ADVANCE_ONCE"
        review["decided_at"] = utc_now()
        self._reset_phase_transient_metadata(lane, lane.get("last_commit"))
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

    def _review_file_text(
        self, value: str | Path | None, allowed_root: Path | None, limit: int
    ) -> tuple[str | None, str]:
        if not value:
            return None, "not configured"
        try:
            path = Path(str(value))
            if allowed_root is not None:
                root = allowed_root.resolve()
                resolved = path.resolve()
                resolved.relative_to(root)
                path = resolved
            metadata = path.lstat()
            if path.is_symlink() or not path.is_file():
                return None, "not a regular file"
            if metadata.st_size > limit:
                return None, f"omitted: {metadata.st_size} bytes exceeds {limit}-byte bound"
            content = path.read_text(encoding="utf-8", errors="replace")
            return redact(content), "included"
        except (OSError, ValueError) as exc:
            return None, f"unavailable: {type(exc).__name__}"

    def build_review_bundle(self, lane_name: str) -> dict[str, Any]:
        return super().build_review_bundle(lane_name)

    def _phase_counter(self, lane_name: str, phase_id: str, commit_sha: str) -> dict[str, Any]:
        review = self.state.setdefault("architect_review", {})
        counters = review.setdefault("phase_counters", {})
        key = f"{lane_name}:{phase_id}"
        counter = counters.get(key)
        if not isinstance(counter, dict) or counter.get("phase") != phase_id or counter.get("commit_sha") != commit_sha:
            counter = {
                "phase": phase_id,
                "commit_sha": commit_sha,
                "development_attempts": 0,
                "ci_repair_attempts": 0,
                "architect_review_attempts": 0,
                "consecutive_retry_cycles": 0,
                "last_failure_fingerprint": None,
                "same_fingerprint_cycles": 0,
            }
            counters[key] = counter
        lane = self.state.get("lanes", {}).get(lane_name, {})
        counter["development_attempts"] = int(lane.get("worker_attempt", 0))
        return counter

    def queue_sol_reviews(self) -> int:
        return super().queue_sol_reviews()

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
            f"- prospective temporary-index check: {(structural.get('prospective_commit') or {}).get('status', 'not recorded')}",
            f"- real index unchanged during preview: {(structural.get('prospective_commit') or {}).get('real_index_unchanged', 'not recorded')}",
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
- review tier: {review_info.get('review_tier') or 'architect'}
- selected model: {review_info.get('selected_model') or 'not selected'}
- selected reasoning effort: {review_info.get('selected_reasoning_effort') or 'not selected'}
- escalation reason: {review_info.get('escalation_reason') or 'ordinary bounded review'}
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

## Worker result semantics

- worker result: {validation.get('worker_result') or lane.get('worker_result') or 'not recorded'}
- validation gap: {json.dumps(validation.get('validation_gap') or lane.get('validation_gap') or {}, sort_keys=True)}

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
        # RECOVERING is runnable, but schedule() is the sole worker-admission
        # authority.  Keeping this state transition-free prevents a lane from
        # bypassing the global concurrency cap during resume.

    def schedule(self) -> None:
        if self.state.get("global_mode") != "RUNNING":
            return
        limit = self._worker_limit()
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
        self.process_operator_requests()
        self.poll_workers()
        self.sync_control_plane()
        self.periodic_guards()
        self.reconcile_invalid_empty_current_phase_reviews()
        self._recompute_scheduler()
        for lane_name in LANE_ORDER:
            self.advance_lane(lane_name)
        if self.state.get("global_mode") == "RUNNING" and self.periodic_guards():
            self.schedule()
        try:
            self.architect_review_tick()
        except Exception as exc:
            review_state = self.state.setdefault("architect_review", {})
            review_state["idle_reason"] = f"architect review tick failed: {type(exc).__name__}"
            self.log(f"architect review tick failed safely: {type(exc).__name__}: {redact(str(exc))}")
        if time.monotonic() - self.last_save > 10:
            self.save_state()

    def shutdown(self) -> None:
        for lane_name in list(self.processes):
            process = self.processes.get(lane_name)
            self.terminate_worker(lane_name, "supervisor stopping")
            if process is None:
                continue
            thread = self.output_threads.pop(lane_name, None)
            if thread:
                thread.join(timeout=5)
            lane = self.state["lanes"][lane_name]
            lane["worker_exit_code"] = process.returncode
            lane["worker_interrupted"] = True
            self.processes.pop(lane_name, None)
            self._reconcile_interrupted_worker(lane_name, lane)
        self._recompute_scheduler()
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

    def scheduler_idle_summary(self) -> dict[str, Any]:
        scheduler = self.state.get("scheduler", {})
        records = scheduler.get("tasks", {}) if isinstance(scheduler, dict) else {}
        runnable: list[str] = []
        review_gated: list[str] = []
        waiting_checkpoint: list[str] = []
        for lane_name in LANE_ORDER:
            lane = self.state.get("lanes", {}).get(lane_name, {})
            review = lane.get("review", {})
            terminal_architect_block = (
                lane.get("state") == "BLOCKED"
                and review.get("decision") in {"SOL_BLOCKED", "LUNA_BLOCKED"}
                and str(review.get("commit_sha") or "") == str(lane.get("last_commit") or "")
            )
            if lane.get("state") in REVIEW_WAITING_STATES or terminal_architect_block:
                review_gated.append(lane_name)
            if (lane.get("state") in REVIEW_WAITING_STATES or terminal_architect_block) and review.get("type") == "POST_PHASE_CHECKPOINT":
                waiting_checkpoint.append(lane_name)
            record = records.get(lane.get("phase_id")) if isinstance(records, dict) else None
            if (
                lane.get("state") in RUNNABLE_STATES
                and isinstance(record, dict)
                and record.get("state") == "READY"
                and lane_name not in getattr(self, "processes", {})
            ):
                runnable.append(lane_name)
        external = sum(
            1
            for record in records.values() if isinstance(record, dict)
            and record.get("source") == "ROADMAP"
            and record.get("state") == "BLOCKED_EXTERNAL_GATE"
        ) if isinstance(records, dict) else 0
        architect = self.state.get("architect_review", {})
        review_items = architect.get("items", {}) if isinstance(architect, dict) else {}
        queued_reviews = sorted(
            review_id for review_id, item in review_items.items()
            if isinstance(item, dict) and item.get("status") in {
                "QUEUED", "WAITING_FOR_ARCHITECT_MODEL", "WAITING_FOR_LUNA_MODEL"
            }
        ) if isinstance(review_items, dict) else []
        evidence_errors = architect.get("evidence_errors", {}) if isinstance(architect, dict) else {}
        evidence_error_lanes = sorted({
            str(item.get("lane")) for item in evidence_errors.values()
            if isinstance(item, dict) and item.get("status") in {"EVIDENCE_ERROR", "REQUIRES_INFRA_REVIEW"}
        }) if isinstance(evidence_errors, dict) else []
        active_review = architect.get("active_review_id") if isinstance(architect, dict) else None
        availability = architect.get("availability", {}) if isinstance(architect, dict) else {}
        tier_availability = architect.get("tier_availability", {}) if isinstance(architect, dict) else {}
        if not isinstance(tier_availability, dict):
            tier_availability = {}
        if runnable:
            idle_reason = "runnable work is waiting for Luna worker capacity or availability"
        elif active_review:
            idle_reason = "the independent Sol architect reviewer is working"
        elif queued_reviews:
            waits = []
            for review_id in queued_reviews:
                item = review_items.get(review_id, {}) if isinstance(review_items, dict) else {}
                tier = str(item.get("review_tier") or "architect") if isinstance(item, dict) else "architect"
                record = tier_availability.get(tier, {})
                retry_at = record.get("next_retry_at") if isinstance(record, dict) else None
                if retry_at:
                    waits.append(f"{tier} until {retry_at}")
            idle_reason = "review is queued" + ("; backoff " + ", ".join(waits) if waits else "")
        elif evidence_error_lanes:
            idle_reason = "review evidence failure requires infrastructure review for " + ", ".join(evidence_error_lanes)
        elif review_gated and external:
            idle_reason = "active lanes blocked by architect decisions; future roadmap tasks are externally gated"
        elif review_gated:
            failed = any(
                isinstance(item, dict) and item.get("status") == "FAILED"
                for item in review_items.values()
            ) if isinstance(review_items, dict) else False
            idle_reason = "reviewer failure limit reached; human review is required" if failed else "active lanes are blocked or gated on exact-state architect review"
        elif external:
            idle_reason = "remaining roadmap tasks are externally gated"
        elif self.state.get("global_mode") != "RUNNING":
            idle_reason = "global mode is paused"
        else:
            idle_reason = "no runnable active-phase work is currently available"
        return {
            "runnable": runnable,
            "review_gated": sorted(set(review_gated)),
            "waiting_checkpoint": sorted(set(waiting_checkpoint)),
            "review_evidence_error_lanes": evidence_error_lanes,
            "external_gated_future_tasks": external,
            "queued_reviews": queued_reviews,
            "active_review": active_review,
            "reviewer_availability": availability.get("status", "UNKNOWN") if isinstance(availability, dict) else "UNKNOWN",
            "reviewer_availability_by_tier": tier_availability,
            "reviewer_cap": self._reviewer_limit(),
            "idle_reason": idle_reason,
        }

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
            print(f"worker result: {lane.get('worker_result') or 'none'}")
            gap = lane.get("validation_gap", {})
            if isinstance(gap, dict) and gap.get("status") == "PRESENT":
                print(f"validation gap: {gap.get('reason') or 'unspecified'}")
            review = lane.get("review", {})
            if review.get("type"):
                print(
                    f"review: {review.get('type')} phase={review.get('reviewed_phase')} "
                    f"next={review.get('next_phase') or 'none'}"
                )
                print(
                    f"  tier={review.get('review_tier') or 'architect'} "
                    f"model={review.get('selected_model') or 'not selected'} "
                    f"effort={review.get('selected_reasoning_effort') or 'not selected'} "
                    f"escalation={review.get('escalation_reason') or 'ordinary bounded review'}"
                )
            if lane.get("review_packet"):
                print(f"review packet: {lane['review_packet']}")
            print()
        print("FUTURE BLOCKED WORK")
        for task_id, record in self.state.get("scheduler", {}).get("tasks", {}).items():
            if isinstance(record, dict) and record.get("source") == "ROADMAP" and record.get("state") != "DONE":
                print(f"{task_id} {record.get('state')}: {record.get('reason') or 'none'}")
        architect = self.state.get("architect_review", {})
        architect_items = architect.get("items", {}) if isinstance(architect, dict) else {}
        queued_reviews = sum(
            1 for item in architect_items.values()
            if isinstance(item, dict) and item.get("status") == "QUEUED"
        ) if isinstance(architect_items, dict) else 0
        active_review = architect.get("active_review_id") if isinstance(architect, dict) else None
        availability = architect.get("availability", {}) if isinstance(architect, dict) else {}
        tier_availability = architect.get("tier_availability", {}) if isinstance(architect, dict) else {}
        print("AUTONOMOUS SOL REVIEW")
        print(f"enabled: {bool(architect.get('enabled')) if isinstance(architect, dict) else False}")
        print(f"active review: {active_review or 'none'}")
        print(f"queued reviews: {queued_reviews}")
        print(f"reviewer availability: {availability.get('status', 'UNKNOWN') if isinstance(availability, dict) else 'UNKNOWN'}")
        if isinstance(tier_availability, dict):
            for tier in ("normal", "architect"):
                record = tier_availability.get(tier, {})
                print(
                    f"  {tier}: {record.get('status', 'UNKNOWN')} "
                    f"model={record.get('selected_model') or record.get('configured_model') or 'not selected'} "
                    f"effort={record.get('selected_reasoning_effort') or 'not selected'} "
                    f"next_retry_at={record.get('next_retry_at') or 'none'}"
                )
        print(f"reviewer cap: {self._reviewer_limit()}")
        print(f"idle reason: {architect.get('idle_reason') or 'none' if isinstance(architect, dict) else 'unavailable'}")
        if self.state.get("global_mode") == "RUNNING":
            idle = self.scheduler_idle_summary()
            if not idle["runnable"]:
                print()
                print("SCHEDULER IDLE SUMMARY")
                print("RUNNABLE WORK: 0")
                print(
                    "REVIEW-GATED LANES: "
                    + (", ".join(idle["review_gated"]) or "none")
                )
                print(
                    "WAITING CHECKPOINT: "
                    + (", ".join(idle["waiting_checkpoint"]) or "none")
                )
                print(
                    "EXTERNAL-GATED FUTURE TASKS: "
                    + str(idle["external_gated_future_tasks"])
                )
                print(f"IDLE REASON: {idle['idle_reason']}")
                print("QUEUED SOL REVIEWS: " + str(len(idle["queued_reviews"])))
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
                print(f"  tier: {review.get('review_tier') or 'architect'}")
                print(f"  selected_model: {review.get('selected_model') or 'not selected'}")
                print(f"  selected_reasoning_effort: {review.get('selected_reasoning_effort') or 'not selected'}")
                print(f"  escalation_reason: {review.get('escalation_reason') or 'ordinary bounded review'}")
        architect = self.state.get("architect_review", {})
        if isinstance(architect, dict):
            availability = architect.get("availability", {})
            tier_availability = architect.get("tier_availability", {})
            print("AUTONOMOUS SOL REVIEW")
            print(f"  enabled: {bool(architect.get('enabled'))}")
            print(f"  active_review_id: {architect.get('active_review_id') or 'none'}")
            print(f"  queued: {len(architect.get('queue', []))}")
            print(f"  availability: {availability.get('status', 'UNKNOWN') if isinstance(availability, dict) else 'UNKNOWN'}")
            print(f"  next_retry_at: {availability.get('next_retry_at') or 'none' if isinstance(availability, dict) else 'none'}")
            if isinstance(tier_availability, dict):
                for tier in ("normal", "architect"):
                    record = tier_availability.get(tier, {})
                    print(
                        f"  {tier}: status={record.get('status', 'UNKNOWN')} "
                        f"model={record.get('selected_model') or record.get('configured_model') or 'not selected'} "
                        f"effort={record.get('selected_reasoning_effort') or 'not selected'} "
                        f"next_retry_at={record.get('next_retry_at') or 'none'}"
                    )
            print(f"  reviewer_cap: {self._reviewer_limit()}")
            print(f"  idle_reason: {architect.get('idle_reason') or 'none'}")
            evidence_errors = architect.get("evidence_errors", {})
            if isinstance(evidence_errors, dict):
                active_errors = [item for item in evidence_errors.values()
                                 if isinstance(item, dict) and item.get("status") in {"EVIDENCE_ERROR", "REQUIRES_INFRA_REVIEW"}]
                print(f"  evidence_errors: {len(active_errors)}")
                for item in sorted(active_errors, key=lambda row: (str(row.get("lane")), str(row.get("phase")))):
                    retry = item.get("next_retry_at") or "none"
                    print(f"    {item.get('status')} lane={item.get('lane')} phase={item.get('phase')} sha={item.get('reviewed_sha')} code={item.get('error_code')} attempts={item.get('attempts')} retry_at={retry}")
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
        operator_paths = self._operator_paths()
        worker_roots = [
            Path(str(spec.get("worktree"))).resolve()
            for spec in self.config.get("lanes", {}).values()
            if isinstance(spec, dict) and spec.get("worktree")
        ]
        try:
            operator_root = operator_paths["root"].resolve()
            outside_workers = all(
                root != operator_root and root not in operator_root.parents
                for root in worker_roots
            )
            private = os.name == "nt" or all(
                (path.stat().st_mode & 0o077) == 0
                for path in operator_paths.values()
            )
        except OSError:
            outside_workers = False
            private = False
        check("operator request inbox outside worker workspaces", outside_workers and private)
        check(
            "worker sandbox negative inbox contract",
            "WORKER_OPERATOR_INBOX: BLOCKED" in " ".join(self.worker_smoke_command(Path("/tmp/smoke"))),
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
            dirty = self.git_dirty(lane["worktree"])
            if not dirty:
                check(f"{lane_name} worktree clean", True)
            else:
                review = lane.get("review", {})
                status_ok, entries, status_error = self.git_status_details(lane["worktree"])
                conflict_free = status_ok and not any(
                    entry.get("kind") == "unmerged" for entry in entries
                )
                recovery_context = lane.get("recovery_context")
                retry_evidence = (
                    recovery_context.get("architect_review", {})
                    if isinstance(recovery_context, dict) else {}
                )
                applied_retry = any(
                    isinstance(item, dict)
                    and item.get("status") == "APPLIED"
                    and item.get("lane") == lane_name
                    and item.get("review_id") == retry_evidence.get("review_id")
                    and isinstance(item.get("decision"), dict)
                    and item["decision"].get("decision") == "RETRY"
                    for item in self.state.get("architect_review", {}).get("items", {}).values()
                )
                bounded_review_recovery = (
                    lane.get("state") == "RECOVERING"
                    and isinstance(retry_evidence, dict)
                    and bool(retry_evidence.get("required_actions"))
                    and applied_retry
                )
                legacy_retry_reviewable = any(
                    isinstance(item, dict)
                    and item.get("lane") == lane_name
                    and should_recheck_legacy_blocked_retry(item, lane)
                    for item in self.state.get("architect_review", {}).get("items", {}).values()
                ) and bool(entries)
                expected_recovery_diff = (
                    lane.get("state") in {"NEEDS_SOL_REVIEW", "PAUSED"}
                    or bounded_review_recovery
                    or legacy_retry_reviewable
                ) and review.get("type") == "CURRENT_PHASE_REVIEW" and not lane.get("worker_pid")
                check(
                    f"{lane_name} expected review worktree preserved",
                    expected_recovery_diff and conflict_free,
                )
                if not conflict_free and status_error:
                    print(f"  {lane_name} status evidence: {status_error}")
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


def run_daemon() -> int:
    """Acquire the daemon lock before constructing a runtime owner."""
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    with LOCK_PATH.open("w", encoding="utf-8") as lock_handle:
        if fcntl is not None:
            try:
                fcntl.flock(lock_handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                print("another supervisor instance is already running", file=sys.stderr)
                return 2

        # Startup reconciliation is intentionally below the lock acquisition.
        supervisor = Supervisor(runtime_owner=True)

        def stop_handler(signum: int, frame: Any) -> None:
            supervisor.stop_requested = True

        signal.signal(signal.SIGTERM, stop_handler)
        signal.signal(signal.SIGINT, stop_handler)
        return supervisor.run()


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
    sub.add_parser("worker-smoke-test")
    sub.add_parser("architect-review-smoke-test")
    sub.add_parser("normal-review-smoke-test")
    re_review = sub.add_parser("re-review-v3")
    re_review.add_argument("--target", action="append", nargs=3, required=True,
                           metavar=("LANE", "PHASE", "SHA"))
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

    if args.command == "run":
        return run_daemon()

    if args.command in OPERATOR_MUTATION_COMMANDS:
        operator_args: dict[str, Any]
        if args.command in {"approve", "retry", "block"}:
            operator_args = {"lane": args.lane}
        elif args.command == "approve-task":
            operator_args = {"task_id": args.task_id}
        elif args.command == "approve-control-plane":
            operator_args = {"sha": args.sha}
        elif args.command == "re-review-v3":
            operator_args = {"targets": [
                {"lane": lane, "phase": phase, "sha": sha}
                for lane, phase, sha in args.target
            ]}
        elif args.command == "accept-milestone":
            evidence: dict[str, Any] = {}
            if args.evidence_file:
                try:
                    evidence = json.loads(
                        Path(args.evidence_file).read_text(encoding="utf-8")
                    )
                except (OSError, json.JSONDecodeError) as exc:
                    print(f"cannot read evidence file: {exc}", file=sys.stderr)
                    return 2
                if not isinstance(evidence, dict):
                    print("evidence file must contain a JSON object", file=sys.stderr)
                    return 2
            operator_args = {"milestone_id": args.milestone_id, "evidence": evidence}
        else:
            operator_args = {"force": True}
        return submit_operator_request(
            args.command,
            operator_args,
            config=read_json(CONFIG_PATH, {}),
        )

    supervisor = Supervisor(runtime_owner=False)
    if args.command == "status":
        return supervisor.status()
    if args.command == "roadmap-status":
        return supervisor.roadmap_status()
    if args.command == "milestone-status":
        return supervisor.milestone_status()
    if args.command == "review-status":
        return supervisor.review_status()
    if args.command == "healthcheck":
        return supervisor.healthcheck()
    if args.command == "self-test":
        return supervisor.self_test()
    if args.command == "worker-smoke-test":
        return supervisor.worker_smoke_test()
    if args.command == "architect-review-smoke-test":
        return supervisor.architect_review_smoke_test()
    if args.command == "normal-review-smoke-test":
        return supervisor.normal_review_smoke_test()
    raise AssertionError(f"unhandled command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
