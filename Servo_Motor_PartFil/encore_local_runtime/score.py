from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Any


def cents_between(left_hz: float, right_hz: float) -> float:
    if left_hz <= 0.0 or right_hz <= 0.0:
        return 9999.0
    return abs(1200.0 * math.log(left_hz / right_hz, 2.0))


@dataclass(frozen=True)
class ScoreNote:
    start_ms: int
    duration_ms: int
    frequency_hz: float
    measure: int
    page: int

    @property
    def end_ms(self) -> int:
        return self.start_ms + self.duration_ms


@dataclass(frozen=True)
class ScoreFlipMarker:
    time_ms: int
    event_index: int
    measure: int
    target_page: int


@dataclass(frozen=True)
class ReferenceScore:
    title: str
    part: tuple[str, ...]
    duration_ms: int
    notes: tuple[ScoreNote, ...]
    markers: tuple[ScoreFlipMarker, ...]

    @classmethod
    def from_path(cls, path: str | Path) -> "ReferenceScore":
        return cls.from_json_text(Path(path).read_text(encoding="utf-8"))

    @classmethod
    def from_json_text(cls, text: str) -> "ReferenceScore":
        return cls.from_bundle(json.loads(text))

    @classmethod
    def from_bundle(cls, bundle: dict[str, Any]) -> "ReferenceScore":
        notes = tuple(
            ScoreNote(
                start_ms=max(0, int(row[0])),
                duration_ms=max(0, int(row[1])),
                frequency_hz=float(row[2]) / 100.0,
                measure=int(row[3]),
                page=int(row[4]),
            )
            for row in bundle.get("notes", [])
        )
        markers = tuple(
            ScoreFlipMarker(
                time_ms=max(0, int(row[0])),
                event_index=int(row[1]),
                measure=int(row[2]),
                target_page=int(row[3]),
            )
            for row in bundle.get("markers", [])
        )
        if not notes:
            raise ValueError("Score bundle must contain at least one note.")

        duration_ms = int(bundle.get("dur") or 0)
        if duration_ms <= 0:
            duration_ms = notes[-1].end_ms

        return cls(
            title=str(bundle.get("title") or "Untitled Score"),
            part=tuple(bundle.get("part", [])),
            duration_ms=duration_ms,
            notes=notes,
            markers=markers,
        )

    def reference_pitch_at(self, position_ms: float) -> float:
        for note in self.notes:
            if position_ms < note.start_ms:
                break
            if note.start_ms <= position_ms < note.end_ms:
                return note.frequency_hz
        return 0.0

    def score_start_window_end_ms(self, floor_ms: int = 250, cap_ms: int = 1200) -> int:
        first_note = self.notes[0]
        window_end_ms = first_note.end_ms
        window_end_ms = max(window_end_ms, first_note.start_ms + floor_ms)
        window_end_ms = min(window_end_ms, first_note.start_ms + cap_ms)
        return window_end_ms

    def pitch_matches_start(
        self,
        observed_pitch_hz: float,
        tolerance_cents: float = 60.0,
        floor_ms: int = 250,
        cap_ms: int = 1200,
    ) -> bool:
        if observed_pitch_hz <= 0.0:
            return False

        window_end_ms = self.score_start_window_end_ms(floor_ms=floor_ms, cap_ms=cap_ms)
        for note in self.notes:
            if note.start_ms > window_end_ms:
                break
            if cents_between(observed_pitch_hz, note.frequency_hz) <= tolerance_cents:
                return True
        return False

    def snap_pitch_to_reference(
        self,
        observed_pitch_hz: float,
        tolerance_cents: float = 60.0,
    ) -> float:
        if observed_pitch_hz <= 0.0:
            return -1.0

        best_pitch_hz = -1.0
        best_cents = 9999.0
        for note in self.notes:
            cents = cents_between(observed_pitch_hz, note.frequency_hz)
            if cents < best_cents:
                best_cents = cents
                best_pitch_hz = note.frequency_hz

        if best_cents > tolerance_cents:
            return -1.0
        return best_pitch_hz
