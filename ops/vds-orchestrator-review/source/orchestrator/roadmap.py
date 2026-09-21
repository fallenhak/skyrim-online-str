"""Validated control-plane roadmap and deterministic dependency scheduling.

This module deliberately has no Git, network, worker, or operating-system
side effects.  The supervisor owns trusted control-plane synchronization and
passes a validated snapshot here.  Keeping parsing and scheduling pure makes
malformed/cyclic roadmap behavior deterministic and easy to test.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


KNOWN_LANES = ("combat", "authority", "population", "ui")
TASK_STATES = {
    "READY",
    "RUNNING",
    "BLOCKED_DEPENDENCY",
    "BLOCKED_EXTERNAL_GATE",
    "NEEDS_SOL_REVIEW",
    "PAUSED",
    "DONE",
}
MILESTONE_STATES = {
    "ACTIVE",
    "ENGINEERING_COMPLETE",
    "WAITING_RUNTIME_ACCEPTANCE",
    "ACCEPTED",
    "BLOCKED",
    "PLANNED",
}
WORKSTREAM_MODES = {"EXISTING_PLAN", "ROADMAP_TASKS"}
WORKSTREAM_STATES = TASK_STATES | {
    "ACTIVE",
    "PLANNED",
    "ENGINEERING_COMPLETE",
}


class RoadmapValidationError(ValueError):
    """Raised when a control-plane roadmap cannot be safely scheduled."""


def _error(path: str, message: str) -> RoadmapValidationError:
    return RoadmapValidationError(f"{path}: {message}")


def _string(value: Any, path: str, *, required: bool = True) -> str:
    if not isinstance(value, str) or not value.strip():
        if required:
            raise _error(path, "must be a non-empty string")
        return ""
    return value.strip()


def _string_list(value: Any, path: str) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, (list, tuple)) or any(
        not isinstance(item, str) or not item.strip() for item in value
    ):
        raise _error(path, "must be a list of non-empty strings")
    return tuple(item.strip() for item in value)


def _bool(value: Any, path: str, default: bool) -> bool:
    if value is None:
        return default
    if not isinstance(value, bool):
        raise _error(path, "must be boolean")
    return value


def _stable_digest(value: Mapping[str, Any]) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


@dataclass(frozen=True)
class RoadmapTask:
    """Compatibility adapter used by existing lane prompts and tests."""

    milestone: str
    task_id: str
    lane: str
    dependencies: tuple[str, ...]
    risk: str
    requires_sol_review: bool
    acceptance_criteria: tuple[str, ...]
    title: str = ""
    source: str = "EXISTING_PLAN"
    workstream_id: str = ""
    control_plane_sha: str | None = None


@dataclass(frozen=True)
class ScheduledTask:
    """Common internal task identity persisted by the supervisor."""

    milestone_id: str
    workstream_id: str
    task_id: str
    lane: str
    title: str
    source: str
    dependencies: tuple[str, ...]
    blocked_by: tuple[str, ...]
    external_gates: tuple[str, ...]
    risk: str
    requires_sol_review: bool
    acceptance_criteria: tuple[str, ...]
    start_phase: str | None = None
    control_plane_sha: str | None = None
    definition_hash: str = ""

    def definition(self) -> dict[str, Any]:
        return {
            "milestone_id": self.milestone_id,
            "workstream_id": self.workstream_id,
            "task_id": self.task_id,
            "lane": self.lane,
            "title": self.title,
            "source": self.source,
            "dependencies": list(self.dependencies),
            "blocked_by": list(self.blocked_by),
            "external_gates": list(self.external_gates),
            "risk": self.risk,
            "requires_sol_review": self.requires_sol_review,
            "acceptance_criteria": list(self.acceptance_criteria),
            "start_phase": self.start_phase,
        }

    def to_dict(self) -> dict[str, Any]:
        result = self.definition()
        result.update({
            "control_plane_sha": self.control_plane_sha,
            "definition_hash": self.definition_hash,
        })
        return result


@dataclass(frozen=True)
class RoadmapSnapshot:
    """Validated, immutable view of one control-plane roadmap."""

    raw: dict[str, Any]
    version: int
    current_milestone: str
    scheduler_policy: dict[str, Any]
    milestones: dict[str, dict[str, Any]]
    workstreams: dict[str, dict[str, Any]]
    tasks: dict[str, ScheduledTask]
    sha: str | None = None
    file_hashes: dict[str, str] | None = None

    @property
    def content_hash(self) -> str:
        return _stable_digest(self.raw)

    def task_definitions(self) -> dict[str, str]:
        return {
            task_id: task.definition_hash
            for task_id, task in self.tasks.items()
        }

    def milestone(self, milestone_id: str) -> dict[str, Any]:
        return self.milestones[milestone_id]


def task_definition_digest(task: Mapping[str, Any]) -> str:
    """Return a stable identity hash for approval binding."""
    fields = {
        key: task.get(key)
        for key in (
            "milestone_id",
            "workstream_id",
            "task_id",
            "lane",
            "title",
            "source",
            "dependencies",
            "blocked_by",
            "external_gates",
            "risk",
            "requires_sol_review",
            "acceptance_criteria",
            "start_phase",
        )
    }
    return _stable_digest(fields)


def _future_lane_is_explicit(workstream: Mapping[str, Any], mode: str) -> bool:
    """Recognize only an explicitly non-existent/future lane definition."""
    return (
        mode == "ROADMAP_TASKS"
        and (
            workstream.get("future") is True
            or ("branch" in workstream and workstream.get("branch") is None)
        )
        and workstream.get("status") in {
            "PLANNED",
            "BLOCKED",
            "BLOCKED_DEPENDENCY",
            "BLOCKED_EXTERNAL_GATE",
            "PAUSED",
        }
    )


def _check_cycles(graph: Mapping[str, Iterable[str]]) -> None:
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(node: str, trail: tuple[str, ...] = ()) -> None:
        if node in visiting:
            cycle = " -> ".join((*trail, node))
            raise _error("dependencies", f"dependency cycle detected ({cycle})")
        if node in visited:
            return
        visiting.add(node)
        for dependency in graph.get(node, ()):
            visit(dependency, (*trail, node))
        visiting.remove(node)
        visited.add(node)

    for node in graph:
        visit(node)


def validate_roadmap(
    data: Mapping[str, Any],
    *,
    known_lanes: Sequence[str] = KNOWN_LANES,
    source_sha: str | None = None,
    file_hashes: Mapping[str, str] | None = None,
) -> RoadmapSnapshot:
    """Validate and normalize a roadmap without applying any state changes."""
    if not isinstance(data, Mapping):
        raise _error("roadmap", "top level must be an object")

    version = data.get("version")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        raise _error("version", "must be a positive integer")
    current_milestone = _string(data.get("current_milestone"), "current_milestone")
    policy = data.get("scheduler_policy")
    if not isinstance(policy, Mapping):
        raise _error("scheduler_policy", "must be an object")

    milestones_raw = data.get("milestones")
    if not isinstance(milestones_raw, list) or not milestones_raw:
        raise _error("milestones", "must be a non-empty list")

    milestones: dict[str, dict[str, Any]] = {}
    workstreams: dict[str, dict[str, Any]] = {}
    tasks: dict[str, ScheduledTask] = {}
    all_ids: set[str] = set()
    dependency_graph: dict[str, set[str]] = {}
    known = set(known_lanes)

    def reserve(identifier: str, path: str) -> None:
        if identifier in all_ids:
            raise _error(path, f"duplicate ID {identifier}")
        all_ids.add(identifier)
        dependency_graph.setdefault(identifier, set())

    def add_task(
        task_raw: Mapping[str, Any],
        *,
        milestone_id: str,
        milestone: Mapping[str, Any],
        workstream_id: str,
        workstream: Mapping[str, Any],
        path: str,
    ) -> None:
        task_id = _string(task_raw.get("id"), f"{path}.id")
        title = _string(task_raw.get("title"), f"{path}.title")
        task_lane = _string(
            task_raw.get("lane", workstream.get("lane")),
            f"{path}.lane",
        )
        mode = str(workstream.get("mode"))
        if task_lane != workstream.get("lane"):
            raise _error(
                f"{path}.lane",
                f"must match workstream lane {workstream.get('lane')}",
            )
        if task_lane not in known and not _future_lane_is_explicit(workstream, mode):
            raise _error(
                f"{path}.lane",
                "unknown lane is not explicitly defined as future/nonexistent",
            )

        task_status = task_raw.get("status")
        if task_status is not None and task_status not in TASK_STATES:
            raise _error(f"{path}.status", f"invalid state {task_status!r}")
        executable = _bool(
            task_raw.get("executable"),
            f"{path}.executable",
            True,
        )
        if not milestone["executable"] and (
            executable or task_status in {"READY", "RUNNING", "DONE"}
        ):
            raise _error(
                path,
                "contains an executable task under executable=false milestone",
            )

        dependencies = _string_list(
            task_raw.get(
                "depends_on",
                task_raw.get("dependencies", []),
            ),
            f"{path}.depends_on",
        )
        acceptance = _string_list(
            task_raw.get(
                "acceptance_criteria",
                task_raw.get("acceptance", milestone.get("acceptance", [])),
            ),
            f"{path}.acceptance_criteria",
        )
        risk = _string(task_raw.get("risk", "unspecified"), f"{path}.risk")
        requires_sol_review = _bool(
            task_raw.get("requires_sol_review"),
            f"{path}.requires_sol_review",
            False,
        )
        blocked_by = _string_list(
            task_raw.get("blocked_by", []),
            f"{path}.blocked_by",
        )
        external_gates = _string_list(
            task_raw.get("external_gates", workstream.get("external_gates", [])),
            f"{path}.external_gates",
        )
        reserve(task_id, f"{path}.id")
        normalized = {
            "milestone_id": milestone_id,
            "workstream_id": workstream_id,
            "task_id": task_id,
            "lane": task_lane,
            "title": title,
            "source": "ROADMAP",
            "dependencies": list(dependencies),
            "blocked_by": list((*blocked_by, *workstream.get("blocked_by", ()))),
            "external_gates": list(external_gates),
            "risk": risk,
            "requires_sol_review": requires_sol_review,
            "acceptance_criteria": list(acceptance),
            "start_phase": None,
        }
        definition_hash = task_definition_digest(normalized)
        tasks[task_id] = ScheduledTask(
            milestone_id=milestone_id,
            workstream_id=workstream_id,
            task_id=task_id,
            lane=task_lane,
            title=title,
            source="ROADMAP",
            dependencies=dependencies,
            blocked_by=tuple((*blocked_by, *workstream.get("blocked_by", ()))),
            external_gates=external_gates,
            risk=risk,
            requires_sol_review=requires_sol_review,
            acceptance_criteria=acceptance,
            control_plane_sha=source_sha,
            definition_hash=definition_hash,
        )
        dependency_graph[task_id].update((*dependencies, *blocked_by))

    for milestone_index, milestone_raw in enumerate(milestones_raw):
        path = f"milestones[{milestone_index}]"
        if not isinstance(milestone_raw, Mapping):
            raise _error(path, "must be an object")
        milestone_id = _string(milestone_raw.get("id"), f"{path}.id")
        title = _string(milestone_raw.get("title"), f"{path}.title")
        status = milestone_raw.get("status", "PLANNED")
        if status not in MILESTONE_STATES:
            raise _error(f"{path}.status", f"invalid state {status!r}")
        executable = _bool(
            milestone_raw.get("executable"),
            f"{path}.executable",
            status == "ACTIVE" and milestone_id == current_milestone,
        )
        acceptance = _string_list(
            milestone_raw.get(
                "acceptance",
                milestone_raw.get("acceptance_criteria", []),
            ),
            f"{path}.acceptance",
        )
        reserve(milestone_id, f"{path}.id")
        milestones[milestone_id] = {
            "id": milestone_id,
            "title": title,
            "status": status,
            "executable": executable,
            "acceptance": acceptance,
            "objective": milestone_raw.get("objective", ""),
        }

        workstreams_raw = milestone_raw.get("workstreams", [])
        if not isinstance(workstreams_raw, list):
            raise _error(f"{path}.workstreams", "must be a list")
        for ws_index, ws_raw in enumerate(workstreams_raw):
            ws_path = f"{path}.workstreams[{ws_index}]"
            if not isinstance(ws_raw, Mapping):
                raise _error(ws_path, "must be an object")
            ws_id = _string(ws_raw.get("id"), f"{ws_path}.id")
            lane = _string(ws_raw.get("lane"), f"{ws_path}.lane")
            mode = _string(ws_raw.get("mode"), f"{ws_path}.mode")
            if mode not in WORKSTREAM_MODES:
                raise _error(f"{ws_path}.mode", f"invalid mode {mode!r}")
            ws_status = ws_raw.get(
                "status", "ACTIVE" if executable else "PLANNED"
            )
            if ws_status not in WORKSTREAM_STATES:
                raise _error(f"{ws_path}.status", f"invalid state {ws_status!r}")
            if lane not in known and not _future_lane_is_explicit(ws_raw, mode):
                raise _error(
                    f"{ws_path}.lane",
                    "unknown lane is not explicitly defined as future/nonexistent",
                )
            if lane not in known and ws_raw.get("branch") is not None:
                raise _error(
                    f"{ws_path}.branch",
                    "future/nonexistent lane must not carry a branch",
                )
            start_phase = ws_raw.get("start_phase")
            if mode == "EXISTING_PLAN":
                _string(start_phase, f"{ws_path}.start_phase")
            elif start_phase is not None and not isinstance(start_phase, str):
                raise _error(f"{ws_path}.start_phase", "must be a string or null")
            ws_dependencies = _string_list(
                ws_raw.get(
                    "depends_on",
                    ws_raw.get("dependencies", []),
                ),
                f"{ws_path}.depends_on",
            )
            ws_blocked_by = _string_list(
                ws_raw.get("blocked_by", []),
                f"{ws_path}.blocked_by",
            )
            external_gates = _string_list(
                ws_raw.get("external_gates", []),
                f"{ws_path}.external_gates",
            )
            reserve(ws_id, f"{ws_path}.id")
            workstreams[ws_id] = {
                "id": ws_id,
                "milestone_id": milestone_id,
                "lane": lane,
                "mode": mode,
                "status": ws_status,
                "start_phase": start_phase,
                "completion": ws_raw.get("completion"),
                "branch": ws_raw.get("branch"),
                "worktree": ws_raw.get("worktree"),
                "dependencies": ws_dependencies,
                "blocked_by": ws_blocked_by,
                "external_gates": external_gates,
            }
            dependency_graph[ws_id].update((*ws_dependencies, *ws_blocked_by))

            tasks_raw = ws_raw.get("tasks", [])
            if mode == "ROADMAP_TASKS" and not isinstance(tasks_raw, list):
                raise _error(f"{ws_path}.tasks", "must be a list")
            if mode == "EXISTING_PLAN" and tasks_raw not in ([], None):
                raise _error(
                    f"{ws_path}.tasks",
                    "EXISTING_PLAN workstreams cannot declare roadmap tasks",
                )
            for task_index, task_raw in enumerate(tasks_raw or []):
                task_path = f"{ws_path}.tasks[{task_index}]"
                if not isinstance(task_raw, Mapping):
                    raise _error(task_path, "must be an object")
                add_task(
                    task_raw,
                    milestone_id=milestone_id,
                    milestone=milestones[milestone_id],
                    workstream_id=ws_id,
                    workstream=workstreams[ws_id],
                    path=task_path,
                )

    if current_milestone not in milestones:
        raise _error(
            "current_milestone",
            f"references unknown milestone {current_milestone}",
        )

    top_level_tasks = data.get("tasks", [])
    if top_level_tasks not in (None, []):
        if not isinstance(top_level_tasks, list):
            raise _error("tasks", "must be a list")
        for task_index, task_raw in enumerate(top_level_tasks):
            task_path = f"tasks[{task_index}]"
            if not isinstance(task_raw, Mapping):
                raise _error(task_path, "must be an object")
            milestone_id = _string(
                task_raw.get("milestone_id"),
                f"{task_path}.milestone_id",
            )
            workstream_id = _string(
                task_raw.get("workstream_id"),
                f"{task_path}.workstream_id",
            )
            if milestone_id not in milestones:
                raise _error(
                    f"{task_path}.milestone_id",
                    f"unknown milestone {milestone_id}",
                )
            if workstream_id not in workstreams:
                raise _error(
                    f"{task_path}.workstream_id",
                    f"unknown workstream {workstream_id}",
                )
            add_task(
                task_raw,
                milestone_id=milestone_id,
                milestone=milestones[milestone_id],
                workstream_id=workstream_id,
                workstream=workstreams[workstream_id],
                path=task_path,
            )

    valid_dependency_ids = set(all_ids)
    for node, dependencies in dependency_graph.items():
        for dependency in dependencies:
            if dependency not in valid_dependency_ids:
                raise _error(
                    f"dependencies.{node}",
                    f"unknown dependency ID {dependency}",
                )
            if dependency == node:
                raise _error(
                    f"dependencies.{node}",
                    "a task/workstream cannot depend on itself",
                )
    _check_cycles(dependency_graph)

    return RoadmapSnapshot(
        raw=json.loads(json.dumps(data)),
        version=version,
        current_milestone=current_milestone,
        scheduler_policy=dict(policy),
        milestones=milestones,
        workstreams=workstreams,
        tasks=tasks,
        sha=source_sha,
        file_hashes=dict(file_hashes or {}),
    )


def load_roadmap(
    path: str | Path,
    *,
    known_lanes: Sequence[str] = KNOWN_LANES,
    source_sha: str | None = None,
    file_hashes: Mapping[str, str] | None = None,
) -> RoadmapSnapshot:
    """Load and validate one roadmap file."""
    path = Path(path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise _error(str(path), "file does not exist") from exc
    except OSError as exc:
        raise _error(str(path), f"cannot read ({exc})") from exc
    except json.JSONDecodeError as exc:
        raise _error(str(path), f"malformed JSON ({exc.msg})") from exc
    return validate_roadmap(
        data,
        known_lanes=known_lanes,
        source_sha=source_sha,
        file_hashes=file_hashes,
    )


def dependency_evaluation(
    task: ScheduledTask,
    *,
    task_states: Mapping[str, str],
    workstream_states: Mapping[str, str] | None = None,
    milestone_status: str = "ACTIVE",
    milestone_executable: bool = True,
    lane_available: bool = True,
    lane_busy: bool = False,
    global_mode: str = "RUNNING",
    control_plane_valid: bool = True,
    resolved_external_gates: Iterable[str] = (),
    approval: Mapping[str, Any] | None = None,
) -> tuple[str, str]:
    """Evaluate one task with explicit fail-closed reasons."""
    if not control_plane_valid:
        return "NEEDS_SOL_REVIEW", "control-plane snapshot is not valid"
    if not milestone_executable or milestone_status != "ACTIVE":
        return (
            "BLOCKED_DEPENDENCY",
            f"milestone {task.milestone_id} is not executable/active",
        )
    if task.external_gates:
        resolved = set(resolved_external_gates)
        unresolved = [gate for gate in task.external_gates if gate not in resolved]
        if unresolved:
            return (
                "BLOCKED_EXTERNAL_GATE",
                "unresolved external gate(s): " + "; ".join(unresolved),
            )

    missing = [
        dependency for dependency in task.dependencies
        if task_states.get(dependency) != "DONE"
    ]
    missing.extend(
        dependency for dependency in task.blocked_by
        if (workstream_states or {}).get(dependency, task_states.get(dependency))
        != "DONE"
    )
    if missing:
        return (
            "BLOCKED_DEPENDENCY",
            "waiting for " + ", ".join(dict.fromkeys(missing)),
        )
    if task.requires_sol_review:
        if not approval:
            return (
                "NEEDS_SOL_REVIEW",
                "PRE_TASK_ROADMAP_REVIEW required before implementation",
            )
        if (
            approval.get("control_plane_sha") != task.control_plane_sha
            or approval.get("definition_hash") != task.definition_hash
        ):
            return (
                "NEEDS_SOL_REVIEW",
                "task approval does not match this definition/control-plane SHA",
            )
    if global_mode != "RUNNING" or not lane_available or lane_busy:
        reason = (
            "lane is busy" if lane_busy
            else "lane is unavailable" if not lane_available
            else "global mode is PAUSED"
        )
        return "PAUSED", reason
    return "READY", "all dependencies, gates, review, and safety conditions satisfied"


def next_round_robin_lane(
    ready_lanes: Iterable[str],
    lane_order: Sequence[str],
    cursor: int,
) -> tuple[str | None, int]:
    """Choose the next ready lane and persist the next search position."""
    ready = set(ready_lanes)
    if not lane_order:
        return None, 0
    start = cursor % len(lane_order)
    for offset in range(len(lane_order)):
        index = (start + offset) % len(lane_order)
        lane = lane_order[index]
        if lane in ready:
            return lane, (index + 1) % len(lane_order)
    return None, start


def current_lane_task(
    lane: str,
    phase_id: str,
    phase_title: str,
    boundaries: Sequence[str] = (),
    *,
    milestone: str = "M01",
    workstream_id: str | None = None,
    control_plane_sha: str | None = None,
) -> RoadmapTask:
    """Adapt one existing PLAN phase to the common task shape."""
    criteria = (phase_title,) + tuple(boundaries[:3])
    return RoadmapTask(
        milestone=milestone,
        task_id=phase_id,
        lane=lane,
        dependencies=(),
        risk="lane-local change; preserve server authority and ownership boundaries",
        requires_sol_review=False,
        acceptance_criteria=criteria,
        title=phase_title,
        source="EXISTING_PLAN",
        workstream_id=workstream_id or f"{milestone}-{lane.upper()}",
        control_plane_sha=control_plane_sha,
    )
