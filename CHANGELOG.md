# Changelog

What changed for players, newest first. Every branch adds its changes under **Unreleased**;
a release renames that section to its version, and its notes on GitHub's Releases page start
from it. Developer-only changes (refactoring, tests, docs) aren't listed unless they change
what FRIZZ does.

## Unreleased

### Changed

- Turning the master compressor's knobs takes far less processing time while they glide,
  so it no longer adds to crackles on busy scenes.
- Mono input (VOLUME's page 3) needs its 3 detents in one direction: turning back starts the
  count over, so wiggling the knob doesn't switch it.
- A flash on a key that's already lit nearly white (a select on a loud effect or on the
  compressor working hard, a tap on LOOP near the loop's end) goes dark instead of white, so
  it's seen.
- LOOP's blink while a quantized recording closes or an erase waits for the loop's end is
  slower, the same as a picked scene slot's, so it doesn't look like a refusal's 3 quick blinks.

### Fixed

- Holding an FX key and tapping CHOMPI twice in a save, copy or delete mode confirmed the
  scene and dropped the latch; it now latches and doesn't confirm, as with one tap.
- The delay's pitch-up events (random knob left of centre) read from the wrong place after
  switching on, until the division knob or the tempo first changed.
- The flanger clicked when its stereo knob went back to 0, as a scene recall or morph does.
- A card put in after switching on without one lost its saved scenes and master settings
  to the first save. Its scenes are now read into the empty slots, and what a save would
  overwrite is kept as `.bak`.
- A failed write of the compressor's settings or the mono switch is tried again 3 times,
  as the manual says (it was twice, and not at all for a mono change after earlier failures).
- LEDs at full brightness no longer flicker dark: a value a hair above full wrapped to off
  (the transport's colours at full speed, the boot animation).
- Holding an FX key, then SHIFT with a control that does nothing (the transport, or a knob the
  effect doesn't use) no longer cancels the latch or a pending confirm.

## v0.10 (2026-10-08)

### Added

- **Mono input** on VOLUME's page 3: turn 3 detents left for a mono (TS) cable, such as one
  from a Lyra-8 or a pedal, and the left channel feeds both sides; 3 right for stereo. Saved
  on the card. By sfaber02 ([#1](https://github.com/ZUHcYa/FrizZ/pull/1)).
- **Headphone feed** on VOLUME's page 4: turned left, blends steplessly from the master out
  to the dry input on its own; back on the master at power-on.
- SHIFT + press VOLUME resets the input/loop mix.
- Picking a VOLUME page blinks its number in white.
- A failed write of the compressor's settings blinks its key red
  and is tried again, up to 3 times.

### Changed

- **New key layout:** the delay and reverb moved one key right (12th and 13th) and the master
  compressor to the last key (15th). The 11th and 14th keys are free, setting the inserts,
  the sends and the compressor apart.
- The headphone feed moved from the mode switch to VOLUME; the mode switch does nothing in
  play mode.
- A morph started with SHIFT + scene key waits while SHIFT is held, however long, and on
  release glides the whole way to the next bar line (one more per tap). The effects it
  fades in come on when the glide starts.
- The DELETE scene mode is amber, so red only means refused.
- VOLUME turns faster: the gains move 2% per detent, the mix (SHIFT + turn) 4%.
- The battery check shows red below 3 V and reads correctly from a second after boot.
- The crusher is held to its input's level.
- The reverb's modulation runs at its intended rate (it was 1/32 too fast); the delay's
  feedback and the filter's resonance glide like every other knob. The sound changes slightly.
- The filter's LFO follows a loop slowed below 50 BPM instead of running ahead.
- The CHOMPI shows up as FrizZ over USB.
- The delay's random events only start while its key is on: after you release it, or a
  scene turns it off, its tail rings out as plain echoes instead of firing new grains.

### Fixed

- Crackles while a loop plays with the delay on, since v0.10-beta.2: the audio processing ran
  at the edge of its time, partly because effects that were switched off kept working in the
  background. They now rest once faded out (the delay and reverb only once their tails have
  rung out), which roughly halves the work with a loop and the delay running. Many effects on
  at once cost as much as before.
- Letting go of CHOMPI before VOLUME no longer also changes the page, and SHIFT + VOLUME press
  doesn't start the battery check.
- Keys without a function no longer cancel a pending confirm or latch; SHIFT + transport
  press does nothing.
- An FX key pressed while a morph switched another effect could lose its on/off.
- The shifter keeps its swoop when a knob turns during it.
- A quantized recording closes when MIDI clock drops out and comes back within a block.
- A delay event on a slowed-down loop no longer runs off the buffer.
- On a loop whose tempo isn't a whole BPM, the delay's echoes, the freezer's length and the
  tape stop follow the loop's exact tempo instead of the nearest whole BPM. The echoes used
  to drift off the loop's beats a little more with every repeat (up to 4 ms a repeat on a
  1/4 delay), which smeared and flammed against the loop while it played.
- The delay no longer zips when a loop closes on a tempo of its own (or the tempo jumps
  otherwise): it crossfades to the new delay time instead of scrubbing through its buffer.
- The master compressor, turned off under a hot signal, releases before bypassing.
- The master compressor no longer crackles at extreme settings: its detector holds a peak
  for 10 ms, and its fastest attack is 1 ms (was 0.5 ms). It sounds smoother on bass and
  reduces slightly more at the same settings (about 1 dB with the amount at 30%).

### Removed

- **The randomizer.** Its line in `FRIZZ/frizz_master.txt` is skipped and dropped at the next
  save; the compressor's settings and the mono input stay as they were.
- The factory self-test page (TAPE or WAVE run the same test).

### Known issues

- Some combinations of effects, especially the shifter and the crusher with a loop playing
  and the master compressor working hard, can need more processing time than there is, which
  is heard as crackles. Switching one of them off clears it. Effects that are off cost (almost)
  nothing.

## v0.9 (2026-10-06)

The first public release. See its
[release notes](https://github.com/ZUHcYa/FrizZ/releases/tag/v0.9).
