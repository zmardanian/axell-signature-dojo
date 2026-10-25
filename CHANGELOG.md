# Changelog

## 2.0.1
- **Voice Profile, small noisy rooms:** when the room is small and the background sits less than 35 dB under the voice,
  De-Reverb is set to Clean (95 %) - measured on home recordings, where 95-100 % helped and lower settings left the
  room audible (DESIGN D21).
- **Batch Render on the terminal:** `--render <chain.RfxChain> <out folder> <take>...` renders every take through a
  saved chain to 24-bit WAV (never replacing a file) and prints loudness and true peak; a missing chain is reported.
- `--profile` takes `--reduction <%>` (De-Reverb) and `--gate-range <dB>` (Gate) to override the tuned values.
- Tests no longer name personal takes (personal lists are read from local files outside the repository).

## 2.0.0
- **Open in REAPER**: one click from a Voice Profile (or any chain) to a new REAPER project with a vocal track that
  holds the tuned chain, armed on input 1 with monitoring off. Projects go to ~/Documents/Axell Signature and never
  replace a file.
- **Step-through listening**: hear the take as recorded and after each plugin of the chain; switch while it plays, the
  position stays.
- **Reference Match**: choose a vocal whose tone you love; the Penta EQ step gets up to six bells fitted to the
  difference between what Penta receives and the reference (shape only, never loudness), and the remaining difference
  is measured on the rendered take. Amount 100 / 75 / 50 %.
- **Confidence**: every Voice Profile measurement says how far to trust it on this take (short, clipped, processed,
  few esses, a noisy room, an untested sample rate), on its tile and in the notes.
- **Tools**:
  - Batch Render: many takes through one saved chain (yours or a template), 24-bit WAV with loudness and true peak.
  - Streaming Preview: a master's loudness, true peak and range, what Spotify, Apple Music, YouTube, Tidal and
    Amazon Music do to its level, and playback as each service plays it.
  - Backup & Move: presets, Voice Profiles and REAPER chains to any folder with a SHA-256 manifest; restore copies
    only intact files and never replaces one.
- **Reliability** (an outside code review): a malformed archive entry can no longer crash the reader; a library check
  that cannot run stops the install; updates and restores swap the plugin atomically, so it is never missing from
  ~/.vst3.
- CLI: `--profile <take> [--reference <vocal>] [--name N] [--save [--open]]`.

## 1.0.0
First release: Plugins (checksum-verified install, update, backup, restore), Voice Profile, nine REAPER chain
templates, Workstation checks.
