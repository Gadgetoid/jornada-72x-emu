#!/bin/sh
set -e
cd "$(dirname "$0")/.."
out=build/test
mkdir -p "$out"
calibrate="--key=62:0x4c --key=70:0x4c --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 --key=95:0x4c"
wizard="--tap=2:605:226 --tap=6:605:226 --tap=10:605:226 --tap=14:605:226 --tap=18:605:226 --tap=22:605:226"
failed=0

check() {
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; failed=1; fi
}

check_different() {
    if [ "$2" != "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; failed=1; fi
}

run_app_case() {
    name=$1
    x=$2
    y=$3
    baseline="$out/$name.png"
    optimised="$out/$name-optimised.png"
    repeated="$out/$name-optimised-repeat.png"
    optimised_log="$out/$name-optimised.log"
    taps="--tap=1:$x:$y:0.1 --tap=1.5:$x:$y:0.1"

    ./headless --load=$out/desktop.state --seconds=20 $taps --screenshot="$baseline" > /dev/null
    ./headless --load=$out/desktop.state --seconds=20 $taps --optimisations --screenshot="$optimised" > "$optimised_log"
    ./headless --load=$out/desktop.state --seconds=20 $taps --optimisations --screenshot="$repeated" > /dev/null

    check_different "$name opens" "$(shasum < "$baseline")" "$(shasum < "$out/desktop.png")"
    check "$name optimized screen matches" "$(shasum < "$baseline")" "$(shasum < "$optimised")"
    check "$name optimized run repeats" "$(shasum < "$optimised")" "$(shasum < "$repeated")"
    if ! grep -Eq '^native memmove: entries [1-9][0-9]*, fingerprint matched, fallbacks [0-9]+, hits [1-9][0-9]*$' "$optimised_log"; then
        echo "FAIL $name native memmove exercised"
        failed=1
    else
        echo "ok   $name native memmove exercised"
    fi
}

app_check() {
    data="$out/app"
    rm -rf "$data"
    mkdir -p "$data"
    cp "$out/desktop.state" "$data/app.state"
    HOME="$data" XDG_DATA_HOME="$data" XDG_CONFIG_HOME="$data" SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy \
        ./jornada720 --state="$data/app.state" > "$out/app.log" 2>&1 &
    app=$!
    sleep 3
    if kill -0 $app 2>/dev/null; then echo "ok   app runs"; else echo "FAIL app runs"; cat "$out/app.log"; failed=1; return; fi
    kill -TERM $app
    if wait $app; then echo "ok   app quits"; else echo "FAIL app quits"; cat "$out/app.log"; failed=1; fi
    check_different "app saves state" "$(shasum < "$out/desktop.state")" "$(shasum < "$data/app.state")"
}

./headless --seconds=100 $calibrate --save=$out/calibrated.state --screenshot=$out/calibrated.png > /dev/null
./headless --load=$out/calibrated.state --seconds=30 $wizard --save=$out/desktop.state --screenshot=$out/desktop.png > /dev/null
./headless --load=$out/desktop.state --seconds=90 --screenshot=$out/idle.png > /dev/null
./headless --load=$out/desktop.state --seconds=90 --backup=0x3ff --screenshot=$out/idle-full.png > /dev/null
./headless --seconds=130 $calibrate --tap=102:605:226 --tap=106:605:226 --tap=110:605:226 --tap=114:605:226 --tap=118:605:226 --tap=122:605:226 --screenshot=$out/continuous.png > /dev/null
./headless --seconds=0.01 --screenshot=$out/cold-start.png > /dev/null
./headless --seconds=0.01 --optimisations --screenshot=$out/cold-start-optimised.png > "$out/cold-start-optimised.log"
run_app_case pocket-word 514 35
run_app_case my-handheld-pc 34 18
run_app_case pocket-excel 590 18
app_check

check "resume matches continuous run" "$(shasum < $out/desktop.png)" "$(shasum < $out/continuous.png)"
check "desktop stays clear of battery warnings" "$(shasum < $out/idle-full.png)" "$(shasum < $out/idle.png)"
check "cold-start screen matches with optimisations" "$(shasum < $out/cold-start.png)" "$(shasum < $out/cold-start-optimised.png)"
if ! grep -Eq '^native OSCR delay: fingerprint matched, poll skips [1-9][0-9]*$' "$out/cold-start-optimised.log"; then
    echo "FAIL native OSCR delay exercised"
    failed=1
else
    echo "ok   native OSCR delay exercised"
fi
exit $failed
