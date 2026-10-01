# Mla Speech

A VST3 text-to-speech instrument in the style of the Atari ST's speech
synthesizer (`STSPEECH.TOS`): the blunt, buzzing 1980s formant voice, played
back through an emulation of the ST's YM2149 sound chip (subcategory
`Instrument|Synth`).

A note speaks a phrase. The words come with the note as a VST3 note-expression
text event (`kTextTypeID`; `kPhonemeTypeID` takes phoneme codes). mlacker sends
a pattern's comment (TEXT) cells this way. A note without words repeats the
last phrase at its own pitch, so a melody can sing it. Before any words arrive
the plug-in says "Hello. I am Mla Speech."

The synthesis is MLang: `src/mla_speech_dsp.mla`. The C++ layer
(`src/plugin.cpp`) declares the buses, turns text into phonemes
(`src/speech_rules.h`), schedules events sample-accurately, maps parameters and
saves state.

This is an ST-*style* design, not an emulation of the original program's code.
The 1980s home-computer speech programs read English with the US Naval
Research Laboratory's letter-to-sound rules and spoke it through a small
formant synthesizer at a low sample rate. Mla Speech does the same with its own
phoneme table and synthesis. The YM2149's 4-bit logarithmic volume DAC, which
the ST played its samples through, gives the result its grain.

## In mlacker

1. Add Mla Speech as an instrument (Instrument > Add VST3 instrument) and
   create an Instrument track for it (Track > Create Instrument track).
2. **Track > Add / remove comment column** adds a TEXT column after the
   track's CC columns. The column is shown, and the cursor moves to it.
3. Press **Enter** on a TEXT cell, type the words, and press **Enter** again.
   The column widens to its longest text (up to 48 cells; a text holds up to
   240 characters).
4. Play. A row's text is spoken with the row's first note, at that note's
   pitch and velocity. A row with text but no note speaks at C-3, velocity 100.
   A row with a note but no text repeats the track's last phrase.

The phrase plays to its end however long the row or note is: one row can hold
a whole sentence. With **Gate** set to *Note length*, a note-off cuts the voice
instead.

The TEXT column belongs to the third column stage, with the CC columns: `z`
cycles NOTE/VEL, then NOTE/VEL/LEN/OFF, then all columns. Track > Note lines >
Expand all shows it on every track. On a plain MIDI track, a TEXT column is
just a comment column; nothing is sent. Sessions and pattern files keep the
column and its texts.

## Text

- English text is read with letter-to-sound rules after NRL Report 7948
  (Elovitz et al., 1976). The rules miss some irregular words, as the
  originals did. Respell those words ("Atari" is built in).
- Numbers are read as words. Four-digit numbers from 1100 are read as years
  ("1987" is "nineteen eighty seven"). Numbers with more than 12 digits, and
  numbers that start with 0, are read digit by digit.
- Words without vowels are spelled out ("ST", "TV").
- `.` `!` `?` `;` end a clause with a long pause, and `,` `:` `-` with a short
  one. Pitch falls through each clause, rises before `?` and lifts at `!`. The
  first vowel of a content word is stressed.
- Text in `[brackets]` is phoneme codes: two capitals for vowels and digraphs
  (`IY IH EY EH AE AA AO OW UH UW ER AX AH AY AW OY TH DH SH ZH NG CH WH`), one
  lower-case letter for other consonants (`p b t d k g f v s z h m n l w y r j`).
  A `1` after a vowel stresses it, and a `0` reduces it. A space is a word gap.
  For example: `[hEH1lOW wER1ld]`.
- Common accented Latin-1 letters lose their accents. Other characters are
  skipped.

## Signal flow

- **Utterance.** Each phoneme has a duration, three formant targets at its
  start and end (diphthongs glide), and voicing, frication and aspiration
  levels. Stops add a closure and a burst. The rules' pitch and duration
  factors scale them. Targets are followed with one-pole glides, which give the
  formant transitions between sounds.
- **Synthesis** runs at the **ST Rate** (4-24 kHz, default 10 kHz). The source
  is a glottal pulse train: the derivative of a Rosenberg pulse (*Buzz*), the
  pulse itself (*Soft*), or noise (*Whisper*). The source and aspiration noise
  go through a cascade of four resonators (F1-F3 and a fixed F4). Frication
  noise goes through its own band-pass, clamped below the ST rate's Nyquist
  frequency, like the original's dull "s".
- **DAC.** *YM 4-bit* quantizes every sample to the YM2149's 16 volume levels,
  3 dB apart. *YM 3 voices* uses 1 dB steps, like the replay routines that
  summed all three channels. *Clean* skips the DAC.
- **Output.** The samples are held at the host rate, which keeps the aliasing,
  then low-passed (**Smooth**) and DC-blocked. The voice is mono, on both
  channels.

## Parameters

| ID  | Name       | Range | Notes |
|-----|------------|-------|-------|
| 100 | Speed      | 0.5 .. 2x | MIDI CC 76. |
| 101 | Mouth      | ±6 st | Formant shift: down is a larger head, up a smaller one. MIDI CC 71. |
| 102 | Intonation | 0..1 | 0 = monotone robot; 1 = full clause contour and stress. MIDI CC 77. |
| 103 | Voice      | Buzz / Soft / Whisper | |
| 104 | ST Rate    | 4 .. 24 kHz | Internal sample rate (at most the host's). |
| 105 | DAC        | Clean / YM 3 voices / YM 4-bit | Default YM 3 voices. |
| 106 | Smooth     | 0..1 | 0 keeps the held steps, 1 low-passes at the ST rate's band edge. MIDI CC 74. |
| 107 | Gate       | Whole phrase / Note length | |
| 108 | Tune       | ±24 st | |
| 109 | Velocity   | 0..1 | Velocity sensitivity of the level. |
| 110 | Output     | -60 .. +6 dB (bottom = off) | MIDI CC 7. |
| 111 | Pitch Bend | ±2 st | MIDI pitch bend. |

The voice is monophonic, like the original. A new note restarts it. The key
sets the pitch (C-3 = 130.8 Hz), and Intonation moves the pitch around it.
Parameter IDs are stable, and new parameters are appended.

## Build and test

From the repository root, `./build.sh --plugins` builds every plug-in including
this one (`--install` copies it to the plug-in directory). On its own:

```sh
../../subprojects/mlang/build/mlang pkg --config mlang.toml run build   # -> build/cmake/VST3/Release/MlaSpeech.vst3
../../subprojects/mlang/build/mlang pkg --config mlang.toml run test
```

`tests/mla_speech_tests.cpp` loads the built bundle through the SDK hosting
classes and checks rendered audio:

- bus layout and arrangements
- silence without notes
- the default phrase, spoken to its end after an immediate note-off
- words that come with a note: a short and a long text speak for matching lengths, from the note's sample offset
- words that arrive after their note restart it; a note without words repeats them
- Gate = Note length cuts the voice
- the pitch follows the key (autocorrelation)
- phoneme input, every voice and DAC mode
- numbers read as years, restarts, and state round trips

mlacker's host test (`tests/vst3_tests.cpp`) also loads the bundle and speaks
phrases through the controller's `InstrumentText` events.
