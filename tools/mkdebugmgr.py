import sys


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: mkdebugmgr.py DEBUGMGR.EXE OUTPUT.c")
    with open(sys.argv[1], "rb") as file:
        image = file.read()
    lines = ['#include "rapi/debugmgr_image.h"', "", "const uint8_t DEBUGMGR[] = {"]
    for start in range(0, len(image), 16):
        lines.append("    " + " ".join(f"0x{byte:02X}," for byte in image[start:start + 16]))
    lines += ["};", "const size_t DEBUGMGR_SIZE = sizeof DEBUGMGR;", ""]
    with open(sys.argv[2], "w") as file:
        file.write("\n".join(lines))


main()
