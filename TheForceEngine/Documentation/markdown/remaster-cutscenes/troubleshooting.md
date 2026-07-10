# Troubleshooting

If a cutscene isn't behaving, work through the checks below in order.
Most issues are path, format, or timing mismatches; each has a distinct
signature in `the_force_engine_log.txt`.

## Where is the log?

```
%USERPROFILE%\Documents\TheForceEngine\the_force_engine_log.txt
```

If your Documents folder is redirected to OneDrive, it's at
`%USERPROFILE%\OneDrive\Documents\TheForceEngine\…` instead. TFE logs
the resolved path at startup:

```
[Paths] User Documents: "C:\Users\you\OneDrive\Documents\TheForceEngine\"
```

Previous runs' logs are kept as `the_force_engine_log.txt.1`,
`.txt.2`, etc.

## "The original LFD cutscene plays, not my OGV"

### Check 1: is the remaster path detected?

Look near the top of the log for one of:

```
[Remaster] Found remaster cutscenes at: <path>/movies/
[Remaster] Using custom cutscene path: <path>/movies/
[Remaster] Remaster OGV cutscene directory found.
```

If you see:

```
[Remaster] No remaster cutscene directory found; using original LFD cutscenes.
```

…TFE couldn't find the `movies/` directory. Fix in `settings.ini`:

```ini
[Dark_Forces]
df_remasterCutscenesPath = "C:/path/to/movies/"
```

Note: point at `movies/`, **not** at `movies/` 's parent.

### Check 2: is the feature toggle on?

```ini
[Dark_Forces]
df_enableRemasterCutscenes = true
```

Absent from `settings.ini` → defaults to `true`. But UI interactions
might have written `false`.

### Check 3: is there an OGV for this specific scene?

For scene `<name>`, TFE looks for:

```
<movies>/<name>.ogv              (or)
<movies>/<name>_<lang>.ogv       (if user's language is set)
```

If neither exists, the LFD path runs for that scene. Log will show
neither a `[Cutscene] Playing remastered…` nor a failure — it just
silently falls back. To confirm whether TFE tried, enable verbose
logging or check the file with `ls`:

```sh
ls <movies_dir> | grep -i <name>
```

### Check 4: is the scene name correct?

Per [architecture.md](architecture.md), file lookup uses the `scene`
field from `cutscene.lst`, not the archive name. For the stock intro,
`scene = "logo"` (lowercase), so `logo.ogv` is expected, not
`LOGO.OGV`. Windows filesystems are case-insensitive, but it's safer
to match the stock convention.

## "TFE crashes when the cutscene plays"

### If the log stops abruptly after `[Cutscene] Playing remastered OGV…`

The OGV decoder hit something it can't handle. Re-check your source
file:

```sh
ffprobe -v error -show_streams problem.ogv
```

Expected:
- One stream with `codec_name=theora`
- Optionally one with `codec_name=vorbis`

If you see `codec_name=vp8` or `theoraX` or anything else, your ffmpeg
didn't actually encode Theora. See [video-conversion.md](video-conversion.md).

### `[OgvPlayer] No Theora stream found in: <path>`

Same cause. Re-encode.

### `[OgvPlayer] Failed to read OGV headers from: <path>`

File is truncated or corrupted. Try re-running ffmpeg.

## "Music doesn't play / cues don't fire"

### Check 1: is the DCSS being loaded?

Expected log line:

```
[DcssParser] Loaded N cue entries from <path>/<name>.dcss
```

If missing, your DCSS file wasn't found. Check:

```
[Remaster] Found cutscene scripts at: <some_path>/cutscene_scripts/
```

The DCSS must be at that path as `<scene>.dcss`. If the log shows the
script path but your file is elsewhere, move it.

### Check 2: DCSS fallback behavior

If no DCSS is found but `cutscene.lst` lists a music sequence, TFE
falls back to playing just that sequence at t=0 with no cues:

```
[Cutscene] No DCSS script for scene 'myscene'; using cutscene.lst music=1 only.
```

This is fine for simple cases. Add a DCSS with `cue:` entries to
transition within the sequence.

### Check 3: parse errors

Each DCSS parse problem is logged as a warning:

```
[DcssParser] Expected numeric index, got 'seq: 5'
[DcssParser] Bad timestamp 'huh' at index 3
[DcssParser] Out-of-order index 5 (expected 3)
```

Read [dcss-format.md](dcss-format.md) for the exact syntax.

Common parse-breaking mistakes:

| Mistake | Symptom |
|---|---|
| Blank line in the *middle* of a block (between timestamp and directives) | Entry ends after timestamp; no directives applied. |
| Trailing comment on a data line (`seq: 1 # main`) | Comment text captured as part of the value; `strtol` returns 1 as expected here but `musicvol: 80 # quiet` returns 80 too — it happens to work for numeric directives but *don't* rely on it. |
| Bare `seq:5` without space | Unrecognized directive, silently ignored. The parser specifically looks for `seq: ` (with the trailing space). |
| Using `MM:SS` (no hours) | Timestamp rejected; entry skipped. |

### Check 4: sequence/cue numbers out of range

`seq:` must be 1..20. `cue:` must be 1..20. Zero means "no change"
and anything outside the range is silently clamped.

Stock sequence assignments are listed in
[dcss-format.md](dcss-format.md#cue-values-reference).

## "Cues fire at the wrong times"

### Turn on timing tracing

In `cutscene.cpp` near the top, flip:

```cpp
#define DCSS_TIMING_TRACE 1
```

Rebuild. Each cue fire now logs:

```
[DcssTiming] [myscene] cue #2 expected=10.000s video=10.033s wall=10.041s videoDrift=+0.033s wallDrift=+0.041s
```

Interpret:

| Column | Meaning |
|---|---|
| `expected` | DCSS timestamp. |
| `video` | Video's intrinsic time (decoded-frame clock). |
| `wall` | System time since playback started. |
| `videoDrift` | `video - expected`. Should be ≤ one frame at the OGV's fps. |
| `wallDrift` | `wall - expected`. Normally 5-20 ms more than `videoDrift`. |

### What drift values mean

- **videoDrift ≤ 1 frame**: Correct. The cue fired on the first frame
  at or after the DCSS timestamp. This is the theoretical best.

- **videoDrift negative**: Shouldn't happen. If it does, the video
  clock went backwards — file a bug.

- **videoDrift growing linearly with cue index**: Your DCSS
  timestamps drift relative to the actual video. Re-check the video's
  actual content timing in a standalone player and correct the DCSS.

- **videoDrift stays small but wallDrift grows**: Game is hitching and
  falling behind real-time. Not a TFE bug — look at why the game
  loop is slow (CPU load, GPU contention, other cutscene code).

- **`cuesFired=N/M` where N < M**: Some cues didn't fire. Usually
  because they're past the OGV's natural end, as with stock
  `logo.dcss` cue #5 at 1:59 in a 1:53 video. Either shorten the DCSS
  or encode a longer OGV.

## "Subtitles don't appear"

### Check 1: captions enabled in settings?

The captions have to be turned on in TFE's accessibility panel, or
set explicitly:

```ini
[A11y]
cutsceneCaptionsEnabled = true
```

### Check 2: is the SRT found?

Look for:

```
[SrtParser] Loaded N subtitle entries from <path>/<name>.srt
```

If missing, the SRT lookup failed. TFE tries, in order:

1. `<Subtitles>/<name>_<lang>.srt` (the remaster convention)
2. `<Subtitles>/<name>.<lang>.srt` (legacy / TFE back-compat)
3. `<Subtitles>/<name>.srt` (default)

Where `<lang>` is your `language` setting in `[A11y]` (default `en`).

If your Subtitles/ directory isn't being found, TFE also checks for
SRT files alongside the OGV itself as a fallback.

### Check 3: SRT parse errors

SRT timestamps use the same `HH:MM:SS,mmm` syntax as DCSS but
**require** both a start and an end separated by ` --> `:

```
1
00:00:01,000 --> 00:00:04,000
Subtitle text here.
```

If you see `[SrtParser] Cannot open SRT file:` — it's a path issue. If
the parse silently produces zero entries, the format is off.

## "The video looks wrong (colors, stretching, black bars)"

### Colors shifted (green/purple/overly saturated)

YUV↔RGB conversion issue, usually from an unusual pixel format. Force
`yuv420p` on re-encode:

```sh
ffmpeg -i in.mp4 -pix_fmt yuv420p -c:v libtheora -q:v 7 ... out.ogv
```

### Video stretched or squashed

TFE letterboxes based on the OGV's `pic_width` / `pic_height` headers.
If those don't match the intended aspect ratio, re-encode with an
explicit scale:

```sh
ffmpeg -i in.mp4 -vf "scale=1280:720,setsar=1" -c:v libtheora ... out.ogv
```

### Black bars too large

Expected: TFE pillarboxes/letterboxes to preserve aspect ratio in any
window size. If the game is running at a 16:9 resolution but your OGV
is 4:3, you'll see side bars. Fix by either:

- Re-encoding the OGV at 16:9 (crop or pad the source as appropriate)
- Setting the game window's aspect ratio to match the OGV

## "Performance is bad during cutscenes"

The OGV decoder is single-threaded. Theora at 1920×1080 and high
quality can briefly exceed one frame of work on slower CPUs,
particularly on keyframes.

Try:

- Reducing `-q:v` (6 or lower encodes with smaller motion-compensation
  residuals, faster to decode).
- Reducing resolution (`-vf "scale=1280:720"`).
- Increasing GOP size (fewer keyframes): `-g 250` (default is around
  64 for libtheora).

## "I changed settings.ini but TFE ignored them"

Two files exist:

- `%USERPROFILE%\Documents\TheForceEngine\settings.ini`
- `%USERPROFILE%\OneDrive\Documents\TheForceEngine\settings.ini`
  (when Documents is OneDrive-redirected)

TFE uses whichever resolves from `SHGetFolderPath(CSIDL_MYDOCUMENTS)`.
The log line `[Paths] User Documents: …` shows the one TFE actually
reads. Edit *that* file.

TFE rewrites `settings.ini` on exit, so uncommitted edits made while
TFE is running get overwritten. Always edit with TFE closed, or use
TFE's in-game settings UI.

## Still stuck?

Attach:

1. The full `the_force_engine_log.txt` from a run where the problem
   occurred.
2. Your DCSS file.
3. Your `cutscene.lst` modifications (if any).
4. Your `settings.ini`.
5. Output of `ffprobe -v error -show_streams yourfile.ogv`.

…to a GitHub issue at
[github.com/luciusDXL/TheForceEngine](https://github.com/luciusDXL/TheForceEngine).
