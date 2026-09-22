from __future__ import annotations

from array import array
from dataclasses import dataclass
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave

from .runtime import Observation


@dataclass(frozen=True)
class AudioAnalysisConfig:
    sample_rate: int = 16000
    buffer_size: int = 1024
    hop_size: int = 1024
    min_freq_hz: float = 250.0
    max_freq_hz: float = 1000.0
    rms_threshold: float = 30.0
    noise_calibration_samples: int = 8
    noise_floor_multiplier: float = 1.4
    noise_floor_margin: float = 20.0
    noise_calibration_max_rms: float = 800.0
    correlation_threshold: float = 0.35
    snap_tolerance_cents: float = 35.0


@dataclass(frozen=True)
class MediaObservationBatch:
    observations: tuple[Observation, ...]
    noise_floor_rms: float
    active_rms_threshold: float
    frame_hop_ms: float


def load_observations_from_media(
    path: str | Path,
    config: AudioAnalysisConfig | None = None,
) -> MediaObservationBatch:
    media_path = Path(path)
    if not media_path.exists():
        raise FileNotFoundError(f"Media file not found: {media_path}")

    analysis_config = config or AudioAnalysisConfig()
    with tempfile.TemporaryDirectory(prefix="encore_media_") as temp_dir:
        wav_path = Path(temp_dir) / "input.wav"
        source_wav_path = _prepare_wav_for_analysis(media_path, wav_path, analysis_config)
        samples, sample_rate = _load_pcm_samples(source_wav_path)

    effective_config = AudioAnalysisConfig(
        sample_rate=sample_rate,
        buffer_size=analysis_config.buffer_size,
        hop_size=analysis_config.hop_size,
        min_freq_hz=analysis_config.min_freq_hz,
        max_freq_hz=analysis_config.max_freq_hz,
        rms_threshold=analysis_config.rms_threshold,
        noise_calibration_samples=analysis_config.noise_calibration_samples,
        noise_floor_multiplier=analysis_config.noise_floor_multiplier,
        noise_floor_margin=analysis_config.noise_floor_margin,
        noise_calibration_max_rms=analysis_config.noise_calibration_max_rms,
        correlation_threshold=analysis_config.correlation_threshold,
        snap_tolerance_cents=analysis_config.snap_tolerance_cents,
    )

    return analyze_pcm_samples(samples, config=effective_config)


def analyze_pcm_samples(
    samples: list[int],
    config: AudioAnalysisConfig | None = None,
) -> MediaObservationBatch:
    analysis_config = config or AudioAnalysisConfig()
    if analysis_config.buffer_size <= 0:
        raise ValueError("buffer_size must be positive.")
    if analysis_config.hop_size <= 0:
        raise ValueError("hop_size must be positive.")
    if analysis_config.sample_rate <= 0:
        raise ValueError("sample_rate must be positive.")

    frame_starts = range(
        0,
        max(0, len(samples) - analysis_config.buffer_size + 1),
        analysis_config.hop_size,
    )
    windows = [
        samples[start : start + analysis_config.buffer_size]
        for start in frame_starts
        if start + analysis_config.buffer_size <= len(samples)
    ]

    noise_floor_rms = _calibrate_noise_floor(windows, analysis_config)
    active_rms_threshold = _active_rms_threshold(noise_floor_rms, analysis_config)

    observations: list[Observation] = []
    for frame_index, window in enumerate(windows):
        rms = _compute_rms(window)
        detected_pitch_hz = -1.0
        if rms > active_rms_threshold:
            raw_pitch_hz = _estimate_pitch(window, analysis_config)
            if raw_pitch_hz > 0.0:
                detected_pitch_hz = _snap_pitch_to_nearest_semitone(
                    raw_pitch_hz,
                    tolerance_cents=analysis_config.snap_tolerance_cents,
                )

        observations.append(
            Observation(
                time_ms=int(round(frame_index * analysis_config.hop_size * 1000.0 / analysis_config.sample_rate)),
                raw_pitch_hz=detected_pitch_hz,
                rms=rms,
            )
        )

    return MediaObservationBatch(
        observations=tuple(observations),
        noise_floor_rms=noise_floor_rms,
        active_rms_threshold=active_rms_threshold,
        frame_hop_ms=analysis_config.hop_size * 1000.0 / analysis_config.sample_rate,
    )


def _prepare_wav_for_analysis(
    input_path: Path,
    wav_path: Path,
    config: AudioAnalysisConfig,
) -> Path:
    if input_path.suffix.lower() == ".wav":
        try:
            _validate_wav(input_path)
            return input_path
        except ValueError:
            pass

    afconvert = shutil.which("afconvert")
    if afconvert is not None:
        command = [
            afconvert,
            str(input_path),
            "-f",
            "WAVE",
            "-d",
            f"LEI16@{config.sample_rate}",
            "-o",
            str(wav_path),
        ]
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode == 0:
            return wav_path
        afconvert_message = completed.stderr.strip() or completed.stdout.strip() or "Unknown conversion error."
    else:
        afconvert_message = "afconvert is unavailable."

    swift_path = shutil.which("swift")
    if swift_path is None:
        raise RuntimeError(
            f"Failed to extract audio from {input_path.name}: {afconvert_message}"
        )

    swift_script = Path(__file__).with_name("audio_extract.swift")
    swift_cache = Path(tempfile.gettempdir()) / "encore_swift_cache"
    swift_cache.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [swift_path, str(swift_script), str(input_path), str(wav_path)],
        check=False,
        capture_output=True,
        text=True,
        env={
            **os.environ,
            "CLANG_MODULE_CACHE_PATH": str(swift_cache),
        },
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip() or "Unknown conversion error."
        suffix_note = ""
        if input_path.suffix.lower() in {".m4a", ".aac"}:
            suffix_note = " This AAC-based file currently needs a WAV export or an ffmpeg-style decoder on this machine."
        raise RuntimeError(
            f"Failed to extract audio from {input_path.name}: {afconvert_message}; swift fallback failed: {message}{suffix_note}"
        )
    return wav_path


def _validate_wav(path: Path) -> None:
    with wave.open(str(path), "rb") as handle:
        if handle.getcomptype() != "NONE":
            raise ValueError("Only PCM WAV is supported without conversion.")
        if handle.getsampwidth() != 2:
            raise ValueError("Only 16-bit WAV is supported without conversion.")


def _load_pcm_samples(path: Path) -> tuple[list[int], int]:
    _validate_wav(path)
    with wave.open(str(path), "rb") as handle:
        sample_rate = handle.getframerate()
        channel_count = handle.getnchannels()
        raw_frames = handle.readframes(handle.getnframes())

    samples = array("h")
    samples.frombytes(raw_frames)
    if sys.byteorder != "little":
        samples.byteswap()
    sample_list = list(samples)
    if channel_count <= 1:
        return sample_list, sample_rate

    mono_samples: list[int] = []
    for index in range(0, len(sample_list), channel_count):
        frame = sample_list[index : index + channel_count]
        if not frame:
            continue
        mono_samples.append(int(round(sum(frame) / len(frame))))
    return mono_samples, sample_rate


def _calibrate_noise_floor(
    windows: list[list[int]],
    config: AudioAnalysisConfig,
) -> float:
    if config.noise_calibration_samples <= 0 or not windows:
        return 0.0

    quiet_floor_candidate = -1.0
    fallback_floor_candidate = -1.0
    for window in windows[: config.noise_calibration_samples]:
        rms = _compute_rms(window)
        if fallback_floor_candidate < 0.0 or rms < fallback_floor_candidate:
            fallback_floor_candidate = rms
        if rms <= config.noise_calibration_max_rms and (
            quiet_floor_candidate < 0.0 or rms < quiet_floor_candidate
        ):
            quiet_floor_candidate = rms

    if quiet_floor_candidate >= 0.0:
        return quiet_floor_candidate
    if 0.0 <= fallback_floor_candidate <= config.noise_calibration_max_rms:
        return fallback_floor_candidate
    return 0.0


def _active_rms_threshold(
    noise_floor_rms: float,
    config: AudioAnalysisConfig,
) -> float:
    multiplied = noise_floor_rms * config.noise_floor_multiplier
    offset = noise_floor_rms + config.noise_floor_margin
    return max(config.rms_threshold, multiplied, offset)


def _compute_rms(window: list[int]) -> float:
    if not window:
        return 0.0
    sum_squares = 0.0
    for sample in window:
        sum_squares += float(sample) * float(sample)
    return math.sqrt(sum_squares / len(window))


def _estimate_pitch(window: list[int], config: AudioAnalysisConfig) -> float:
    count = len(window)
    if count <= 1:
        return -1.0

    mean = sum(window) / count
    centered = [sample - mean for sample in window]

    min_lag = max(1, int(config.sample_rate / config.max_freq_hz))
    max_lag = min(count - 1, int(config.sample_rate / config.min_freq_hz))
    if min_lag > max_lag:
        return -1.0

    best_correlation = -1.0
    best_lag = -1
    for lag in range(min_lag, max_lag + 1):
        correlation = 0.0
        energy_a = 0.0
        energy_b = 0.0
        limit = count - lag
        for index in range(limit):
            left = centered[index]
            right = centered[index + lag]
            correlation += left * right
            energy_a += left * left
            energy_b += right * right

        if energy_a < 1e-9 or energy_b < 1e-9:
            continue

        normalized = correlation / math.sqrt(energy_a * energy_b)
        if normalized > best_correlation:
            best_correlation = normalized
            best_lag = lag

    if best_lag <= 0 or best_correlation < config.correlation_threshold:
        return -1.0
    return float(config.sample_rate) / best_lag


def _snap_pitch_to_nearest_semitone(
    pitch_hz: float,
    tolerance_cents: float,
) -> float:
    if pitch_hz <= 0.0:
        return -1.0

    midi = 69.0 + 12.0 * math.log(pitch_hz / 440.0, 2.0)
    nearest_midi = round(midi)
    snapped_pitch_hz = 440.0 * math.pow(2.0, (nearest_midi - 69) / 12.0)
    if _cents_off(pitch_hz, snapped_pitch_hz) > tolerance_cents:
        return -1.0
    return snapped_pitch_hz


def _cents_off(pitch_hz: float, reference_hz: float) -> float:
    if pitch_hz <= 0.0 or reference_hz <= 0.0:
        return 9999.0
    return abs(1200.0 * math.log(pitch_hz / reference_hz, 2.0))
