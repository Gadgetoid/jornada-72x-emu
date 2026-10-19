# Using the emulator

## Data folder and settings

The data folder is `$XDG_DATA_HOME/jornada-72x-emu` (otherwise `~/Library/Application Support/jornada-72x-emu` on macOS, `~/.local/share/jornada-72x-emu` elsewhere). It holds the ROMs, machines, states and snapshots. Settings are in `jornada720.ini` in `$XDG_CONFIG_HOME/jornada-72x-emu` (otherwise the data folder on macOS, `~/.config/jornada-72x-emu` elsewhere).

## Machines

`jornada720 [OPTIONS] [ROM [EEPROM]]`. With no ROM it opens the last machine used, and the first run makes a machine from the ROM it finds. With a ROM it runs that ROM with its own state, outside the machine list. `./jornada720 --help` lists the options.

## Menus

The menus replace the old Cmd keys:

- Machine: Power Button (the On/Off key, which suspends and wakes the machine), Soft Reset (Cmd-R), the machines, New Machine and Manage Machines (reset or delete), Pause (Cmd-P), CPU Speed (1x or 2x the CPU clock against the timers, and Optimisations), GDB Server.
- State: Save State (Cmd-S), Load State (Cmd-L), snapshots to and from any file, Show State Folder. The state is also saved on quit and every minute, a backup goes in `snapshots/Backups` every 10 minutes and before a load or reset, and the clock is advanced by the time away. An unreadable state is moved aside to `.old`.
- Edit: Copy Screen, Paste as Typing, Save Screenshot.
- View: 50% to 200%, Zoom In and Out, Full Screen.
- Devices: Insert Card Image (Cmd-O) and Eject Card (Cmd-E) for the CompactFlash slot, the same with Shift for the PC Card slot, Ethernet Card, the serial port (not connected, PPP network, pseudo-terminal, TCP port or a host serial port), Sound.
- PC Link: send files, copy My Documents back, a shared folder synced with My Documents, Pocket IE proxy, RAPI over the network, connection speed.

## Machines and the clock

New Machine's clock setting sets CE's clock from the host's local time at the first boot, as CE clears it then. `--fresh` cold boots and `--state=PATH` uses another state file. `--speed` and `--optimisations` set the CPU speed and optimisations and are remembered.
