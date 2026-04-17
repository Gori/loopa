# Loopa

A minimal audio looper for macOS. Four tracks, sample-accurate looping, Metal-rendered waveforms, no nonsense.

Early days but playable — record a loop, it locks BPM from the length, overdub, replace, mute, swap active loops. All with an audible count-in and latency-compensated capture so what you played lines up with what you heard.

## What's in the box

- **Four tracks**, each with multiple loop slots (one plays at a time).
- **BPM auto-lock** on the first recording. Picks `(bars ∈ {1,2,4,8,16}, BPM ∈ [60,140])` at two-decimal precision from the length of what you played.
- **Record modes**: `NEW` / `OVERDUB` (destructive sum) / `REPLACE`.
- **Count-in** — metronome clicks every beat until the transport wraps to the top of the loop, then capture starts.
- **Latency compensation** — `AudioIODevice::getInput/OutputLatencyInSamples()` → shift at loop finalisation. Auto-tracks device changes.
- **GPU waveforms** — `CAMetalLayer`, triangle-strip ribbon from a pre-built peak cache, 4× MSAA, continuous-float X for sub-pixel smooth scroll. All four tracks render in lock-step off one main clock.
- **Per-track palette** — amber / cyan / magenta / green. Grid lines, loop-start markers, playhead and selected-row tint all hue-locked to the track.
- **Settings** window — audio device, buffer size, live latency readout, master gain, metronome volume, default bars. Persists to `~/Library/Application Support/Loopa/settings.json`.

## Stack

- JUCE 8.0.12, C++20, CMake ≥ 3.28
- Catch2 v3 for tests
- Metal for waveform rendering
- macOS 13+ (tested on Apple Silicon)

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
open build/src/app/LoopaApp_artefacts/Release/Loopa.app
```

Tests:

```sh
ctest --test-dir build --output-on-failure
```

Everything fetches via CMake `FetchContent` — no extra dependencies to install.

## Design rules

- **No fallbacks.** One code path per feature.
- **Lock-free SPSC** command/event queues between UI and audio threads.
- **Pre-allocated buffers** everywhere on the hot path; no audio-thread allocation.
- **DJ software feel**, not a DAW. Waveforms scroll leftward, playhead lives in the middle.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `R` | Toggle record |
| `Space` | Toggle play |
| `M` | Toggle metronome |
| `⌘,` | Open settings |

## Roadmap

- MIDI foot-pedal mapping (5 actions: start/stop record, cycle ±, mute selected, next loop)
- Project save/load — `.loopa` folder with `project.json` + per-loop WAVs
- Undo / redo wired to the chip UI
- AI style transfer (server-side; per-track button in UI)
- VST3 plugin + iOS targets

## Status

Actively in development. APIs, file formats, and UI are all still subject to change.
