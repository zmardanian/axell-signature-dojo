#!/usr/bin/env bash
# Loads the Dojo's REAPER chains into a *portable copy* of REAPER and checks what REAPER shows: the plugins, their
# preset names and the tuned parameter values. The user's REAPER and its configuration are never touched: the copy
# lives in a temporary folder with its own configuration and sees only the Axell plugins.
#   tools/reaper-verify.sh <folder from DojoPlatformTests --reaper-prep> [REAPER folder, default ~/opt/REAPER]
set -euo pipefail
PREP="$(realpath "$1")"
SRC="${2:-$HOME/opt/REAPER}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/dojo-reaper-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
cp -a "$SRC" "$WORK/reaper"
R="$WORK/reaper"
mkdir -p "$WORK/vst3" "$R/Scripts"
for b in "Axell De-Reverb" "Axell Gate" "Axell Penta EQ" "Axell Duo Compressor" "Axell Velvet" "Axell Master" "Axell Space"; do
  ln -s "$HOME/.vst3/$b.vst3" "$WORK/vst3/$b.vst3"
done
# a reaper.ini next to the program makes REAPER portable: it keeps all its settings in this folder
cat > "$R/reaper.ini" <<INI
[REAPER]
vstpath=$WORK/vst3
verchk=0
splashshow=0
INI
[ -f "$HOME/.config/REAPER/reaper-reginfo2.ini" ] && cp "$HOME/.config/REAPER/reaper-reginfo2.ini" "$R/"
cat > "$R/Scripts/__startup.lua" <<'LUA'
local out_path = reaper.GetResourcePath() .. "/dojo-result.txt"
local tries = 0
local function dump()
  local tr = reaper.GetTrack(0, 0)
  if tr == nil then
    tries = tries + 1
    if tries < 400 then reaper.defer(dump) else reaper.Main_OnCommand(40004, 0) end
    return
  end
  local f = io.open(out_path, "w")
  local n = reaper.TrackFX_GetCount(tr)
  f:write("count\t" .. n .. "\n")
  f:write("arm\t" .. math.floor(reaper.GetMediaTrackInfo_Value(tr, "I_RECARM")) .. "\n")
  for i = 0, n - 1 do
    local _, name = reaper.TrackFX_GetFXName(tr, i, "")
    local _, preset = reaper.TrackFX_GetPreset(tr, i, "")
    f:write("FX\t" .. i .. "\t" .. name .. "\t" .. preset .. "\n")
    for p = 0, reaper.TrackFX_GetNumParams(tr, i) - 1 do
      local _, pn = reaper.TrackFX_GetParamName(tr, i, p, "")
      local _, pv = reaper.TrackFX_GetFormattedParamValue(tr, i, p, "")
      f:write("P\t" .. i .. "\t" .. pn .. "\t" .. pv .. "\n")
    end
  end
  -- the master track's FX (a signature session) and the first item's volume (its gain match)
  local m = reaper.GetMasterTrack(0)
  for i = 0, reaper.TrackFX_GetCount(m) - 1 do
    local _, name = reaper.TrackFX_GetFXName(m, i, "")
    local _, preset = reaper.TrackFX_GetPreset(m, i, "")
    f:write("MFX\t" .. i .. "\t" .. name .. "\t" .. preset .. "\n")
    for p = 0, reaper.TrackFX_GetNumParams(m, i) - 1 do
      local _, pn = reaper.TrackFX_GetParamName(m, i, p, "")
      local _, pv = reaper.TrackFX_GetFormattedParamValue(m, i, p, "")
      f:write("MP\t" .. i .. "\t" .. pn .. "\t" .. pv .. "\n")
    end
  end
  local it = reaper.GetTrackMediaItem(tr, 0)
  if it ~= nil then
    local v = reaper.GetMediaItemInfo_Value(it, "D_VOL")
    local tk = reaper.GetActiveTake(it)
    if tk ~= nil then v = v * reaper.GetMediaItemTakeInfo_Value(tk, "D_VOL") end
    f:write("vol\t" .. string.format("%.4f", 20 * math.log(v, 10)) .. "\n")
  end
  f:close()
  reaper.Main_OnCommand(40004, 0)   -- File: Quit
end
reaper.defer(dump)
LUA
status=0
for chain in "$PREP"/*.RfxChain "$PREP"/*.RPP; do
  [ -f "$chain" ] || continue
  name="$(basename "$chain")"; name="${name%.*}"
  proj="$WORK/$name.RPP"
  if [[ "$chain" == *.RPP ]]; then cp "$chain" "$proj"; else
  { echo '<REAPER_PROJECT 0.1 "7.80/linux-x86_64" 0'
    echo '  <TRACK'
    echo '    NAME "dojo verify"'
    echo '    <FXCHAIN'
    echo '      SHOW 0'
    echo '      LASTSEL 0'
    echo '      DOCKED 0'
    sed 's/^/      /' "$chain"
    echo '    >'
    echo '  >'
    echo '>'; } > "$proj"
  fi
  rm -f "$R/dojo-result.txt"
  timeout 120 "$R/reaper" -newinst -nosplash "$proj" >/dev/null 2>&1 || true
  if [ ! -f "$R/dojo-result.txt" ]; then echo "FAIL  $name: REAPER produced no result"; status=1; continue; fi
  if python3 - "$PREP/$name.expect" "$R/dojo-result.txt" "$name" <<'PY'
import sys
exp = [l.rstrip("\n").split("\t") for l in open(sys.argv[1]) if l.strip()]
got = [l.rstrip("\n").split("\t") for l in open(sys.argv[2]) if l.strip()]
name = sys.argv[3]
fx = {int(g[1]): (g[2], g[3]) for g in got if g[0] == "FX"}
mfx = {int(g[1]): (g[2], g[3]) for g in got if g[0] == "MFX"}
params, mparams = {}, {}
for g in got:
    if g[0] == "P": params[(int(g[1]), g[2])] = g[3]
    if g[0] == "MP": mparams[(int(g[1]), g[2])] = g[3]
bad, checked = [], 0
for e in exp:
    if e[0] == "VOL":
        vol = [g[1] for g in got if g[0] == "vol"]
        if not vol or abs(float(vol[0]) - float(e[1])) > 0.01: bad.append(f"item volume {vol} dB, expected {e[1]} dB")
        checked += 1
        continue
    if e[0] in ("MFX", "MP"):
        i = int(e[1])
        if e[0] == "MFX":
            n, preset = mfx.get(i, ("<missing>", ""))
            if e[2] not in n: bad.append(f"master FX {i}: REAPER shows '{n}', expected {e[2]}")
            if preset and e[3] and preset != e[3]: bad.append(f"master FX {i}: preset '{preset}', expected '{e[3]}'")
        else:
            v = mparams.get((i, e[2]))
            num = lambda t: float("".join(c for c in t if c in "0123456789.-+") or "nan")
            if v is None or (v.strip() != e[3].strip() and abs(num(v) - num(e[3])) >= 0.051): bad.append(f"master FX {i} '{e[2]}': REAPER shows '{v}', the Dojo set '{e[3]}'")
        checked += 1
        continue
    if e[0] == "ARM":
        armed = [g[1] for g in got if g[0] == "arm"]
        if armed != [e[1]]: bad.append(f"track armed {armed}, expected {e[1]}")
        checked += 1
        continue
    i = int(e[1])
    if e[0] == "FX":
        n, preset = fx.get(i, ("<missing>", ""))
        if e[2] not in n: bad.append(f"FX {i}: REAPER shows '{n}', expected {e[2]}")
        if preset and e[3] and preset != e[3]: bad.append(f"FX {i}: preset '{preset}', expected '{e[3]}'")
        checked += 1
    else:
        v = params.get((i, e[2]))
        num = lambda t: float("".join(c for c in t if c in "0123456789.-+") or "nan")
        ok = v is not None and (v.strip() == e[3].strip() or abs(num(v) - num(e[3])) < 0.051)
        if not ok: bad.append(f"FX {i} '{e[2]}': REAPER shows '{v}', the Dojo set '{e[3]}'")
        checked += 1
presets = ", ".join(fx[i][1] or "-" for i in sorted(fx))
print(("PASS" if not bad else "FAIL") + f"  {name}: {len(fx)} plugins" + (f" + {len(mfx)} on the master" if mfx else "") + f", {checked} values checked (presets: {presets})" + ("" if not bad else "\n      " + "\n      ".join(bad)))
sys.exit(1 if bad else 0)
PY
  then :; else status=1; fi
done
exit $status
