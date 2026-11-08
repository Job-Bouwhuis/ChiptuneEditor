# Application architecture

## Current modules

```text
chiptune_studio (application entry point)
├── chiptune_ui (Raven window and workspace navigation)
└── chiptune_audio (oscillator, WAV player, PCM renderer, WAV codec, optional ALSA output)

chiptune_wav_info ── chiptune_audio
```

- `chiptune_studio` is the composition root. It starts the UI and links the UI and audio modules.
- `chiptune_ui` owns Raven widgets and translates UI actions into application actions. It does not own audio data or WAV parsing.
- `chiptune_audio` is independent of Raven. Its `Oscillator` produces mono waveforms, `apply_adsr` shapes a note, and its `WavPlayer` service coordinates WAV decoding, PCM rendering, and output. ALSA output is compiled when the ALSA development package is present.
- `chiptune_wav_info` is a small diagnostic utility that exercises WAV loading without initializing the UI.

## Dependency rules

1. The audio module must not include Raven headers or depend on UI widgets.
2. The UI may call application and audio interfaces, but it must not implement synthesis or WAV encoding itself.
3. Audio rendering must be usable without a window or an audio device. Output backends consume rendered buffers at the device boundary.
4. Project and song data will be plain C++ types in an application/model module. They must not contain Raven handles or device state.
5. Project-relative asset references and WAV data remain separate: projects store references and editable metadata; WAV files remain independent assets.
6. Export rendering will use project/model data and the audio renderer without recording the live output device.

## Planned module growth

As features arrive, add only the boundaries needed by them:

- `chiptune_model`: instruments, samples, tracks, notes, tempo, and project state; no Raven or ALSA types.
- `chiptune_audio`: waveform synthesis, sample playback, track mixing, and offline rendering. It consumes model data through explicit render inputs.
- `chiptune_audio_output`: device setup, buffering, and transport on a worker thread; it calls the audio renderer and never blocks the UI thread.
- `chiptune_project`: project serialization and project-relative asset resolution; it does not own or duplicate referenced WAV data.
- `chiptune_ui`: views and user interaction; application state changes are passed to model/services through ordinary C++ interfaces.

The planned targets are not created until they have real behavior to own. The current audio engine has a basic oscillator and PCM playback renderer, not yet envelopes, instrument presets, or a song mixer.
