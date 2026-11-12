# PRIZM V2: Technical Design

PRIZM V2 is an AI-assisted mixing and mastering plugin (JUCE 8.0.12, C++17; VST3, AU and Standalone). It builds on the V1 project in this folder: same class names, plugin codes and look. The V1 README described features that were never in the code; this document describes what V2 actually contains.

## 1. What changed from V1

| Area | V1 (actual code) | V2 |
|---|---|---|
| Audio processing | None (`processBlock` only metered) | Input trim → parametric EQ → compressor → 4× oversampled saturation → stereo imager → true-peak limiter → output trim |
| Chat | Keyword match returning canned text | Local language parser → proposals with real parameter changes; nothing is applied until you approve it |
| Metering | Block RMS/peak, spectrum assumed 44.1 kHz | BS.1770-4 / EBU R128 LUFS (momentary, short-term, integrated, loudness range), true peak, RMS/peak, gain reduction, vectorscope, correlation |
| Reference tracks | None | Background analysis; comparison of loudness, dynamics, tonal balance and width; "match reference" proposals |
| Undo / A-B | None | Undo and redo for every change, restore any earlier state or the original, delay-matched and level-matched A/B, mono check |
| Parameters / state | None | 44 parameters the host can automate (APVTS); state includes the reference analysis |
| Standalone | Forced silence | Normal processing. JUCE's standalone wrapper mutes the input by default to avoid feedback; unmute it in Options |

The V1 commands still exist and now change real parameters: warm, bright, telephone/radio/vintage, punch, smooth, tight, reset and help.

## 2. Research basis

* **Ozone Master Assistant** ([Ozone 9 docs](https://s3.amazonaws.com/izotopedownloads/docs/ozone9/en/master-assistant/index.html)). Workflow:
  1. Listen to the loudest part of the track.
  2. Build a target, either from genre curves or from a reference track's loudness and EQ curve.
  3. Set the EQ, dynamics (only above a crest-factor threshold) and maximizer (ceiling −1 dBTP for streaming, −0.3 dBTP for CD).
  4. The user accepts or cancels, and cancelling restores the previous settings.

  PRIZM follows the same accept-or-cancel principle, with its own algorithms.
* **Ozone Maximizer** ([docs](https://downloads.izotope.com/docs/ozone9/en/maximizer/index.html)): threshold and ceiling, true-peak limiting, character (timing), gain-reduction metering.
* **Ozone Imager** ([docs](https://s3.amazonaws.com/izotopedownloads/docs/ozone9/en/imager/index.html)) and goniometer reading: patterns inside the 45° lines are in phase; correlation from −1 to +1, with mixes normally between 0 and +1.
* **ITU-R BS.1770-4 / EBU R128, Tech 3341/3342** ([EBU loudness](https://tech.ebu.ch/loudness)): K-weighting, 400 ms and 3 s windows, −70 LUFS absolute gate and −10 LU relative gate, loudness range from the 10th to 95th percentile.
* **Giannoulis, Massberg and Reiss**, "Digital Dynamic Range Compressor Design – A Tutorial and Analysis", JAES 60(6), 2012 ([AES](https://www.aes.org/e-lib/browse.cfm?elib=16354)): soft-knee gain computer in the dB domain, and a smooth decoupled peak detector.
* **Pestana, Ma, Reiss et al.**, "Spectral Characteristics of Popular Commercial Recordings 1950–2010", AES 135, 2013 ([PDF](https://joshreiss.github.io/documents/2013/Pestana%20Ma%20Reiss%20-%20AES135.pdf)). Their average long-term spectrum falls about 5 dB/octave between 100 Hz and 10 kHz, so PRIZM uses −5 dB/oct as the typical tilt and treats −6.5 to −3.5 as normal.

No iZotope code, assets or branding are used. The overlap is only in public workflow concepts.

## 3. Architecture

```
Source/
  Common/ParamTable       single list of all parameters (IDs, ranges, units, formatting)
  DSP/                    plain C++17, no JUCE, unit-tested offline
    Biquad, ParametricEQ, Compressor, Oversampler, Saturator, StereoImager,
    TruePeak, Limiter, LoudnessMeter, FFT, AudioAnalyzer, MasteringChain
  Assistant/              plain C++17, no JUCE, unit-tested offline
    IntentParser          text → intents (intensity, direction, numbers, platforms)
    ChatAssistant         intents + context → reply / proposal / action
    MasteringAssistant    analysis + simulation → full-chain proposals
    Proposal              changes with before/after values and reasons, selectable
  Engine/                 JUCE, threading and session
    AudioFifo             lock-free single-producer/single-consumer FIFO (AbstractFifo)
    MeterData             atomic meter snapshot (audio thread → UI)
    AnalysisWorker        background thread: reference files, listening, simulations
    HistoryManager        undo/redo/restore snapshots, gesture-aware
    ChatSession           transcript, proposal cards, actions (owned by the processor)
  UI/                     LookAndFeel, SpectrumDisplay (+ EQ nodes), Meters/Vectorscope,
                          ModuleRack, ReferencePanel, ChatPanel
  PluginProcessor / PluginEditor
Tests/                    offline unit tests, plus an end-to-end harness for the real plugin
```

### Threading

* **Audio thread** (`processBlock`): reads parameters from APVTS raw atomics and runs the chain, meters (atomics) and FIFO pushes. It never takes a lock, allocates, does file I/O or touches the UI. Work is split into fixed scratch chunks, so any host block size is safe.
* **Analysis thread** (`AnalysisWorker`): drains the input and output FIFOs into 20 s rolling buffers, keeping audible audio only. It also streams reference files from disk and runs offline chain simulations. If it falls behind, audio is dropped, and the live path is never affected.
* **Message thread**: editor (30 Hz timer pulls meters and FIFOs), `ChatSession` (15 Hz poll of worker results), `HistoryManager`, state.

The reference track is only ever analysed. It is never routed to the plugin's output.

## 4. DSP

* **Smoothing everywhere.** Gains and frequencies use linear ramps (frequency in log2 space) of 20 to 50 ms. EQ coefficients are recomputed every 16 samples while anything is moving. Every on/off switch crossfades. Tested: switching all modules, A/B and mono mid-signal gives a worst second difference of 0.0023, against more than 0.05 for a hard switch.
* **EQ.** RBJ biquads, transposed direct form II with double-precision state: 24 dB/oct low cut and high cut, low shelf, three bells, high shelf. `responseDb()` draws the curve and is used for fitting.
* **Compressor.** Stereo-linked, 5 ms RMS detector with an optional detector high-pass, soft knee, smooth decoupled peak detector, makeup and parallel mix.
* **Saturation.** Tape (tanh), Tube (biased tanh with DC blocker) and Soft Clip (cubic). Each curve has unity small-signal gain, so drive adds harmonics rather than volume. It runs at 4× through two linear-phase polyphase half-band FIR stages (Kaiser window). The round trip is exactly 59 samples, so dry and wet line up perfectly; the round trip nulls to −78 dB at 18 kHz and −93 to −121 dB lower down.
* **Imager.** Mid/side width (0 to 200%) and mono bass (24 dB/oct Linkwitz-Riley high-pass on the side channel).
* **Limiter.** Stereo-linked:
  * 4× true-peak detection (49-tap Nyquist interpolator)
  * required gain, then a sliding-window minimum (hold) over the lookahead
  * instant attack with exponential release
  * a box-filter average over the 2 ms lookahead, then a safety clip at the ceiling

  Tested: 12 dB of drive into −1 dBTP gives −1.000 dBFS sample peak and −0.999 dBTP true peak.
* **Latency.** Fixed at 59 + lookahead + 6 samples (161 at 48 kHz) whatever is enabled, and reported to the host. Host bypass is delayed by the same amount.
* **A/B.** The unprocessed input is delayed by the same latency and crossfaded over 30 ms. With level match on, it is gained to the processed short-term loudness. Measured: original as heard −14.04 LUFS against processed −14.16 LUFS.

## 5. Assistant

* **Parser.**
  * Splits messages into clauses ("… but …", commas).
  * Reads intensity words (a touch → 0.5×, much → 1.5×, extremely → 2×).
  * Reads direction: "too bright" and "less bright" mean darker, "not enough bass" means more, "darker" means darker.
  * Picks up numbers with units (Hz/kHz/k, dB, LUFS, ms, %, x:1) and platform targets (Spotify −14, Apple Music −16, YouTube −14, club −8, CD −10 …).
  * Matches whole words only, so "hi" no longer matches inside "this" as it did in V1.
* **Tweaks.** Each tweak uses what PRIZM has measured:
  * EQ cuts land on build-ups it measured.
  * Compressor thresholds are calibrated to the track's loud-part level.
  * Brightness and bass boosts are halved when the mix already leans that way.
* **Mastering proposals**, run on the worker:
  1. Listen for at least 8 s of the input before processing (it waits and reports "Listening…").
  2. Analyse it.
  3. Gain staging: bring a very quiet mix up to about −6 dBTP, never past the loudness target.
  4. EQ: fit the five bands to a correction curve, either 60% of the 1/2-octave-smoothed difference to the reference, or only the tilt outside −6.5 to −3.5 dB/oct plus gentle flattening of resonances.
  5. Compressor: added only if loudness range or PLR shows a need, and never on dense material. Its threshold comes from a bisection search on simulated gain reduction (about 1.5 to 2.5 dB on loud parts).
  6. Saturation and imager: only with a stated reason, such as side content in the low end, low correlation or a width difference from the reference.
  7. Limiter drive: a bisection search on a simulation of the whole proposed chain over the captured audio, to hit the integrated-loudness target. It's capped at 6 dB average limiting, and the proposal explains when it stops short.
  8. Final verification pass, which gives the predicted LUFS, true peak and amount of limiting.

  Measured: predicted −14.00 LUFS, live output −14.13 LUFS. The prediction covers the section PRIZM heard; a whole song with quieter or louder sections can differ, and the card says so.
* **Safety limits** for assistant changes: EQ ±6 dB, ratio ≤ 4:1, limiter drive ≤ 12 dB for "louder", width ≤ 160%, saturation drive ≤ 12 dB. If you type explicit numbers, they can use the full parameter range. Every limit that applies is stated in the card.

## 6. Building, installing and testing

* **Linux:**
  ```
  cd Builds/LinuxMakefile
  make CONFIG=Release -j16 JUCE_VST3DESTDIR=
  ```
  Builds `build/prizmv1` (standalone) and `build/prizmv1.vst3`. Leave out `JUCE_VST3DESTDIR=` to let the Makefile copy the VST3 into `~/.vst3`.
* **macOS:** open `prizmv1.jucer` in Projucer (or use `Builds/MacOSX/prizmv1.xcodeproj`). JUCE modules are found through the relative path `../juce-8.0.12-linux/JUCE/modules`, so keep the folder layout.
* **Regenerating build files** after adding sources: `../juce-8.0.12-linux/JUCE/Projucer --resave prizmv1.jucer`
* **Offline tests** (no JUCE needed): `Tests/run_tests.sh`. 17 test groups, 137 checks: EBU Tech 3341 cases 1, 3 and 4 at three sample rates, the Tech 3342 loudness-range case, true peak, oversampler, EQ, compressor static curve, limiter ceiling and latency, imager, constant latency, click-free switching, analyser, parser, chat proposals, mastering assistant accuracy and reference matching.
* **End-to-end harness:** `Tests/run_plugin_harness.sh [outdir] [Debug|Release]`. Runs the real plugin code with a simulated audio thread and drives the chat, mastering, A/B and level match, mono check, reference loading and matching, card controls, state save and restore, and bypass latency. It also writes editor screenshots.

## 7. Known limitations and next steps

* Single-band dynamics and imaging. Multiband versions (Linkwitz-Riley crossovers) are the natural next step.
* The reference can't be listened to in-app yet; it is analysis only by design. Level-matched reference playback would need its own buffering path.
* The assistant uses a deterministic local parser. An optional local LLM could be added behind `assistant::respond()` without touching the DSP or the approval flow.
* The plugin is still named `prizmv1` and the manufacturer is still the Projucer default `yourcompany`. Both were kept so existing DAW sessions keep finding the plugin.
* MP3 references use JUCE's `JUCE_USE_MP3AUDIOFORMAT` decoder (the MP3 patents have expired); macOS also decodes through CoreAudio.
