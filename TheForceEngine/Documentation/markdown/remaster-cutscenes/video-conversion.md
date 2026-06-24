# Converting video to TFE's OGV format

TFE uses the **Ogg container** with **Theora** video and **Vorbis**
audio. This is the same combination the Dark Forces Remaster ships,
and it's what TFE's `ogvPlayer` decodes.

This guide shows a verified command line for converting MP4 (or
anything ffmpeg can read) into a `.ogv` that TFE plays correctly.

## Prerequisites

- **ffmpeg** built with `--enable-libtheora --enable-libvorbis`.
  The standard Windows release builds from
  [ffmpeg.org](https://ffmpeg.org/download.html) include both.
  Verify with:
  ```sh
  ffmpeg -encoders 2>&1 | grep -iE "libtheora|libvorbis"
  ```
  You should see both listed.

Nothing else. No Theora-specific tools required; ffmpeg handles it.

## The one-liner

```sh
ffmpeg -i input.mp4 \
  -c:v libtheora -q:v 7 \
  -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
  -f ogg output.ogv
```

### What each flag does

| Flag | Purpose |
|---|---|
| `-c:v libtheora` | Video codec: Theora (what TFE decodes). |
| `-q:v 7` | Video quality 0–10 (higher = better, larger). 7 is a good default. The stock remaster targets similar quality. |
| `-c:a libvorbis` | Audio codec: Vorbis. |
| `-q:a 4` | Audio quality 0–10 (higher = better). 4 ≈ 128 kbps stereo. |
| `-ar 44100` | Resample audio to 44.1 kHz (what TFE's mixer targets). |
| `-ac 2` | Force stereo output. Mono sources get upmixed; 5.1 gets downmixed. |
| `-f ogg` | Force Ogg container. Not strictly needed since the `.ogv` extension implies it, but explicit is safer. |

## Verified result

This exact command was tested by converting a 30-second 640×400 MPEG-4
test clip to OGV and playing it through TFE with a hand-written DCSS:

```
[OgvPlayer] Opened OGV: 640x400, 20.00 fps, with audio (rate=44100, channels=2)
[DcssParser] Loaded 3 cue entries from test.dcss
[Cutscene] Playing remastered OGV cutscene for scene 10 ('logo').
[DcssTiming] cue #1 expected=0.000s video=0.000s videoDrift=+0.000s
[DcssTiming] cue #2 expected=10.000s video=10.000s videoDrift=+0.000s
[DcssTiming] cue #3 expected=20.000s video=20.000s videoDrift=+0.000s
[DcssTiming] END videoDuration=29.950s cuesFired=3/3
```

All three cues fired with zero drift against the DCSS timestamps.

## Choosing quality and file size

Theora's quality scale is non-linear. Rough rule of thumb:

| `-q:v` | Use case | Bitrate at 640×400 20fps |
|---|---|---|
| 4 | Small preview / low-priority content | ~300 kbps |
| 7 | **Recommended** for game cutscenes | ~700 kbps |
| 9 | Near-lossless, for pristine masters | ~2 Mbps |
| 10 | Use if source is critical; big files | ~4+ Mbps |

For reference, the stock remaster `logo.ogv` is ~180 MB for 1:53, which
is ~13 Mbps — suggesting they used a very high quality setting (8-10)
on a high-resolution source (the video is 1280×800 or similar).

Two-pass encoding is available if you need to hit a specific bitrate
budget:

```sh
# Pass 1
ffmpeg -y -i input.mp4 -c:v libtheora -b:v 1500k -pass 1 -an -f ogg /dev/null
# Pass 2
ffmpeg -i input.mp4 \
  -c:v libtheora -b:v 1500k -pass 2 \
  -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
  output.ogv
```

## Aspect ratio and resolution

TFE's `ogvPlayer` **letterboxes** the video into whatever window
resolution the game is running at, preserving the video's aspect ratio.
You do not need to encode at 320×200.

**Recommendations:**

- Keep the **source aspect ratio**. If your source is 16:9, encode at
  16:9; TFE will pillarbox if the game window is narrower.
- **Even-numbered dimensions.** Theora requires both width and height
  to be multiples of 16 for best quality (2 at minimum). If your
  source is odd, ffmpeg will silently pad or crop.
- **Scale if the source is huge.** 1920×1080 decodes fine on modern
  hardware, but the file size balloons. 1280×720 is a good sweet spot
  for fan content.

Forcing a specific target resolution:

```sh
ffmpeg -i input.mp4 \
  -vf "scale=1280:720" \
  -c:v libtheora -q:v 7 \
  -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
  -f ogg output.ogv
```

## Framerate

TFE plays OGVs at whatever framerate they're encoded at. The decoder
respects the file's `fps_numerator/denominator` header and the
dispatcher locks cue timing to decoded frames.

**Don't force 60fps** on a source that's natively 24 or 30 — you'll
triple the file size for no visible benefit and the cue dispatch will
still only resolve to frame boundaries of whatever the actual fps is.

If you *must* change the source framerate:

```sh
ffmpeg -i input.mp4 -vf "fps=30" ...
```

## Audio

### Mixing

TFE's audio system mixes OGV audio in at the **`cutsceneSoundFxVolume
* masterVolume`** level at the same time the MIDI music plays at
**`cutsceneMusicVolume * masterVolume * <dcss musicvol>`**. Both volume
sliders are in TFE's settings UI.

If your cutscene has dialogue, leave the music bed *quiet* in the OGV
audio (or absent entirely) and let the DCSS-dispatched MIDI be the
music. That's what the remaster does.

### Format

Vorbis can go up to 48 kHz / 8 channels, but TFE's player:

- Resamples to 44.1 kHz internally.
- Downmixes to stereo.

Encoding directly to 44.1 kHz stereo saves the decoder some work and
avoids subtle resample artifacts.

### Silent cutscenes

Some of the stock `exp1xx`, `gromasx`, etc. files effectively have no
audio — the MIDI score provided by DCSS is all. If your cutscene is
music-only, pass `-an` to skip audio entirely:

```sh
ffmpeg -i input.mp4 -c:v libtheora -q:v 7 -an -f ogg output.ogv
```

TFE's player handles no-audio OGVs correctly.

## Batch conversion

If you've got a stack of MP4s to convert:

```sh
for f in *.mp4; do
  ffmpeg -i "$f" \
    -c:v libtheora -q:v 7 \
    -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
    -f ogg "${f%.mp4}.ogv"
done
```

(Bash; Windows `cmd` equivalent uses `for %f in (*.mp4) do …`.)

## Checking your result

### File-level sanity

```sh
ffprobe -v error -show_streams output.ogv
```

You should see one `codec_name=theora` video stream and (optionally)
one `codec_name=vorbis` audio stream.

### Running it through TFE

1. Drop your `output.ogv` into `<remaster_root>/movies/` (or your
   custom cutscene directory).
2. Write a minimal DCSS at
   `<remaster_root>/cutscene_scripts/<scene>.ogv`:
   ```
   1
   00:00:00,000
   seq: 1
   cue: 1
   ```
3. Add or modify an entry in `cutscene.lst` to point `scene` at your
   file (see [modding-guide.md](modding-guide.md)).
4. Start TFE with `--game dark`, trigger the cutscene, and watch
   `~/Documents/TheForceEngine/the_force_engine_log.txt` for:
   ```
   [OgvPlayer] Opened OGV: WxH, FPS fps, with audio (rate=..., channels=...)
   [Cutscene] Playing remastered OGV cutscene for scene N ('<scene>').
   ```

### Diagnosing drift or sync issues

Edit `cutscene.cpp` and flip `DCSS_TIMING_TRACE` from `0` to `1`,
rebuild, and re-run. The log will show per-cue drift in seconds:

```
[DcssTiming] [myscene] cue #2 expected=10.000s video=10.000s wall=10.010s videoDrift=+0.000s wallDrift=+0.010s
```

`videoDrift` should be ≤ one frame. If it's much larger, something
is wrong — usually a hand-written DCSS timestamp that doesn't actually
exist in the video. See [troubleshooting.md](troubleshooting.md).

## Common mistakes

**"My OGV looks purple/green/corrupted."**
Theora only supports `yuv420p` / `yuv422p` / `yuv444p` pixel formats.
If ffmpeg got a different input format, it should convert
automatically, but passing `-pix_fmt yuv420p` explicitly is safe:

```sh
ffmpeg -i input.mp4 -pix_fmt yuv420p -c:v libtheora -q:v 7 ... output.ogv
```

**"Audio is garbled/slow/fast."**
Check that you passed `-ar 44100`. Without it, ffmpeg keeps the
source's sample rate (say 48000 Hz) and TFE's resampler has to do more
work, which in rare cases produces pitch drift.

**"TFE says `No Theora stream found`."**
Your output file probably isn't actually Theora. Some ffmpeg builds
fall back silently to another codec when `libtheora` isn't compiled
in. Re-check `ffmpeg -encoders | grep theora` — you need the one with
`V..... libtheora` (uppercase V means video encoder).

**"My video plays but at the wrong resolution / stretched."**
TFE letterboxes to preserve aspect ratio based on the OGV's
`pic_width` / `pic_height` headers. If those got set incorrectly
during encoding (rare, but happens with cropped inputs), re-encode
with `-vf "scale=W:H,setsar=1"` to normalize.

**"Huge file sizes."**
Drop `-q:v` from 7 to 5 for a 30-40% size reduction with barely
noticeable quality loss on typical game cutscene content. Or move to
two-pass bitrate encoding.

## What about other codecs?

TFE only reads **Theora** video and **Vorbis** audio, in an **Ogg**
container. MP4/H.264, MKV/AV1, WebM/VP9 — all unreadable by TFE's
player. No plans to add other codecs: Theora is sufficient, fully
free/open (no patent licensing), and matches what the remaster ships.
