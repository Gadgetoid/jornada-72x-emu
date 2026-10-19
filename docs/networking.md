# Networking and PC Link

There are three ways to connect CE to the outside world:

| Connection | Use it for | Turn on with |
| --- | --- | --- |
| PC Link over PPP | RAPI, file transfer, Pocket IE through the web proxy | `--net` or Devices > Network (PPP) |
| Ethernet card | General networking without the serial cable | `--ethernet` or Devices > Ethernet Card |
| Serial ports over TCP | Talking to CE programs over a raw serial port | `--tcp[=PORT]`, `--tcp-com4=PORT` |

## Requirements

PPP and the Ethernet card need a build with libslirp (included in the releases). The web proxy for Pocket IE also needs libcurl. See [building.md](building.md).

## PC Link over PPP

1. Start with `--net`, or choose Devices > Network (PPP). The setting is remembered.
2. CE sees the serial cable plugged in and starts PC Link, which connects at 57600.
3. Use `jornada-rapi` to talk to CE over RAPI.

```sh
./jornada-rapi info
./jornada-rapi ls '\My Documents'
./jornada-rapi put local.txt '\My Documents\local.txt'
```

`jornada-rapi --help` lists the commands: `info`, `ls`, `put`, `get`, `run`, `reg` and more.

The PC Link menu also sends files, copies My Documents back, syncs a shared folder, points Pocket IE at the web proxy, offers RAPI over the network and sets the connection speed.

CE is at 10.0.2.15 on this network, with the host at 10.0.2.2 and DNS at 10.0.2.3.

## Ethernet card

`--ethernet`, or Devices > Ethernet Card, puts an NE2000-compatible card in the PC Card slot, on its own network with DHCP. Addresses are the same as on the PPP network.

**First use:** CE opens the Socket LP-E driver's settings the first time the card goes in. Choose DHCP there and the card works straight away, with no reset.

To set it up without the dialog, for scripts, set this over RAPI before inserting the card:

```sh
key='HKLM\Comm\SOCKETLPE1\Parms\TcpIp'
./jornada-rapi reg set "$key" EnableDHCP dword 1
./jornada-rapi reg set "$key" IpAddress multi 0.0.0.0
./jornada-rapi reg set "$key" Subnetmask multi 0.0.0.0
```

Notes:

- The card uses the PC Card slot, so it can't be in at the same time as a PC Card image.
- A saved state keeps the card.
- The card and PPP can be used together. RAPI stays on PPP.
- headless takes `--ethernet[=SECONDS]`, inserting the card at that time.
- The card identifies itself as a Socket Communications Low Power Ethernet CF, which the ROM's `SocketLpe.dll` drives.

## Serial ports over TCP

| Option | Port in CE | Notes |
| --- | --- | --- |
| `--tcp[=PORT]` or Devices > TCP Port | COM1, the serial cable | Default port 9991. Replaces PPP on COM1. The cable counts as connected while a client is attached. |
| `--tcp-com4=PORT` | COM4, the raw IR port | Works alongside PPP on COM1, so RAPI stays available. For this run only. |

Connect with any raw TCP client, such as `nc localhost 9991`.

COM4 passes bytes straight through, with no IrDA framing or infrared timing. It's a convenience for testing, not how the real port behaves. CE's other ports: COM3 is IrCOMM, a virtual port over the IrDA stack, and the J720 has no COM2.
