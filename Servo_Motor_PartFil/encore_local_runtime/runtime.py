from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import random
from typing import Iterable

from .particle_filter import ParticleFilter
from .particle_filter import ParticleFilterConfig
from .score import ReferenceScore
from .score import ScoreFlipMarker


@dataclass(frozen=True)
class Observation:
    time_ms: int
    raw_pitch_hz: float
    rms: float = 0.0


@dataclass(frozen=True)
class FlipEvent:
    observation_time_ms: int
    estimated_ms: float
    marker: ScoreFlipMarker


@dataclass(frozen=True)
class FrameResult:
    observation: Observation
    tracked_pitch_hz: float
    expected_pitch_hz: float
    estimated_ms: float
    tracking_locked: bool
    tracking_started: bool
    next_marker_index: int
    flip_event: FlipEvent | None


@dataclass(frozen=True)
class RuntimeRun:
    frames: tuple[FrameResult, ...]
    flip_events: tuple[FlipEvent, ...]


@dataclass(frozen=True)
class RuntimeConfig:
    tracking_tolerance_cents: float = 60.0
    startup_window_floor_ms: int = 250
    startup_window_cap_ms: int = 1200


class LocalEncoreRuntime:
    def __init__(
        self,
        score: ReferenceScore,
        filter_config: ParticleFilterConfig | None = None,
        runtime_config: RuntimeConfig | None = None,
        seed: int | None = None,
    ) -> None:
        self.score = score
        self.filter = ParticleFilter(score, config=filter_config, seed=seed)
        self.runtime_config = runtime_config or RuntimeConfig()
        self.reset()

    def reset(self) -> None:
        self.tracking_locked = False
        self.next_marker_index = 0
        self.last_loop_ms = 0
        self.last_observation_time_ms: int | None = None
        self.filter.initialize_particles()
        self.filter.set_estimated_position_ms(0.0)

    def step(self, observation: Observation) -> FrameResult:
        if self.last_observation_time_ms is not None and observation.time_ms < self.last_observation_time_ms:
            raise ValueError("Observations must be processed in time order.")
        self.last_observation_time_ms = observation.time_ms

        tracked_pitch_hz = self.score.snap_pitch_to_reference(
            observation.raw_pitch_hz,
            tolerance_cents=self.runtime_config.tracking_tolerance_cents,
        )
        tracking_started = False
        flip_event = None

        if not self.tracking_locked:
            if not self.score.pitch_matches_start(
                tracked_pitch_hz,
                tolerance_cents=self.runtime_config.tracking_tolerance_cents,
                floor_ms=self.runtime_config.startup_window_floor_ms,
                cap_ms=self.runtime_config.startup_window_cap_ms,
            ):
                estimated_ms = self.filter.estimated_position_ms
                expected_pitch_hz = self.score.reference_pitch_at(estimated_ms)
                return FrameResult(
                    observation=observation,
                    tracked_pitch_hz=tracked_pitch_hz,
                    expected_pitch_hz=expected_pitch_hz,
                    estimated_ms=estimated_ms,
                    tracking_locked=False,
                    tracking_started=False,
                    next_marker_index=self.next_marker_index,
                    flip_event=None,
                )

            tracking_started = True
            self.tracking_locked = True
            self.filter.initialize_particles()
            self.filter.set_estimated_position_ms(0.0)
            self.next_marker_index = 0
            self.last_loop_ms = observation.time_ms

        estimated_ms = self.filter.estimated_position_ms
        if tracked_pitch_hz > 0.0:
            delta_ms = max(0.0, float(observation.time_ms - self.last_loop_ms))
            self.last_loop_ms = observation.time_ms
            self.filter.predict(delta_ms)
            self.filter.update_weights(tracked_pitch_hz)
            estimated_ms = self.filter.estimate_state()
            if self.filter.should_resample():
                self.filter.resample_particles()
                estimated_ms = self.filter.estimate_state()
            self.filter.set_estimated_position_ms(estimated_ms)

            if self.next_marker_index < len(self.score.markers):
                marker = self.score.markers[self.next_marker_index]
                if estimated_ms >= marker.time_ms:
                    self.next_marker_index += 1
                    flip_event = FlipEvent(
                        observation_time_ms=observation.time_ms,
                        estimated_ms=estimated_ms,
                        marker=marker,
                    )
        else:
            self.last_loop_ms = observation.time_ms

        expected_pitch_hz = self.score.reference_pitch_at(estimated_ms)
        return FrameResult(
            observation=observation,
            tracked_pitch_hz=tracked_pitch_hz,
            expected_pitch_hz=expected_pitch_hz,
            estimated_ms=estimated_ms,
            tracking_locked=self.tracking_locked,
            tracking_started=tracking_started,
            next_marker_index=self.next_marker_index,
            flip_event=flip_event,
        )

    def run(self, observations: Iterable[Observation]) -> RuntimeRun:
        frames: list[FrameResult] = []
        flips: list[FlipEvent] = []
        for observation in observations:
            frame = self.step(observation)
            frames.append(frame)
            if frame.flip_event is not None:
                flips.append(frame.flip_event)
        return RuntimeRun(frames=tuple(frames), flip_events=tuple(flips))


def generate_synthetic_observations(
    score: ReferenceScore,
    frame_ms: int = 125,
    lead_in_ms: int = 0,
    tail_ms: int = 500,
    pitch_jitter_hz: float = 0.0,
    dropout_rate: float = 0.0,
    seed: int | None = None,
) -> list[Observation]:
    rng = random.Random(seed)
    end_time_ms = lead_in_ms + score.duration_ms + tail_ms
    observations: list[Observation] = []

    for time_ms in range(0, end_time_ms + 1, frame_ms):
        score_time_ms = time_ms - lead_in_ms
        raw_pitch_hz = (
            score.reference_pitch_at(score_time_ms)
            if 0 <= score_time_ms <= score.duration_ms
            else 0.0
        )
        if raw_pitch_hz > 0.0 and pitch_jitter_hz > 0.0:
            raw_pitch_hz += rng.gauss(0.0, pitch_jitter_hz)
        if raw_pitch_hz > 0.0 and dropout_rate > 0.0 and rng.random() < dropout_rate:
            raw_pitch_hz = 0.0

        observations.append(
            Observation(
                time_ms=time_ms,
                raw_pitch_hz=raw_pitch_hz if raw_pitch_hz > 0.0 else -1.0,
                rms=1.0 if raw_pitch_hz > 0.0 else 0.0,
            )
        )

    return observations


def load_observations(path: str | Path) -> list[Observation]:
    raw_items = json.loads(Path(path).read_text(encoding="utf-8"))
    observations: list[Observation] = []
    for item in raw_items:
        if isinstance(item, dict):
            observations.append(
                Observation(
                    time_ms=int(item["time_ms"]),
                    raw_pitch_hz=float(item.get("raw_pitch_hz", item.get("pitch_hz", -1.0))),
                    rms=float(item.get("rms", 0.0)),
                )
            )
            continue
        if isinstance(item, list) and len(item) >= 2:
            observations.append(
                Observation(
                    time_ms=int(item[0]),
                    raw_pitch_hz=float(item[1]),
                    rms=float(item[2]) if len(item) > 2 else 0.0,
                )
            )
            continue
        raise ValueError(f"Unsupported observation item: {item!r}")
    return observations
