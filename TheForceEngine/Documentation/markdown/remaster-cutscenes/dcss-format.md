# DCSS format reference

**DCSS** = Dark Cutscene Script. A tiny text format that tells TFE when
to change MIDI sequences, fire cue points, and override music volume
while an OGV cutscene plays. One `.dcss` file per cutscene, named to
match the OGV: `<scene>.dcss`.

## Overall shape

The file is a list of **entries**, separated by blank lines. Each
entry is three parts:

```
<index>
<HH:MM:SS,mmm timestamp>
<directive line>
<directive line>
...
(blank line ends the entry)
```

Two optional **header flags** may appear before the first entry:
`+credits` and `+openingcredits`.

Comments are supported on their own lines: `# comment` or `// comment`.
Everything else on a directive or timestamp line is parsed, so don't
put trailing comments after data.

## Complete example

```
+credits

+openingcredits

# Opening logo — seq 1 plays the main title music.
1
00:00:00,000
seq: 1
cue: 1

# Intro title card wipe.
2
00:00:14,853
cue: 2

3
00:01:39,700
cue: 4

# Crawl ends.
4
00:01:50,487
cue: 5

# Fade to dark logo.
5
00:01:59,000
cue: 7
```

This is the stock remaster's `logo.dcss`, annotated.

## Directives

### `seq: N`

Start (or switch to) MIDI sequence `N`. Sequences are defined in
`cutmuse.txt` (inside `dark.gob`) and are indexed 1..20.

When a DCSS entry fires a `seq:` directive, TFE internally calls
`lmusic_setSequence(N)`, which unloads the current MIDI and loads the
new sequence's patch set. This is an expensive operation — use it when
the cutscene transitions between distinct musical pieces.

Typical pattern: the first DCSS entry sets `seq:` to the scene's main
music sequence. Later entries leave `seq` unspecified and just fire
`cue:` values to transition within that sequence.

### `cue: N`

Trigger cue point `N` (1..20) within the currently loaded sequence.
`N = 0` is reserved for "stop all sounds" and is not used in the stock
data.

Cue points handle intra-sequence transitions: moving from the intro
section to the main theme, crossfading tracks, fading out, etc. The
specific behavior depends on how the sequence was authored in
`cutmuse.txt`.

### `musicvol: N`

Set the MIDI music volume. `N` is a **percentage** where `100 = normal
volume**; values above 100 boost, below 100 attenuate. Practical range
is 0..127 (127 ≈ 27% louder than normal).

Applied as: `scaled_volume = settings.cutsceneMusicVolume * (N / 100)`.
It persists until the next `musicvol:` directive or until the cutscene
ends (at which point TFE restores the user's base music volume).

Stock examples:

- `kflyby.dcss` entry 1: `musicvol: 80` (8% quieter during the arrival)
- `kflyby.dcss` entry 14: `musicvol: 90` (slight boost for the ending)
- `fullcred.dcss` entry 1: `musicvol: 110` (credits music is louder)

## Header flags

### `+credits`

Signals that this cutscene should render TFE's credits overlay on top
of the video. Used by `logo.dcss` and `fullcred.dcss` in stock data.

> **Implementation status**: TFE parses the flag but does not yet act
> on it. Reserved for future compatibility with the remaster's baked
> credits scroll.

### `+openingcredits`

Signals the "opening credits" variant. Only `logo.dcss` uses this.

> **Implementation status**: parsed but not yet acted on.

Both flags must appear **before** the first entry (i.e. before any
numbered block). Blank lines between them are fine.

## Timestamp syntax

The canonical form is SRT-style: `HH:MM:SS,mmm`.

The parser is **deliberately tolerant** to match the remaster's own
implementation, which accepts several variants that appear in the
stock data as typos:

| Variant | Example | Interpreted as |
|---|---|---|
| Canonical | `00:01:50,487` | 1m 50.487s |
| Period instead of comma | `00:01:50.487` | same |
| Colon instead of comma | `00:00:58:827` | 58.827s (yes, this is in `kflyby.dcss` as-shipped) |
| Short minute field | `00:1:50,487` | 1m 50.487s (yes, `logo.dcss` ships this) |

Rules:

- At least **three colon-separated numeric fields** are required
  (HH:MM:SS). Two-field forms like `MM:SS` are rejected.
- Milliseconds are optional; no ms = 0.
- Leading zeros are optional on each field.
- The separator before milliseconds may be `,`, `:`, or `.`.
- At most 3 digits are consumed for the milliseconds field; extras are
  silently dropped.

## Entry index

The number on the first line of each block is the **1-based entry
index**. It's informational and exists to mirror the SRT format;
parsers should see `1, 2, 3, …` in order.

**Out-of-order indices log a warning** but the entry is still accepted.
**Entries are sorted by timestamp** on load, so even if you write them
out of order, dispatch will be correct.

## Comments

Lines whose first non-whitespace character is `#` or `//` are ignored.
They can appear:

- At the top of the file, before any entry.
- Between entries (blank-line-delimited).
- Inside a directive block.

Comments inline on a data line (e.g. `seq: 1 # main theme`) are **not**
supported — the comment marker is consumed as part of the value and
will break the parse.

## What the parser does on invalid input

| Condition | Behavior |
|---|---|
| Non-numeric where an index is expected | Log warning, skip this entry, resume at next blank line. |
| Unparseable timestamp | Log warning, skip this entry, resume. |
| Unknown directive line (`foo: 123`) | Silently ignored (forward-compat for future directives). |
| Truncated file (e.g. index line with no timestamp) | Return what was parsed so far. |
| Empty or missing file | Return `false` from `dcss_loadFromFile`; no entries, flags cleared. |

Unknown directives being silently accepted is intentional — it lets a
newer version of TFE's DCSS parser add directives (say,
`subtitleColor:` or `hud:`) without breaking existing DCSS files on
older builds.

## Minimum viable DCSS

The smallest file that does anything useful has one entry with a
sequence and cue:

```
1
00:00:00,000
seq: 6
cue: 1
```

If you have no MIDI ambitions at all and just want the video to play
without music, you can skip the DCSS entirely. TFE's cutscene.cpp
falls back to `lmusic_setSequence(scene->music)` from `cutscene.lst`
and dispatches no cues.

## Cue values reference

The legal range for `seq:` is 1..20 and for `cue:` is 1..20. These are
indices into the sequence table in `cutmuse.txt` (packaged inside
`dark.gob`). TFE loads that catalog at startup and logs the mapping
when `SHOW_MUSIC_MSG` is enabled in `lmusic.cpp`.

Stock sequence assignments (from the `cutscene.lst` `music_seq`
column):

| Seq | Scene / purpose |
|---|---|
| 1 | Logo / opening crawl |
| 2 | Mission 1 (Talay / kflyby) |
| 3 | Gromas intro |
| 4 | Gromas exit |
| 5 | Mission 6 intro (arcfly) |
| 6 | Robotics intro (rob1) |
| 7 | Robotics exit (robotx) |
| 8 | Jabship intro (jabba1) |
| 9 | Jabship escape (jabescp) |
| 10 | Cargo (cargo1) |
| 11 | Finale intro (exp1xx) |
| 12 | Full credits |

If you're adding a mod with new cutscenes, you can reuse these
sequences or extend `cutmuse.txt` via a mod GOB to add more (up to 20).

## Differences from real SRT

If you think of DCSS as "SRT with a different payload," these are the
places it diverges:

| SRT | DCSS |
|---|---|
| `-->` on the timestamp line, with a start and end time | Only a single start timestamp |
| Payload is free-form text | Payload is `seq:` / `cue:` / `musicvol:` lines |
| No header flags | `+credits`, `+openingcredits` |

Subtitles for TFE cutscenes use real SRT files, not DCSS. See
[modding-guide.md](modding-guide.md) for how those fit in.
