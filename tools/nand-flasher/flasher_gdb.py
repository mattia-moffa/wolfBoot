# Drives the nand_flasher over a J-Link GDB server connection.
# Invoked by flash_nand_max32666.sh as:
#   gdb -batch -nx nand_flasher.elf -x flasher_gdb.py
# with NAND_FILE, NAND_ADDR, NAND_VERIFY, JLINK_GDB_PORT in the environment.
import os
import gdb

BLOCK = 0x40000

CMD_INIT = 1
CMD_PROGRAM = 2
CMD_READ = 3
CMD_FINALIZE = 4
CMD_CHIPID = 5
CMD_FACTORY_RESET = 6

dump = os.environ.get("NAND_DUMP", "")
port = os.environ.get("JLINK_GDB_PORT", "2331")
if dump == "":
    fname = os.environ["NAND_FILE"]
    base = int(os.environ["NAND_ADDR"], 0)
    verify = os.environ.get("NAND_VERIFY", "1") != "0"

    if base % BLOCK:
        raise SystemExit("NAND address 0x%x is not aligned to the 0x%x block "
                         "size" % (base, BLOCK))

    with open(fname, "rb") as f:
        data = f.read()
    if not data:
        raise SystemExit("%s is empty" % fname)

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote localhost:%s" % port)
gdb.execute("monitor reset")
gdb.execute("monitor halt")
gdb.execute("load")
gdb.execute("hbreak flasher_ready")

inf = gdb.selected_inferior()
buf = int(gdb.parse_and_eval("(unsigned int)&fl_buf"))


def wait_ready():
    gdb.execute("continue")
    name = gdb.newest_frame().name()
    if name != "flasher_ready":
        pc = int(gdb.parse_and_eval("$pc"))
        raise SystemExit("stub stopped in %s at 0x%08x instead of "
                         "flasher_ready: fault on target" % (name, pc))


def run(cmd, addr=0, length=0, what="command", check=True):
    gdb.execute("set var fl_cmd.addr = 0x%x" % addr)
    gdb.execute("set var fl_cmd.len = %d" % length)
    gdb.execute("set var fl_cmd.cmd = %d" % cmd)
    wait_ready()
    status = int(gdb.parse_and_eval("fl_cmd.status"))
    if status != 0 and check:
        raise SystemExit("%s failed at 0x%08x: status %d"
                         % (what, addr, status))
    return status


wait_ready()
if dump != "":
    dump_addr = int(dump, 0)
    run(CMD_INIT, what="NAND init")
    run(CMD_READ, dump_addr, 32, "dump read")
    data = bytes(inf.read_memory(buf, 32))
    print("NAND @ 0x%08x: %s" % (dump_addr,
          " ".join("%02x" % b for b in data)))
    gdb.execute("monitor reset")
    gdb.execute("disconnect")
    raise SystemExit(0)
if os.environ.get("NAND_FACTORY_RESET", "0") == "1":
    print("factory reset: erasing whole NAND and rebuilding the bad block "
          "table (takes ~1 min)")
    run(CMD_FACTORY_RESET, what="factory reset")
    st = 0
else:
    st = run(CMD_INIT, what="NAND init", check=False)
if st != 0:
    if run(CMD_CHIPID, check=False) == 0:
        chip = bytes(inf.read_memory(buf, 2))
        chip = "%02x %02x" % (chip[0], chip[1])
    else:
        chip = "read failed"
    if st <= -1000:
        why = ("ONFI init OK, bad block table init failed with eZioResult %d "
               "(see ZioResult.h)" % -(st + 1000))
    else:
        why = "ONFI init failed, eZioResult %d (see ZioResult.h)" % st
    for label, cmd in (("GCR clkcn/perckcn0/perckcn1",
                        "x/1wx 0x40000008\nx/1wx 0x40000024\nx/1wx 0x40000048"),
                       ("SPI17Y0 regs", "x/16wx 0x400BE000"),
                       ("GPIO0 regs", "x/40wx 0x40008000")):
        print("--- %s" % label)
        print(gdb.execute(cmd, to_string=True))
    raise SystemExit("NAND init failed: %s; chip ID: %s" % (why, chip))

total = len(data)
for off in range(0, total, BLOCK):
    chunk = data[off:off + BLOCK]
    inf.write_memory(buf, chunk)
    run(CMD_PROGRAM, base + off, len(chunk), "program")
    print("programmed block 0x%08x (%d/%d bytes)"
          % (base + off, off + len(chunk), total))

run(CMD_FINALIZE, what="finalize")

if verify:
    for off in range(0, total, BLOCK):
        chunk = data[off:off + BLOCK]
        run(CMD_READ, base + off, len(chunk), "readback")
        if bytes(inf.read_memory(buf, len(chunk))) != chunk:
            raise SystemExit("verify mismatch in block at 0x%08x"
                             % (base + off))
    print("verify OK")

print("done: %d bytes written to NAND at 0x%08x" % (total, base))
gdb.execute("monitor reset")
gdb.execute("disconnect")
