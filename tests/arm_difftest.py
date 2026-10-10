import argparse
import random
import struct
import subprocess
import sys

import unicorn
from unicorn import arm_const

WINDOW_BASE = 0x10000
WINDOW_SIZE = 0x4000
REGISTERS = [getattr(arm_const, f"UC_ARM_REG_R{i}") for i in range(13)] + [
    arm_const.UC_ARM_REG_SP,
    arm_const.UC_ARM_REG_LR,
    arm_const.UC_ARM_REG_PC,
]


def field(op, shift, width=4):
    return (op >> shift) & ((1 << width) - 1)


def classify(op):
    kind = field(op, 25, 3)
    if kind == 0:
        if op & 0x0FC000F0 == 0x00000090:
            return "multiply"
        if op & 0x0F8000F0 == 0x00800090:
            return "multiply_long"
        if op & 0x0FB00FF0 == 0x01000090:
            return "swap"
        if op & 0x0E000090 == 0x00000090:
            return "halfword"
        if op & 0x0FBF0FFF == 0x010F0000:
            return "mrs"
        if op & 0x0FB0FFF0 == 0x0120F000:
            return "msr"
        if op & 0x01900000 == 0x01000000:
            return None
        return "data"
    if kind == 1:
        if op & 0x0FB0F000 == 0x0320F000:
            return "msr"
        if op & 0x01900000 == 0x01000000:
            return None
        return "data"
    if kind == 2:
        return "single"
    if kind == 3:
        return None if op & 0x10 else "single"
    if kind == 4:
        return "block"
    if kind == 5:
        return "branch"
    return None


def predictable(op, kind):
    rn, rd, rs, rm = field(op, 16), field(op, 12), field(op, 8), field(op, 0)
    pre, writeback, loading = op >> 24 & 1, op >> 21 & 1, op >> 20 & 1
    if kind == "data":
        if not op & (1 << 25) and op & 0x10 and 15 in (rd, rn, rm, rs):
            return False
        return not (rd == 15 and op & (1 << 20))
    if kind == "multiply":
        return 15 not in (rn, rd, rs, rm) and rd != rm
    if kind == "multiply_long":
        return 15 not in (rn, rd, rs, rm) and rn != rd and rm not in (rn, rd)
    if kind == "swap":
        return 15 not in (rn, rd, rm) and rn not in (rd, rm)
    if kind == "halfword":
        kind_bits = field(op, 5, 2)
        if kind_bits == 0 or (not loading and kind_bits != 1):
            return False
        if rd == 15 or (not op & (1 << 22) and rm == 15):
            return False
        if (not pre or writeback) and (rn == 15 or rn == rd):
            return False
        return not (not pre and writeback)
    if kind == "single":
        if not pre and writeback:
            return False
        if op & (1 << 25) and rm == 15:
            return False
        if (not pre or writeback) and (rn == 15 or rn == rd):
            return False
        return not (not loading and rd == 15)
    if kind == "block":
        registers = op & 0xFFFF
        if not registers or rn == 15 or op & (1 << 22):
            return False
        if writeback and registers & (1 << rn):
            return False
        return not (not loading and registers & 0x8000)
    if kind == "msr":
        return not op & (1 << 16) and not op & (1 << 22)
    if kind == "mrs":
        return rd != 15 and not op & (1 << 22)
    return True


TEMPLATES = [
    (0x003FFF0F, 0x00000090),
    (0x007FFF0F, 0x00800090),
    (0x004FF00F, 0x01000090),
    (0x01FFFF6F, 0x00000090),
    (0x004F000F, 0x0120F000),
    (0x004F0FFF, 0x0320F000),
    (0x0040F000, 0x010F0000),
]


def random_instruction(rng):
    while True:
        op = rng.getrandbits(32)
        if rng.random() < 0.4:
            mask, bits = rng.choice(TEMPLATES)
            op = (op & mask) | bits | (op & 0xF0000000)
        if rng.random() < 0.6:
            op = (op & 0x0FFFFFFF) | 0xE0000000
        if op >> 28 == 0xF:
            continue
        kind = classify(op)
        if kind and predictable(op, kind):
            return op, kind


def random_state(rng):
    registers = []
    for _ in range(15):
        if rng.random() < 0.5:
            registers.append(WINDOW_BASE + 0x1000 + rng.randrange(0x2000))
        else:
            registers.append(rng.choice([0, 1, 31, 32, 33, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.getrandbits(32)]))
    flags = rng.getrandbits(4) << 28
    return registers, flags | 0x13 | 0xC0


def run_unicorn(registers, cpsr, op, memory):
    uc = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_ARM)
    uc.ctl_set_cpu_model(arm_const.UC_CPU_ARM_SA1110)
    uc.mem_map(WINDOW_BASE, WINDOW_SIZE)
    image = bytearray(memory)
    image[0:4] = struct.pack("<I", op)
    uc.mem_write(WINDOW_BASE, bytes(image))
    uc.reg_write(arm_const.UC_ARM_REG_CPSR, cpsr)
    for index, value in enumerate(registers):
        uc.reg_write(REGISTERS[index], value)
    uc.reg_write(arm_const.UC_ARM_REG_PC, WINDOW_BASE)
    unaligned = []

    def watch(uc, access, address, size, value, data):
        if address % size:
            unaligned.append(address)

    uc.hook_add(unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE, watch)
    try:
        uc.emu_start(WINDOW_BASE, WINDOW_BASE + 0x100000, count=1)
    except unicorn.UcError:
        return None
    if unaligned:
        return None
    result = [uc.reg_read(r) for r in REGISTERS]
    return result, uc.reg_read(arm_const.UC_ARM_REG_CPSR), bytes(uc.mem_read(WINDOW_BASE, WINDOW_SIZE))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("step")
    parser.add_argument("--cases", type=int, default=20000)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    step = subprocess.Popen([args.step], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    compared = failures = 0
    by_kind = {}
    for _ in range(args.cases):
        op, kind = random_instruction(rng)
        registers, cpsr = random_state(rng)
        memory = bytes(rng.getrandbits(8) for _ in range(64)) * (WINDOW_SIZE // 64)
        expected = run_unicorn(registers, cpsr, op, memory)
        if expected is None:
            continue
        step.stdin.write(struct.pack("<18I", *registers, 0, cpsr, op) + memory)
        step.stdin.flush()
        reply = step.stdout.read(18 * 4 + WINDOW_SIZE)
        values = struct.unpack("<18I", reply[:72])
        actual_memory = reply[72:]
        actual_registers, actual_cpsr = list(values[:16]), values[16]
        expected_registers, expected_cpsr, expected_memory = expected
        mask = 0xF00000DF if kind not in ("multiply", "multiply_long") else 0xD00000DF
        expected_memory = bytearray(expected_memory)
        actual_memory = bytearray(actual_memory)
        expected_memory[0:4] = actual_memory[0:4] = b"\0\0\0\0"
        compared += 1
        by_kind[kind] = by_kind.get(kind, 0) + 1
        problems = []
        for index in range(16):
            if actual_registers[index] != expected_registers[index]:
                problems.append(f"r{index} {actual_registers[index]:08x} != {expected_registers[index]:08x}")
        if (actual_cpsr & mask) != (expected_cpsr & mask):
            problems.append(f"cpsr {actual_cpsr:08x} != {expected_cpsr:08x}")
        if actual_memory != expected_memory:
            at = next(i for i in range(WINDOW_SIZE) if actual_memory[i] != expected_memory[i])
            problems.append(f"memory differs at {WINDOW_BASE + at:08x}")
        if problems:
            failures += 1
            if failures <= 30:
                print(f"{kind} {op:08x} cpsr={cpsr:08x} regs={' '.join(f'{r:08x}' for r in registers)}")
                for problem in problems:
                    print("   ", problem)
    step.stdin.close()
    step.wait()
    print(f"compared {compared}, failed {failures}: {by_kind}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
