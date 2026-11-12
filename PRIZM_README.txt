PRIZM
AI-Powered Audio Mixing & Mastering Assistant Plugin

Making professional audio accessible through natural language interaction and intelligent DSP processing.


TABLE OF CONTENTS

1. What Is This Project
2. Purpose
3. Core Problem
4. Solution Summary
5. Project Status
6. Current Stage
7. What Has Been Built
8. Audio Analysis
9. AI System
10. DSP Suggestions
11. NLP System
12. Audio Processing
13. Tech Stack
14. DSP Concepts & Audio Features
15. AI/ML Concepts
16. UX Concepts
17. Final Summary


1. WHAT IS THIS PROJECT

PRIZM is an AI-assisted audio plugin built with the JUCE framework (C++) that serves as a real-time mixing and mastering assistant designed to run inside a Digital Audio Workstation (DAW) or as a standalone application. It analyzes incoming audio signals in real time, accepts natural language commands (e.g., "make it warmer", "add punch"), and translates those commands into precise Digital Signal Processing (DSP) operations — equalization, compression, filtering — all while enforcing mastering-safe constraints to prevent destructive over-processing.

The plugin is designed to be an Effect Plugin (Audio Processor) supporting VST3, AU (Audio Unit), and Standalone formats via JUCE's cross-platform build system.


2. PURPOSE

PRIZM aims to democratize professional audio post-production by reducing the barrier to entry for mixing and mastering. Specifically, it seeks to:

- Empower independent artists, producers, and students who lack access to expensive professional engineers or deep technical expertise.
- Accelerate workflows for experienced engineers through conversational AI-driven parameter control, replacing manual knob-turning with natural language commands.
- Educate users by providing transparent, interpretable parameter feedback — users see exactly what the AI changed and why.
- Ensure safety through built-in mastering constraints that prevent the AI from making destructive audio decisions.


3. CORE PROBLEM

Knowledge Barrier: Mixing and mastering require deep understanding of EQ curves, compression ratios, attack/release times, frequency interactions, and loudness standards.

Cost of Expertise: Professional mastering engineers charge significant fees, making high-quality sound inaccessible to many independent creators.

Tool Complexity: Existing DAW plugins expose raw technical parameters (Q-values, threshold dB, spectral frequencies) that overwhelm beginners.

Lack of Guidance: Beginners often don't know what "right" sounds like — they need guidance on what processing to apply, not just the ability to apply it.

Risk of Damage: Unconstrained automated processing can ruin audio (over-compression, excessive EQ boosts) with no safeguards.


4. SOLUTION SUMMARY

PRIZM addresses these problems by combining four core capabilities into a single plugin:

USER INPUT
"make it warmer" / "add punch" / "reset"
    |
    v
NLP ENGINE (Rule-Based)
Interprets natural language into DSP parameters.
Example: "warm" maps to lowShelfGain=3.0, highShelfGain=-1.5
    |
    v
SAFETY RULES ENGINE
Clamps parameters to mastering-safe ranges.
Max EQ boost: +6dB | Max ratio: 4:1 | Crest check
    |
    v
DSP PROCESSING CHAIN
EQ Filters -> Compressor -> Output Gain
(Low/High Shelf, Bandpass, HighCut)
    |
    v
REAL-TIME ANALYSIS + VISUALIZATION
Spectrum Analyzer | LUFS Meters | Mix Context Display

Key differentiator: PRIZM is not a black box. Every AI decision is transparent — users see the exact parameters applied and receive safety feedback when processing is constrained.


5. PROJECT STATUS

Concept & Architecture: Complete — Fully defined system architecture and data flow
JUCE Plugin Framework: Built — Compiles as VST3, AU, and Standalone
DSP Processing Chain: Functional — EQ (low/high shelf, bandpass, high-cut), compression, output gain
Real-Time Audio Analysis: Functional — LUFS, crest factor, spectral tilt, mono compatibility
NLP (Rule-Based): Functional — 8+ commands with intensity modifiers
Safety Rules System: Functional — EQ/compression clamping with user feedback
Plugin GUI: Functional — Spectrum analyzer, LUFS meters, mix display, chat interface
Parameter Smoothing: Functional — 50ms ramp time, zipper-free transitions
OpenRouter API (LLM): Stubbed — HTTP integration planned but not yet implemented
Real Training Data: Not Started — Currently uses rule-based mappings, no ML training data
Preset Save/Load: Not Started — State serialization is minimal
Multi-Band Processing: Not Started — Only single-band EQ currently implemented


6. CURRENT STAGE

Phase 1 — Early Functional Prototype (Proof of Concept)

The project is in its first functional phase. The JUCE plugin compiles and runs as a standalone application or VST3 plugin. Users can interact with the chat interface using natural language commands to apply audio processing in real time. The code explicitly labels itself as "Phase 1" throughout the UI, with AI features planned for future phases.

The codebase is structured for extensibility — the architecture cleanly separates DSP processing, audio analysis, NLP, safety rules, and UI into distinct classes. However, significant work remains to transition from a prototype to a production-ready product.


7. WHAT HAS BEEN BUILT

7.1 Core Source Files

The entire plugin is implemented in 4 C++ source files totaling approximately 1,549 lines of custom code:

PluginProcessor.h — 216 lines — DSP engine headers, data structures, class declarations
PluginProcessor.cpp — 805 lines — DSP implementation, NLP engine, audio analysis, safety rules
PluginEditor.h — 102 lines — UI component declarations
PluginEditor.cpp — 526 lines — UI rendering, chat interface, spectrum analyzer, level meters

7.2 Implemented Classes

PRIZMAudioProcessor (main plugin class):
    - EnhancedProcessor — Multi-stage DSP chain
    - MixAnalyzer — Real-time audio analysis
    - SafetyRules — AI safety constraints

PRIZMAudioProcessorEditor (main plugin window):
    - SpectrumAnalyzer — FFT-based frequency display
    - LevelMeter x2 — L/R LUFS-style meters
    - MixContextDisplay — Real-time mix analysis panel
    - ChatInterface — NLP command input/output

7.3 DSP Features

- Low-Shelf EQ Filter: 50–500 Hz range, adds warmth/body or removes muddiness
- High-Shelf EQ Filter: 1–10 kHz range, enhances brightness or softens harshness
- Bandpass Filter: Creates vintage/telephonic/lo-fi effects
- High-Cut Filter: Rolls off high frequencies for warm analog character
- Dynamic Compressor: JUCE dsp::Compressor with threshold, ratio, attack, release
- Pre/Post-Compression EQ: dsp::ProcessorDuplicator based IIR filters for tonal shaping around compression
- Output Gain Stage: Final level control
- Parameter Smoothing: juce::SmoothedValue with 50ms linear ramp — prevents zipper noise during transitions

7.4 Preset System

Six built-in tonal presets, each with intensity scaling:

Warm: Low-shelf boost at 150 Hz + high-mid cut at 3.5 kHz
Bright: High-shelf boost at 3 kHz
Telephonic: Bandpass filter at 1 kHz (vintage radio effect)
Punchy: Fast-attack compression (20ms) + presence boost at 3 kHz
Smooth: Gentle compression (40ms attack) + low-shelf warmth + high-shelf rolloff
Tight: Aggressive compression + mid-cut at 600 Hz + presence at 5 kHz

7.5 GUI Components

- 800 x 650 pixel plugin window with dark gradient UI
- Spectrum Analyzer: 2048-sample FFT with Hann windowing, logarithmic frequency mapping (20 Hz–20 kHz), gradient-filled spectrum path
- Level Meters: Color-coded (green to yellow to red) with peak-hold indicators
- Mix Context Display: Shows LUFS, dynamics, tone balance, bass mono score
- Chat Interface: Text input + scrollable chat history with sender-colored messages

7.6 Build System

- Projucer project file (NewProject.jucer) — configured for AU and VST3 on macOS (Xcode)
- CMake build (CMakeLists.txt) — configured for AU, VST3, and Standalone
- VS Code task for Clang build (.vscode/tasks.json)
- JUCE Framework v8.0.8 included in repository (13 modules linked)


8. AUDIO ANALYSIS

Real-Time Mix Analysis (MixAnalyzer class)

The MixAnalyzer performs continuous, real-time audio analysis on every audio block, updating a MixContext struct every 4,096 samples (approximately 93ms at 44.1 kHz). All processing is stack-allocated with no dynamic memory in the audio thread.

Integrated LUFS: RMS calculation + ITU-R BS.1770 offset (-0.691 dB) — Perceived loudness (dB)
Crest Factor: Peak dB minus RMS dB — Dynamic range indicator
Spectral Tilt: Low-band energy vs. high-band energy via 2 kHz Butterworth crossover — Tonal balance (dark/balanced/bright)
Mono Compatibility: 120 Hz low-pass filter + L/R correlation score — Bass phase compatibility (0.0–1.0)
Over-Compression: Crest factor < 4.0 dB flag — Boolean guard for safety rules

Analysis Pipeline:

Audio Buffer
    - Per-sample: RMS accumulation, peak tracking
    - Per-sample: Low-band IIR filter (< 2 kHz) -> energy accumulation
    - Per-sample: High-band IIR filter (> 2 kHz) -> energy accumulation
    - Per-sample: Mono-check filter (< 120 Hz) -> L/R correlation
    - Every 4096 samples: Compute LUFS, crest, tilt, mono score -> Update MixContext

FFT Spectrum Analysis (SpectrumAnalyzer class)

- FFT Size: 2,048 samples (order 11)
- Windowing: Hann window (reduces spectral leakage)
- Display: 256-point scope with logarithmic frequency mapping
- Update Rate: 30 Hz timer
- Thread Safety: Lock-free FIFO for sample injection from audio thread


9. AI SYSTEM

Current Implementation: Rule-Based NLP

The AI system is implemented in the processStaticChat() method of PRIZMAudioProcessor. It uses keyword matching on lowercased user input to map natural language commands to DSP parameter sets encoded as [PARAMS:key=val,...] tags.

Supported commands: warm, bright, telephonic, punch, smooth, tight, reset, help, analyze, plus conversational responses (hello, thanks, who are you).

Example flow:
    User: "make it warmer"
    Keyword match: "warm"
    NLP response: "Applying warm preset. [PARAMS:lowShelfGain=3.0,lowShelfFreq=150,highShelfGain=-1.5]"
    DSP: Apply lowShelfGain=3.0, lowShelfFreq=150, highShelfGain=-1.5
    Safety clamping applied
    Output: Processed audio with warm character

Planned: OpenRouter API Integration

The TECHNICAL_ARCHITECTURE.md documents a planned (but currently stubbed) integration with the OpenRouter API using Meta LLaMA 3.1 8B Instruct (free tier). The architecture includes:

- API key sourcing: environment variable -> config file -> embedded fallback
- A comprehensive system prompt with DSP parameter mappings and few-shot examples
- Intensity modifier detection ("very", "subtle", "more")
- Context-aware parameter selection based on MixContext
- Async processing via background threads with MessageManager::callAsync() callback marshaling

Note: In the current codebase (PluginProcessor.cpp), the AI system is fully rule-based. The OpenRouter API integration exists only in documentation and the technical architecture spec, not in the compiled source code of this repository.


10. DSP SUGGESTIONS

PRIZM's DSP suggestion layer translates user intent into specific, bounded parameter adjustments:

"warm" / "warmer" / "bass": lowShelfGain=3.0, lowShelfFreq=150, highShelfGain=-1.5
"bright" / "brighter" / "treble": highShelfGain=4.0, highShelfFreq=3000
"telephonic" / "vintage" / "radio": useBandpass=1, midFreq=1500, midQ=2.0, useHighCut=1
"punch" / "punchy": compThreshold=-25, compRatio=2.5, compAttack=15, compRelease=150
"smooth": compThreshold=-20, compRatio=1.8, compAttack=30, compRelease=300
"tight": compThreshold=-24, compRatio=3.0, compAttack=5, compRelease=100
"reset" / "clear" / "off": Reset all DSP parameters to defaults

Safety Clamping (SafetyRules)

All AI-suggested parameters pass through safety validation before reaching the DSP chain:

Max EQ Boost: +6.0 dB — Mastering industry standard
Max EQ Cut: -8.0 dB — Prevents over-hollowing
Min Crest for Compression: 6.0 dB — Don't compress already-crushed material
Max Compression Ratio: 4.0:1 — Safe mastering limit
Min Threshold (Loud Mix): -20.0 dB — Prevents over-crushing loud material
Spectral Tilt Awareness: Context-dependent — Limits brightness boost if mix is already bright, limits bass if already dark

When safety rules override an AI decision, the user receives explicit feedback: "Bass boost limited.", "Compression reduced.", "Brightness adjusted."


11. NLP SYSTEM

Architecture

The NLP system is a rule-based keyword matching engine implemented in PRIZMAudioProcessor::processStaticChat(). Each rule checks whether the lowercased user message contains a keyword and returns a natural language response plus a parameter tag.

Response Format

Every NLP response follows a consistent two-part format:
1. Natural language explanation (displayed to user)
2. Parameter tag [PARAMS:key=val,...] (parsed and applied to DSP chain)

Parameter Parsing

The ChatInterface::applyDSPFromResponse() method tokenizes the [PARAMS:] tag by commas and equals signs, mapping each key-value pair to the corresponding field in the EnhancedProcessor::DSPParameters struct. A special reset token triggers full DSP state clearance.

NLP Capabilities

Tonal commands: warm, warmer, bass, bright, brighter, treble
Effects commands: telephonic, vintage, radio
Dynamics commands: punch, punchy, smooth, tight
Control commands: reset, clear, off, help
Conversational commands: hi, hello, hey, thanks, who are you, about, project
Info commands: analyze (returns Phase 2 placeholder)

Limitations

- No semantic understanding — purely keyword-based
- No intensity modifiers in the current codebase (planned for API integration)
- No context-awareness from previous commands or audio state
- Single-turn interactions only — no conversation memory


12. AUDIO PROCESSING

Signal Flow

Audio Input (Stereo)
    |
    |- MixAnalyzer.process()          <- Analysis (non-destructive read)
    |- RMS/Peak Level Metering        <- Level measurement
    |- Spectrum Analyzer FIFO Push    <- FFT sample injection
    |
    +- EnhancedProcessor.process()    <- DSP processing (destructive write)
          |
          |- Smoothed Parameter Update
          |- Low-Shelf Filter (per channel, IIR)
          |- High-Shelf Filter (per channel, IIR)
          |- Bandpass Filter (per channel, optional)
          |- High-Cut Filter (per channel, optional)
          |- Compressor (JUCE dsp::Compressor)
          +- Output Gain (linear)
          |
Audio Output (Stereo)

Thread Model

Audio Thread: processBlock(), MixAnalyzer.process(), EnhancedProcessor.process()
    Constraints: No allocations, no locks, no blocking

UI Thread: paint(), ChatInterface, SpectrumAnalyzer
    Constraints: Can allocate, use String, blocking I/O

Cross-Thread Communication

- Processor to UI: Lock-free reads (getMixContext(), currentRMSLevel, currentPeakLevel)
- UI to Processor: Direct method calls (applyDSPParameters(), processStaticChat())
- Spectrum Data: Lock-free FIFO buffer for sample injection from audio thread to UI thread
- Parameter Transitions: juce::SmoothedValue objects for zipper-free audio parameter changes

Filter Implementation

All EQ filters use JUCE's IIRCoefficients factory methods:

- makeLowShelf() — low-shelf filter with Q=0.7
- makeHighShelf() — high-shelf filter with Q=0.7
- makeBandPass() — bandpass for telephonic effect
- makeLowPass() — high-cut filter
- makePeakFilter() — parametric EQ for preset fine-tuning
- makeAllPass() — unity-gain placeholder when filter is inactive


13. TECH STACK

Core Framework

JUCE 8.0.8 — Cross-platform C++ audio application framework
C++ 17 — Primary programming language
CMake >= 3.22 — Build system (alternative to Projucer)
Projucer — JUCE project generator and IDE exporter

JUCE Modules Used

juce_audio_basics — Audio buffer types, MIDI, sample conversion
juce_audio_devices — Audio I/O device management
juce_audio_formats — WAV, AIFF, and other format reading/writing
juce_audio_plugin_client — VST3, AU, Standalone plugin wrapper
juce_audio_processors — AudioProcessor base class
juce_audio_utils — Audio utility components
juce_core — Foundation classes (String, File, Thread)
juce_data_structures — ValueTree, UndoManager
juce_dsp — FFT, IIR filters, Compressor, SmoothedValue, Windowing
juce_events — Timer, MessageManager, async callbacks
juce_graphics — 2D rendering, fonts, colors, gradients
juce_gui_basics — Component, TextEditor, TextButton
juce_gui_extra — Extended UI components

DSP Libraries

FFT: juce::dsp::FFT (order 11, 2048 samples)
Windowing: juce::dsp::WindowingFunction<float> (Hann)
IIR Filters: juce::IIRFilter + juce::IIRCoefficients
Compressor: juce::dsp::Compressor<float>
Smoothing: juce::SmoothedValue<float, ValueSmoothingTypes::Linear>
Process Chain: juce::dsp::ProcessorDuplicator + juce::dsp::IIR::Filter

Build Targets

VST3: Configured — Windows, macOS, Linux
AU (Audio Unit): Configured — macOS
Standalone: Configured — All platforms
AAX: Not configured
LV2: Not configured

Development Tools

Visual Studio 2019/2022 — Windows C++ IDE
Xcode 14+ — macOS C++ IDE
Clang — Alternative compiler (configured in VS Code tasks)
Git — Version control

Planned (Not Yet Implemented)

OpenRouter API — Remote LLM integration (Meta LLaMA 3.1)
Python (librosa, sklearn) — Earlier prototype (separate from current JUCE codebase)


14. DSP CONCEPTS & AUDIO FEATURES

DSP Techniques Implemented

Equalization (EQ): IIR low-shelf and high-shelf filters — EnhancedProcessor::applyParameters()
Bandpass Filtering: IIR bandpass for telephonic/vintage effects — EnhancedProcessor::process()
Dynamic Compression: JUCE dsp::Compressor with threshold, ratio, attack, release — EnhancedProcessor::updateCompressor()
Saturation: std::tanh() soft-clipping for Punchy/Smooth/Tight presets — EnhancedProcessor::process()
Windowing: Hann window applied before FFT — SpectrumAnalyzer::drawNextFrameOfSpectrum()
FFT: 2048-point FFT for spectrum visualization — SpectrumAnalyzer class
Parameter Smoothing: 50ms linear ramp via SmoothedValue — EnhancedProcessor::updateSmoothedParameters()
Denormalization Prevention: ScopedNoDenormals in processBlock() — PRIZMAudioProcessor::processBlock()

Audio Features Extracted

RMS Level: buffer.getRMSLevel() + per-sample accumulation — LUFS estimation, level metering
Peak Level: buffer.getMagnitude() + max tracking — Peak-hold metering, crest factor
Spectral Centroid (approx.): Low/high band energy ratio via IIR split at 2 kHz — Tonal balance assessment
Spectral Tilt: lowEnergyDb - highEnergyDb — Dark/balanced/bright classification
Crest Factor: peakDb - rmsDb — Dynamic range & over-compression detection
LUFS (estimated): rmsDb - 0.691 (simplified K-weighting offset) — Loudness metering for streaming standards
Mono Correlation: L/R correlation below 120 Hz — Bass phase compatibility scoring
Zero-Crossing Rate: Documented in reference spec, not in current JUCE code — Planned for future analysis
Spectral Roll-Off: Documented in reference spec, not in current JUCE code — Planned for future analysis


15. AI/ML CONCEPTS

Currently Implemented

Rule-Based NLP: Keyword matching on lowercased input -> parameter mapping
Feature-to-Parameter Mapping: Direct mapping from audio terms to DSP parameter structs
Safety Constraints: Hard-coded bounds that clamp AI-suggested values
Context-Aware Safety: Mix analysis (crest factor, LUFS, spectral tilt) influences whether compression or EQ changes are allowed

Documented / Planned (Not in Current Code)

MLP (Multi-Layer Perceptron): Basic neural network mapping audio features to DSP parameters (Python prototype, separate codebase)
LLM Integration: OpenRouter API with Meta LLaMA 3.1 8B for context-aware NLP
Intensity Modifiers: Detecting "very", "subtle", "more" to scale parameter values
Style Learning: Learning a user's tonal preferences over time (future goal)
Data-Driven Tuning: Training on real audio data to improve parameter suggestions (not started)
On-Device Inference: Local ML models for faster AI responses (future)


16. UX CONCEPTS

Interface Design

Dark Theme: Professional dark gradient background with accent colors
Color Coding: Green (safe) to Yellow (caution) to Red (danger) across all meters and displays
Section Labels: Clear typography ("SPECTRUM ANALYZER", "LUFS METERS", "MIX ANALYSIS", "CHAT INTERFACE")
Window Size: 800 x 650 pixels — compact enough to fit alongside DAW windows

Interaction Patterns

Natural Language Control: Users type plain English commands instead of adjusting technical knobs
Chat UI: Familiar messaging-style interface with color-coded sender labels
Enter-to-Submit: textEditorReturnKeyPressed listener for keyboard-friendly workflow
Real-Time Visual Feedback: Spectrum, meters, and mix analysis update at 10–30 Hz
Safety Feedback: Explicit messages when AI decisions are overridden by safety rules
Phase Indicator: Orange "Phase 1" label reminds users this is an early version
Welcome Message: On-load guidance with available commands and usage instructions
Transparent Parameters: Users see exactly what DSP changes were applied (not a black box)

Accessibility Considerations

Command-Based Input: Eliminates need to understand EQ curves or compression settings
Plain-Language Responses: "Applying warm preset" instead of raw parameter dumps
Visual Guidance: Color-coded meters teach users what "good" looks like
Help Command: "help" lists all available commands for discoverability


17. FINAL SUMMARY

PRIZM is a well-architected, functional Phase 1 prototype of an AI-powered audio mixing and mastering assistant, built on the JUCE 8.0.8 framework in C++. The project demonstrates a compelling proof-of-concept that bridges the gap between complex DSP tools and accessible natural language control.

What Works Today

The plugin compiles and runs as a VST3 or Standalone application. Users can type natural language commands ("warm", "bright", "punch", etc.) into a chat interface, and the plugin applies real-time DSP processing — EQ shaping, compression, filtering — with 50ms smoothed parameter transitions. A comprehensive safety system prevents destructive over-processing, and real-time visual feedback (spectrum analyzer, LUFS meters, mix analysis) gives users immediate insight into their audio.

Technical Strengths

Clean Architecture: Clear separation between DSP processing, audio analysis, NLP logic, safety rules, and UI — each in its own class.
Thread Safety: Lock-free FIFO for spectrum data, SmoothedValue for parameter transitions, no allocations in the audio thread.
Extensibility: Adding new commands, presets, or safety rules requires changes in well-defined locations.
Cross-Platform: JUCE enables building for Windows, macOS, and Linux from a single codebase.

What Remains

The project is in early stages with significant work ahead: implementing the OpenRouter API for intelligent LLM-based NLP, collecting real training data for ML-driven parameter suggestions, building a preset save/load system, adding multi-band processing capabilities, implementing true ITU-R BS.1770 LUFS metering, and developing DAW parameter automation support.

Trajectory

The core idea — natural language to transparent parameters to safety-clamped DSP — addresses a genuine problem in audio production. The architecture is sound, the prototype is functional, and the path from proof-of-concept to viable product is clearly mapped out. The next phase requires transitioning from rule-based NLP to LLM-powered intelligent processing and validating audio quality improvements with real-world users.


Built by the PRIZM Development Team
Contact: contactprizmteam@gmail.com


