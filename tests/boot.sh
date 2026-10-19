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

app_profile_check() {
    data=/tmp/jornada-profile-$$
    rm -rf "$data"
    mkdir -p "$data/jornada-72x-emu"
    tcp=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    HOME="$data" XDG_DATA_HOME="$data" XDG_CONFIG_HOME="$data" SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy \
        ./jornada720 --tcp=$tcp > "$out/app-profile.log" 2>&1 &
    app=$!
    connected=fail
    for attempt in $(seq 1 40); do
        python3 -c 'import socket, sys; socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=1).close()' $tcp 2>/dev/null && connected=ok && break
        sleep 0.5
    done
    check "app offers the serial port on TCP" "$connected" ok
    kill -TERM $app
    wait $app || true
    check "app makes its first machine" "$(sed -n 's/^name=//p' "$data/jornada-72x-emu/machines/jornada-720.ini")" "Jornada 720"
    if [ -s "$data/jornada-72x-emu/machines/jornada-720.state" ]; then echo "ok   app saves the first machine's state"; else echo "FAIL app saves the first machine's state"; failed=1; fi
    rm -rf "$data"
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

build_test_program() {
    tools=${JORNADA_TOOLS:-../jornada-72x-tools}
    if [ ! -f "$tools/cmake/jornada-ce.cmake" ]; then
        echo "skip $2 (no jornada-72x-tools at $tools)"
        return 1
    fi
    tools=$(cd "$tools" && pwd)
    if ! cmake -S tests/$1 -B "$out/$1" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$tools/cmake/jornada-ce.cmake" > "$out/$1-build.log" 2>&1 ||
        ! cmake --build "$out/$1" >> "$out/$1-build.log" 2>&1; then
        echo "FAIL $1 builds"
        failed=1
        return 1
    fi
}

com4_check() {
    build_test_program comecho "COM4 over TCP" || return 0
    socket=/tmp/jornada-com4-$$.sock
    rm -f "$socket"
    port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    ./headless --load=$out/desktop.state --seconds=300 --net=0 --rapi="$socket" --tcp-com4=$port --realtime=4 > "$out/com4.log" 2>&1 &
    emulator=$!
    for attempt in $(seq 1 120); do grep -q "^desktop: connection" "$out/com4.log" && break; sleep 0.5; done
    echoed=fail
    if ./jornada-rapi --socket="$socket" put "$out/comecho/comecho.exe" '\comecho.exe' > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" run '\comecho.exe' > /dev/null 2>&1; then
        echoed=$(python3 - $port <<'PYTHON'
import socket, sys, time
connection = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=2)
time.sleep(2)
connection.sendall(b"hello com4\r\n")
received = b""
deadline = time.time() + 20
connection.settimeout(1)
while time.time() < deadline and b"com4" not in received:
    try:
        received += connection.recv(256)
    except socket.timeout:
        pass
print("ok" if received == b"hello com4\r\n" else "fail")
PYTHON
)
    fi
    check "COM4 echoes over TCP alongside RAPI" "$echoed" ok
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    if grep -q "^unknown accesses: [1-9]" "$out/com4.log"; then echo "FAIL COM4 leaves no unknown accesses"; failed=1; else echo "ok   COM4 leaves no unknown accesses"; fi
    rm -f "$socket"
}

modem_check() {
    build_test_program atprobe "internal modem" || return 0
    socket=/tmp/jornada-modem-$$.sock
    rm -f "$socket" "$out/atresult.txt"
    ./headless --load=$out/desktop.state --seconds=300 --net=0 --rapi="$socket" --realtime=4 > "$out/modem.log" 2>&1 &
    emulator=$!
    for attempt in $(seq 1 120); do grep -q "^desktop: connection" "$out/modem.log" && break; sleep 0.5; done
    if ./jornada-rapi --socket="$socket" put "$out/atprobe/atprobe.exe" '\atprobe.exe' > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" run '\atprobe.exe' > /dev/null 2>&1; then
        for attempt in $(seq 1 40); do
            ./jornada-rapi --socket="$socket" get '\atresult.txt' "$out/atresult.txt" > /dev/null 2>&1 && grep -q "end" "$out/atresult.txt" && break
            sleep 1
        done
    fi
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    rm -f "$socket"
    check "the internal modem answers AT commands" "$(grep -c 'Hewlett Packard Internal V.90 Fax Modem' "$out/atresult.txt" 2>/dev/null)" 1
}

ethernet_check() {
    build_test_program tcpprobe "Ethernet card" || return 0
    socket=/tmp/jornada-ethernet-$$.sock
    rm -f "$socket" "$out/ethernet.state"
    port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    ./headless --load=$out/desktop.state --seconds=300 --net=0 --rapi="$socket" --ethernet=120 --realtime=4 --save="$out/ethernet.state" > "$out/ethernet-setup.log" 2>&1 &
    emulator=$!
    for attempt in $(seq 1 120); do grep -q "^desktop: connection" "$out/ethernet-setup.log" && break; sleep 0.5; done
    target="\\Windows\\tcpprobe.exe 10.0.2.2 $port"
    printf '%s#%s' "${#target}" "$target" > "$out/tcpprobe.lnk"
    tcpip='HKLM\Comm\SOCKETLPE1\Parms\TcpIp'
    ./jornada-rapi --socket="$socket" put "$out/tcpprobe/tcpprobe.exe" '\Windows\tcpprobe.exe' > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" put "$out/tcpprobe.lnk" '\Windows\StartUp\tcpprobe.lnk' > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" reg set "$tcpip" EnableDHCP dword 1 > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" reg set "$tcpip" IpAddress multi 0.0.0.0 > /dev/null 2>&1 &&
        ./jornada-rapi --socket="$socket" reg set "$tcpip" Subnetmask multi 0.0.0.0 > /dev/null 2>&1
    for attempt in $(seq 1 60); do ./jornada-rapi --socket="$socket" reg get 'HKLM\Comm\SOCKETLPE1\Parms\TcpIp' DhcpServer > "$out/ethernet-dhcp.txt" 2>&1 && break; sleep 2; done
    check "Ethernet card gets a DHCP lease when inserted" "$(cat "$out/ethernet-dhcp.txt")" 'string "10.0.2.2"'
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    rm -f "$socket"
    python3 - $port > "$out/ethernet-reply.txt" <<'PYTHON' &
import socket, sys
listener = socket.create_server(("127.0.0.1", int(sys.argv[1])))
listener.settimeout(240)
connection = listener.accept()[0]
connection.settimeout(30)
line = connection.recv(100)
connection.sendall(b"hello from the host\n")
echo = connection.recv(100)
print("ok" if line == b"hello from the jornada\n" and echo == b"hello from the host\n" else "fail")
PYTHON
    listener=$!
    ./headless --load="$out/ethernet.state" --soft-reset=0 --ethernet=0 --seconds=240 --realtime=4 > "$out/ethernet.log" 2>&1 &
    emulator=$!
    wait $listener || true
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    check "CE talks TCP over the Ethernet card after a soft reset" "$(cat "$out/ethernet-reply.txt")" ok
}

card_check() {
    socket=/tmp/jornada-card-$$.sock
    cards=$out/cards
    rm -rf "$cards" "$socket" && mkdir -p "$cards/cf/files" "$cards/pc/files"
    echo "hello from the host" > "$cards/cf/files/HELLO.TXT"
    echo "lower case" > "$cards/cf/files/readme.txt"
    echo "hello from the PC Card" > "$cards/pc/files/PCCARD.TXT"
    marker="written by CE over RAPI $$"
    echo "$marker" > "$cards/put.txt"
    sh tools/mkcard.sh "$cards/cf.img" 16 "$cards/cf/files" > /dev/null
    sh tools/mkcard.sh "$cards/pc.img" 16 "$cards/pc/files" > /dev/null
    ./headless --load=$out/desktop.state --card="$cards/cf.img" --pc-card="$cards/pc.img" --seconds=5 --save="$cards/card.state" > /dev/null
    ./headless --load="$cards/card.state" --seconds=300 --net=0 --rapi="$socket" --realtime=4 > "$cards/headless.log" 2>&1 &
    emulator=$!
    for attempt in $(seq 1 120); do grep -q "^desktop: connection" "$cards/headless.log" && break; sleep 0.5; done
    ./jornada-rapi --socket="$socket" get '\Storage Card\files\PCCARD.TXT' "$cards/pccard.back" > /dev/null 2>&1 || true
    ./jornada-rapi --socket="$socket" get '\Storage Card2\files\HELLO.TXT' "$cards/hello.back" > /dev/null 2>&1 || true
    ./jornada-rapi --socket="$socket" put "$cards/put.txt" '\Storage Card2\put.txt' > /dev/null 2>&1 || true
    ./jornada-rapi --socket="$socket" ls '\Storage Card2\files' > "$cards/listing.txt" 2>&1 || true
    kill -TERM $emulator 2>/dev/null || true
    wait $emulator 2>/dev/null || true
    rm -f "$socket"
    check "CE mounts a PC Card image as Storage Card" "$(cat "$cards/pccard.back" 2>/dev/null)" "hello from the PC Card"
    check "CE mounts a CompactFlash image as Storage Card2" "$(cat "$cards/hello.back" 2>/dev/null)" "hello from the host"
    check "CE keeps a card file's lower-case name" "$(grep -c ' readme\.txt$' "$cards/listing.txt")" 1
    check "a file put over RAPI is written to the image" "$(LC_ALL=C grep -a -c "$marker" "$cards/cf.img")" 1
    open="--tap=10:34:18:0.1 --tap=10.5:34:18:0.1"
    ./headless --load=$out/desktop.state --seconds=20 $open --screenshot="$cards/none.png" > /dev/null
    ./headless --load="$cards/card.state" --seconds=20 $open --screenshot="$cards/kept.png" > /dev/null
    ./headless --load="$cards/card.state" --eject-card=2 --eject-pc-card=2 --seconds=20 $open --screenshot="$cards/ejected.png" > /dev/null
    check_different "a saved state keeps its cards" "$(shasum < "$cards/kept.png")" "$(shasum < "$cards/none.png")"
    check "ejecting the cards removes them" "$(shasum < "$cards/ejected.png")" "$(shasum < "$cards/none.png")"
}

power_check() {
    word="--tap=1:514:35:0.1 --tap=1.5:514:35:0.1"
    ./headless --load=$out/desktop.state $word --key=10:0x7f --seconds=12 > "$out/sleep.log"
    ./headless --load=$out/desktop.state $word --key=10:0x7f --key=13:0x7f --seconds=16 --screenshot=$out/wake.png > /dev/null
    ./headless --load=$out/desktop.state $word --seconds=16 --screenshot=$out/awake.png > /dev/null
    if grep -q "powered off" "$out/sleep.log"; then echo "ok   On/Off key suspends"; else echo "FAIL On/Off key suspends"; failed=1; fi
    check "On/Off key resumes Pocket Word" "$(shasum < $out/wake.png)" "$(shasum < $out/awake.png)"
}

type_check() {
    word="--tap=1:514:35:0.1 --tap=1.5:514:35:0.1"
    ./headless --load=$out/desktop.state $word --type='6:Hello, World! (1+2) "x_y"' --seconds=20 --screenshot=$out/typed.png > /dev/null
    check_different "typing reaches Pocket Word" "$(shasum < $out/typed.png)" "$(shasum < $out/pocket-word.png)"
}

speed_check() {
    single=$(./headless --seconds=2 | sed -n 's/.* \([0-9][0-9]*\) instructions.*/\1/p' | head -1)
    double=$(./headless --seconds=2 --speed=2 | sed -n 's/.* \([0-9][0-9]*\) instructions.*/\1/p' | head -1)
    if [ $((double * 10)) -ge $((single * 15)) ]; then echo "ok   2x speed runs more instructions"; else echo "FAIL 2x speed runs more instructions ($single, $double)"; failed=1; fi
}

gdb_check() {
    port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    ./headless --load=$out/desktop.state --gdb=$port > "$out/gdb.log" 2>&1 &
    stub=$!
    if python3 tests/gdb_client.py $port > "$out/gdb_client.log" 2>&1; then echo "ok   gdb stub"; else echo "FAIL gdb stub"; cat "$out/gdb_client.log" "$out/gdb.log"; failed=1; kill $stub; fi
    wait $stub || true
}

./headless --seconds=100 $calibrate --save=$out/calibrated.state --screenshot=$out/calibrated.png > /dev/null
./headless --seconds=100 $calibrate --host-clock --screenshot=$out/host-clock.png > /dev/null
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
card_check
power_check
type_check
speed_check
debugmgr_check
com4_check
ethernet_check
modem_check
app_check
app_profile_check
gdb_check

check_different "host clock sets the date at a cold boot" "$(shasum < $out/host-clock.png)" "$(shasum < $out/calibrated.png)"
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
