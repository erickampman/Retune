<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">Retune</h1>
</div>

<p>
  <em>Retune is a CLAP note effect on macOS and Linux. Its VST® 3 and AUv2 (macOS) builds work too, rounded to semitones.</em>
</p>

::: {.callout .callout-note .card}
## On This Page
[Overview](#overview) · [Getting Started](#getting-started) · [The Window](#window)

**The controls —** [Divisions](#divisions) · [Steps](#steps) · [Octaves](#octaves) · [The Readouts](#readouts) · [Dry](#dry)

**Playing —** [Hosts and the Fraction](#hosts) · [Held Notes and MIDI](#midi) · [Tips](#tips) · [Troubleshooting](#troubleshooting)
:::

::: {.callout .callout-note .card}
## Overview {#overview}
Retune is a **note effect**: it makes no sound. It sits in front of a synth and moves every note you play by the same interval, measured in an **equal division of the octave** (EDO).

You choose **N**, the number of equal steps in an octave (5 to 96), and how many of those steps to move (**Steps**), plus whole **Octaves**. So a 12-TET keyboard can play shifted by 3 steps of 19-EDO, 5 steps of 31-EDO, or a quarter tone of 24-EDO.

The shift in semitones is **12 × Octaves + 12 × Steps / N**. With N = 12 every shift is a whole number of semitones; with any other N it usually has a fraction, and that fraction is what Retune is for:
- **MIDI 2.0:** the note carries its exact pitch (Pitch 7.9), so a synth that reads it plays the exact EDO step.
- **CLAP notes** (Bitwig and other CLAP hosts): the note goes to the nearest key, with a tuning expression carrying the fraction.
- **MIDI 1.0:** the note goes to the nearest semitone and the fraction is lost.

[Hosts and the Fraction](#hosts) says which one you get.
:::

::: {.callout .callout-note .card}
## Getting Started {#getting-started}
1. **Put Retune in front of a synth**: in Bitwig, a note FX before the instrument; in REAPER, before the instrument in the track's FX chain; in StageHand, the row's **MIDI FX** menu; in Logic, a **MIDI FX** slot.
2. **Play.** By default Retune does nothing: 12-EDO, no steps, no octaves.
3. **Drag the Divisions handle** (the outer arc) to the EDO you want, say 19.
4. **Drag the Steps handle** (the middle ring) clockwise to shift up, anticlockwise to shift down. The middle of the dial shows the shift in cents.
5. **Drag the Octaves handle** (the inner ring) for whole octaves.
6. **Tick Dry** in the header to hear the original note as well.

The host saves every setting with its project.
:::

::: {.callout .callout-note .card}
## The Window {#window}
**The header:** the Unlikelyware logo (a click opens unlikelyware.com), **Retune**, the **Dry** checkbox, and **Help** (the **?** box that opens this page in your browser).

**The dial:** three controls as rings round one centre, each named just inside its top and each with a round handle that shows its value:
- **Divisions**, the outer arc
- **Steps**, the middle ring
- **Octaves**, the inner ring

In the middle, the whole shift in cents and in semitones; at the bottom, the shift in words and what MIDI 1.0 makes of it ([The Readouts](#readouts)).

**Resizing:** drag the host window's corner. The window keeps its shape (520 × 560) and scales everything in it; the smallest size is 416 × 448.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">Using the rings</summary>

::: disclosure-content
| Gesture | Result |
|---|---|
| Drag a handle | Moves it round its ring |
| Press anywhere else on a ring | Jumps the handle there, then drags from there |
| Double-click a ring | Back to its default: Divisions 12, Steps 0, Octaves 0 |
| Scroll over a ring | One step per notch: up or right for more |

**Steps** and **Octaves** go all the way round: a drag carries on past the bottom instead of flipping from + to −, and stops at its end (a whole turn). A press on a ring takes the shorter way round from 0. **Divisions** is an arc with a gap at the bottom: a drag into the gap stays at the end it went past.

Each drag, and each scroll notch, is one gesture to the host, so it records as one automation move.
:::

</details>

::: {.callout .callout-note .card}
## Divisions {#divisions}
**N**, the number of equal steps in an octave: 5 to 96, default 12. The arc runs from 5 at the lower left, over the top, to 96 at the lower right, with the common EDOs marked: 7, 12, 19, 22, 24, 31, 41, 53 and 72.

The minimum is 5 because every N below it (1, 2, 3, 4) divides 12: its steps are an octave, a tritone, a major third or a minor third, all of which 12-EDO already makes.
:::

::: {.callout .callout-note .card}
## Steps {#steps}
How many steps of 1/N octave to move: from −N to +N, default 0. The ring is one octave, its dots the N steps, with 12-TET's semitones ticked outside it so you can see where an EDO step falls between them. The green arc runs from 0 at the top to where Steps lands: clockwise is up, anticlockwise is down. A whole turn either way (±N) is an octave.

**Lowering Divisions** limits Steps to the new ±N at once, and the dial shows the limited value. Retune keeps the value you set, though: raise Divisions again and Steps comes back. (Changing it for you would fight the host's automation.)
:::

::: {.callout .callout-note .card}
## Octaves {#octaves}
Whole octaves to move, from −4 to +4, default 0. The ring goes round like Steps, an octave a quarter turn: a whole turn clockwise is +4, anticlockwise −4.
:::

::: {.callout .callout-note .card}
## The Readouts {#readouts}
- **The middle:** the whole shift in cents (an octave is 1200¢), and in semitones to three decimals.
- **Below the dial:** the shift in words ("19-EDO: +1 octave, +3 steps") and what **MIDI 1.0** makes of it: the nearest semitone, and how far that is from the real shift. For example, 19-EDO +1 octave +3 steps is +1389.47¢, and MIDI 1.0 plays +14 semitones, 10.5¢ sharp.
:::

::: {.callout .callout-note .card}
## Dry {#dry}
With **Dry** ticked, each note goes out twice: the original, unchanged, and the shifted one. You hear the interval Retune moves by as a two-note chord.

- **On its own key:** when the shifted note lands on the original's key (no shift, or a small one that rounds there), only the shifted note plays: a MIDI channel can't sound one key twice.
- **While you hold notes:** a note that started with Dry on ends with its original too, even if you untick Dry before letting go. A note that started with Dry off never gets one.
:::

::: {.callout .callout-note .card}
## Hosts and the Fraction {#hosts}
What Retune receives decides what it can send. It answers in the same form, so a synth gets the fraction only where the host passes it on.

| Host | Retune gets | The synth hears |
|---|---|---|
| StageHand | MIDI 2.0 | The exact step (Pitch 7.9) |
| Bitwig | CLAP notes | The exact step, from a synth that honours tuning expressions |
| A host that sends MIDI 1.0 only | MIDI 1.0 | The nearest semitone |
| Any host, VST3 or AUv2 build (Logic, Live, Cubase…) | MIDI 1.0 | The nearest semitone |

The Unlikelyware synths play Pitch 7.9, and PlainOldSynth, StruckSynth, StruckRoom and StruckGaussian also play CLAP tuning expressions. A synth that ignores them plays the nearest key below (MIDI 2.0) or the nearest key (CLAP notes).

**Logic** loads the AUv2 build, so there Retune rounds to semitones, like any 12-EDO transpose.
:::

::: {.callout .callout-note .card}
## Held Notes and MIDI {#midi}
- **Changing the shift while notes are held** moves only the notes you play next. Each sounding note remembers where it went, so its Note Off, pressure and other per-note messages follow it there, and no note gets stuck.
- **Notes shifted below 0 or above 127** (MIDI's range) are dropped, with everything that belongs to them.
- **Everything that isn't a note** passes through unchanged: CCs, channel pitch bend, program changes, SysEx, clock.
- **All Notes Off and All Sound Off** pass through and make Retune forget that channel's held notes.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">MIDI 2.0 and CLAP details</summary>

::: disclosure-content
- **MIDI 2.0:** a Note On leaves with a Pitch 7.9 attribute holding its exact pitch, and its note number is the whole part, so a receiver that ignores the attribute plays the nearest key below. A note that arrives with its own Pitch 7.9 keeps its offset from its key. A Pitch 7.25 per-note controller (an absolute pitch) moves by the note's whole shift, fraction included.
- **CLAP notes:** a Note On goes to the nearest key, followed straight away by a tuning expression with the fraction (same note id). A tuning expression the host sends for that note gets the fraction added. Other note expressions follow the note to its new key.
- **Dry with CLAP notes:** the original goes out with note id −1, leaving the host's note id to the shifted note.
:::

</details>

::: {.callout .callout-note .card}
## Tips {#tips}
- **Microtonal intervals over a 12-TET keyboard:** pick N, then try Steps one at a time and listen to the interval with **Dry** ticked.
- **Quarter tones:** 24-EDO, Steps ±1.
- **Two Retunes in a row add up:** 19-EDO +3 then 31-EDO −2 is one shift of their sum, fraction included in MIDI 2.0 and with CLAP notes (MIDI 1.0 rounds each one).
- **Watch the MIDI 1.0 line:** if it says "exact", the shift is whole semitones and every host plays it exactly.
:::

::: {.callout .callout-note .card}
## Troubleshooting {#troubleshooting}
- **The shift sounds rounded to semitones.** The host is sending MIDI 1.0, or you're using the VST3 or AUv2 build, or the synth ignores Pitch 7.9 and tuning expressions. See [Hosts and the Fraction](#hosts).
- **Changing a ring didn't change the note I'm holding.** That's the design: only new notes take the new shift.
- **Some notes make no sound.** They were shifted outside MIDI's 0–127 range. Lower Octaves or Steps.
- **With Dry ticked, I hear one note, not two.** The shifted note lands on the original's key (see [Dry](#dry)).
- **With Dry ticked and an octave shift, a note ends too soon.** When you hold C4 and C5 and shift up an octave, C4's shifted note and C5's original both land on C5. The synth decides what that means: one that stacks voices plays both; one that retriggers ends the sound at the first Note Off.
- **Steps shows a smaller number than I set.** Divisions is lower than your Steps, so Retune limits it to ±N; raise Divisions and it comes back.
- **Retune doesn't appear in Logic.** Rescan Audio Units in Logic's Plug-in Manager, or quit and restart Logic.
- **A rebuilt Retune doesn't show its changes.** Quit and restart the host: a running host keeps the plugin it loaded first.
:::

VST is a registered trademark of Steinberg Media Technologies GmbH.
