# Using the emulator

## Running

```sh
jornada720 [OPTIONS] [ROM [EEPROM]]
```

- With no ROM it opens the last machine used. The first run makes a machine from the ROM it finds.
- With a ROM it runs that ROM with its own state, outside the machine list.
- `jornada720 --help` lists the options.

## Data folder and settings

| | macOS | Linux |
| --- | --- | --- |
| Data folder: ROMs, machines, states, snapshots, cards | `~/Library/Application Support/jornada-72x-emu` | `~/.local/share/jornada-72x-emu` |
| Settings, `jornada720.ini` | The data folder | `~/.config/jornada-72x-emu` |

`$XDG_DATA_HOME` and `$XDG_CONFIG_HOME` override these on both.

## Controls

- The mouse is the stylus. The scroll wheel sends arrow keys.
- Keys map to the Jornada keyboard. Right Alt is Fn, and F12 is the On/Off key.

## Menus

| Menu | Items |
| --- | --- |
| Machine | Power Button (the On/Off key, which suspends and wakes the machine), Soft Reset (Cmd-R), your machines, New Machine, Manage Machines (reset or delete), Pause (Cmd-P), CPU Speed (1x or 2x, and Optimisations), GDB Server |
| State | Save State (Cmd-S), Load State (Cmd-L), snapshots to and from any file, Show State Folder |
| Edit | Copy Screen, Paste as Typing, Save Screenshot |
| View | 50% to 200%, Zoom In and Out, Full Screen |
| Devices | Insert and Eject Card for the CompactFlash slot (Cmd-O, Cmd-E) and the PC Card slot (with Shift), Ethernet Card, the serial port (not connected, PPP network, pseudo-terminal, TCP port or a host serial port), Sound |
| PC Link | Send files, copy My Documents back, a shared folder synced with My Documents, Pocket IE proxy, RAPI over the network, connection speed |

## Saved states

- The state is saved on quit and every minute.
- A backup goes in `snapshots/Backups` every 10 minutes, and before a load or reset.
- CE's clock is advanced by the time the machine was away.
- An unreadable state is moved aside to `.old`.
- `--fresh` ignores the saved state and cold boots. `--state=FILE` uses another state file.

## Clock

CE clears its clock on a cold boot. New Machine's clock setting sets it from the host's local time at the first boot instead.

## Speed

`--speed=1|2` runs the CPU at 1x or 2x its clock against the timers, and `--optimisations` turns on [native optimisations](optimisations.md). Both are remembered.
