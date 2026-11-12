# PRIZM V2: Architecture and Technical System Overview

Status as of 2026-11-01, version 2.0.0. Paths are relative to `prizmv1/`. For full explanations, see `PRIZM_V2_COMPLETE_REPORT.md`.

## 1. System summary

PRIZM V2 is a stereo or mono JUCE audio effect, with these parts:

- A fixed five-module mastering chain plus trims and a monitoring stage.
- BS.1770 and EBU R128 metering, plus true-peak and stereo measurement.
- A rule-based, local assistant whose output is data (proposals) rather than direct action.
- A background thread for file analysis and offline simulation.

**Core design principle.** Everything converges on one set of 44 parameters. The assistant, manual controls, host automation and undo all write to them, and the DSP only reads them.

```mermaid
flowchart LR
    A["Assistant proposal after approval"] --> P["44 parameters"]
    M["Knobs, buttons and EQ dots"] --> P
    H["Host automation"] --> P
    U["Undo, redo and restore"] --> P
    P --> D["MasteringChain"]
```

## 2. Layers and dependencies

```mermaid
flowchart TB
    subgraph JuceLayer["Depends on JUCE"]
        Top["PluginProcessor and PluginEditor"]
        UI["Source/UI"]
        Eng["Source/Engine"]
    end
    subgraph PlainLayer["Plain C++17, no JUCE, unit-testable offline"]
        As["Source/Assistant"]
        DSP["Source/DSP"]
        Com["Source/Common: ParamTable"]
    end
    Top --> UI
    Top --> Eng
    UI --> Eng
    Eng --> As
    Eng --> DSP
    As --> DSP
    As --> Com
    DSP --> Com
```

| Layer | Lines | Depends on JUCE? | Tested by |
|---|---|---|---|
| `Source/Common` | 284 | No | Offline tests |
| `Source/DSP` | 3,093 | No (verified by search) | Offline tests |
| `Source/Assistant` | 2,983 | No (verified by search) | Offline tests |
| `Source/Engine` | 1,499 | Yes | Harness only |
| `Source/UI` | 3,364 | Yes | Harness only; display content isn't checked automatically |
| `PluginProcessor`, `PluginEditor` | 1,088 | Yes | Harness only |

## 3. Runtime threads

```mermaid
flowchart TB
    subgraph AT["Audio thread, host-owned"]
        A1["processBlock"]
    end
    subgraph WT["Analysis thread, low priority"]
        W1["AnalysisWorker::run"]
    end
    subgraph MT["Message thread"]
        M1["Editor timer 30 Hz"]
        M2["ChatSession timer 15 Hz"]
        M3["ChatPanel timer 10 Hz"]
        M4["HistoryManager"]
    end
    A1 -->|"atomics: MeterData"| M1
    A1 -->|"FIFO: spectrum 32768, scope 16384 frames"| M1
    A1 -->|"FIFOs: input and output, 65536 frames each"| W1
    M1 -->|"atomics: parameters and monitor switches"| A1
    M2 -->|"job deque under jobLock"| W1
    W1 -->|"result deque under resultLock, status under statusLock"| M2
    M4 -->|"setParamState: parameter atomics"| A1
```

| Thread | Allowed | Forbidden, by design |
|---|---|---|
| Audio | DSP, atomics, FIFO push, stack and preallocated memory | Locks, heap allocation, file I/O, UI |
| Analysis | File reads, heap, long simulations, locks on its own queues | Touching the live chain or UI |
| Message | UI, parameters, chat, history, FFT for the display | Touching the live chain's objects directly |

These rules were established by code review. No real-time sanitizer has been run.

**Overflow behaviour.** If a FIFO is full, audio for display or analysis is dropped, and the output is never affected. `pushAudio` writes the same frame count to both worker FIFOs, so that input and output stay aligned.

## 4. Audio signal path

```mermaid
flowchart LR
    In["Host input"] --> IC["inCopy"]
    In --> T1["Input trim"]
    T1 --> HP["Low cut"]
    HP --> LS["Low shelf"]
    LS --> B1["Bells 1 to 3"]
    B1 --> HS["High shelf"]
    HS --> LP["High cut"]
    LP --> C["Compressor"]
    C --> S["Saturator at 4x"]
    S --> I["Imager"]
    I --> L["Limiter"]
    L --> T2["Output trim"]
    T2 --> Tap["Tap"]
    Tap --> AB["A/B blend"]
    In --> OD["Delay by chain latency and match gain"]
    OD --> AB
    AB --> MO["Mono blend"]
    MO --> Out["Host output"]
    IC --> MI["Input meters and FIFOs"]
    Tap --> MOut["Output meters and FIFOs"]
```

Host blocks are split to at most 2048 samples in `processBlock`, then to at most 512 in `MasteringChain`. All switches crossfade.

**Latency budget**, constant in every state (**Measured**):

| Component | Samples at 48 kHz |
|---|---|
| Oversampler round trip | 59 |
| Limiter lookahead, 2 ms | 96 |
| True-peak detector | 6 |
| **Total** | **161 (3.35 ms)** |

A disabled module's signal still follows a matching delay path. The saturator and limiter run all the time, so CPU use barely depends on which modules are on: 1.6% with all off and 1.8% with all on, on this machine at 48 kHz (**Measured**, DSP only).

## 5. Assistant architecture

```mermaid
flowchart TD
    Text["Message"] --> Parse["parseMessage"]
    Ctx["AssistantContext: parameters, meters, analysis, reference"] --> Resp
    Parse --> Resp["respond"]
    Resp --> TextOnly["Text reply"]
    Resp --> Act["ActionType: session command"]
    Resp --> Tweak["handleTweak and Builder"]
    Resp --> MR["MasterRequest"]
    Tweak --> Prop["Proposal: from, to, reason, selected"]
    MR --> Worker["runMaster"]
    Worker --> BMP["buildMasteringProposal"]
    BMP --> Sim["MasteringChain simulations"]
    Sim --> Prop
    Prop --> Card["Pending card"]
    Card -->|"Apply"| H["HistoryManager"]
    H --> Params["Parameters"]
```

**Classification:** rule-based parsing, hand-written recipes, threshold rules and numerical search. There's **no trained model**, no network access, and no learning. Output is deterministic for a given message and state.

**Boundary.** `respond` receives parameters as read-only values and returns a `Reply`. It has no reference to the processor, so it can't change the sound. This boundary is what enforces the approval rule, and a future language model could replace the parser without changing it.

## 6. Proposal lifecycle

```mermaid
sequenceDiagram
    actor User
    participant CP as ChatPanel
    participant CS as ChatSession
    participant HM as HistoryManager
    participant Proc as Processor
    participant Chain as MasteringChain

    User->>CP: "make it warmer"
    CP->>CS: sendUserMessage
    CS->>CS: respond returns a Proposal
    CS-->>CP: card in Pending state
    User->>CP: untick items, then Apply
    CP->>CS: applyProposal(id)
    CS->>HM: applyWithUndo(target, title)
    HM->>HM: push snapshot of all parameters
    HM->>Proc: setParamState(target)
    Proc->>Proc: gesture and setValueNotifyingHost per change
    Chain->>Proc: next block reads the new values
    Chain->>Chain: ramps over about 50 ms
    User->>CP: Undo
    CP->>CS: undoProposal(id)
    CS->>HM: undo or restoreBefore
```

## 7. Mastering proposal pipeline

```mermaid
flowchart TD
    Cap["Rolling capture: audible input only"] --> Clip["Latest 15 s, at least 8 s"]
    Clip --> An["AudioAnalyzer"]
    An --> Gain["Gain staging"]
    Gain --> EQ["EQ target and fit"]
    EQ --> Rum["Rumble check"]
    Rum --> Comp["Compressor decision and threshold search"]
    Comp --> Sat["Saturation decision"]
    Sat --> Img["Imager decision"]
    Img --> Pre["Render chain up to the limiter"]
    Pre --> Bis["Bisect limiter drive to reach target"]
    Bis --> Cap6{"Average limiting above 6 dB?"}
    Cap6 -->|"Yes"| Back["Reduce drive and add a note"]
    Cap6 -->|"No"| Diff["Difference from current settings"]
    Back --> Diff
    Diff --> Ver["Final simulate for the prediction"]
```

## 8. State and persistence

| Data | Owner | Lifetime | Saved in session? |
|---|---|---|---|
| 44 parameters | `apvts` | Plugin instance | **Yes** |
| Target LUFS, ceiling target | Processor | Plugin instance | **Yes** |
| Reference name, path, analysis | Processor | Plugin instance | **Yes**, as a serialised string |
| Reference audio | None | Never kept | No |
| A/B, level match, mono | Processor | Plugin instance | No; defaults to Processed, match on, mono off |
| Undo history, up to 100 snapshots | `HistoryManager` | Plugin instance | No |
| Chat, up to 200 messages | `ChatSession` | Plugin instance; survives closing the window | No |
| Recent analyses | `ChatSession` | Plugin instance | No |
| Rolling capture, 20 s | `AnalysisWorker` | Until stale or rate change | No |

`getStateInformation` writes the APVTS XML plus a `PRIZM_V2` child element, which `setStateInformation` reads and removes before restoring.

## 9. Build and deployment

```mermaid
flowchart LR
    J["prizmv1.jucer"] -->|"Projucer --resave"| LM["Builds/LinuxMakefile"]
    J -->|"Projucer --resave"| XC["Builds/MacOSX Xcode project"]
    LM -->|"make"| A["prizmv1.a shared code"]
    A --> V["prizmv1.vst3"]
    A --> ST["prizmv1 Standalone"]
    A --> HN["Tests/PluginHarness"]
    XC -.->|"no build evidence"| AU["AU, VST3, Standalone on macOS"]
    SRC["Source: Common, DSP, Assistant"] -->|"g++ directly"| OT["Offline tests"]
```

- `prizmv1/Builds/` is untracked in git.
- The Makefile copies the VST3 to `~/.vst3` unless `JUCE_VST3DESTDIR=` is set empty.
- The installed `~/.vst3` copy on this machine is V1 (1.0.0).
- Plugin identity is still the placeholder: name `prizmv1`, manufacturer `yourcompany`, codes `Manu` and `Mpkc` (shown in hexadecimal in `JuceLibraryCode/JucePluginDefines.h`).

## 10. Architecture strengths and risks

| Strength | Why it matters |
|---|---|
| One DSP engine for live and simulation | Predictions reflect the real chain. Prediction and live output agreed within about 0.12 LU (**Simulated**) |
| Assistant as a pure function | Testable; can't change the sound without approval |
| Single parameter table | Host, DSP, UI and assistant can't disagree |
| No locks on the audio path | Reader problems can't delay audio |
| Fixed latency | Simple for hosts; A/B stays aligned |

| Risk | Detail | Suggested change |
|---|---|---|
| Monitoring is in the output path | A/B and Mono print to a bounce | Warn, or disable when `isNonRealtime` is true |
| Chat card state is separate from history | Cards can show stale state | Derive card state from history IDs |
| Single job queue with no cancellation path | A waiting job blocks others | Add a Cancel button and a busy check |
| Gain reduction read after chunked processing | Under-reads above 512-sample blocks | Hold a per-call maximum in the chain |
| Analysis of only 15 s | Prediction may not reflect the whole song | Offer a whole-song capture |
| Static single-band chain | No targeted fixes | Multiband modules with crossovers |
| Never run in a real host | The host boundary is the largest unverified area | DAW and validator testing first |
