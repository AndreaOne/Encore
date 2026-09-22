from __future__ import annotations

import argparse
from pathlib import Path

from .audio import AudioAnalysisConfig
from .audio import load_observations_from_media
from .motion import build_virtual_motion_events
from .particle_filter import ParticleFilterConfig
from .plot import save_run_plot_svg
from .runtime import LocalEncoreRuntime
from .runtime import RuntimeConfig
from .runtime import generate_synthetic_observations
from .runtime import load_observations
from .score import ReferenceScore


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCORE_PATH = ROOT / "generated" / "encore" / "data" / "score_bundle.json"
DEFAULT_CONFIG_PATH = ROOT / "generated" / "encore" / "data" / "particle_filter_config.yaml"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the Encore particle-filter runtime locally without Arduino hardware.",
    )
    parser.add_argument(
        "--score",
        type=Path,
        default=DEFAULT_SCORE_PATH,
        help=f"Path to a score bundle JSON. Default: {DEFAULT_SCORE_PATH}",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG_PATH,
        help=f"Path to a particle filter YAML config. Default: {DEFAULT_CONFIG_PATH}",
    )
    parser.add_argument(
        "--observations",
        type=Path,
        help="Optional JSON observation file. If omitted, synthetic observations are generated from the score.",
    )
    parser.add_argument(
        "--media",
        "--mp4",
        dest="media",
        type=Path,
        help="Optional audio/video file such as mp3, mp4, m4a, mov, or wav to analyze locally.",
    )
    parser.add_argument("--frame-ms", type=int, default=125, help="Synthetic frame spacing in milliseconds.")
    parser.add_argument("--lead-in-ms", type=int, default=0, help="Synthetic silence before the score starts.")
    parser.add_argument("--tail-ms", type=int, default=500, help="Synthetic silence after the score ends.")
    parser.add_argument("--pitch-jitter-hz", type=float, default=0.0, help="Gaussian pitch jitter for synthetic observations.")
    parser.add_argument("--dropout-rate", type=float, default=0.0, help="Dropout probability for synthetic observations.")
    parser.add_argument("--audio-sample-rate", type=int, default=16000, help="Sample rate used when analyzing media input.")
    parser.add_argument("--audio-buffer-size", type=int, default=1024, help="Analysis window size for media input.")
    parser.add_argument(
        "--audio-hop-ms",
        type=float,
        help="Frame hop for media input in milliseconds. Defaults to one analysis window.",
    )
    parser.add_argument(
        "--plot",
        type=Path,
        help="Optional SVG path for a local plot of pitch tracking and marker crossings.",
    )
    parser.add_argument("--seed", type=int, default=7, help="Random seed for particle noise and synthetic observations.")
    parser.add_argument("--trace-limit", type=int, default=12, help="How many frame lines to print from the start and end.")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    score = ReferenceScore.from_path(args.score)
    filter_config = (
        ParticleFilterConfig.from_path(args.config)
        if args.config.exists()
        else ParticleFilterConfig()
    )
    runtime = LocalEncoreRuntime(
        score,
        filter_config=filter_config,
        runtime_config=RuntimeConfig(),
        seed=args.seed,
    )

    if args.observations is not None and args.media is not None:
        parser.error("--observations and --media cannot be used together.")

    media_batch = None
    if args.media is not None:
        hop_size = (
            max(1, int(round(args.audio_hop_ms * args.audio_sample_rate / 1000.0)))
            if args.audio_hop_ms is not None
            else args.audio_buffer_size
        )
        media_batch = load_observations_from_media(
            args.media,
            config=AudioAnalysisConfig(
                sample_rate=args.audio_sample_rate,
                buffer_size=args.audio_buffer_size,
                hop_size=hop_size,
            ),
        )
        observations = list(media_batch.observations)
        source_label = f"analyzed {len(observations)} observations from {args.media}"
    elif args.observations is not None:
        observations = load_observations(args.observations)
        source_label = f"loaded {len(observations)} observations"
    else:
        observations = generate_synthetic_observations(
            score,
            frame_ms=args.frame_ms,
            lead_in_ms=args.lead_in_ms,
            tail_ms=args.tail_ms,
            pitch_jitter_hz=args.pitch_jitter_hz,
            dropout_rate=args.dropout_rate,
            seed=args.seed,
        )
        source_label = f"generated {len(observations)} synthetic observations"

    run = runtime.run(observations)

    print(f"Score: {score.title}")
    print(f"Part: {list(score.part)}")
    print(f"Duration: {score.duration_ms} ms")
    print(f"Markers: {[marker.time_ms for marker in score.markers]}")
    print(f"Particle config: count={filter_config.particle_count}, process_sigma_ms={filter_config.process_sigma_ms}, resample_sigma_ms={filter_config.resample_sigma_ms}")
    print(source_label)
    if media_batch is not None:
        print(
            f"Audio analysis: noise_floor_rms={media_batch.noise_floor_rms:.2f}, "
            f"active_threshold={media_batch.active_rms_threshold:.2f}, "
            f"frame_hop_ms={media_batch.frame_hop_ms:.2f}"
        )

    if run.flip_events:
        print("Flip events:")
        for event in run.flip_events:
            print(
                f"  t={event.observation_time_ms} ms "
                f"estimate={event.estimated_ms:.2f} ms "
                f"marker={event.marker.time_ms} ms "
                f"target_page={event.marker.target_page}"
            )
    else:
        print("Flip events: none")

    virtual_motion_events = build_virtual_motion_events(run.flip_events)
    if virtual_motion_events:
        print("Virtual motion:")
        for motion_event in virtual_motion_events:
            detail_suffix = f" {motion_event.detail}" if motion_event.detail else ""
            print(f"  t={motion_event.time_ms} ms {motion_event.label}{detail_suffix}")

    if args.plot is not None:
        plot_path = save_run_plot_svg(
            args.plot,
            score,
            run,
            title=score.title,
            subtitle=source_label,
        )
        print(f"Plot: {plot_path}")

    if not run.frames:
        return 0

    print("Trace sample:")
    trace_limit = max(1, args.trace_limit)
    sampled_frames = list(run.frames[:trace_limit])
    if len(run.frames) > trace_limit:
        sampled_frames.extend(run.frames[-trace_limit:])

    seen_keys: set[tuple[int, float]] = set()
    for frame in sampled_frames:
        key = (frame.observation.time_ms, frame.estimated_ms)
        if key in seen_keys:
            continue
        seen_keys.add(key)
        print(
            f"  t={frame.observation.time_ms:6d} "
            f"raw={frame.observation.raw_pitch_hz:8.2f} "
            f"tracked={frame.tracked_pitch_hz:8.2f} "
            f"expected={frame.expected_pitch_hz:8.2f} "
            f"estimate={frame.estimated_ms:9.2f} "
            f"next_marker={frame.next_marker_index}"
        )

    return 0
