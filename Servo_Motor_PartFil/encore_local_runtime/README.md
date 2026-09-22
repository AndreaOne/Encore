# Encore Local Runtime

Pure-Python local runtime for the `generated/encore` particle-filter logic.

This folder is intentionally separate from the Arduino runtime. It lets you:

- load the same `score_bundle.json`
- load the same particle filter YAML defaults
- simulate pitch observations locally
- analyze a local `mp4` or `wav` recording like a fake microphone input
- print a virtual motor/servo action log when a page flip would fire
- verify when flip markers would fire

## Run

From the repo root:

```bash
python3 -m encore_local_runtime
```

That uses:

- `generated/encore/data/score_bundle.json`
- `generated/encore/data/particle_filter_config.yaml`

and generates a synthetic pitch stream directly from the score.

## Use A Recording

On macOS you can point it at an `mp3`, `mp4`, `mov`, `m4a`, or `wav` file and it will
extract audio with `afconvert`, run the same RMS + autocorrelation style pitch
estimation as the firmware, and feed that into the local runtime:

```bash
python3 -m encore_local_runtime --media /absolute/path/to/take.mp3
```

Useful audio flags:

```bash
python3 -m encore_local_runtime --media /absolute/path/to/take.mp3 --audio-hop-ms 64
python3 -m encore_local_runtime --media /absolute/path/to/take.wav --audio-buffer-size 1024
python3 -m encore_local_runtime --media /absolute/path/to/take.wav --plot /tmp/encore_run.svg
```

## Useful Flags

```bash
python3 -m encore_local_runtime --lead-in-ms 1000 --pitch-jitter-hz 2.0
python3 -m encore_local_runtime --frame-ms 100 --dropout-rate 0.1
python3 -m encore_local_runtime --observations path/to/observations.json
```

Observation JSON may be:

```json
[
  {"time_ms": 0, "raw_pitch_hz": -1},
  {"time_ms": 125, "raw_pitch_hz": 293.66}
]
```

or:

```json
[
  [0, -1],
  [125, 293.66]
]
```

## Tests

```bash
python3 -m unittest tests.test_encore_local_runtime
```
