# PRIZM V2: Executive Summary

*For judges, reviewers and general readers. No technical background needed. Status as of 2026-11-01.*

## What is PRIZM?

PRIZM is an audio plugin that helps independent musicians finish their songs. It works inside music-production software and combines a complete set of mastering tools with a chat assistant. You describe what you want in plain English, such as "make it warmer" or "master it for Spotify". PRIZM then proposes exact changes, explains each one, and waits for your approval.

## The problem

Mastering is the final polish that makes a song sound balanced and ready for release. It takes years to learn, and hiring a professional can be costly. Many producers can describe what they hear ("it sounds muddy") but don't know which of many technical controls would fix it. Automatic tools can also change a song in ways the user doesn't understand.

## How PRIZM helps

1. **You ask.** Type a request, or click a suggestion.
2. **PRIZM listens and measures.** It measures loudness, tone, dynamics and stereo width as your song plays, using the same loudness measure as broadcasters.
3. **PRIZM proposes.** For a full master, it tests its idea on a private copy of your audio to predict the final loudness.
4. **You decide.** You see every proposed change with its reason, and you can accept, reject or remove individual items.
5. **You compare and can always go back.** One button plays your original at matching volume, so you judge sound rather than loudness. Everything can be undone.

You can also load a favourite commercial song as a reference. PRIZM measures it and shows how your track differs, and can propose changes to move closer.

## What makes it different

- **Nothing changes without your approval.** The assistant can only suggest. Your click applies the change.
- **It explains itself.** Every suggestion gives its reason, so it also teaches.
- **It tests before suggesting.** Loudness predictions come from a trial run on your own audio.
- **It's private.** Everything runs on your computer. Your music is never uploaded.

## An honest note about "AI"

PRIZM is labelled an AI assistant, but it doesn't use a trained AI model or machine learning. It works by recognising known words and following hand-written rules, combined with audio measurement and a numerical search for the right settings. This makes it predictable and explainable. It also means that it can misread requests outside its vocabulary, and it can't treat individual instruments separately: "make my vocals warmer" changes the whole mix.

## Current status: a working prototype

**What has been shown:**

- The audio processing, measurement, assistant, comparison and undo features all exist and work in automated tests.
- All **137 automated checks across 17 test groups** passed, along with **28 end-to-end checks** that run the real plugin code with a simulated audio signal.
- In that simulation, a target of -14 LUFS (a common streaming loudness) was predicted at -14.00 and measured at -14.12.
- It used under 4% of one processor core at 48 kHz on the test laptop, and it added about 3.4 milliseconds of delay, which music software compensates for automatically.

**What has not yet been shown:**

- **Real songs:** all testing so far has used computer-generated test signals.
- **Real music software:** V2 hasn't yet been tested inside a production program such as Logic, Ableton or Reaper. The only recorded image of PRIZM inside Logic shows V1.
- **Real listeners:** no listening tests or user studies have been done, so whether it makes music *sound better* is still unproven.
- **Mac and Windows:** V2 has only been built on Linux. A Mac project exists but hasn't been shown to build, and Windows isn't set up yet.

## Issues found during this review

The review also found issues that should be fixed before release. The most important are:

- If the "Original" or "Mono" listening buttons are left on while exporting, the exported file will contain that signal.
- A few unrecognised requests produce the wrong suggestion. For example, "de-ess the vocal" proposes a boost rather than a reduction. Because of the approval step, the user sees this before anything changes.
- The product still has placeholder names, and the older documentation describes V1.

## What comes next

1. Test in real music software on Linux and Mac.
2. Master real songs, and have experienced listeners judge the results.
3. Fix the issues above.
4. Prepare for release with a final name, a licence decision, and installers.
5. Add advanced features as needed, such as processing by frequency band and reference playback.

## Conclusion

PRIZM V2 has moved from a prototype whose chat replies didn't change the sound to a real, testable mastering plugin with a transparent, approval-first assistant. Its engineering has held up in automated testing. The next and most important step is to prove it on real music, in real software, with real users.

*Full details: `PRIZM_V2_COMPLETE_REPORT.md`.*
