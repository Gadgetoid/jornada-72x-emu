# Networking and PC Link

## PC Link over PPP

`--net`, or Devices > Network (PPP), plugs a serial cable into the PPP gateway, and is remembered. CE sees the cable (GPIO 15 goes low) and starts PC Link, which connects at 57600. `./jornada-rapi` then talks to it over RAPI: `info`, `ls`, `put`, `get`, `run`, `reg` and more (`./jornada-rapi --help`).

## Ethernet card

`--ethernet`, or Devices > Ethernet Card, puts an NE2000-compatible card in the PC Card slot, on its own slirp network with DHCP giving CE 10.0.2.15, the host at 10.0.2.2 and DNS at 10.0.2.3, as on the PPP network. Its CIS identifies it as a Socket Communications Low Power Ethernet CF, which the ROM's `SocketLpe.dll` claims. The first time it's inserted, CE opens the Socket LP-E settings; choose DHCP there and it binds without a reset. That writes `HKLM\Comm\SOCKETLPE1\Parms\TcpIp` (`EnableDHCP` 1), which can also be set over RAPI before inserting the card. The card takes the PC Card slot, so it and a PC Card image can't be in at once. A saved state keeps it. headless takes `--ethernet[=SECONDS]`. The card and PPP can be used together; RAPI stays on PPP.

## Serial ports over TCP

`--tcp[=PORT]`, or Devices > TCP Port, offers the serial port as raw bytes on a TCP port instead (default 9991).

`--tcp-com4=PORT` (app and headless) offers COM4, CE's "Raw IR Port" on the SA-1110's serial port 2, as raw bytes on a TCP port, alongside whatever COM1 is doing. That keeps RAPI over PPP on COM1 available while a program talks to a serial port. The emulator passes the UART's bytes straight through, with no IrDA framing or infrared timing, so it's a convenience for testing rather than how the real port behaves. COM3 is IrCOMM, a virtual port over the IrDA stack, and the J720 has no COM2.
