#!/usr/bin/env bash
# Flash the desk-HUD firmware for a given device mounting and dashboard
# set.
#
# Right-hand devices use the default landscape builds (setRotation(1));
# left-hand devices are physically flipped 180° on the desk and use the
# _left builds (setRotation(3)) so the image reads correctly in that
# orientation. --dash selects which dashboards are baked in: all (weather
# Now + Forecast + test pattern), weather only, or test only.
#
# Usage: ./flash.sh [OPTIONS]   (see --help)
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./flash.sh [OPTIONS]
  --hand left|right   Device mounting (default: right). left = panel image
                      rotated 180° (env *_left); right = default landscape.
  --dash all|weather|test  Dashboard set to bake in (default: all — the
                      full cycle. weather = Now+Forecast only; test =
                      static pattern only).
  --port PORT         Serial device to flash (e.g. /dev/ttyUSB0). With
                      multiple boards attached this is REQUIRED; with
                      exactly one it is auto-used; with none given and
                      zero/one detected, PlatformIO auto-detects.
  --clean             Clean the env's build tree first (stale font/sprite
                      headers after layout changes).
  --monitor           Open `pio device monitor` after a successful flash.
  -h, --help          This text.
USAGE
}

# ---------------------------------------------------------------------------
# Parse arguments.
# ---------------------------------------------------------------------------
HAND="right"
DASH="all"
PORT=""
CLEAN=0
MONITOR=0

while [ $# -gt 0 ]; do
    case "$1" in
        --hand)    HAND="${2:-}"; shift 2 ;;
        --dash)    DASH="${2:-}"; shift 2 ;;
        --port)    PORT="${2:-}"; shift 2 ;;
        --clean)   CLEAN=1; shift ;;
        --monitor) MONITOR=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "Error: unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [ "$HAND" != "left" ] && [ "$HAND" != "right" ]; then
    echo "Error: --hand must be 'left' or 'right' (got '$HAND')" >&2
    exit 1
fi

if [ "$DASH" != "all" ] && [ "$DASH" != "weather" ] && [ "$DASH" != "test" ]; then
    echo "Error: --dash must be 'all', 'weather', or 'test' (got '$DASH')" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Port detection: enumerate candidate serial devices. More than one attached
# board is ambiguous — require an explicit --port rather than guessing.
# ---------------------------------------------------------------------------
ports=()
while IFS= read -r dev; do
    [ -n "$dev" ] && ports+=("$dev")
done < <(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)

if [ ${#ports[@]} -gt 1 ] && [ -z "$PORT" ]; then
    echo "Error: multiple boards detected — pass --port with one of:" >&2
    for dev in "${ports[@]}"; do
        echo "  $dev" >&2
    done
    exit 1
fi
if [ ${#ports[@]} -eq 1 ] && [ -z "$PORT" ]; then
    PORT="${ports[0]}"
    echo "Using single detected board: $PORT"
fi

# ---------------------------------------------------------------------------
# Map mounting × dashboard-set → PlatformIO env, optionally clean, flash.
# "all" keeps the original env names (esp32dev / esp32dev_left);
# single-dash builds append the dashboard name to the base.
# ---------------------------------------------------------------------------
# Env naming scheme: esp32dev[_<dash>][_left] — the hand suffix always
# comes last, exactly as the envs are named in platformio.ini.
if [ "$DASH" != "all" ]; then
    ENV_NAME="esp32dev_${DASH}"
else
    ENV_NAME="esp32dev"
fi
[ "$HAND" = "left" ] && ENV_NAME="${ENV_NAME}_left"

if [ "$CLEAN" -eq 1 ]; then
    echo "Cleaning $ENV_NAME build tree..."
    pio run -e "$ENV_NAME" -t clean
fi

cmd=(pio run -e "$ENV_NAME" -t upload)
if [ -n "$PORT" ]; then
    cmd+=(--upload-port "$PORT")
fi

echo "Flashing env $ENV_NAME to ${PORT:-auto-detected port}..."
"${cmd[@]}"

# ---------------------------------------------------------------------------
# Post-flash guidance.
# ---------------------------------------------------------------------------
hand_note="R"
[ "$HAND" = "left" ] && hand_note="L"
rotation_log="\"[display] rotation 1 (right-hand device)\""
[ "$HAND" = "left" ] && rotation_log="\"[display] rotation 3 (left-hand device)\""

# Per-set "what happens next" bullets (hand marker + rotation log lines
# below are common to every set).
case "$DASH" in
    weather)
        set_notes="  - The Now view takes over once weather data lands.
  - The button toggles Now <-> Forecast (no test screen on this build)."
        ;;
    test)
        set_notes="  - The static pattern is the whole firmware — the entire
    device is the verification screen. A button press just re-renders it
    (the legend still works on hold)."
        ;;
    *)
        set_notes="  - The Now view takes over once weather data lands.
  - Short-press the button TWICE to reach the Test Pattern screen."
        ;;
esac

cat <<POST
Flash OK ($DASH dashboards, $HAND-hand build).

What happens next:
  - The device cold-boots into the boot screen — its subtitle ends
    " $hand_note" for this hand.
$set_notes
  - On a left-hand build, "LEFT HAND" + the corner labels read correctly
    only when the device sits flipped.
  - 'pio device monitor' shows the rotation log line $rotation_log
POST

if [ "$MONITOR" -eq 1 ]; then
    exec pio device monitor
fi
