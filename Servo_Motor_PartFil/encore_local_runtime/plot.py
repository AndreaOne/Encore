from __future__ import annotations

from html import escape
from pathlib import Path

from .runtime import RuntimeRun
from .score import ReferenceScore


def save_run_plot_svg(
    path: str | Path,
    score: ReferenceScore,
    run: RuntimeRun,
    *,
    title: str = "Encore Local Runtime",
    subtitle: str = "",
) -> Path:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not run.frames:
        output_path.write_text(
            "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='200'>"
            "<rect width='100%' height='100%' fill='#f8f7f2'/>"
            "<text x='40' y='80' font-size='24' fill='#2a2a2a'>No frames to plot.</text>"
            "</svg>",
            encoding="utf-8",
        )
        return output_path

    width = 1440
    height = 980
    left = 90
    right = 60
    top = 90
    chart_width = width - left - right
    chart_height = 300
    gap = 110
    pitch_y0 = top
    timeline_y0 = top + chart_height + gap

    max_time_ms = max(frame.observation.time_ms for frame in run.frames)
    max_time_ms = max(1, max_time_ms)

    positive_pitches = [
        pitch_hz
        for frame in run.frames
        for pitch_hz in (
            frame.observation.raw_pitch_hz,
            frame.tracked_pitch_hz,
            frame.expected_pitch_hz,
        )
        if pitch_hz > 0.0
    ]
    if positive_pitches:
        pitch_min = min(positive_pitches)
        pitch_max = max(positive_pitches)
        pitch_padding = max(20.0, (pitch_max - pitch_min) * 0.12)
        pitch_min = max(0.0, pitch_min - pitch_padding)
        pitch_max += pitch_padding
    else:
        pitch_min = 200.0
        pitch_max = 1000.0

    duration_ms = max(1, score.duration_ms)

    def x_from_time(time_ms: float) -> float:
        return left + chart_width * (time_ms / max_time_ms)

    def y_from_pitch(pitch_hz: float) -> float:
        return pitch_y0 + chart_height * (1.0 - (pitch_hz - pitch_min) / max(1e-6, pitch_max - pitch_min))

    def y_from_score_ms(score_ms: float) -> float:
        return timeline_y0 + chart_height * (1.0 - score_ms / duration_ms)

    def axis_label(text: str, x: float, y: float, *, size: int = 15, anchor: str = "middle") -> str:
        return (
            f"<text x='{x:.2f}' y='{y:.2f}' font-size='{size}' "
            f"font-family='Menlo, Monaco, monospace' fill='#2b2b2b' text-anchor='{anchor}'>"
            f"{escape(text)}</text>"
        )

    def make_segment_path(points: list[tuple[float, float]]) -> str:
        if not points:
            return ""
        commands = [f"M {points[0][0]:.2f} {points[0][1]:.2f}"]
        for x_value, y_value in points[1:]:
            commands.append(f"L {x_value:.2f} {y_value:.2f}")
        return " ".join(commands)

    def build_series_path(values: list[tuple[float, float | None]], y_mapper) -> str:
        segments: list[str] = []
        current: list[tuple[float, float]] = []
        for time_ms, value in values:
            if value is None:
                if current:
                    segments.append(make_segment_path(current))
                    current = []
                continue
            current.append((x_from_time(time_ms), y_mapper(value)))
        if current:
            segments.append(make_segment_path(current))
        return " ".join(segments)

    raw_series = [
        (frame.observation.time_ms, frame.observation.raw_pitch_hz if frame.observation.raw_pitch_hz > 0.0 else None)
        for frame in run.frames
    ]
    tracked_series = [
        (frame.observation.time_ms, frame.tracked_pitch_hz if frame.tracked_pitch_hz > 0.0 else None)
        for frame in run.frames
    ]
    expected_series = [
        (frame.observation.time_ms, frame.expected_pitch_hz if frame.expected_pitch_hz > 0.0 else None)
        for frame in run.frames
    ]
    estimate_series = [
        (frame.observation.time_ms, frame.estimated_ms)
        for frame in run.frames
    ]

    raw_path = build_series_path(raw_series, y_from_pitch)
    tracked_path = build_series_path(tracked_series, y_from_pitch)
    expected_path = build_series_path(expected_series, y_from_pitch)
    estimate_path = build_series_path(estimate_series, y_from_score_ms)

    tracking_start = next((frame for frame in run.frames if frame.tracking_started), None)

    pitch_ticks = 5
    time_ticks = 6
    score_ticks = 6

    svg_parts = [
        f"<svg xmlns='http://www.w3.org/2000/svg' width='{width}' height='{height}' viewBox='0 0 {width} {height}'>",
        "<rect width='100%' height='100%' fill='#f8f7f2'/>",
        f"<text x='{left}' y='42' font-size='30' font-family='Menlo, Monaco, monospace' fill='#1f1f1f'>{escape(title)}</text>",
    ]
    if subtitle:
        svg_parts.append(
            f"<text x='{left}' y='67' font-size='16' font-family='Menlo, Monaco, monospace' fill='#5c5c5c'>{escape(subtitle)}</text>"
        )

    for chart_top, label in ((pitch_y0, "Detected / Tracked Pitch (Hz)"), (timeline_y0, "Estimated Score Position (ms)")):
        svg_parts.append(
            f"<rect x='{left}' y='{chart_top}' width='{chart_width}' height='{chart_height}' fill='white' stroke='#d6d2c4' stroke-width='1.5' rx='10'/>"
        )
        svg_parts.append(axis_label(label, left + 10, chart_top - 14, size=17, anchor="start"))

    for tick_index in range(time_ticks + 1):
        tick_ratio = tick_index / time_ticks
        x_value = left + chart_width * tick_ratio
        time_label = f"{(max_time_ms * tick_ratio) / 1000.0:.1f}s"
        for chart_top in (pitch_y0, timeline_y0):
            svg_parts.append(
                f"<line x1='{x_value:.2f}' y1='{chart_top:.2f}' x2='{x_value:.2f}' y2='{chart_top + chart_height:.2f}' stroke='#ece7da' stroke-width='1'/>"
            )
        svg_parts.append(axis_label(time_label, x_value, timeline_y0 + chart_height + 28, size=13))

    for tick_index in range(pitch_ticks + 1):
        tick_ratio = tick_index / pitch_ticks
        pitch_value = pitch_min + (pitch_max - pitch_min) * tick_ratio
        y_value = y_from_pitch(pitch_value)
        svg_parts.append(
            f"<line x1='{left:.2f}' y1='{y_value:.2f}' x2='{left + chart_width:.2f}' y2='{y_value:.2f}' stroke='#ece7da' stroke-width='1'/>"
        )
        svg_parts.append(axis_label(f"{pitch_value:.0f}", left - 12, y_value + 5, size=13, anchor="end"))

    for tick_index in range(score_ticks + 1):
        tick_ratio = tick_index / score_ticks
        score_value = duration_ms * tick_ratio
        y_value = y_from_score_ms(score_value)
        svg_parts.append(
            f"<line x1='{left:.2f}' y1='{y_value:.2f}' x2='{left + chart_width:.2f}' y2='{y_value:.2f}' stroke='#ece7da' stroke-width='1'/>"
        )
        svg_parts.append(axis_label(f"{score_value:.0f}", left - 12, y_value + 5, size=13, anchor="end"))

    if expected_path:
        svg_parts.append(
            f"<path d='{expected_path}' fill='none' stroke='#f28e2b' stroke-width='2' opacity='0.9'/>"
        )
    if raw_path:
        svg_parts.append(
            f"<path d='{raw_path}' fill='none' stroke='#4e79a7' stroke-width='1.8' opacity='0.75'/>"
        )
    if tracked_path:
        svg_parts.append(
            f"<path d='{tracked_path}' fill='none' stroke='#59a14f' stroke-width='2.6' opacity='0.95'/>"
        )
    if estimate_path:
        svg_parts.append(
            f"<path d='{estimate_path}' fill='none' stroke='#4e79a7' stroke-width='2.8' opacity='0.95'/>"
        )

    for marker in score.markers:
        y_value = y_from_score_ms(marker.time_ms)
        svg_parts.append(
            f"<line x1='{left:.2f}' y1='{y_value:.2f}' x2='{left + chart_width:.2f}' y2='{y_value:.2f}' stroke='#c44e52' stroke-width='1.5' stroke-dasharray='8 6' opacity='0.9'/>"
        )
        svg_parts.append(
            axis_label(
                f"marker {marker.time_ms} ms -> p{marker.target_page}",
                left + chart_width - 10,
                y_value - 8,
                size=13,
                anchor="end",
            )
        )

    if tracking_start is not None:
        x_value = x_from_time(tracking_start.observation.time_ms)
        for chart_top in (pitch_y0, timeline_y0):
            svg_parts.append(
                f"<line x1='{x_value:.2f}' y1='{chart_top:.2f}' x2='{x_value:.2f}' y2='{chart_top + chart_height:.2f}' stroke='#2ca02c' stroke-width='1.6' stroke-dasharray='6 5' opacity='0.9'/>"
            )
        svg_parts.append(
            axis_label(
                f"tracking start {tracking_start.observation.time_ms} ms",
                x_value + 8,
                pitch_y0 + 18,
                size=13,
                anchor="start",
            )
        )

    for event in run.flip_events:
        x_value = x_from_time(event.observation_time_ms)
        estimate_y = y_from_score_ms(event.estimated_ms)
        for chart_top in (pitch_y0, timeline_y0):
            svg_parts.append(
                f"<line x1='{x_value:.2f}' y1='{chart_top:.2f}' x2='{x_value:.2f}' y2='{chart_top + chart_height:.2f}' stroke='#d62728' stroke-width='1.8' stroke-dasharray='4 4' opacity='0.95'/>"
            )
        svg_parts.append(
            f"<circle cx='{x_value:.2f}' cy='{estimate_y:.2f}' r='5.5' fill='#d62728' stroke='white' stroke-width='1.5'/>"
        )
        svg_parts.append(
            axis_label(
                f"flip p{event.marker.target_page} @ {event.observation_time_ms} ms",
                x_value + 8,
                estimate_y - 10,
                size=13,
                anchor="start",
            )
        )

    legend_x = left + chart_width - 260
    legend_y = pitch_y0 + 18
    legend_items = [
        ("#4e79a7", "raw detected pitch"),
        ("#59a14f", "tracked score pitch"),
        ("#f28e2b", "expected pitch from PF"),
        ("#4e79a7", "estimated score ms"),
    ]
    for index, (color, label) in enumerate(legend_items):
        y_value = legend_y + index * 22
        svg_parts.append(
            f"<line x1='{legend_x:.2f}' y1='{y_value:.2f}' x2='{legend_x + 28:.2f}' y2='{y_value:.2f}' stroke='{color}' stroke-width='3'/>"
        )
        svg_parts.append(axis_label(label, legend_x + 36, y_value + 5, size=13, anchor="start"))

    svg_parts.append(axis_label("recording time", left + chart_width / 2, timeline_y0 + chart_height + 58, size=16))
    svg_parts.append(
        f"<text transform='translate(24 {pitch_y0 + chart_height / 2:.2f}) rotate(-90)' font-size='16' font-family='Menlo, Monaco, monospace' fill='#2b2b2b'>pitch (Hz)</text>"
    )
    svg_parts.append(
        f"<text transform='translate(24 {timeline_y0 + chart_height / 2:.2f}) rotate(-90)' font-size='16' font-family='Menlo, Monaco, monospace' fill='#2b2b2b'>score ms</text>"
    )
    svg_parts.append("</svg>")

    output_path.write_text("\n".join(svg_parts), encoding="utf-8")
    return output_path
