from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import random

from .score import ReferenceScore


@dataclass
class ParticleFilterConfig:
    particle_count: int = 160
    initial_spread_ms: float = 80.0
    process_sigma_ms: float = 20.0
    resample_sigma_ms: float = 8.0
    pitch_sigma_hz: float = 16.0
    minimum_step_ms: float = 0.0
    resample_threshold_ratio: float = 0.5

    @classmethod
    def from_path(cls, path: str | Path) -> "ParticleFilterConfig":
        config = cls()
        for raw_line in Path(path).read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            if "#" in line:
                line = line.split("#", 1)[0].strip()
            if ":" not in line:
                continue

            key, value = [part.strip() for part in line.split(":", 1)]
            if key == "particle_count":
                config.particle_count = int(value)
            elif key == "initial_spread_ms":
                config.initial_spread_ms = float(value)
            elif key == "process_sigma_ms":
                config.process_sigma_ms = float(value)
            elif key == "resample_sigma_ms":
                config.resample_sigma_ms = float(value)
            elif key == "pitch_sigma_hz":
                config.pitch_sigma_hz = float(value)
            elif key == "minimum_step_ms":
                config.minimum_step_ms = float(value)
            elif key == "resample_threshold_ratio":
                config.resample_threshold_ratio = float(value)

        config.particle_count = max(1, config.particle_count)
        config.initial_spread_ms = max(0.0, config.initial_spread_ms)
        config.process_sigma_ms = max(0.0, config.process_sigma_ms)
        config.resample_sigma_ms = max(0.0, config.resample_sigma_ms)
        config.pitch_sigma_hz = max(1e-6, config.pitch_sigma_hz)
        config.minimum_step_ms = max(0.0, config.minimum_step_ms)
        if not 0.0 < config.resample_threshold_ratio <= 1.0:
            config.resample_threshold_ratio = 0.5
        return config


class ParticleFilter:
    def __init__(
        self,
        score: ReferenceScore,
        config: ParticleFilterConfig | None = None,
        seed: int | None = None,
    ) -> None:
        self.score = score
        self.config = config or ParticleFilterConfig()
        self._rng = random.Random(seed)
        self.particles = [0.0] * self.config.particle_count
        self.weights = [1.0 / self.config.particle_count] * self.config.particle_count
        self.estimated_position_ms = 0.0

    def initialize_particles(self) -> None:
        duration_ms = float(self.score.duration_ms)
        self.particles = [
            self._clamp(
                self.config.initial_spread_ms * self._rand_normal(),
                0.0,
                duration_ms,
            )
            for _ in range(self.config.particle_count)
        ]
        self.weights = [1.0 / self.config.particle_count] * self.config.particle_count
        self.estimated_position_ms = 0.0

    def predict(self, delta_ms: float) -> None:
        duration_ms = float(self.score.duration_ms)
        for index, position_ms in enumerate(self.particles):
            step_ms = delta_ms + self.config.process_sigma_ms * self._rand_normal()
            if step_ms < self.config.minimum_step_ms:
                step_ms = self.config.minimum_step_ms
            self.particles[index] = self._clamp(position_ms + step_ms, 0.0, duration_ms)

    def update_weights(self, observed_pitch_hz: float) -> None:
        total = 0.0
        for index, position_ms in enumerate(self.particles):
            expected_pitch_hz = self._best_reference_frequency_at(position_ms, observed_pitch_hz)
            likelihood = (
                self._gaussian(observed_pitch_hz, expected_pitch_hz, self.config.pitch_sigma_hz)
                if expected_pitch_hz > 0.0
                else 0.01
            )
            self.weights[index] *= likelihood + 1e-12
            total += self.weights[index]

        if total <= 0.0:
            self.weights = [1.0 / self.config.particle_count] * self.config.particle_count
            return

        self.weights = [weight / total for weight in self.weights]

    def effective_sample_size(self) -> float:
        sum_squared = sum(weight * weight for weight in self.weights)
        if sum_squared <= 1e-12:
            return 0.0
        return 1.0 / sum_squared

    def should_resample(self) -> bool:
        threshold = self.config.resample_threshold_ratio * self.config.particle_count
        return self.effective_sample_size() < threshold

    def resample_particles(self) -> None:
        cumulative_weights: list[float] = []
        cumulative_weight = 0.0
        for weight in self.weights:
            cumulative_weight += weight
            cumulative_weights.append(cumulative_weight)

        step = 1.0 / self.config.particle_count
        offset = self._rng.random() * step
        duration_ms = float(self.score.duration_ms)
        source_index = 0
        new_particles: list[float] = []

        for index in range(self.config.particle_count):
            threshold = offset + index * step
            while (
                source_index < self.config.particle_count - 1
                and cumulative_weights[source_index] < threshold
            ):
                source_index += 1
            new_particles.append(
                self._clamp(
                    self.particles[source_index] + self.config.resample_sigma_ms * self._rand_normal(),
                    0.0,
                    duration_ms,
                )
            )

        self.particles = new_particles
        self.weights = [1.0 / self.config.particle_count] * self.config.particle_count

    def estimate_state(self) -> float:
        return sum(
            particle_ms * weight
            for particle_ms, weight in zip(self.particles, self.weights, strict=True)
        )

    def set_estimated_position_ms(self, estimated_position_ms: float) -> None:
        self.estimated_position_ms = self._clamp(
            estimated_position_ms,
            0.0,
            float(self.score.duration_ms),
        )

    def _best_reference_frequency_at(self, position_ms: float, observed_pitch_hz: float) -> float:
        best_frequency_hz = 0.0
        best_score = -1.0
        for note in self.score.notes:
            if note.start_ms > position_ms:
                break
            if not note.start_ms <= position_ms < note.end_ms:
                continue

            score = (
                self._gaussian(observed_pitch_hz, note.frequency_hz, self.config.pitch_sigma_hz)
                if observed_pitch_hz > 0.0
                else 1.0
            )
            if score > best_score:
                best_score = score
                best_frequency_hz = note.frequency_hz
        return best_frequency_hz

    def _rand_normal(self) -> float:
        return self._rng.gauss(0.0, 1.0)

    @staticmethod
    def _gaussian(x: float, mean: float, sigma: float) -> float:
        delta = x - mean
        return pow(2.718281828459045, -(delta * delta) / (2.0 * sigma * sigma))

    @staticmethod
    def _clamp(value: float, lower: float, upper: float) -> float:
        return min(max(value, lower), upper)
