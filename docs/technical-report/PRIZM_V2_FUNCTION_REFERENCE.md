# PRIZM V2: Function and Parameter Reference

A lookup guide to the classes, functions, parameters and constants. Paths are relative to `prizmv1/`. Line numbers refer to commit `fd9f197` and point to each definition. See `PRIZM_V2_COMPLETE_REPORT.md` for explanations.

**Thread key:** **A** audio thread, **W** analysis thread, **M** message thread, **Any** pure function usable from any thread.

## 1. Processor and editor

### `Prizmv1AudioProcessor` (`Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`)

| Function | Line | Thread | Purpose |
|---|---|---|---|
| `createParameterLayout` | 15 | M | Builds host parameters from `ParamTable` |
| constructor | 57 | M | Caches parameter pointers, allocates scratch, creates history and chat |
| `prepareToPlay` | 155 | Host | Prepares DSP, reports latency |
| `isBusesLayoutSupported` | 191 | Host | Mono or stereo, input equal to output |
| `measureInput` | 213 | A | Input peak, RMS, loudness |
| `measureOutput` | 237 | A | Output peak, RMS, true peak, correlation, loudness |
| `processBlock` | 278 | A | Main callback |
| `processBlockBypassed` | 387 | A | Delay-only bypass |
| `getParamState` | 404 | Any | Reads all parameters |
| `setParamState` | 412 | M | Writes changed parameters with host gestures |
| `getReference`, `setReference`, `clearReference` | 430, 436, 445 | M | Lock-protected reference access |
| `getStateInformation` | 462 | Host | Session save |
| `setStateInformation` | 484 | Host | Session load |
| `createPluginFilter` | 514 | Host | Plugin factory |

Inline accessors in `Source/PluginProcessor.h:86-124`: `getHistory`, `getChat`, `getWorker`, `getMeters`, `resetLoudnessMeters`, `setCompareOriginal`, `setLevelMatch`, `setMonoCheck`, `getSpectrumFifo`, `getScopeFifo`, `getTargetLufs`, `setTargetLufs`, `getCeilingDb`, `setCeilingDb`.

### `Prizmv1AudioProcessorEditor` (`Source/PluginEditor.cpp`, thread M)

| Function | Line | Purpose |
|---|---|---|
| constructor | 15 | Creates and wires all panels; starts 30 Hz timer |
| `showTab` | 89 | Switches chain and reference panels |
| `updateHeaderButtons` | 98 | Syncs A/B, match, mono, undo and redo |
| `openReferenceChooser` | 118 | File dialog |
| `showHistoryMenu` | 134 | Undo, redo, restore menu |
| `timerCallback` | 167 | Refreshes meters and displays |
| `paint` | 204 | Header, titles, footer |
| `resized` | 267 | Layout |

## 2. Parameters

Defined in `Source/Common/ParamTable.cpp:26-74`. IDs are the host parameter IDs. "Skew" is the value shown at the middle of a knob.

| ID | Name | Range | Default | Skew | Read by |
|---|---|---|---|---|---|
| `in_gain` | Input Gain | -24 to +24 dB | 0 | | Chain |
| `out_gain` | Output Gain | -24 to +12 dB | 0 | | Chain, after limiter |
| `eq_on` | EQ On | Off/On | On | | EQ |
| `eq_hpf_on` | Low Cut On | Off/On | Off | | EQ |
| `eq_hpf_freq` | Low Cut Freq | 10 to 400 Hz | 30 | 60 | EQ |
| `eq_ls_freq` | Low Shelf Freq | 20 to 1000 Hz | 100 | 150 | EQ |
| `eq_ls_gain` | Low Shelf Gain | ±12 dB | 0 | | EQ |
| `eq_ls_q` | Low Shelf Q | 0.3 to 2 | 0.707 | | EQ |
| `eq_p1_freq` | Bell 1 Freq | 20 Hz to 20 kHz | 250 | 1000 | EQ |
| `eq_p1_gain` | Bell 1 Gain | ±12 dB | 0 | | EQ |
| `eq_p1_q` | Bell 1 Q | 0.2 to 10 | 1 | 1.5 | EQ |
| `eq_p2_freq` | Bell 2 Freq | 20 Hz to 20 kHz | 1000 | 1000 | EQ |
| `eq_p2_gain` | Bell 2 Gain | ±12 dB | 0 | | EQ |
| `eq_p2_q` | Bell 2 Q | 0.2 to 10 | 1 | 1.5 | EQ |
| `eq_p3_freq` | Bell 3 Freq | 20 Hz to 20 kHz | 4000 | 1000 | EQ |
| `eq_p3_gain` | Bell 3 Gain | ±12 dB | 0 | | EQ |
| `eq_p3_q` | Bell 3 Q | 0.2 to 10 | 1 | 1.5 | EQ |
| `eq_hs_freq` | High Shelf Freq | 1 to 20 kHz | 10000 | 5000 | EQ |
| `eq_hs_gain` | High Shelf Gain | ±12 dB | 0 | | EQ |
| `eq_hs_q` | High Shelf Q | 0.3 to 2 | 0.707 | | EQ |
| `eq_lpf_on` | High Cut On | Off/On | Off | | EQ |
| `eq_lpf_freq` | High Cut Freq | 1 to 22 kHz | 20000 | 8000 | EQ |
| `comp_on` | Compressor On | Off/On | Off | | Compressor |
| `comp_thresh` | Comp Threshold | -60 to 0 dB | -18 | | Compressor |
| `comp_ratio` | Comp Ratio | 1 to 10 | 2 | 3 | Compressor |
| `comp_attack` | Comp Attack | 0.1 to 100 ms | 20 | 10 | Compressor |
| `comp_release` | Comp Release | 10 to 1000 ms | 200 | 150 | Compressor |
| `comp_knee` | Comp Knee | 0 to 12 dB | 6 | | Compressor |
| `comp_makeup` | Comp Makeup | -12 to +24 dB | 0 | | Compressor |
| `comp_mix` | Comp Mix | 0 to 100% | 100 | | Compressor |
| `comp_sc_hpf` | Comp Detector HPF | 20 to 300 Hz | 20 | 80 | Compressor |
| `sat_on` | Saturation On | Off/On | Off | | Saturator |
| `sat_type` | Saturation Type | Tape, Tube, Soft Clip | Tape | | Saturator |
| `sat_drive` | Saturation Drive | 0 to 24 dB | 6 | | Saturator |
| `sat_mix` | Saturation Mix | 0 to 100% | 50 | | Saturator |
| `img_on` | Imager On | Off/On | Off | | Imager |
| `img_width` | Stereo Width | 0 to 200% | 100 | | Imager |
| `img_mono_on` | Mono Bass On | Off/On | Off | | Imager |
| `img_mono_freq` | Mono Bass Freq | 40 to 300 Hz | 120 | | Imager |
| `lim_on` | Limiter On | Off/On | Off | | Limiter |
| `lim_gain` | Limiter Gain | 0 to 24 dB | 0 | | Limiter |
| `lim_ceiling` | Limiter Ceiling | -12 to 0 dB | -1 | | Limiter |
| `lim_release` | Limiter Release | 1 to 1000 ms | 100 | 100 | Limiter |
| `lim_tp` | True Peak | Off/On | On | | Limiter |

### `Source/Common/ParamTable.cpp` functions (Any)

| Function | Line | Purpose |
|---|---|---|
| `paramInfo` | 95 | Metadata by index |
| `defaultParamState` | 100 | All defaults |
| `findParam` | 108 | Index by ID |
| `clampParam` | 117 | Limits to range; rounds switches and choices |
| `formatParamValue` | 128 | Display text such as "+2.0 dB" |
| `moduleName` | 172 | Display name |
| `moduleEnableParam` | 187 | On/off parameter of a module |

## 3. DSP

All in `prizm::dsp`, usable on any single thread per instance. `process` functions are `noexcept` and don't allocate.

### Helpers: `Source/DSP/DspCommon.h`

| Item | Line | Purpose |
|---|---|---|
| `dbToGain`, `gainToDb`, `powerToDb` | 25, 28, 33 | Level conversions |
| `LinearSmoother` | 45 | Linear ramp: `prepare`, `reset`, `setTarget`, `next`, `skip` |
| `DelayLine` | 114 | Integer delay: `prepare`, `setDelay`, `process` |
| `besselI0`, `kaiserWindow`, `sinc` | 153, 169, 178 | Filter design |

### `BiquadCoeffs` and `Biquad`: `Source/DSP/Biquad.h`

| Function | Line | Purpose |
|---|---|---|
| `peak`, `lowShelf`, `highShelf` | 39, 49, 63 | RBJ gain filters |
| `highPass`, `lowPass` | 77, 86 | RBJ cut filters |
| `response`, `magnitudeDb` | 96, 103 | Frequency response |
| `Biquad::process` | 127 | Transposed Direct Form II, one sample |

### `ParametricEQ`: `Source/DSP/ParametricEQ.cpp`

| Function | Line | Purpose |
|---|---|---|
| `prepare` | 24 | Sets ramp times |
| `setSettings` | 44 | Sets smoothed targets |
| `reset` | 71 | Snaps to settings, clears state |
| `updateCoefficients` | 113 | Rebuilds filters |
| `process` | 134 | Processes stereo audio; updates coefficients every 16 samples |
| `responseDb` | 213 | Static response, for UI and fitting |

Settings: `EQSettings` in `Source/DSP/ParametricEQ.h:21-38`.

### `Compressor`: `Source/DSP/Compressor.cpp`

| Function | Line | Purpose |
|---|---|---|
| `prepare`, `setSettings`, `reset` | 24, 37, 59 | Setup |
| `gainComputerDb` | 79 | Static soft-knee curve |
| `process` | 95 | Detector, smoothing, gain |

Settings: `CompressorSettings` in `Source/DSP/Compressor.h:24-35`. Constant: `rmsWindowSeconds` 5 ms.

### `Saturator`: `Source/DSP/Saturator.cpp`

| Function | Line | Purpose |
|---|---|---|
| `shape` | 26 | Static curves: Tape, Tube, Soft Clip |
| `prepare`, `setSettings`, `reset` | 50, 71, 80 | Setup |
| `process` | 97 | 4× processing, DC blocking, mix, dry delay |

Constants: `tubeBias` 0.2, `dcBlockHz` 5, drive gain 0.25 × dB gain.

### `HalfbandStage` and `Oversampler4x`: `Source/DSP/Oversampler.cpp`

| Function | Line | Purpose |
|---|---|---|
| `HalfbandStage::design` | 25 | Kaiser half-band design |
| `HalfbandStage::upsample`, `downsample` | 63, 84 | Polyphase conversion by 2 |
| `Oversampler4x::prepare` | 108 | Builds stages, computes latency |
| `Oversampler4x::upsample`, `downsample` | 140, 148 | Conversion by 4 |

Constants: 107 taps, beta 8.0; 23 taps, beta 8.9; latency 59 samples.

### `StereoImager`: `Source/DSP/StereoImager.cpp`

| Function | Line | Purpose |
|---|---|---|
| `prepare`, `setSettings`, `reset` | 14, 25, 34 | Setup |
| `updateFilter` | 46 | Mono-bass filter |
| `process` | 54 | Mid/side width, mono bass |

### `TruePeakDetector`: `Source/DSP/TruePeak.h`

| Function | Line | Purpose |
|---|---|---|
| `process` | 36 | Max of 4 interpolated points |
| `processSamplePeak` | 57 | Same delay, sample peak only |
| `design` | 69 | 49-tap Kaiser (beta 6) prototype, 13 taps per phase |

Constant: `delaySamples` 6.

### `Limiter`: `Source/DSP/Limiter.cpp`

| Function | Line | Purpose |
|---|---|---|
| `prepare`, `setSettings`, `reset` | 19, 45, 54 | Setup |
| `pushHold` | 78 | Sliding minimum with a monotonic deque |
| `process` | 104 | Limiting |

Constant: `lookaheadSeconds` 2 ms.

### `LoudnessMeter`: `Source/DSP/LoudnessMeter.cpp`

| Function | Line | Purpose |
|---|---|---|
| `prepare` | 21 | K-weighting for the sample rate |
| `reset`, `resetIntegrated` | 57, 69 | Clears everything, or integrated data only |
| `process` | 79 | Filtering and sub-blocks |
| `finishSubBlock` | 92 | Momentary and short-term |
| `updateIntegrated` | 145 | Two-stage gating |
| `updateLoudnessRange` | 178 | LRA |

Getters are in `Source/DSP/LoudnessMeter.h:39-44`. Constants: 100 ms sub-blocks, 800 histogram bins of 0.1 LU from -70 to +10 LUFS.

### `MasteringChain`: `Source/DSP/MasteringChain.cpp`

| Function | Line | Purpose |
|---|---|---|
| `makeEQSettings`, `makeChainSettings` | 14, 40 | `ParamState` to typed settings |
| `prepare`, `setSettings`, `reset` | 76, 105, 116 | Setup |
| `setMonitor` | 130 | A/B, match gain, mono |
| `process` | 137 | Full chain, splits into 512-sample chunks |
| `processPreLimiter` | 145 | Chain without limiter, output trim or monitor |
| `processChunk` | 151 | The module sequence |

### `FFT`: `Source/DSP/FFT.h`

| Function | Line | Purpose |
|---|---|---|
| `setOrder` | 26 | Builds tables |
| `perform` | 47 | In-place forward transform |
| `hann` | 73 | Window |

### `AudioAnalyzer` and `AnalysisResult`: `Source/DSP/AudioAnalyzer.cpp`

| Function | Line | Purpose |
|---|---|---|
| `AnalysisResult::pointFrequency` | 31 | 64-point grid, 20 Hz to 20 kHz |
| `AnalysisResult::normalisedMid` | 36 | Spectrum with mean 0 dB over 100 Hz to 8 kHz |
| `AnalysisResult::serialise`, `deserialise` | 57, 82 | Text for session state |
| `AudioAnalyzer::prepare` | 141 | FFT 4096, 8192 or 16384, by sample rate |
| `AudioAnalyzer::process` | 186 | Streaming measurement |
| `AudioAnalyzer::analyseFrame` | 235 | One FFT into level buckets |
| `AudioAnalyzer::finish` | 322 | Gating and final metrics |

`AnalysisResult` fields: `Source/DSP/AudioAnalyzer.h:30-70`.

## 4. Assistant

All in `prizm::assistant`. Pure functions except for the simulation's use of memory. Parser and `respond` run on M, mastering on W.

### `Source/Assistant/Proposal.h`

| Item | Line | Purpose |
|---|---|---|
| `ParamChange` | 24 | `param`, `from`, `to`, `reason`, `selected` |
| `Prediction` | 32 | LUFS, true peak, limiter and compressor reduction, PLR, seconds |
| `Proposal::setChange` | 56 | Adds one clamped change; drops no-ops |
| `Proposal::setFromDiff` | 81 | Builds from two states |
| `Proposal::applyTo` | 89 | Result of the ticked changes |
| `Proposal::numSelected` | 98 | Count ticked |

### `Source/Assistant/IntentParser.cpp`

| Function or data | Line | Purpose |
|---|---|---|
| `findWord` | 30 | Whole-word search |
| `extractNumbers` | 84 | Numbers with units |
| `intensityOf` | 180 | 0.5, 1, 1.5 or 2 |
| `isNegated` | 193 | "less", "too", "reduce"... |
| `isDesireContext` | 203 | "make it", "more"... |
| `descriptors[]` | 221 | Vocabulary and direction rules |
| `splitClauses` | 356 | Splits on "but", "and", commas... |
| `moduleIn` | 395 | Module named in a clause |
| `platforms[]` | 407 | Loudness and ceiling targets |
| `normaliseText` | 430 | Lower-case, dashes, whitespace |
| `parseMessage` | 479 | Main parser |

**Platform targets** (`Source/Assistant/IntentParser.cpp:407-426`, project values):

| Keyword | LUFS | Ceiling dBTP |
|---|---|---|
| spotify, youtube, tidal, streaming | -14 | -1 |
| apple music, itunes, podcast | -16 | -1 |
| amazon | -14 | -2 |
| deezer | -15 | -1 |
| soundcloud | -10 | -1 |
| broadcast, r128 | -23 | -1 |
| club, dj, edm | -8 | -0.5 |
| competitive | -9 | -0.5 |
| cd | -10 | -0.3 |

### `Source/Assistant/ChatAssistant.cpp`

| Function | Line | Purpose |
|---|---|---|
| `Builder` | 37 | Collects changes, reasons, notes, title |
| `Builder::loudLevel` | 77 | Level for compressor calibration |
| `Builder::setupCompressor` | 86 | Common compressor recipe |
| `Builder::problemFreq` | 119 | Finds a build-up or falls back |
| `handleTweak` | 138 | All tweak recipes |
| `isTweak` | 559 | Filter for tweak intents |
| `applyStyle` | 591 | "Warm", "punchy" for mastering |
| `statusText` | 608 | Current level report |
| `welcomeText`, `helpText` | 624, 634 | Fixed text |
| `respond` | 647 | Top-level decision |

**Recipe amounts at normal intensity (k = 1), with no analysis:**

| Intent | Changes |
|---|---|
| Warmth | Low shelf to 150 Hz +2 dB; high shelf -1 dB |
| Brightness | High shelf to 6 kHz +2 dB |
| Air | High shelf to 12 kHz +2 dB |
| Bass, Sub | Low shelf at 100 Hz or 60 Hz, +2 dB |
| Mud | Bell 1 at 300 Hz, -2 dB, Q 1.2 |
| Boxy | Bell 2 at 600 Hz, -2 dB, Q 1.4 |
| Harsh | Bell 3 at 3.5 kHz, -2 dB, Q 1.5 |
| Sibilance | Bell 3 at 7 kHz, -2 dB, Q 2.5 |
| Presence | Bell 3 at 3 kHz, +1.5 dB, Q 1 |
| Punch | Compressor 2.5:1, 20 ms, 120 ms, knee 6 |
| Glue | Compressor 1.8:1, 30 ms, 250 ms, knee 9; shelves +0.5 and -0.5 dB |
| Tight | Compressor 3:1, 5 ms, 100 ms, knee 3; bell 1 at 350 Hz -1.5 dB; bell 3 at 4 kHz +1 dB |
| Louder | Limiter on, +3 dB; if already on, +2 dB; capped at 12 dB |
| Saturate | Tape, drive 6 dB, mix 35% |
| Vintage | Tape, drive 6 dB, mix 40%; low shelf +1 dB; high shelf at 9 kHz -1.5 dB |
| Telephone | Cut below 400 Hz and above 3.2 kHz; bell 2 at 1.5 kHz +3 dB; Tube 12 dB, mix 60% |
| Wider | Width ±15% |
| MonoBass | Below 120 Hz |

### `Source/Assistant/MasteringAssistant.cpp`

| Function | Line | Purpose |
|---|---|---|
| `smoothCurve` | 35 | Moving-average smoothing |
| `residualFromLine` | 74 | Spectrum deviation from its trend |
| `genericCorrection` | 97 | Target curve without a reference |
| `referenceCorrection` | 138 | Target curve from a reference |
| `addStyleNudges` | 161 | Applies warmth and brightness |
| `computeBlockStats` | 181 | Finds the "loud half" |
| `measureCompressorGr` | 201 | Simulated compressor gain reduction |
| `runLimiter` | 237 | Simulated limiter |
| `fitEqToCurve` | 287 | Fits 5 bands, 40 iterations |
| `simulate` | 369 | Full chain prediction |
| `buildMasteringProposal` | 417 | The whole process |
| `findBuildUp` | 789 | Finds resonances |
| `referenceRecommendations` | 811 | Advice lines |
| `describeAnalysis` | 851 | Analysis report text |

**Decision thresholds:**

| Constant | Value | Effect |
|---|---|---|
| `typicalTiltDbPerOctave` | -5 dB/oct, normal -6.5 to -3.5 | Tonal target |
| Gain staging trigger | Peak below -9 dBTP | Raises to about -6 dBTP |
| Dense | PLR below 9 and LRA below 6 | No compression |
| Wants compression | LRA above 7, or PLR above target + 4 | Adds compressor |
| Target PLR | 8.5 dB for loud targets, otherwise 11 dB | Or the reference's PLR |
| `maxDrive` | 24 dB | Search limit |
| `maxAvgLimiting` | 6 dB | Stops the loudness search |
| Bisection counts | 8 (compressor), 11 (limiter) | Search precision |

## 5. Engine

### `AudioFifo`: `Source/Engine/AudioFifo.h`

`push` (line 29, thread A), `pop` (line 48, reader thread), `getNumReady`, `getFreeSpace`.

### `MeterData`: `Source/Engine/MeterData.h`

`MeterSnapshot` (line 20), `MeterData::store` (line 39, thread A), `MeterData::load` (line 65, thread M).

### `AnalysisWorker`: `Source/Engine/AnalysisWorker.cpp`

| Function | Line | Thread | Purpose |
|---|---|---|---|
| `pushAudio` | 40 | A | Writes to both FIFOs, dropping if full |
| `requestReference`, `requestMaster`, `requestAnalysis` | 56, 68, 83 | M | Queues jobs, returns ID |
| `cancelAll` | 95 | M | Clears the queue. **Has no callers** |
| `getStatus`, `popResult` | 105, 111 | M | Progress and results |
| `ensureRollingBuffers` | 138 | W | Allocates capture on rate change |
| `drainFifos` | 152 | W | Moves audible audio to the capture |
| `copyClip` | 192 | W | Copies the most recent audio |
| `waitForAudio` | 208 | W | Waits for enough audio, with a timeout |
| `run` | 241 | W | Main loop |
| `runReference`, `runMaster`, `runAnalysis` | 280, 341, 389 | W | Jobs |

### `ChatSession`: `Source/Engine/ChatSession.cpp` (thread M)

| Function | Line | Purpose |
|---|---|---|
| `addMessage` | 32 | Adds a message, marks proposals as pending |
| `buildContext` | 65 | Assembles `AssistantContext` |
| `sendUserMessage` | 104 | Entry for chat input |
| `handleReply` | 114 | Performs actions |
| `applyProposal` | 230 | Applies ticked changes with undo |
| `dismissProposal` | 268 | Marks dismissed |
| `undoProposal` | 280 | Reverts a card |
| `setChangeSelected` | 298 | Ticks or unticks |
| `loadReference` | 312 | Starts a reference job |
| `requestTrackAnalysis` | 318 | Starts an analysis job |
| `startJob`, `finishJob` | 327, 335 | Status bubble |
| `timerCallback` | 346 | Polls at 15 Hz for results and status |
| `getSuggestions` | 430 | Suggestion chips |

### `HistoryManager`: `Source/Engine/HistoryManager.cpp` (thread M)

| Function | Line | Purpose |
|---|---|---|
| `pushUndoPoint` | 39 | Snapshot, clears redo |
| `applyWithUndo` | 51 | Snapshot and apply |
| `undo`, `redo` | 64, 79 | Moves between stacks |
| `restoreBefore` | 94 | Restores an earlier state, itself undoable |
| `captureSessionStart`, `restoreSessionStart` | 108, 114 | "Original settings" |
| `parameterGestureChanged` | 123 | Records manual changes, merging within 1.5 s |

Limit: 100 entries.

## 6. UI (thread M)

| Class | File and line | Key functions |
|---|---|---|
| `SpectrumDisplay` | `Source/UI/SpectrumDisplay.cpp` | `pushSamples` 48, `setReference` 59, `computeSpectrum` 82, `update` 126, `nodeAt` 220, `paint` 240, `mouseDown` 441, `mouseDrag` 453, `mouseDoubleClick` 477, `mouseWheelMove` 491 |
| `StereoLevelMeter` | `Source/UI/Meters.cpp` | `setLevels` 32, `paint` 49 |
| `GainReductionMeter` | `Source/UI/Meters.cpp` | `setGainReduction` 90 |
| `Vectorscope` | `Source/UI/Meters.cpp` | `pushSamples` 127, `update` 143 |
| `CorrelationMeter` | `Source/UI/Meters.cpp` | `paint` 203 |
| `LoudnessReadout` | `Source/UI/Meters.cpp` | `setValues` 236, `paint` 250 |
| `MetersPanel` | `Source/UI/Meters.cpp` | `update` 319 |
| `ModuleRack` | `Source/UI/ModuleRack.cpp` | constructor 463, `showPage` 495, `update` 504 |
| `CurveView` | `Source/UI/ModuleRack.cpp` | class 243 |
| `ReferencePanel` | `Source/UI/ReferencePanel.cpp` | `refresh` 49, `update` 69, `paint` 119 |
| `ChatPanel` | `Source/UI/ChatPanel.cpp` | constructor 570, `sendText` 607, `refreshMessages` 629, `refreshChips` 645, `keyPressed` 685 |
| `MessageItem` | `Source/UI/ChatPanel.cpp` | class 70, `setMessage` 99, `rebuildControls` 249, `layoutCard` 279, `paintCard` 369 |
| `MessageList` | `Source/UI/ChatPanel.cpp` | class 488, `sync` 494 |
| `Knob`, `ParamToggle`, `ParamCombo` | `Source/UI/Controls.h` | 20, 60, 76 |
| `PrizmLookAndFeel` | `Source/UI/PrizmLookAndFeel.cpp` | constructor 58, `drawRotarySlider` 99, `drawButtonBackground` 140, `drawToggleButton` 212 |

## 7. Tests

| File | Item | Purpose |
|---|---|---|
| `Tests/TestHarness.h` | `TEST_CASE`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR` | Test macros |
| `Tests/DSPTests.cpp` | 12 `TEST_CASE`s | DSP |
| `Tests/AssistantTests.cpp` | 5 `TEST_CASE`s | Parser, chat and mastering |
| `Tests/PluginHarness.cpp` | `MixGenerator` line 46, `writeReferenceWav` line 80, `Harness` line 151 | Simulated end-to-end flow |

## 8. Quick call paths

**A chat tweak:**

`ChatPanel::sendText` → `ChatSession::sendUserMessage` → `assistant::respond` → `parseMessage`, then `handleTweak` → `Proposal::setFromDiff` → `ChatSession::handleReply` → `addMessage` → card. On Apply: `MessageItem` → `ChatSession::applyProposal` → `Proposal::applyTo` → `HistoryManager::applyWithUndo` → `Prizmv1AudioProcessor::setParamState`. On the next block: `processBlock` → `makeChainSettings` → `MasteringChain::setSettings`.

**A master:**

`respond` returns `RunMaster` → `handleReply` → `AnalysisWorker::requestMaster` → `run` → `runMaster` → `waitForAudio`, `copyClip`, `AudioAnalyzer` → `buildMasteringProposal` (`fitEqToCurve`, `measureCompressorGr`, `runLimiter`, `simulate`) → `postResult` → `ChatSession::timerCallback` → `finishJob` → card.

**Reference:**

`openReferenceChooser` → `ChatSession::loadReference` → `requestReference` → `runReference` → `AudioAnalyzer` → `timerCallback` → `setReference` → editor timer → `SpectrumDisplay::setReference` and `ReferencePanel::refresh`.

**One audio block:**

`processBlock` → `measureInput` → `MasteringChain::process` → `processChunk` (`ParametricEQ::process`, `Compressor::process`, `Saturator::process`, `StereoImager::process`, `Limiter::process`) → `measureOutput` → `AudioFifo::push`, `AnalysisWorker::pushAudio` → `MeterData::store`.
