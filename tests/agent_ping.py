import socket
import struct
import sys
import time

COMMAND_PING = 1
REPLY_FLAG = 0x8000
SEQUENCE = 1


def connect(path, seconds):
    deadline = time.time() + seconds
    while True:
        try:
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.connect(path)
            return connection
        except OSError:
            connection.close()
            if time.time() > deadline:
                raise SystemExit("no agent socket at %s" % path)
            time.sleep(0.2)


def receive(connection, length):
    data = b""
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise SystemExit("the emulator closed the agent socket")
        data += chunk
    return data


def main():
    path, seconds = sys.argv[1], float(sys.argv[2])
    connection = connect(path, seconds)
    connection.settimeout(seconds)
    request = struct.pack("<HH", COMMAND_PING, SEQUENCE)
    connection.sendall(struct.pack("<I", len(request)) + request)
    try:
        while True:
            reply = receive(connection, struct.unpack("<I", receive(connection, 4))[0])
            command, sequence, status = struct.unpack_from("<HHI", reply)
            if command == COMMAND_PING | REPLY_FLAG and sequence == SEQUENCE and status == 0:
                version, size, wince = struct.unpack_from("<III", reply, 8)
                print("debugmgr protocol %d, _WIN32_WCE %d" % (version, wince))
                return
    except socket.timeout:
        raise SystemExit("debugmgr didn't answer")


main()
