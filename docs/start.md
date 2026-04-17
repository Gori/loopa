I want to create a minimal audio looper with AI style transfer support. The name of the project is Loopa.

The primary platform is MacOS, but we might want it as a VST plugin and an iOS app too.

Here’s what I’m thinking:
* 4 audio tracks (possibly more later, but NOT now)
* MIDI foot pedal support.
* Project load/save
* Absolutely latest versions of every single library
* JUCE 8
* The most - absolutely most - performant UI library / model available. This is a minimal and clean and super performant app. That means OpenGL.
* Really minimal UI. This is where you need to be your best UX designer.
* Use the best components / UI library available for JUCE 8, as long as it is super performant.
* A waveform based UI with four waveforms below each other that scroll to the left. The playhead is still in the middle, the waveforms move.
* It should be possible to record or load waveforms.
* BPM from first recorded track or otherwise set before
* NO Time stretching and pitch shifting
* Overdubs or recording over
* Each track can have a set of loops, but only play one at a time
* SUPER SUPER SUPER performant
* Metronome (but not before there is a BPM set / got from the first track)
* Multiple audio inputs (for example, one guitar track, one vocal track)
* Record / Play / Overdub
* Record a loop and play it back continuously
* The most common usecase is that you start recording and then never ever stop the playback.
* A count-in for recording, if recording is started when it's playing
* Overdub (layer new audio on top of existing loop)
* Undo / Redo
* Fully built to work without ever stopping
* First loop sets BPM (unless the user has set the BPM before) and when the user stops the recording, the application detects the BPM and how many bars it is. The number of bars for each loop can only be 1,2,4,8,16
* The total length is based on the longest loop.
* Tracks with shorter loops play the shorter loops several times as long as the longest loop.
* Super low latency, super reliable
* Very minimal and well designed UI that is more inspired by DJ Software than a DAW
* Bar position is shown as it is played. It never reaches more than the length of the longest loop.
* There is three recording modes, as set on each track: New (records a new loop, in a new slot), Overdub (records a new loop on top of the old loop, creating a new loop with both audio) and Replace (replaces the existing loop totally)
* The AI features are going to be added later but will be server based and based on style transfer. This means that the user will be able to press a button on a track and enter an instrument, and the audio will be sent to a server that will then return a style transferred version of the track). You will build and implement the AI part too.
* I want it to be built so that when it is run, we can both see all logs
* I want you to fully build it with automated tests. Everything should be tested.
* STRICT RULE: The application can contain no fallbacks or alternative ways of doing anything. Full DRY implementation. NO WORKAROUNDS. CLEAN IMPLEMENTATION. Super important since this will be an open source application.


  - 4-track audio looper with record/playback/overdub/replace
  - BPM auto-detection from first recording (length-based)
  - Count-in visual display for subsequent track recording
  - Auto-stop recording at configured bar count
  - Undo/Redo for all track actions (Cmd+Z / Cmd+Shift+Z)
  - Per-track: arm, mute, volume, monitor, record mode, loop slot selector
  - Waveform display with grid lines and loop repeat markers
  - Level meters with clip indicators
  - Metronome (togglable, synthesized click)
  - MIDI learn mode with global mappings
  - Project save/load (folder with WAV + JSON)
  - Keyboard shortcuts (Space, R, 1-4, M, Cmd+S/O/N)
  - macOS menu bar (File, Edit)
  - Dark DJ-inspired theme
  - AI style transfer placeholder (greyed out button + text field per track)
  - Performance profiling infrastructure

Here's the most common usecase:
1. The user starts with a clean / new project, which means all tracks are empty and there is no BPM set. The first channel is also armed.
2. The user presses record, which both starts playback and record. (playback = whether the timeline is moving, record = whether the armed tracks are being recorded)
3. The record starts directly, without pause. While it is recording, the waveform that is recorded is shown.
4. Once the user presses record again, the recording stops and the recorded audio starts playing immediately, from the start, as the playhead moves there. This recorded audio is now looping. The track is still armed, but the record mode is off.
5. The BPM / length of the first recording is set automatically by the application. This means that based on the length of the recording it is set to any of the existing loop lengths, based on a range of normal BPM (60-140) This is shown at the top.
6. Now the user, as it is playing, presses to arm the second track. This does nothing except arm it and SELECTS the track. (Only one track is SELECTED at once)
7. After that the user enables record again. This does not start recording directly, it waits until the playhead is at the start - and a countdown is shown (in beats) on the second track is shown until it reaches the start.
8. The second track starts recording, this time limited to the LOOP LENGTH next to the track. So it records the specified loop length. After that, recording is stopped, and the looper is now playing both tracks.
