# Remastered Cutscenes in The Force Engine

TFE can play the Dark Forces Remaster's OGV cutscenes in place of the
original LFD FILM animations, with the original iMuse MIDI soundtrack
kept in perfect sync. The same system is open to modders: drop in your
own OGV + a tiny text script and TFE will play it.

This documentation covers:

| Doc | Who it's for |
|---|---|
| [architecture.md](architecture.md) | Anyone who wants to understand how the pipeline works end to end — what files get loaded from where, how MIDI cues dispatch against the video clock, how TFE's path resolution works. |
| [modding-guide.md](modding-guide.md) | Modders who want to **add or replace** a cutscene. Step-by-step walkthrough from an MP4 source to a playable scene. |
| [dcss-format.md](dcss-format.md) | Complete reference for the `.dcss` script format: every directive, every quirk, annotated examples from the stock remaster data. |
| [video-conversion.md](video-conversion.md) | Converting MP4/MKV/etc. to the OGV format TFE expects, with ffmpeg command lines that have been verified to produce working output. |
| [troubleshooting.md](troubleshooting.md) | When the cutscene doesn't play, the music is wrong, or subtitles don't show — start here. |

## Quick start for modders

1. Convert your video to OGV:
   ```sh
   ffmpeg -i mycutscene.mp4 -c:v libtheora -q:v 7 \
          -c:a libvorbis -q:a 4 -ar 44100 -ac 2 \
          -f ogg mycutscene.ogv
   ```
2. Write a `mycutscene.dcss` next to it describing the music cue points
   (see [dcss-format.md](dcss-format.md)).
3. Point TFE at the directory holding them (`df_remasterCutscenesPath`
   in `settings.ini`, or drop them in the Steam remaster's folder).
4. Add an entry to `cutscene.lst` so the game knows when to play it.

That's it. No recompile, no plugins.

## Quick start for players

If you own the **Star Wars: Dark Forces Remaster** on Steam or GOG, TFE
will auto-detect it. Nothing to configure; the intro will play using the
remaster's HD video the next time you start Dark Forces.

To turn it off, open TFE's settings UI (or `settings.ini`) and set
`df_enableRemasterCutscenes = false`.

## When *not* to use this

- Playing the original DOS Dark Forces? The LFD FILM path is still the
  default and covers every cutscene.
- On a system without the remaster install and no modded content?
  Nothing changes — the original LFD cutscenes play as before.

The remaster OGV path is an **opt-in overlay**, not a replacement.

## Source layout

The code lives entirely under `TFE_DarkForces/`:

```
TFE_DarkForces/Landru/cutscene.cpp          # Dispatch: which path to use, cue firing
TFE_DarkForces/Remaster/remasterCutscenes.* # Path detection, file resolution
TFE_DarkForces/Remaster/ogvPlayer.*         # Ogg/Theora/Vorbis decode + YUV render
TFE_DarkForces/Remaster/dcssParser.*        # .dcss script parser
TFE_DarkForces/Remaster/srtParser.*         # .srt subtitle parser
```

Everything behind the `ENABLE_OGV_CUTSCENES` preprocessor flag. Builds
without the flag compile to the original LFD-only path exactly.

## License note on your cutscene assets

If you ship a mod containing remastered OGV files *from* the Dark Forces
Remaster, you are redistributing Disney/LucasArts content and that is
your problem to sort out with them. **Cutscenes you produce yourself
from scratch** (for a fan campaign, a new mission pack, etc.) belong to
you and you can ship them however you like — TFE has no claim.
