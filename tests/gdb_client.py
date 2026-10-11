import socket
import sys
import time

IRQ_VECTOR = 0xFFFF0018
IRQ_LITERAL = 0xFFFF03F8
PC_REGISTER = 15
REGISTER_COUNT = 17
PACKET_SIZE = 0x4000
SCRATCH = 0x88380000


class Client:
    def __init__(self, port):
        for attempt in range(50):
            try:
                self.connection = socket.create_connection(("127.0.0.1", port), timeout=30)
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise SystemExit("no GDB stub on port %d" % port)
        self.buffer = b""

    def send(self, payload):
        data = payload.encode("latin-1")
        self.connection.sendall(b"$%s#%02x" % (data, sum(data) & 0xFF))

    def receive(self):
        while True:
            start = self.buffer.find(b"$")
            end = self.buffer.find(b"#", start)
            if start >= 0 and end >= 0 and len(self.buffer) >= end + 3:
                payload = self.buffer[start + 1:end].decode("latin-1")
                self.buffer = self.buffer[end + 3:]
                return payload
            chunk = self.connection.recv(65536)
            if not chunk:
                raise SystemExit("stub closed the connection")
            self.buffer += chunk

    def request(self, payload):
        self.send(payload)
        while True:
            reply = self.receive()
            if not reply.startswith("O") or reply == "OK":
                return reply

    def monitor(self, command):
        self.send("qRcmd," + command.encode().hex())
        output = ""
        while True:
            reply = self.receive()
            if reply.startswith("O") and reply != "OK":
                output += bytes.fromhex(reply[1:]).decode("latin-1")
            else:
                return output

    def register(self, number):
        return int.from_bytes(bytes.fromhex(self.request("p%x" % number)), "little")


def check(name, condition, detail=""):
    if not condition:
        raise SystemExit("%s failed %s" % (name, detail))


def main():
    client = Client(int(sys.argv[1]))
    client.request("QStartNoAckMode")
    supported = client.request("qSupported:xmlRegisters=arm")
    check("qSupported", "qXfer:features:read+" in supported and "qXfer:libraries:read+" in supported, supported)
    check("stop reason", client.request("?").startswith("T05"))
    target = client.request("qXfer:features:read:target.xml:0,fff")
    check("target.xml", target.startswith("l") and "org.gnu.gdb.arm.core" in target and 'bitsize="32"' in target)
    registers = client.request("g")
    check("registers", len(registers) == REGISTER_COUNT * 8, str(len(registers)))
    processes = client.monitor("processes")
    check("processes", "NK.EXE" in processes and "filesys.exe" in processes and "explorer.exe" in processes, processes)
    handler = int.from_bytes(bytes.fromhex(client.request("m%x,4" % IRQ_LITERAL)), "little")
    check("memory", handler >= 0x80000000, hex(handler))
    libraries = client.request("qXfer:libraries:read::0,fff")
    check("libraries", "coredll.elf" in libraries.lower(), libraries[:200])
    check("breakpoint insert", client.request("Z0,%x,4" % IRQ_VECTOR) == "OK")
    stop = client.request("c")
    check("breakpoint stop", stop.startswith("T05"), stop)
    check("breakpoint pc", client.register(PC_REGISTER) == IRQ_VECTOR, hex(client.register(PC_REGISTER)))
    check("breakpoint remove", client.request("z0,%x,4" % IRQ_VECTOR) == "OK")
    stop = client.request("s")
    check("step", stop.startswith("T05") and client.register(PC_REGISTER) == handler, hex(client.register(PC_REGISTER)))
    client.send("c")
    time.sleep(0.5)
    client.connection.sendall(b"\x03")
    stop = client.receive()
    check("interrupt", stop.startswith("T02"), stop)
    data = bytes(range(256)) * 32
    full = "M0%x,%x:%s" % (SCRATCH, len(data) - 8, data[:-8].hex())
    check("full packet size", len(full) == PACKET_SIZE, str(len(full)))
    original = client.request("m%x,%x" % (SCRATCH, len(data) - 8))
    check("full packet write", client.request(full) == "OK")
    check("full packet read", client.request("m%x,%x" % (SCRATCH, len(data) - 8)) == data[:-8].hex())
    check("full packet restore", client.request("M%x,%x:%s" % (SCRATCH, len(data) - 8, original)) == "OK")
    client.send("k")


main()
