from __future__ import annotations

import argparse
import io
import json
import math
import re
import zipfile
from pathlib import Path
from typing import Any
from typing import Iterable
import xml.etree.ElementTree as ET


DEFAULT_TEMPO_BPM = 120.0
BEAT_UNIT_TO_QUARTERS = {
    "whole": 4.0,
    "half": 2.0,
    "quarter": 1.0,
    "eighth": 0.5,
    "16th": 0.25,
    "32nd": 0.125,
    "64th": 0.0625,
}
STEP_TO_SEMITONE = {
    "C": 0,
    "D": 2,
    "E": 4,
    "F": 5,
    "G": 7,
    "A": 9,
    "B": 11,
}


def strip_ns(tag: str) -> str:
    return tag.split("}", 1)[-1] if "}" in tag else tag


def child_elements(parent: ET.Element, name: str | None = None) -> Iterable[ET.Element]:
    for child in list(parent):
        if name is None or strip_ns(child.tag) == name:
            yield child


def find_child(parent: ET.Element, name: str) -> ET.Element | None:
    for child in child_elements(parent, name):
        return child
    return None


def find_text(parent: ET.Element, name: str, default: str = "") -> str:
    child = find_child(parent, name)
    if child is None or child.text is None:
        return default
    return child.text.strip()


def parse_number(value: str | None, default: float = 0.0) -> float:
    if value is None:
        return default
    try:
        return float(value.strip())
    except (AttributeError, ValueError):
        return default


def parse_int(value: str | None, default: int = 0) -> int:
    if value is None:
        return default
    try:
        return int(value.strip())
    except (AttributeError, ValueError):
        return default


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "score"


def load_musicxml_bytes(raw_bytes: bytes) -> bytes:
    if raw_bytes[:2] != b"PK":
        return raw_bytes

    with zipfile.ZipFile(io.BytesIO(raw_bytes)) as archive:
        try:
            container_bytes = archive.read("META-INF/container.xml")
            container_root = ET.fromstring(container_bytes)
            for root_file in container_root.iter():
                if strip_ns(root_file.tag) != "rootfile":
                    continue
                full_path = root_file.attrib.get("full-path")
                if full_path:
                    return archive.read(full_path)
        except KeyError:
            pass

        for name in archive.namelist():
            lower_name = name.lower()
            if lower_name.endswith(".musicxml") or lower_name.endswith(".xml"):
                return archive.read(name)

    raise ValueError("Could not locate a MusicXML document inside the uploaded .mxl file.")


def extract_title(root: ET.Element) -> str:
    work = find_child(root, "work")
    if work is not None:
        title = find_text(work, "work-title")
        if title:
            return title

    movement_title = find_text(root, "movement-title")
    if movement_title:
        return movement_title

    for credit in child_elements(root, "credit"):
        for credit_words in child_elements(credit, "credit-words"):
            text = (credit_words.text or "").strip()
            if text:
                return text

    return "Untitled Score"


def parse_part_names(root: ET.Element) -> dict[str, str]:
    names: dict[str, str] = {}
    part_list = find_child(root, "part-list")
    if part_list is None:
        return names

    for score_part in child_elements(part_list, "score-part"):
        part_id = score_part.attrib.get("id", "").strip()
        part_name = find_text(score_part, "part-name", part_id or "Part")
        if part_id:
            names[part_id] = part_name
    return names


def measure_number_from_element(measure: ET.Element, fallback: int) -> int:
    raw_number = (measure.attrib.get("number") or "").strip()
    if raw_number.isdigit():
        return int(raw_number)

    match = re.search(r"\d+", raw_number)
    if match:
        return int(match.group(0))

    return fallback


def read_duration_quarters(element: ET.Element, divisions: float) -> float:
    duration_text = find_text(element, "duration")
    if not duration_text:
        return 0.0
    return parse_number(duration_text) / max(divisions, 1.0)


def extract_tempo_bpm(direction: ET.Element) -> float | None:
    sound = find_child(direction, "sound")
    if sound is not None:
        tempo = parse_number(sound.attrib.get("tempo"), 0.0)
        if tempo > 0:
            return tempo

    direction_type = find_child(direction, "direction-type")
    if direction_type is None:
        return None

    metronome = find_child(direction_type, "metronome")
    if metronome is None:
        return None

    beat_unit = find_text(metronome, "beat-unit").lower()
    per_minute = parse_number(find_text(metronome, "per-minute"), 0.0)
    if per_minute <= 0:
        return None

    beat_length = BEAT_UNIT_TO_QUARTERS.get(beat_unit, 1.0)
    dot_count = sum(1 for _ in child_elements(metronome, "beat-unit-dot"))
    if dot_count:
        beat_length *= sum(0.5 ** index for index in range(dot_count + 1))

    return per_minute * beat_length


def parse_pitch_midi(note: ET.Element) -> int | None:
    pitch = find_child(note, "pitch")
    if pitch is None:
        return None

    step = find_text(pitch, "step").upper()
    octave = parse_int(find_text(pitch, "octave"), -99)
    alter = parse_int(find_text(pitch, "alter"), 0)
    if step not in STEP_TO_SEMITONE or octave < -1:
        return None

    return (octave + 1) * 12 + STEP_TO_SEMITONE[step] + alter


def midi_to_freq_hundred(midi_note: int) -> int:
    frequency = 440.0 * math.pow(2.0, (midi_note - 69) / 12.0)
    return int(round(frequency * 100.0))


def parse_part(part: ET.Element, part_name: str) -> dict[str, Any]:
    part_id = part.attrib.get("id", "").strip()
    divisions = 1.0
    cursor_q = 0.0
    page_number = 1
    raw_notes: list[dict[str, Any]] = []
    raw_measures: list[dict[str, Any]] = []
    tempo_events: list[tuple[float, float]] = []

    for fallback_measure_number, measure in enumerate(child_elements(part, "measure"), start=1):
        measure_index = fallback_measure_number - 1
        measure_number = measure_number_from_element(measure, fallback_measure_number)
        if raw_measures:
            new_page = any(
                print_node.attrib.get("new-page") == "yes"
                for print_node in child_elements(measure, "print")
            )
            if new_page:
                page_number += 1

        measure_start_q = cursor_q
        measure_max_q = measure_start_q
        last_note_start_q = measure_start_q

        for node in list(measure):
            tag = strip_ns(node.tag)

            if tag == "attributes":
                new_divisions = parse_number(find_text(node, "divisions"), 0.0)
                if new_divisions > 0:
                    divisions = new_divisions
                continue

            if tag == "direction":
                bpm = extract_tempo_bpm(node)
                if bpm is not None:
                    tempo_events.append((cursor_q, bpm))
                continue

            if tag == "backup":
                cursor_q = max(measure_start_q, cursor_q - read_duration_quarters(node, divisions))
                continue

            if tag == "forward":
                cursor_q += read_duration_quarters(node, divisions)
                measure_max_q = max(measure_max_q, cursor_q)
                continue

            if tag != "note":
                continue

            duration_q = read_duration_quarters(node, divisions)
            is_chord = find_child(node, "chord") is not None
            start_q = last_note_start_q if is_chord else cursor_q

            if not is_chord:
                last_note_start_q = start_q

            measure_max_q = max(measure_max_q, start_q + duration_q)

            if find_child(node, "rest") is None and duration_q > 0:
                midi_note = parse_pitch_midi(node)
                if midi_note is not None:
                    tie_types = {
                        tie.attrib.get("type", "").strip()
                        for tie in child_elements(node, "tie")
                        if tie.attrib.get("type")
                    }
                    raw_notes.append(
                        {
                            "start_q": start_q,
                            "dur_q": duration_q,
                            "midi": midi_note,
                            "measure_index": measure_index,
                            "measure": measure_number,
                            "page": page_number,
                            "voice": find_text(node, "voice", "1"),
                            "staff": find_text(node, "staff", "1"),
                            "tie_start": "start" in tie_types,
                            "tie_stop": "stop" in tie_types,
                        }
                    )

            if not is_chord:
                cursor_q += duration_q
                measure_max_q = max(measure_max_q, cursor_q)

        cursor_q = max(measure_max_q, measure_start_q)
        raw_measures.append(
            {
                "index": measure_index,
                "number": measure_number,
                "page": page_number,
                "start_q": measure_start_q,
                "end_q": cursor_q,
            }
        )

    return {
        "id": part_id or part_name,
        "name": part_name or part_id or "Part",
        "raw_notes": raw_notes,
        "raw_measures": raw_measures,
        "tempo_events": tempo_events,
    }


def merge_ties(raw_notes: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged: list[dict[str, Any]] = []
    open_ties: dict[tuple[str, str, int], dict[str, Any]] = {}

    for note in raw_notes:
        note_end_q = note["start_q"] + note["dur_q"]
        key = (note["voice"], note["staff"], note["midi"])

        if note["tie_stop"] and key in open_ties:
            existing = open_ties[key]
            existing["dur_q"] = max(existing["dur_q"], note_end_q - existing["start_q"])
            if note["tie_start"]:
                continue
            open_ties.pop(key, None)
            continue

        compact = {
            "start_q": note["start_q"],
            "dur_q": note["dur_q"],
            "midi": note["midi"],
            "measure_index": note["measure_index"],
            "measure": note["measure"],
            "page": note["page"],
        }
        merged.append(compact)

        if note["tie_start"]:
            open_ties[key] = compact

    return merged


def normalize_tempo_events(tempo_events: list[tuple[float, float]]) -> list[tuple[float, float]]:
    ordered = sorted(tempo_events, key=lambda item: (item[0], item[1]))
    normalized: list[tuple[float, float]] = []
    for position_q, bpm in ordered:
        if not normalized:
            normalized.append((position_q, bpm))
            continue
        last_position_q, _ = normalized[-1]
        if abs(last_position_q - position_q) < 1e-6:
            normalized[-1] = (position_q, bpm)
        else:
            normalized.append((position_q, bpm))

    if not normalized or normalized[0][0] > 1e-6:
        normalized.insert(0, (0.0, DEFAULT_TEMPO_BPM))
    elif normalized[0][1] <= 0:
        normalized[0] = (0.0, DEFAULT_TEMPO_BPM)

    return normalized


def build_tempo_segments(tempo_events: list[tuple[float, float]]) -> list[tuple[float, float, float]]:
    normalized = normalize_tempo_events(tempo_events)
    segments: list[tuple[float, float, float]] = []
    elapsed_ms = 0.0

    for index, (position_q, bpm) in enumerate(normalized):
        if index > 0:
            previous_q, previous_bpm = normalized[index - 1]
            elapsed_ms += (position_q - previous_q) * 60000.0 / previous_bpm
        segments.append((position_q, bpm, elapsed_ms))

    return segments


def quarter_position_to_ms(position_q: float, segments: list[tuple[float, float, float]]) -> float:
    active_segment = segments[0]
    for segment in segments:
        if position_q >= segment[0]:
            active_segment = segment
        else:
            break

    segment_q, bpm, segment_ms = active_segment
    return segment_ms + ((position_q - segment_q) * 60000.0 / bpm)


def build_pages(measures: list[list[int]]) -> list[list[int]]:
    if not measures:
        return []

    pages: list[list[int]] = []
    current_page = measures[0][3]
    first_measure = measures[0][0]
    last_measure = measures[0][0]

    for measure in measures[1:]:
        measure_number, _, _, page_number, _, _ = measure
        if page_number != current_page:
            pages.append([current_page, first_measure, last_measure])
            current_page = page_number
            first_measure = measure_number
        last_measure = measure_number

    pages.append([current_page, first_measure, last_measure])
    return pages


def build_default_markers(
    notes: list[list[int]],
    measures: list[list[int]],
    pages: list[list[int]],
) -> list[list[int]]:
    if len(pages) <= 1:
        return []

    markers: list[list[int]] = []

    for page_index in range(1, len(pages)):
        target_page = pages[page_index][0]
        first_measure_on_target = pages[page_index][1]
        target_measure_index = next(
            (index for index, measure in enumerate(measures) if measure[0] == first_measure_on_target and measure[3] == target_page),
            None,
        )
        if target_measure_index is None:
            continue

        anchor_index = max(0, target_measure_index - 1)
        while anchor_index > 0 and measures[anchor_index][4] < 0:
            anchor_index -= 1

        anchor_measure = measures[anchor_index]
        event_index = anchor_measure[4]
        time_ms = anchor_measure[1]
        if 0 <= event_index < len(notes):
            time_ms = notes[event_index][0]

        markers.append([time_ms, event_index, anchor_measure[0], target_page])

    markers.sort(key=lambda marker: marker[0])
    return markers


def build_part_bundle(
    title: str,
    parsed_part: dict[str, Any],
    tempo_segments: list[tuple[float, float, float]],
) -> dict[str, Any]:
    display_noteheads = sorted(
        parsed_part["raw_notes"],
        key=lambda note: (note["start_q"], note["midi"], note["dur_q"]),
    )
    merged_notes = merge_ties(parsed_part["raw_notes"])
    merged_notes.sort(key=lambda note: (note["start_q"], note["midi"], note["dur_q"]))

    notes: list[list[int]] = []
    noteheads: list[list[int]] = []
    measure_event_bounds: dict[int, list[int]] = {}

    for note in display_noteheads:
        start_ms = int(round(quarter_position_to_ms(note["start_q"], tempo_segments)))
        end_ms = int(round(quarter_position_to_ms(note["start_q"] + note["dur_q"], tempo_segments)))
        duration_ms = max(1, end_ms - start_ms)
        noteheads.append(
            [
                start_ms,
                duration_ms,
                midi_to_freq_hundred(note["midi"]),
                note["measure"],
                note["page"],
                note["measure_index"],
            ]
        )

    for note in merged_notes:
        start_ms = int(round(quarter_position_to_ms(note["start_q"], tempo_segments)))
        end_ms = int(round(quarter_position_to_ms(note["start_q"] + note["dur_q"], tempo_segments)))
        duration_ms = max(1, end_ms - start_ms)
        compact_note = [
            start_ms,
            duration_ms,
            midi_to_freq_hundred(note["midi"]),
            note["measure"],
            note["page"],
        ]
        event_index = len(notes)
        notes.append(compact_note)

        bounds = measure_event_bounds.setdefault(note["measure_index"], [event_index, event_index])
        bounds[1] = event_index

    measures: list[list[int]] = []
    duration_ms = 0

    for raw_measure in parsed_part["raw_measures"]:
        start_ms = int(round(quarter_position_to_ms(raw_measure["start_q"], tempo_segments)))
        end_ms = int(round(quarter_position_to_ms(raw_measure["end_q"], tempo_segments)))
        duration_ms = max(duration_ms, end_ms)
        bounds = measure_event_bounds.get(raw_measure["index"], [-1, -1])
        measures.append(
            [
                raw_measure["number"],
                start_ms,
                end_ms,
                raw_measure["page"],
                bounds[0],
                bounds[1],
            ]
        )

    if notes:
        duration_ms = max(duration_ms, max(note[0] + note[1] for note in notes))

    pages = build_pages(measures)
    markers = build_default_markers(notes, measures, pages)

    return {
        "v": 1,
        "title": title,
        "part": [parsed_part["id"], parsed_part["name"]],
        "dur": duration_ms,
        "notes": notes,
        "noteheads": noteheads,
        "measures": measures,
        "pages": pages,
        "markers": markers,
    }


def analyze_musicxml(raw_bytes: bytes) -> dict[str, Any]:
    xml_bytes = load_musicxml_bytes(raw_bytes)
    root = ET.fromstring(xml_bytes)

    if strip_ns(root.tag) != "score-partwise":
        raise ValueError("Only score-partwise MusicXML documents are supported right now.")

    title = extract_title(root)
    part_names = parse_part_names(root)
    parsed_parts: list[dict[str, Any]] = []
    tempo_events: list[tuple[float, float]] = []

    for part in child_elements(root, "part"):
        part_id = part.attrib.get("id", "").strip()
        parsed = parse_part(part, part_names.get(part_id, part_id or "Part"))
        parsed_parts.append(parsed)
        tempo_events.extend(parsed["tempo_events"])

    tempo_segments = build_tempo_segments(tempo_events)
    bundles = [
        build_part_bundle(title, parsed_part, tempo_segments)
        for parsed_part in parsed_parts
        if parsed_part["raw_notes"] or parsed_part["raw_measures"]
    ]

    return {
        "title": title,
        "parts": bundles,
    }


def choose_part(score: dict[str, Any], part_id: str | None) -> dict[str, Any]:
    parts = score.get("parts", [])
    if not parts:
        raise ValueError("No playable parts were found in this MusicXML file.")

    if not part_id:
        return parts[0]

    for bundle in parts:
        bundle_part_id = bundle.get("part", ["", ""])[0]
        if bundle_part_id == part_id:
            return bundle

    raise ValueError(f"Part '{part_id}' was not found in the parsed score.")


def validate_bundle(bundle: dict[str, Any]) -> dict[str, Any]:
    required_keys = {"v", "title", "part", "dur", "notes", "measures", "pages", "markers"}
    missing = required_keys.difference(bundle)
    if missing:
        missing_list = ", ".join(sorted(missing))
        raise ValueError(f"Bundle is missing required keys: {missing_list}")

    part = bundle["part"]
    if not isinstance(part, list) or len(part) != 2:
        raise ValueError("Bundle 'part' must be a two-item array: [id, name].")

    notes = bundle["notes"]
    markers = bundle["markers"]
    if not isinstance(notes, list) or not isinstance(markers, list):
        raise ValueError("Bundle notes and markers must be arrays.")

    for note in notes:
        if not isinstance(note, list) or len(note) != 5:
            raise ValueError("Each note entry must be [startMs, durationMs, freq100, measure, page].")

    for marker in markers:
        if not isinstance(marker, list) or len(marker) != 4:
            raise ValueError("Each marker entry must be [timeMs, eventIndex, measure, targetPage].")

    return bundle


def export_bundle(bundle: dict[str, Any], output_path: Path) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    compact_bundle = validate_bundle(bundle)
    output_path.write_text(
        json.dumps(compact_bundle, separators=(",", ":"), ensure_ascii=True),
        encoding="utf-8",
    )
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert MusicXML into a compact score bundle.")
    parser.add_argument("input", type=Path, help="Path to a .musicxml, .xml, or .mxl file.")
    parser.add_argument("--part", help="Part id to export. Defaults to the first parsed part.")
    parser.add_argument("--list-parts", action="store_true", help="List parsed parts and exit.")
    parser.add_argument("-o", "--output", type=Path, help="Where to write the compact JSON bundle.")
    args = parser.parse_args()

    score = analyze_musicxml(args.input.read_bytes())

    if args.list_parts:
        for bundle in score["parts"]:
            part_id, part_name = bundle["part"]
            print(f"{part_id}\t{part_name}\t{len(bundle['notes'])} notes")
        return

    bundle = choose_part(score, args.part)

    if args.output:
        export_bundle(bundle, args.output)
        print(args.output)
        return

    print(json.dumps(bundle, indent=2))


if __name__ == "__main__":
    main()
