# Architecture: how remastered cutscenes work in TFE

This doc traces the pipeline from "game wants to play scene N" to
"pixels on screen + MIDI cues firing." It's aimed at anyone reading or
changing the code, and at modders who want to understand *why* the
format is shaped the way it is so they can push it further.

## High-level flow

```
  game code                 cutscene.lst                     movies/*.ogv
      │                         │                                 │
      │ cutscene_play(id)       │ scene → {id, scene, nextId,     │
      ▼                         │  music, volume, speed}          │
  +───────────────────────+     │                                 │
  │  cutscene.cpp         │◀────┘                                 │
  │  - find scene by id   │                                       │
  │  - try OGV path first │                                       │
  +───────────────────────+                                       │
              │                                                   │
              ├─── found OGV for scene? ─── yes ──┐                │
              │                                  ▼                 │
              │                       +────────────────────+      │
              │                       │ tryPlayOgvCutscene │      │
              │                       │ - open OGV ────────┼──────┘
              │                       │ - load DCSS script │      │
              │                       │ - load SRT subs    │◀──── cutscene_scripts/*.dcss
              │                       │ - reset iMuse      │◀──── Subtitles/*.srt
              │                       +────────────────────+
              │                                  │
              │                                  ▼
              │                       +────────────────────+
              │                       │ ogvCutscene_update │
              │                       │   per frame:       │
              │                       │   - decode frame   │──▶ TFE_OgvPlayer
              │                       │   - dispatch cues  │──▶ lmusic_setSequence/setCuePoint
              │                       │   - update caption │──▶ TFE_A11Y
              │                       +────────────────────+
              │
              └── no OGV, or feature off ── fall back to ──▶ cutscenePlayer (LFD FILM path, unchanged)
```

## Data sources

The remaster doesn't invent a new catalog format. It keeps using the
original `CUTSCENE.LST` shipped inside `dark.gob`, then adds two
per-scene sidecar files.

### 1. `cutscene.lst` — the scene catalog

Lives inside `dark.gob`. One entry per scene, same format as the DOS
original:

```
<id>: <archive.lfd> <scene> <speed> <next_id> <skip_id> <music_seq> <volume>
```

Relevant fields for the remastered path:

| Field | What it does in the OGV path |
|---|---|
| `id` | Which scene we're asked to play. |
| `scene` | **Base name for all remastered files**: `<scene>.ogv`, `<scene>.dcss`, `<scene>.srt`. |
| `next_id` | Not consumed by the OGV path (see "Chain behavior" below). |
| `music_seq` | **Fallback-only**: if no DCSS script is present, `lmusic_setSequence(music_seq)` fires once at playback start. |
| `volume` | Not consumed by the OGV path (DCSS's `musicvol:` directive takes over). |
| `speed`, `skip_id` | Ignored in the OGV path. |

The full stock catalog is extracted at
[Appendix: stock cutscene.lst](#appendix-stock-cutscenelst).

### 2. `<scene>.ogv` — the video

Theora video + Vorbis audio in an Ogg container. See
[video-conversion.md](video-conversion.md) for encoding details.

Locale variants are supported: `<scene>_<lang>.ogv` (e.g.
`logo_de.ogv`) is preferred over `<scene>.ogv` when the A11Y language
setting matches. The stock remaster only localizes `logo` because it
has baked-in credits text.

### 3. `<scene>.dcss` — the timing script

Small SRT-like text file that tells TFE when to change MIDI sequences,
fire cue points, and override music volume. Example (`arcfly.dcss`):

```
1
00:00:00,327
seq: 5
cue: 1

2
00:00:06,213
cue: 2

3
00:00:45,204
cue: 3
```

Each block is `<index> / <HH:MM:SS,mmm timestamp> / <directive lines>`.
Full reference in [dcss-format.md](dcss-format.md).

### 4. `<scene>.srt` / `<scene>_<lang>.srt` — subtitles (optional)

Standard SubRip format. Only shown when the player has
**"Closed captions for cutscenes"** enabled in TFE's accessibility
settings.

## Path resolution

`remasterCutscenes.cpp` finds the remaster's data directory at init
time. It tries, in order:

1. **Custom path** from `df_remasterCutscenesPath` in `settings.ini`.
   If set, must point at the `movies/` directory itself.
2. **Remaster docs path** (`PATH_REMASTER_DOCS`) if defined by the
   platform.
3. **Source path** for Dark Forces (`sourcePath` in `settings.ini`'s
   `[Dark_Forces]` section). Checks for a `movies/` or `Cutscenes/`
   subdirectory.
4. **Windows Steam registry** (retail + TM editions) and GOG.
5. **TFE program directory**.

Whichever wins, that path becomes `s_videoBasePath`. From there:

- **`cutscene_scripts/`** is looked up first at the *parent* of the
  video path (`<remaster_root>/cutscene_scripts/`, matching how
  `DarkEX.kpf` lays it out), then as a sibling of the videos.
- **`Subtitles/`** is looked up as a child of the video path, with a
  fallback to loose `.srt` files alongside the videos.

### File name resolution

Given a `CutsceneState`, paths are built from **`scene->scene`**
(lowercased), not the archive name. This matches the remaster's own
behavior. The archive name (`ARCFLY.LFD` → `arcfly`) is a fallback for
edge cases where `scene` is empty.

For a scene with `scene = "arcfly"` and the player's language = `"de"`:

```
OGV:       movies/arcfly_de.ogv      → fall back → movies/arcfly.ogv
DCSS:      cutscene_scripts/arcfly.dcss
Subtitles: Subtitles/arcfly_de.srt   → fall back → Subtitles/arcfly.de.srt
                                     → fall back → Subtitles/arcfly.srt
```

## The cue dispatch loop

Inside `ogvCutscene_update()` — called once per game frame while an
OGV is playing:

1. Check for ESC/Enter/Space (outside Alt+Enter) → teardown and return.
2. `TFE_OgvPlayer::update()` — decodes packets, advances video time,
   renders the current YUV frame as a fullscreen GPU quad.
3. `ogvCutscene_dispatchCues()` — walks forward through the sorted
   DCSS entries, firing every one whose `timeMs` is ≤ the video's
   intrinsic playback time:
   - `seq` > 0 → `lmusic_setSequence(seq)`
   - `cue` > 0 → `lmusic_setCuePoint(cue)`
   - `musicVol` > 0 → scales MIDI volume by `vol / 100`
4. `ogvCutscene_updateCaptions()` — finds the active SRT entry for the
   current time and hands it to TFE's caption system.

### Why video time, not wall-clock time?

`TFE_OgvPlayer` exposes two clocks:

- `getPlaybackTime()` — seconds since `open()`, from the system timer.
- `getVideoTime()` — internal timeline advanced by `1/fps` per decoded,
  presented frame.

Cue dispatch uses `getVideoTime()`. If the game hitches — a stutter,
an asset load, GC, whatever — the wall-clock races ahead but the video
doesn't. Dispatching against wall-clock would fire music cues *before*
the frame they're meant to accompany. Using the intrinsic video clock
keeps the two locked.

Measured drift on a full 1:53 `logo.ogv` playback: 0–33 ms (≤1 frame
at 30fps), no accumulation.

## Chain behavior

The LFD FILM path plays scene 10 → 20 → 30 → 40 → 41 internally via
`nextId` before returning control. The OGV path does **not** chain:
when one OGV ends, control returns to the game's outer loop.

This matches what the remaster does in practice: each OGV is
self-contained and covers whatever the original LFD chain did visually.
For example, the remaster's `logo.ogv` (~1:53) contains the logo, Star
Wars crawl, text crawl, and closing frames all baked into a single
video, even though the LFD chain spans 5 separate scenes.

**Implication for modders**: if your mod adds scenes 500 → 501 → 502
all of which need cutscenes, either:

- Bake them all into **one** OGV at scene 500 (and give 501/502 trivial
  `nextId` paths that skip through quickly), or
- Ship three separate OGVs, one per scene, and let the game's outer
  loop cycle through them the way it does for the remaster's scene
  transitions.

## Music integration

The MIDI layer (`lmusic.{cpp,h}`) is shared across LFD and OGV paths.
It loads its sequence/cue catalog from `cutmuse.txt` (in `dark.gob`),
which the original DOS game used. The DCSS script's `seq:` and `cue:`
values are indices into those same tables.

On OGV cutscene startup, `lmusic_setSequence(0)` is issued before the
first DCSS entry fires, to match the remaster's reset behavior. On
teardown, `lmusic_setSequence(0)` stops all audio.

## What happens when it's all disabled

`ENABLE_OGV_CUTSCENES` is a compile-time flag. When unset, the OGV code
is excluded entirely — the engine plays only the original LFD FILM
cutscenes. The flag is on by default in the Windows vcxproj; CMake
exposes it as an option behind `theora`/`ogg`/`vorbis` availability.

## Appendix: stock `cutscene.lst`

The remaster's `dark.gob` ships this file unmodified from the DOS
original. Non-trivial entries, annotated with which have OGVs in the
stock remaster:

```
# id   archive      scene        speed next skip seq vol   hasOGV?
10:    logo.lfd     logo          10   20   0    1   110    YES
20:    swlogo.lfd   swlogo        10   30   0    0   110    no (covered by logo.ogv)
30:    ftextcra.lfd ftextcra      10   40   0    0   110    no (covered by logo.ogv)
40:    1e.lfd       1e            10   41   0    0   110    no
41:    darklogo.lfd darklogo      7    0    0    0   110    no
200:   kflyby.lfd   kflyby        10   209  0    2   80     YES
500:   gromas1.lfd  gromas1       10   0    0    3   100    YES
550:   gromasx.lfd  gromasx       8    0    0    4   100    YES
600:   arcfly.lfd   arcfly        6    605  0    5   90     YES
800:   rob1.lfd     rob1          10   0    0    6   100    YES
850:   robotx.lfd   robotx        9    0    0    7   100    YES
1000:  jabba1.lfd   jabba1        10   1010 0    8   100    YES
1050:  jabescp.lfd  jabescp       10   0    0    9   100    YES
1400:  cargo1.lfd   cargo1        10   1410 0    10  100    YES
1450:  exp1xx.lfd   exp1xx        8    1451 0    11  110    YES
1500:  fullcred.lfd fullcred      7    0    0    12  110    YES
```

Scenes 20, 30, 40, 41 have LFDs but no OGV — their visual content is
baked into `logo.ogv`. Same pattern holds for scenes like 209/210/…240
(covered by `kflyby.ogv`).
