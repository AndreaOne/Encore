from __future__ import annotations

import math
from pathlib import Path
import sys
import tempfile
import unittest
import wave


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from encore_local_runtime import AudioAnalysisConfig
from encore_local_runtime import LocalEncoreRuntime
from encore_local_runtime import ParticleFilterConfig
from encore_local_runtime import ReferenceScore
from encore_local_runtime import RuntimeConfig
from encore_local_runtime import ScoreFlipMarker
from encore_local_runtime import FlipEvent
from encore_local_runtime import build_virtual_motion_events
from encore_local_runtime import generate_synthetic_observations
from encore_local_runtime import load_observations_from_media


SCORE_PATH = ROOT / "generated" / "encore" / "data" / "score_bundle.json"


class EncoreLocalRuntimeTests(unittest.TestCase):
    def _write_wave(
        self,
        path: Path,
        samples: list[int],
        sample_rate: int = 16000,
    ) -> None:
        with wave.open(str(path), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(2)
            handle.setframerate(sample_rate)
            handle.writeframes(
                b"".join(int(sample).to_bytes(2, byteorder="little", signed=True) for sample in samples)
            )

    def _build_tone_samples(
        self,
        frequency_hz: float,
        duration_ms: int,
        amplitude: int = 10000,
        sample_rate: int = 16000,
    ) -> list[int]:
        sample_count = int(duration_ms * sample_rate / 1000)
        return [
            int(amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate))
            for index in range(sample_count)
        ]

    def _build_score_samples(
        self,
        score: ReferenceScore,
        duration_ms: int,
        amplitude: int = 10000,
        sample_rate: int = 16000,
    ) -> list[int]:
        sample_count = int(duration_ms * sample_rate / 1000)
        samples: list[int] = []
        for index in range(sample_count):
            time_ms = index * 1000.0 / sample_rate
            frequency_hz = score.reference_pitch_at(time_ms)
            if frequency_hz <= 0.0:
                samples.append(0)
                continue
            samples.append(
                int(amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate))
            )
        return samples

    def test_reference_score_loads_current_bundle(self) -> None:
        score = ReferenceScore.from_path(SCORE_PATH)

        self.assertEqual(score.duration_ms, 24000)
        self.assertEqual(len(score.notes), 42)
        self.assertEqual([marker.time_ms for marker in score.markers], [1500, 6500])
        self.assertEqual([marker.target_page for marker in score.markers], [2, 3])

    def test_perfect_observations_cross_markers_in_order(self) -> None:
        score = ReferenceScore.from_path(SCORE_PATH)
        config = ParticleFilterConfig(
            particle_count=64,
            initial_spread_ms=0.0,
            process_sigma_ms=0.0,
            resample_sigma_ms=0.0,
            pitch_sigma_hz=1.0,
            minimum_step_ms=0.0,
            resample_threshold_ratio=0.5,
        )
        runtime = LocalEncoreRuntime(score, filter_config=config, seed=3)
        observations = generate_synthetic_observations(score, frame_ms=125, tail_ms=0, seed=3)

        run = runtime.run(observations)

        self.assertEqual([event.marker.target_page for event in run.flip_events], [2, 3])
        self.assertEqual([event.marker.time_ms for event in run.flip_events], [1500, 6500])
        self.assertGreaterEqual(run.flip_events[0].estimated_ms, 1500.0)
        self.assertGreaterEqual(run.flip_events[1].estimated_ms, 6500.0)

    def test_lead_in_silence_does_not_start_tracking(self) -> None:
        score = ReferenceScore.from_path(SCORE_PATH)
        runtime = LocalEncoreRuntime(
            score,
            filter_config=ParticleFilterConfig(
                particle_count=32,
                initial_spread_ms=0.0,
                process_sigma_ms=0.0,
                resample_sigma_ms=0.0,
                pitch_sigma_hz=1.0,
                minimum_step_ms=0.0,
                resample_threshold_ratio=0.5,
            ),
            runtime_config=RuntimeConfig(),
            seed=1,
        )
        observations = generate_synthetic_observations(score, frame_ms=250, lead_in_ms=1000, tail_ms=0, seed=1)

        run = runtime.run(observations[:4])

        self.assertTrue(all(not frame.tracking_locked for frame in run.frames))
        self.assertTrue(all(frame.estimated_ms == 0.0 for frame in run.frames))
        self.assertEqual(run.flip_events, ())

    def test_media_loader_detects_pitch_from_wav(self) -> None:
        sample_rate = 16000
        silence = [0] * int(sample_rate * 0.3)
        tone = self._build_tone_samples(293.66, duration_ms=700, sample_rate=sample_rate)

        with tempfile.TemporaryDirectory() as temp_dir:
            wav_path = Path(temp_dir) / "tone.wav"
            self._write_wave(wav_path, silence + tone, sample_rate=sample_rate)

            batch = load_observations_from_media(
                wav_path,
                config=AudioAnalysisConfig(
                    sample_rate=sample_rate,
                    buffer_size=1024,
                    hop_size=1024,
                ),
            )

        detected = [obs.raw_pitch_hz for obs in batch.observations if obs.raw_pitch_hz > 0.0]
        self.assertGreater(len(detected), 0)
        self.assertTrue(any(abs(pitch_hz - 293.66) < 1.0 for pitch_hz in detected))
        self.assertTrue(all(obs.raw_pitch_hz < 0.0 for obs in batch.observations[:2]))

    def test_runtime_can_follow_score_from_wav(self) -> None:
        score = ReferenceScore.from_path(SCORE_PATH)

        with tempfile.TemporaryDirectory() as temp_dir:
            wav_path = Path(temp_dir) / "score_excerpt.wav"
            self._write_wave(
                wav_path,
                self._build_score_samples(score, duration_ms=8000),
            )
            batch = load_observations_from_media(
                wav_path,
                config=AudioAnalysisConfig(
                    sample_rate=16000,
                    buffer_size=1024,
                    hop_size=1024,
                ),
            )

        runtime = LocalEncoreRuntime(
            score,
            filter_config=ParticleFilterConfig(
                particle_count=64,
                initial_spread_ms=0.0,
                process_sigma_ms=0.0,
                resample_sigma_ms=0.0,
                pitch_sigma_hz=1.0,
                minimum_step_ms=0.0,
                resample_threshold_ratio=0.5,
            ),
            runtime_config=RuntimeConfig(),
            seed=5,
        )
        run = runtime.run(batch.observations)

        self.assertEqual([event.marker.target_page for event in run.flip_events], [2, 3])

    def test_virtual_motion_events_follow_motion_control_timing(self) -> None:
        marker = ScoreFlipMarker(time_ms=1500, event_index=3, measure=0, target_page=2)
        flip_event = FlipEvent(observation_time_ms=2475, estimated_ms=1519.18, marker=marker)

        motion_events = build_virtual_motion_events((flip_event,))

        self.assertEqual(
            [(event.time_ms, event.label) for event in motion_events],
            [
                (2475, "FLIP START"),
                (2475, "MOTOR START"),
                (3975, "MOTOR STOP"),
                (3975, "SERVO UP"),
                (5183, "SERVO HOLD"),
                (5683, "SERVO DOWN"),
                (7193, "FLIP END"),
            ],
        )
        self.assertIn("pwm=150", motion_events[1].detail)
        self.assertIn("170->20", motion_events[3].detail)
        self.assertIn("20->170", motion_events[5].detail)


if __name__ == "__main__":
    unittest.main()
