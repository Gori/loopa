# Loopa — Feature Brief

A capability inventory for designers. This document describes **what Loopa does**, not how it looks or how any capability should be surfaced. Every entry is a feature, not a UI element. Treat the layout, controls, visualisations, copy, and information hierarchy as open design problems.

## 1. Product framing

Loopa is a minimal live audio looper for performers and producers. It is **DJ-software inspired**, not DAW-like: optimised for continuous, never-stopping performance rather than offline editing. The most common use is to start a session, capture one or more layered loops, and keep playing — without ever stopping playback.

Primary platform: macOS. Future targets: VST3 plugin, iOS app.

## 2. Session & transport

- A session has a single global transport that runs continuously once started.
- Playback is a toggle (running / stopped).
- The session has a tempo (BPM) and a number of beats per bar.
- The session has a total length, derived from the longest loaded loop, and a current position that wraps at that length.
- The session reports its current bar and current beat at any moment.
- Tempo can be locked automatically from the very first recording (length-based detection across allowed bar counts {1, 2, 4, 8, 16} and a BPM range of 60–140), or set manually before recording.
- Position can be reset to the start of the loop window.
- A metronome can be enabled or disabled at any time; it produces an audible click on every beat and a distinguishable accent on bar one.
- The metronome can be configured to auto-enable after the first tempo lock.
- The metronome has its own volume independent of master output.

## 3. Tracks

The app exposes a fixed number of tracks (currently four) that all play simultaneously and share the global transport.

For each track, the user can:

- Arm or disarm the track for recording.
- Mute or unmute the track's audio output.
- Select the track. One track is the "selected" track at any time; selection drives keyboard- and MIDI-targeted actions.
- Assign which input channel feeds the track (multiple inputs are supported, e.g. guitar on one, vocal on another).
- Choose a loop length in bars for that track from the allowed set {1, 2, 4, 8, 16}.
- Choose a recording mode for that track:
  - **New** — record into a new loop slot on the track.
  - **Overdub** — destructively sum the new audio into the currently-active loop.
  - **Replace** — overwrite the currently-active loop's samples with the new recording.
- Convert the active loop with AI timbre transfer (see §6).

Multiple tracks can be armed at once. Single-track and multi-track recording flows are both supported.

## 4. Loops per track

- Each track can hold many loops; only one of them is "active" (plays) at a time.
- The active loop can be replaced (via Replace recording) or augmented (via Overdub).
- The user can cycle to the next or previous loop on a track.
- The user can switch directly to a specific loop on a track.
- The user can clear the currently active loop on a track.
- Loop switching is phase-aligned: when a loop change is requested (manual or AI-generated), the change takes effect at the next bar-one crossing of the transport, so transitions stay musically in time. If the transport isn't running, the change is immediate.
- Shorter loops on a track repeat to fill the longest loop's window — every track is always in phase.

## 5. Recording flow

- Recording starts on whichever tracks are currently armed.
- For the very first recording in a fresh session (no tempo yet), recording is open-ended: the user stops it manually, and the engine then derives BPM and bar count from the captured length.
- For all subsequent recordings (tempo already locked):
  - A **count-in** waits for the transport to reach bar one before capture begins, communicating remaining beats to the user.
  - Capture runs for exactly the track's configured bar count and stops automatically.
- The user can stop recording at any time.
- The user can perceive the recording's progress against the configured bar count as it happens.
- The system applies **automatic latency compensation** at recording finalisation, using the audio device's reported input and output latency, so what the user played lines up sample-accurately with what they heard. Compensation re-evaluates when the audio device changes.
- The system reports peak input level per track during capture, so the user knows when input is too hot, too quiet, or clipping.
- Recording is fully non-stop: the transport never has to be stopped to record, overdub, or change tracks.

## 6. AI timbre transfer

- Any track's active loop can be converted to a different instrument timbre using a neural style-transfer model. The pitch, rhythm, and length of the source are preserved; the timbre changes.
- A library of named **timbre presets** is available (e.g. specific instruments). The user picks which preset to apply per conversion.
- Conversion runs asynchronously without blocking playback or further recording.
- While a track is converting, the same track cannot start another conversion. Other tracks remain fully usable.
- When conversion completes, the result is added to the track's loop list and activated at the next bar-one crossing, phase-aligned with the playing session.
- Conversion failure is reported to the user without disrupting the session.
- Optional pre-processing can be applied to the source audio before the model runs, each independently togglable in settings:
  - **Denoise** — neural noise reduction.
  - **Vocal isolation** — source separation that keeps only the vocal stem.
  - **Loudness normalisation** — bring loudness to a configurable LUFS target.

## 7. History

- All track actions (record, overdub, replace, mode/bars/arm/mute/input changes, loop switches, AI conversions) can be undone and redone.
- Undo and redo have a bounded depth.
- A new action after an undo discards the redo branch.

## 8. Project save & load

- An entire session can be saved as a project on disk and reopened later.
- A project contains all tempo and timebase information, per-track configuration, and the audio data of every loop on every track.
- A project can be loaded into a fresh session, restoring the full state.

(Project persistence is on the roadmap; settings persistence — §10 — is already in place.)

## 9. Control surfaces

The user can drive every performance-critical action from several sources:

- **Mouse / pointer / touch** on the application's surface.
- **Computer keyboard shortcuts** for transport, recording, metronome, settings, and undo/redo. Shortcuts are global within the app window.
- **MIDI** — the user can map MIDI notes and CCs to a fixed action set:
  - Start / stop recording
  - Cycle to next loop on the selected track
  - Cycle to previous loop on the selected track
  - Mute / unmute the selected track
  - Switch to the next loop on the selected track
- MIDI bindings support a **learn flow**: the user puts an action into learn mode and sends a MIDI message to bind it.
- Each action has at most one MIDI trigger; rebinding a trigger to a different action automatically unbinds it from its previous one.
- The user can clear an individual MIDI binding or all bindings.
- The app supports **MIDI foot-pedal** use specifically, since hands-on-instrument live looping is a primary use case.

## 10. Settings

The user can configure and persist the following across sessions:

- Audio input device, audio output device, sample rate, and buffer size.
- Master output gain.
- Metronome volume.
- Whether the metronome auto-enables after the first tempo lock.
- Default bar count assigned to newly-armed tracks.
- AI pre-processing toggles (denoise, vocal isolation, loudness normalisation).

The user can observe the live audio latency of the current device configuration (input latency, output latency, buffer contribution, and total).

Settings persist across application launches.

## 11. Feedback the app provides to the user

Independent of how it's surfaced, the app must make the following continuously perceivable:

- Whether playback is currently running.
- The current tempo and beats-per-bar.
- The current position within the loop window, in both bars and beats.
- The total length of the loop window, in bars.
- The currently selected track.
- For each track: armed state, mute state, assigned input, configured bar count, configured recording mode, number of loops held, which loop is active, current record state (idle / counting in / recording), and during count-in the beats remaining.
- For each track during recording: an evolving sense of how much of the target length has been captured so far.
- For each track with audio: the loop's audio content in a form the user can scan at a glance and locate musical events within (so they can plan overdubs, mutes, and switches).
- Per-track input level during recording, including a clear indication of clipping.
- Whether the metronome is currently enabled.
- Whether a track's AI conversion is in flight.

## 12. Non-negotiable behaviour

These are operating constraints, not visual choices, but they shape what designs can permit:

- **Never stop the music.** No action in normal use should require stopping playback to perform. Surfaces and flows must respect this.
- **Phase-aligned changes.** Loop swaps, AI conversion results, and similar transitions always land at bar one. The design should communicate that a requested change is "scheduled" rather than "immediate" when relevant.
- **Sample-accurate timing.** What the user hears is what the user recorded, with latency compensated. Surfaces must not lie about position or timing.
- **Single code path per feature.** There are no alternative or fallback flows — the design should not introduce optional modes that secretly do the same thing differently.
- **Minimal, performance-first.** The app targets very low latency and high frame rates. Design choices that would require heavy per-frame work, blocking dialogs on the audio path, or modal interruptions during a take are out of scope.

## 13. Out of scope

The following are explicitly not features and should not be designed for:

- Time-stretching or pitch-shifting of recorded audio.
- Beat detection (tempo is always derived from recorded length or set manually).
- Non-destructive overdub layers (overdub is a destructive sum into the active loop).
- Arbitrary loop lengths outside {1, 2, 4, 8, 16} bars.
- Tempos outside the 60–140 BPM auto-detection range without manual entry.
- DAW-style timeline editing, regions, automation lanes, mix bus routing, or plugin chains.
