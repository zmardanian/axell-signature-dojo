# Changelog

## 3.1.1
- **Axell Forbidden Seal** joins the suite (saturation: eight circuits, three bands, ESS GUARD): installed, updated and
  listed like the others.
- Verified: core 64/64, platform 179/179, REAPER 17/17.

## 3.1.0
- **Signature sounds: your approved chains, one click from a take.** A vocal chain and a master chain you approved by
  ear, with the level of the dry take they were tuned on. Chains page > Your signature sound > "Take at -14 LUFS..."
  (or -11): the Dojo measures the new take, matches its level to the tuning take (so the gate, the compressors and the
  de-esser work as they did on it), sets Axell Master's INPUT for the target and opens a REAPER session - the master
  chain on the master, your take and the vocal chain on a track. Measured on three other takes of the artist (16.9 to
  20.9 LUFS dry): all land at -14.0 +-0.05 LUFS, true peak -1.0 dBTP. Terminal: `--signature <name> --take <file>
  --target <LUFS> [--open]` (D29).
- **Update saved chains** (Chains page; terminal `--update-chains [files]`): chains saved with older plugin versions
  gain what the plugins have learned since - De-Reverb's TRUE TONE, Gate's PITCH HOLD, and Duo Compressor's /
  Master's VOICE HOLD + ESS HOLD in vocal chains - set as the plugins' own presets set them; every value the saved
  chain holds stays as it is. Template chains are rebuilt from the current presets; the old files are kept in the
  Dojo's chain-backups folder; a chain with nothing to gain is not touched. On the artist's Amber profile chain over 28
  of their takes: esses 0.5 dB smoother, pauses 0.5 dB quieter, the fundamental 0.9 dB under the dry take instead of
  2.8 dB thin, UTMOS 2.267 -> 2.279 (better on 19 takes, 3 equal, 6 lower by at most 0.085) (D30).
- **Axell Space** joins the suite: installed, updated and listed like the others, and two new send chains (Vocal FX):
  "Vocal Plate (send)" and "Delay Throw (send)".
- Verified: core 64/64, platform 175/175, ASan/UBSan clean, REAPER 17/17 (every template, the profile and tracking chains, both Space send chains and a signature session: master FX, Axell Master's INPUT and the take's gain match as set).

## 3.0.2
- **The seal icon, redrawn:** 3.0.1's dark lacquer tile was lit only in its centre, so on a dark desktop the icon
  read as a dark blob with black corners. Now the classic calligraphy page: warm washi filling the whole tile, evenly
  lit to the edges, a black sumi-ink ensō brushed from 72 bristles (wet landing, dry split lift), and the vermilion
  道 seal stamped inside it; a fine gold frame. Clear on light and dark desktops, legible down to 32 px (D28).

## 3.0.1
- **New app icon: the Dojo's seal.** A vermilion hanko seal with 道 ("the way") carved in the brush face, stamped
  askew on a torn piece of washi, inside a dry-brush gold ensō on black lacquer. The installer now puts it in every
  icon size (16-512 px), so an older small icon can no longer show on the desktop or in the menu (D28).

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
