"""在Unicorn Cortex-M指令模型运行真实业务测试ELF；不模拟HC32寄存器。"""
from pathlib import Path
import struct
import sys

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "tools" / "test_runtime"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_INTR
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_XPSR


def main():
    """加载散装ELF及向量表，处理半主机输出；限执行两千万条指令。"""
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    emulator.mem_map(0, 0x40000)
    emulator.mem_map(0x20000000, 0x10000)
    emulator.mem_map(0xE0000000, 0x100000)
    with (PROJECT_ROOT / "build" / "tests" / "test_firmware.axf").open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                contents = segment.data()
                emulator.mem_write(segment["p_paddr"], contents)
                if segment["p_vaddr"] != segment["p_paddr"]:
                    emulator.mem_write(segment["p_vaddr"], contents)
    stack_pointer, reset_pointer = struct.unpack("<II", emulator.mem_read(0, 8))
    emulator.reg_write(UC_ARM_REG_SP, stack_pointer)
    emulator.reg_write(UC_ARM_REG_XPSR, 0x01000000)
    output = []
    finished = []

    def read_string(address):
        """读取有上限的半主机NUL终止串，避免宿主机无界访问。"""
        result = bytearray()
        for offset in range(4096):
            value = emulator.mem_read(address + offset, 1)
            if value == b"\0":
                return result.decode("utf-8", errors="replace")
            result.extend(value)
        raise RuntimeError("Unterminated semihost string")

    def interrupt(machine, number, context):
        """仅实现测试运行库使用的半主机控制台与退出调用。"""
        operation = machine.reg_read(UC_ARM_REG_R0)
        argument = machine.reg_read(UC_ARM_REG_R1)
        pc = machine.reg_read(UC_ARM_REG_PC)
        if number != 7:
            raise RuntimeError(f"Unexpected exception {number}, PC={pc:#x}, LR={machine.reg_read(UC_ARM_REG_LR):#x}")
        if operation == 1:
            machine.reg_write(UC_ARM_REG_R0, 1)
        elif operation in (2, 9, 10, 12):
            machine.reg_write(UC_ARM_REG_R0, 0 if operation != 9 else 1)
        elif operation == 3:
            output.append(bytes(machine.mem_read(argument, 1)).decode("utf-8", errors="replace"))
        elif operation == 4:
            output.append(read_string(argument))
        elif operation == 5:
            handle, address, length = struct.unpack("<III", machine.mem_read(argument, 12))
            output.append(bytes(machine.mem_read(address, length)).decode("utf-8", errors="replace"))
            machine.reg_write(UC_ARM_REG_R0, 0)
        elif operation == 0x18:
            finished.append(argument)
            machine.emu_stop()
        elif operation == 0x20:
            finished.append(struct.unpack("<II", machine.mem_read(argument, 8)))
            machine.emu_stop()
        else:
            raise RuntimeError(f"Unsupported semihost call {operation:#x}")
        # Unicorn的BKPT回调PC仍指向断点；手动越过半主机指令。
        if machine.mem_read(pc, 2) == b"\xab\xbe":
            machine.reg_write(UC_ARM_REG_PC, (pc + 2) | 1)

    emulator.hook_add(UC_HOOK_INTR, interrupt)
    try:
        emulator.emu_start(reset_pointer, 0x40000, count=20000000)
    except Exception:
        pc = emulator.reg_read(UC_ARM_REG_PC)
        print(f"PC={pc:#x} LR={emulator.reg_read(UC_ARM_REG_LR):#x} bytes={bytes(emulator.mem_read(pc, 8)).hex()}")
        print("".join(output))
        raise
    transcript = "".join(output)
    (PROJECT_ROOT / "build" / "tests" / "unicorn.log").write_text(transcript, encoding="utf-8")
    print(transcript)
    if not finished or "failures=0" not in transcript:
        raise SystemExit("Tests failed or instruction budget exhausted")


if __name__ == "__main__":
    main()
