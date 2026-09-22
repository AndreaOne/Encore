# Encore
Sound-responsive auto page turner.

This repo now has three layers:

1. A local score-prep web tool for uploading MusicXML, rendering the score, and placing flip markers.
2. A backend parser that turns MusicXML into timed pitch events plus compact page-flip markers.
3. An ESP32 runtime sketch that reads the exported JSON bundle from SPIFFS and triggers the page flip when the particle filter crosses a marker.

## Score Prep Tool

Start the local tool:

```bash
python3 tools/score_server.py
```

Then open `http://127.0.0.1:8000`.

The browser tool lets you:

- upload `.musicxml`, `.xml`, or compressed `.mxl`
- choose which MusicXML part should be tracked at runtime
- view the rendered score
- place or move flip markers by anchoring them to parsed note events
- export one compact runtime JSON bundle into `generated/`

Note: score rendering uses OpenSheetMusicDisplay from a CDN. If that script is unavailable, the marker editor still works from the parsed timeline.

## CLI Export

You can also export directly from the command line:

```bash
python3 tools/score_bundle.py path/to/score.musicxml --list-parts
python3 tools/score_bundle.py path/to/score.musicxml --part P1 -o generated/score_bundle.json
```

## Exported JSON Shape

The exported runtime bundle is intentionally compact:

```json
{
  "v": 1,
  "title": "Simple Tune",
  "part": ["P1", "Melody"],
  "dur": 3000,
  "notes": [[0, 500, 26163, 1, 1]],
  "measures": [[1, 0, 1000, 1, 0, 1]],
  "pages": [[1, 1, 2]],
  "markers": [[1000, 2, 2, 2]]
}
```

Field meanings:

- `dur`: total runtime duration in milliseconds
- `notes`: `[startMs, durationMs, frequencyHzTimes100, measureNumber, pageNumber]`
- `markers`: `[timeMs, eventIndex, measureNumber, targetPage]`

The ESP32 runtime only needs `dur`, `notes`, and `markers`, but the extra arrays stay in the same file so the frontend can reopen or inspect the same bundle later.

## ESP32 Runtime

The JSON-driven runtime sketch lives in:

- `Particle_Filter_JSON_Runtime/Particle_Filter_JSON_Runtime.ino`

It expects the exported bundle to be uploaded to SPIFFS at:

- `/score_bundle.json`

At startup it:

- mounts SPIFFS
- parses the compact JSON bundle
- initializes the particle filter over score time in milliseconds

At runtime it:

- estimates live pitch from the microphone
- compares that pitch to the active reference event(s) in the selected part
- advances the particle filter through the score timeline
- triggers the motor + servo page flip when the estimated position crosses the next marker

## Tests

Backend parsing is covered with a small fixture-based test suite:

```bash
python3 -m unittest tests.test_score_bundle
```
