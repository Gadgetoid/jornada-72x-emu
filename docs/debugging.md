# Debugging

## GDB

```sh
./jornada720 --gdb=1234
arm-none-eabi-gdb -ex 'set architecture armv4' -ex 'target extended-remote :1234'
```

| Option | What it does |
| --- | --- |
| `--gdb=PORT` | Listen for GDB on 127.0.0.1:PORT. GDB can attach and detach while the emulator runs. |
| `--gdb-process=NAME` | Debug one process, such as `hello.exe`. Breakpoints below 0x02000000 only stop in that process, and GDB stops when it starts. |
| Machine > GDB Server | Listen on all interfaces, on port 1234 (`gdb_port` in `jornada720.ini`). |

What works: registers, memory per process, breakpoints, stepping, Ctrl-C, CE's module list as shared libraries, and crashes CE reports (as SIGSEGV with the faulting registers). Hardware watchpoints aren't supported.

With debugmgr running (below), GDB's `remote put`, `remote get`, `remote delete`, `run` and `kill` go through it.

headless takes the same options. It waits for GDB before running, and runs until GDB detaches or kills it.

## debugmgr

debugmgr is the on-device agent from jornada-72x-tools. It copies files and runs programs over a mailbox between CE and the host, using `swi 0x51CE`.

### Requirements

PC Link over PPP (`--net`), so the emulator can install it. See [networking.md](networking.md).

### Usage

1. Start with `--agent=SOCKET`, `--gdb=PORT`, or both, plus `--net`.
2. When PC Link connects, the emulator installs debugmgr and prints `debugmgr: ready` once it's running.
3. Use `jornada-debug --socket=SOCKET` (from jornada-72x-tools) to ping it, copy files and run programs.

Installing means copying `\Windows\debugmgr.exe`, setting `HKLM\Software\debugmgr` `Transport` to `mailbox`, adding an `HKLM\Init` entry so it starts at boot, and starting it.

`make debugmgr` rebuilds the copy built into the emulator from `../jornada-72x-tools`.

## headless

`headless` runs the machine without a window, for tests and scripts. `./headless --help` lists its options: screenshots, key presses, taps, traces and profiles.

This calibrates the touch screen from a cold boot:

```sh
./headless --seconds=100 --key=62:0x4c --key=70:0x4c \
  --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 \
  --key=95:0x4c --screenshot=shot.png
```
