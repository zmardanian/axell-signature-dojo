# Changelog

## 3.0.0
- **The suite at 3.0, connected:** Axell Duo Compressor 3.0 (VOICE HOLD), Gate 3.0 (a second voice network trained on
  111 voices), De-Reverb 3.0 (LIVE: zero latency), Penta EQ 3.0 (ESS detector source), Master 3.0 (VOICE HOLD for a
  vocal bus) and Velvet 3.0.1 - shared through axell-core (the voice decision, the minimum-phase method, the look).
- **Tracking chain with De-Reverb:** every Voice Profile's tracking chain (and the "Tracking (Live)" template) now
  includes De-Reverb in LIVE mode - no latency, so you record hearing yourself without the room (D25).
- **Profiles tuned on the 3.0 plugins:** Duo's VOICE HOLD and the other 3.0 behaviours are what the Dojo measures and
  saves (D26).
- **Axell's Dojo title:** "Axell's Dojo" in the brush face with the Dojo's seal 道 (D27).
- Verified: core tests 64/64, platform tests 149/149 (sandboxed; the tracking chain with De-Reverb LIVE: 3.4 ms), REAPER
  loads all 14 chains with every value (tools/reaper-verify.sh).

## 2.1.0
- **Voice Profile listens with Axell Velvet 3.0's detector** (trained on 111 voices instead of 3): better at the
  rarer sibilants and at accents - Romanian ce / ci, ge / gi, j, ș - when it measures where your esses sit (D22).
- **Velvet's AMOUNT tuned on your take:** the gentlest setting that brings your esses to where natural speech has them
  (measured on 111 voices), instead of a fixed 50 %; the profile says how loud your esses were and where they land (D23).
- **Tracking chain:** every Voice Profile also saves "<name> - Tracking Chain" for recording while you hear yourself
  through the plugins - Velvet's zero-latency de-esser, Gate at LIVE speed, no De-Reverb - with its measured latency
  (about 3 ms on the artist's setup). A ready-made "Tracking (Live)" chain on the Chains page too (D24).
- Verified: core 64/64, platform 149/149, ASan/UBSan clean, REAPER chains 14/14 (the tracking chain and template included).

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
