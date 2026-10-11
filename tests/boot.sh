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
    data=/tmp/jornada-app-$$
    agent=/tmp/jornada-app-agent-$$.sock
    rm -rf "$data" "$agent"
    mkdir -p "$data"
    cp "$out/desktop.state" "$data/app.state"
    port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    HOME="$data" XDG_DATA_HOME="$data" XDG_CONFIG_HOME="$data" SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy \
        ./jornada720 --state="$data/app.state" --net --agent="$agent" --gdb=$port > "$out/app.log" 2>&1 &
    app=$!
    if python3 tests/agent_ping.py "$agent" 60 > /dev/null; then echo "ok   app installs debugmgr"; else echo "FAIL app installs debugmgr"; failed=1; fi
    for attempt in $(seq 1 20); do grep -q "^debugmgr: ready" "$out/app.log" && break; sleep 0.5; done
    if grep -q "^debugmgr: ready" "$out/app.log"; then echo "ok   app reports debugmgr ready"; else echo "FAIL app reports debugmgr ready"; failed=1; fi
    if python3 tests/gdb_client.py $port > "$out/app_gdb.log" 2>&1; then echo "ok   app gdb stub"; else echo "FAIL app gdb stub"; cat "$out/app_gdb.log"; failed=1; fi
    if kill -0 $app 2>/dev/null; then echo "ok   app runs"; else echo "FAIL app runs"; cat "$out/app.log"; failed=1; return; fi
    kill -TERM $app
    if wait $app; then echo "ok   app quits"; else echo "FAIL app quits"; cat "$out/app.log"; failed=1; fi
    check_different "app saves state" "$(shasum < "$out/desktop.state")" "$(shasum < "$data/app.state")"
    rm -rf "$data" "$agent"
}

debugmgr_check() {
    socket=/tmp/jornada-rapi-$$.sock
    agent=/tmp/jornada-agent-$$.sock
    rm -f "$socket" "$agent" "$out/debugmgr.state"
    ./headless --load=$out/desktop.state --seconds=300 --net=0 --rapi="$socket" --agent="$agent" --realtime=4 --save="$out/debugmgr.state" > "$out/debugmgr.log" 2>&1 &
    emulator=$!
    for attempt in $(seq 1 120); do grep -q "^desktop: connection" "$out/debugmgr.log" && break; sleep 0.5; done
    started=fail
    ./jornada-rapi --socket="$socket" debugmgr run > /dev/null 2>&1 && python3 tests/agent_ping.py "$agent" 30 > /dev/null && started=ok
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    rm -f "$socket" "$agent"
    ./headless --load=$out/debugmgr.state --soft-reset=0 --seconds=200 --agent="$agent" --realtime=4 > "$out/debugmgr-reset.log" 2>&1 &
    emulator=$!
    restarted=fail
    python3 tests/agent_ping.py "$agent" 60 > /dev/null && restarted=ok
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    rm -f "$agent"
    check "PC Link starts on cable detect and installs debugmgr" "$started" ok
    check "debugmgr starts after a soft reset" "$restarted" ok
}

gdb_check() {
    port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    ./headless --load=$out/desktop.state --gdb=$port > "$out/gdb.log" 2>&1 &
    stub=$!
    if python3 tests/gdb_client.py $port > "$out/gdb_client.log" 2>&1; then echo "ok   gdb stub"; else echo "FAIL gdb stub"; cat "$out/gdb_client.log" "$out/gdb.log"; failed=1; kill $stub; fi
    wait $stub || true
}

./headless --seconds=100 $calibrate --save=$out/calibrated.state --screenshot=$out/calibrated.png > /dev/null
./headless --load=$out/calibrated.state --seconds=30 $wizard --save=$out/desktop.state --screenshot=$out/desktop.png > /dev/null
./headless --load=$out/desktop.state --seconds=90 --screenshot=$out/idle.png > /dev/null
./headless --load=$out/desktop.state --seconds=90 --backup=0x3ff --screenshot=$out/idle-full.png > /dev/null
./headless --seconds=130 $calibrate --tap=102:605:226 --tap=106:605:226 --tap=110:605:226 --tap=114:605:226 --tap=118:605:226 --tap=122:605:226 --screenshot=$out/continuous.png > /dev/null
./headless --seconds=0.01 --screenshot=$out/cold-start.png > /dev/null
./headless --seconds=0.01 --optimisations --screenshot=$out/cold-start-optimised.png > "$out/cold-start-optimised.log"
./headless --seconds=8 --screenshot=$out/cache-sweep.png > /dev/null
./headless --seconds=8 --optimisations --screenshot=$out/cache-sweep-optimised.png > "$out/cache-sweep-optimised.log"
run_app_case pocket-word 514 35
run_app_case my-handheld-pc 34 18
run_app_case pocket-excel 590 18
debugmgr_check
app_check
gdb_check

check "resume matches continuous run" "$(shasum < $out/desktop.png)" "$(shasum < $out/continuous.png)"
check "desktop stays clear of battery warnings" "$(shasum < $out/idle-full.png)" "$(shasum < $out/idle.png)"
check "cold-start screen matches with optimisations" "$(shasum < $out/cold-start.png)" "$(shasum < $out/cold-start-optimised.png)"
check "cache-sweep screen matches with optimisations" "$(shasum < $out/cache-sweep.png)" "$(shasum < $out/cache-sweep-optimised.png)"
if ! grep -Eq '^native OSCR delay: fingerprint matched, poll skips [1-9][0-9]*$' "$out/cold-start-optimised.log"; then
    echo "FAIL native OSCR delay exercised"
    failed=1
else
    echo "ok   native OSCR delay exercised"
fi
if ! grep -Eq '^native cache sweep: entries [1-9][0-9]*, fingerprint matched, fallbacks [0-9]+, hits [1-9][0-9]*$' "$out/cache-sweep-optimised.log"; then
    echo "FAIL native cache sweep exercised"
    failed=1
else
    echo "ok   native cache sweep exercised"
fi
exit $failed
