# Debugging

`--agent=SOCKET` opens the host mailbox for jornada-72x-tools' debugmgr (`swi 0x51CE`). With either `--agent` or `--gdb`, the app installs debugmgr when PC Link connects: it copies `\Windows\debugmgr.exe`, sets `HKLM\Software\debugmgr` `Transport` to `mailbox`, adds an `HKLM\Init` launch entry and starts it. It prints `debugmgr: ready` once debugmgr polls the mailbox. `jornada-debug --socket=SOCKET` then pings, copies files and runs programs. `make debugmgr` rebuilds the embedded copy from `../jornada-72x-tools`.

`--gdb=PORT` listens for GDB on 127.0.0.1 (`target extended-remote :PORT`, `set architecture armv4`); it can attach and detach while the emulator runs, and `--gdb-process=NAME` limits user-space breakpoints to one process. Registers, memory per process, breakpoints, stepping, Ctrl-C, the CE module list as shared libraries, and crashes CE reports (as SIGSEGV with the faulting registers) work. With debugmgr running, `remote put`/`get`/`delete` and `run`/`kill` go through it. Hardware watchpoints aren't supported. headless takes the same options, waits for GDB before running and runs until GDB detaches or kills it.

Machine > GDB Server listens on all interfaces on port 1234 (`gdb_port` in `jornada720.ini`) instead.

`./headless --help` lists the scripted runner's options (screenshots, key presses, taps, traces, profiles). This calibrates the touch screen from a cold boot:

```sh
./headless --seconds=100 --key=62:0x4c --key=70:0x4c \
  --tap=78:320:120:0.6 --tap=81:64:24:0.6 --tap=84:64:216:0.6 --tap=87:576:216:0.6 --tap=90:576:24:0.6 \
  --key=95:0x4c --screenshot=shot.png
```
