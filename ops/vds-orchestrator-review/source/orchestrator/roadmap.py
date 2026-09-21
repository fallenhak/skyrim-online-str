"""Small adapter for the current lane queues and the future roadmap scheduler.

The V2 supervisor still consumes the existing C/A/L/U PLAN.md queues.  A future
roadmap scheduler can implement the same task shape and provide dependencies,
risk, review policy, and acceptance criteria without changing worker, Git, or
CI machinery.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class RoadmapTask:
    milestone: str
    task_id: str
    lane: str
    dependencies: tuple[str, ...]
    risk: str
    requires_sol_review: bool
    acceptance_criteria: tuple[str, ...]


def current_lane_task(
    lane: str,
    phase_id: str,
    phase_title: str,
    boundaries: Sequence[str] = (),
) -> RoadmapTask:
    """Adapt one current queue phase to the future scheduler contract."""
    criteria = (phase_title,) + tuple(boundaries[:3])
    return RoadmapTask(
        milestone="MILESTONE_01_CORE_WORLD",
        task_id=phase_id,
        lane=lane,
        dependencies=(),
        risk="lane-local change; preserve server authority and ownership boundaries",
        requires_sol_review=False,
        acceptance_criteria=criteria,
    )
