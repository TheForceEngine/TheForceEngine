# Modder's guide: adding or replacing a cutscene

This walks you end-to-end through adding a new cutscene to Dark
Forces running in TFE, or replacing an existing one. The process is:

1. Convert your source video → `.ogv`.
2. Write a DCSS script for music cues.
3. Optionally write SRT subtitles.
4. Decide whether you're **replacing** a stock scene or **adding** a
   new one.
5. Test.

No recompile, no plugins. Everything lives in files TFE reads at
runtime.

## Before you start

You need:

- A working TFE installation.
- **ffmpeg** with libtheora + libvorbis support (see
  [video-conversion.md](video-conversion.md)).
- Your video source (MP4, MKV, whatever ffmpeg can read).
- A text editor.

## Part 1: the video

Convert to OGV:

```sh
ffmpeg -i mycutscene.mp4 \
  -c:v libtheora -q:v 7 \
  -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
  -f ogg mycutscene.ogv
```

Tweak `-q:v` between 5 (smaller) and 9 (larger / higher quality).

Verify it plays in a standalone player like VLC before moving on.

## Part 2: the DCSS script

Create `mycutscene.dcss` next to your video. The simplest possible
script just starts a MIDI sequence when the video opens:

```
1
00:00:00,000
seq: 1
cue: 1
```

This fires iMuse sequence 1 and cue point 1 at t=0. Both 1..20 map
into the sequence/cue tables defined by `cutmuse.txt` (packaged in
`dark.gob`).

For music transitions during the cutscene, add more entries:

```
1
00:00:00,000
seq: 5
cue: 1

2
00:00:12,500
cue: 2

3
00:00:45,200
cue: 3
```

See [dcss-format.md](dcss-format.md) for the complete syntax reference,
including the `musicvol:` directive, comments, and timestamp
tolerances.

## Part 3: subtitles (optional)

TFE reads standard SubRip `.srt` files. If your cutscene has spoken
dialogue, ship an SRT so players with captions enabled can read along.

Naming:

| File | Used when |
|---|---|
| `mycutscene.srt` | Default / English — always tried last as a fallback. |
| `mycutscene_de.srt` | German (`language = de` in settings). |
| `mycutscene_fr.srt` | French. |
| `mycutscene_es.srt` | Spanish. |
| `mycutscene_it.srt` | Italian. |
| `mycutscene_<any>.srt` | Any ISO-639-1 code. |

Example content:

```
1
00:00:00,500 --> 00:00:03,000
Attack pattern delta!

2
00:00:03,500 --> 00:00:06,500
The Empire will not stop us.
```

## Part 4: placement

TFE looks for cutscene files in this order of preference:

1. **Custom path** in `settings.ini` (`df_remasterCutscenesPath`).
   Must point to a directory that contains or sits next to `movies/`.
2. **Remaster docs path** (platform-specific).
3. **Source data path** — your `sourcePath` for Dark Forces, with a
   `movies/` subdirectory.
4. **Windows Steam/GOG registry** auto-detection.
5. **TFE program directory**.

For modding, **use option 1 or option 3**.

### Directory layout (recommended)

```
<your_mod_dir>/
    movies/
        mycutscene.ogv
        mycutscene_de.ogv          (optional localized variant)
    cutscene_scripts/
        mycutscene.dcss
    Subtitles/                      (or loose in movies/ — TFE checks both)
        mycutscene.srt
        mycutscene_de.srt
```

Then in TFE's `settings.ini`, set:

```ini
[Dark_Forces]
df_enableRemasterCutscenes = true
df_remasterCutscenesPath = "C:/path/to/your_mod_dir/movies/"
```

> **Note**: `df_remasterCutscenesPath` points at the `movies/`
> directory itself, not its parent. TFE walks back up one level to
> find the sibling `cutscene_scripts/` directory.

### Alternate layout (single-folder)

If you want to keep everything in one directory:

```
<your_mod_dir>/
    movies/
        mycutscene.ogv
        mycutscene.srt
        cutscene_scripts/
            mycutscene.dcss
```

TFE falls back to looking for `cutscene_scripts/` and subtitles
alongside the videos if it can't find them in the canonical location.

## Part 5: wiring it into the game

This is where "replacing" and "adding" diverge.

### Replacing a stock cutscene

Just name your files to match a stock scene name:

| Stock scene | Your file names |
|---|---|
| `logo` (intro) | `logo.ogv`, `logo.dcss` |
| `arcfly` (level 6 intro) | `arcfly.ogv`, `arcfly.dcss` |
| `jabba1` (Jabba scene) | `jabba1.ogv`, `jabba1.dcss` |
| ...and so on | (see [architecture.md](architecture.md) for the full list) |

TFE will pick up your files in place of the stock ones. No
`cutscene.lst` changes needed.

### Adding a new cutscene

You need to add an entry to `cutscene.lst`. In the original DOS game
and the remaster, `cutscene.lst` lives inside `dark.gob`. For TFE
modding, you override it by shipping a mod GOB.

Write a fresh `cutscene.lst` (plain text) with your new entry:

```
CUT 1.0

CUTS 40

# ...existing stock entries unchanged...
10: logo.lfd logo 10 20 0 1 110
20: swlogo.lfd swlogo 10 30 0 0 110
# ... etc ...

# your new entry — id 2000, scene "mycutscene":
2000: mycutscene.lfd mycutscene 10 0 0 13 100
```

Field breakdown (space-separated):

| Position | Value | Meaning |
|---|---|---|
| 1 | `2000:` | Scene ID, used by game code to request this cutscene. |
| 2 | `mycutscene.lfd` | Archive name. For OGV-only scenes with no LFD fallback, this can be a placeholder — it's only consulted if the OGV can't be found. |
| 3 | `mycutscene` | **Scene name — this is the base filename** for `.ogv` / `.dcss` / `.srt`. |
| 4 | `10` | Speed (fps of the LFD FILM; ignored for OGV path). |
| 5 | `0` | `nextId` — set to 0 for a single-cutscene chain, or to the ID of the next scene. Not used by the OGV path. |
| 6 | `0` | `skipId` — what ESC jumps to. |
| 7 | `13` | **Music sequence** (used if no DCSS script is found, as a fallback). |
| 8 | `100` | **Volume** as a percentage of base. |

Pack this into a mod GOB (see TFE's existing mod GOB documentation).

### Triggering a new cutscene

The Dark Forces game code drives cutscene playback via scripted paths
(level transitions, mission completion, etc.). To trigger your new
scene ID `2000`, you need to hook it into one of these paths. Options:

- **Level-end cutscene**: modify `s_cutsceneData` in `darkForcesMain.cpp`
  — but this requires rebuilding TFE, so it's for engine contributors,
  not runtime mods.
- **External data logic**: if TFE exposes cutscene triggering via JSON
  mod data (check the latest project docs), declaratively reference
  scene ID 2000 there.
- **Override an existing cutscene ID**: instead of adding 2000, reuse
  an existing ID (say, 550 / `gromasx`) and let it play when the game
  would normally trigger that scene.

For most mods, **replacing** an existing scene is the pragmatic path.

## Part 6: testing

1. Launch TFE: `TheForceEngine.exe --game dark`
2. Open the log at
   `%USERPROFILE%\Documents\TheForceEngine\the_force_engine_log.txt`
   (or the OneDrive redirect if Documents is synced).
3. Trigger your cutscene — for the intro, just start a new game.

### What a successful load looks like in the log

```
[Remaster] Using custom cutscene path: C:/.../your_mod_dir/movies/
[Remaster] Found cutscene scripts at: C:/.../your_mod_dir/cutscene_scripts/
[Remaster] Remaster OGV cutscene directory found.
[OgvPlayer] Opened OGV: 1280x720, 30.00 fps, with audio (rate=44100, channels=2)
[DcssParser] Loaded 3 cue entries from C:/.../cutscene_scripts/mycutscene.dcss (credits=0 openingCredits=0)
[Cutscene] Playing remastered OGV cutscene for scene 2000 ('mycutscene').
```

### Verifying cue timing

Flip `DCSS_TIMING_TRACE` from `0` to `1` near the top of
`cutscene.cpp` and rebuild TFE. With tracing on, every cue fire
produces a line like:

```
[DcssTiming] [mycutscene] cue #2 expected=10.000s video=10.000s wall=10.010s videoDrift=+0.000s wallDrift=+0.010s
```

Look for:

- `videoDrift` ≤ one frame at your video's fps (e.g. ≤ 33 ms at 30fps).
- `cuesFired=N/N` at teardown — ideally all of them.
- `videoDuration` matches your source video's actual length.

## Common "now what?" questions

### "How do I find what iMuse sequences exist?"

The sequences are defined in `cutmuse.txt` inside `dark.gob`. You can
extract it with TFE's archive tools or any LucasArts GOB reader.
Sequences 1–12 are used by stock cutscenes; sequences 13–20 are free
for mod use.

### "Can I ship my own iMuse sequences?"

Yes. Package a replacement `cutmuse.txt` in your mod GOB with entries
for your sequences. The format is straightforward; see
[architecture.md](architecture.md).

### "Can I chain multiple OGVs for one cutscene?"

Not directly in the current version of TFE. Each `cutscene_play()` call
runs one OGV to completion, then control returns to the outer game
flow. If you need a long cutscene, bake it into a single OGV.

### "Can I ship locale-specific cutscenes without new OGVs?"

Yes. The OGV is played regardless of locale; the SRT file is the
localized piece. Ship `mycutscene.srt` (English default) and
`mycutscene_de.srt`, `mycutscene_fr.srt`, etc. for other languages.

### "How do I disable remastered cutscenes without deleting my mod?"

In `settings.ini`:
```ini
[Dark_Forces]
df_enableRemasterCutscenes = false
```
TFE will then play the original LFD FILM cutscene (if one exists for
that scene).

## Full working example

The end of this doc is a complete example of a minimal mod.

**File tree:**

```
mymod/
    movies/
        intro.ogv                  (your converted video)
    cutscene_scripts/
        intro.dcss                 (text below)
    Subtitles/
        intro.srt                  (text below)
        intro_de.srt
```

**`intro.dcss`:**

```
# My new intro. Uses iMuse sequence 1 (the logo theme).
1
00:00:00,000
seq: 1
cue: 1
musicvol: 100

# Bump volume for the action beat at 25s.
2
00:00:25,000
musicvol: 115
cue: 2
```

**`intro.srt`:**

```
1
00:00:01,000 --> 00:00:04,000
Mos Eisley. It begins.

2
00:00:25,500 --> 00:00:29,000
Never tell me the odds.
```

**`settings.ini`** (TFE user config):

```ini
[Dark_Forces]
sourcePath = "C:/path/to/your/dark/forces/install"
df_enableRemasterCutscenes = true
df_remasterCutscenesPath = "C:/path/to/mymod/movies/"
```

With the scene name `intro` and your `cutscene.lst` entry wiring it to
scene ID 10 (the intro), launching a new game will play your cutscene
with synced music.

When you're stuck, start with [troubleshooting.md](troubleshooting.md).
