#!/usr/bin/env python3
"""
lar_disas.py — L.A. Rush x86 call-graph tracer.

Loads the XBE, maps sections into virtual address space, and uses Capstone
to recursively disassemble x86 code starting from the entry point. Follows
direct calls and unconditional jumps to build a static call graph.

Forked from MANXFlatOut1's fo1_disas.py with L.A. Rush parameters:
  - kernel thunk table: 144 entries at 0x002A1620
  - code regions derived from the executable section flags at load time
    (the k9 build's section layout is not hardcoded)
  - the real entry (0x001B2594, header field 0x128 ^ retail key) is
    direct CRT code, so it is used as the trace root directly; the
    FlatOut-style init-table hypothesis is only a fallback

Usage:
    python3 tools/lar_disas.py game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe
"""

import struct
import sys
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

# ── L.A. Rush parameters ──────────────────────────────────────

KERNEL_THUNK_BASE = 0x002A1620
KERNEL_THUNK_END  = KERNEL_THUNK_BASE + 144 * 4

# Synthetic data-export region used by larush_kernel_shim.c
KDATA_BASE = 0x00F10000
KDATA_END  = 0x00F11000

# Filled in by load_xbe() from the section table.
CODE_REGIONS = []

# Kernel ordinal names (from larush_kernel_shim.c stub coverage)
ORDINAL_NAMES = {
    15: "ExAllocatePool",
    16: "ExAllocatePoolWithTag",
    24: "ExQueryPoolBlockSize",
    165: "MmAllocateContiguousMemory",
    166: "MmAllocateContiguousMemoryEx",
    171: "ExFreePool",
    187: "NtClose",
    190: "NtCreateFile",
    197: "NtDuplicateObject",
    202: "NtOpenFile",
    210: "NtReadFile",
    218: "NtQueryVolumeInformationFile",
    277: "RtlEnterCriticalSection",
    289: "RtlInitAnsiString",
    291: "RtlInitializeCriticalSection",
    294: "RtlLeaveCriticalSection",
}

# ── XBE loader ────────────────────────────────────────────────

def load_xbe(path):
    """Returns ({va: bytes}, entry, sections). Also fills CODE_REGIONS."""
    with open(path, 'rb') as f:
        data = f.read()

    base = struct.unpack_from('<I', data, 0x104)[0]
    # Real XBE header: entry point at 0x128, XORed with the retail or
    # debug key.  Pick whichever lands inside the image.
    entry_raw = struct.unpack_from('<I', data, 0x128)[0]
    size_of_image = struct.unpack_from('<I', data, 0x10C)[0]
    retail = entry_raw ^ 0xA8FC57AB
    debug = entry_raw ^ 0x94859D4B
    entry = retail if base <= retail < base + size_of_image else debug
    xor_key = 0xA8FC57AB if entry == retail else 0x94859D4B
    nsec = struct.unpack_from('<I', data, 0x11C)[0]
    hdr_off = struct.unpack_from('<I', data, 0x120)[0] - base

    print(f"XBE: base=0x{base:08X} entry=0x{entry:08X} "
          f"(raw=0x{entry_raw:08X} key=0x{xor_key:08X}) sections={nsec}")

    memory = {}
    sections = []
    for i in range(nsec):
        flags, va, vsize, raw, rsize, name_off = struct.unpack_from(
            '<IIIIII', data, hdr_off + i * 0x38)
        name = ''
        if name_off:
            end = data.find(b'\0', name_off - base if name_off >= base else name_off)
            noff = name_off - base if name_off >= base else name_off
            if 0 <= noff < len(data):
                end = data.find(b'\0', noff)
                name = data[noff:end].decode('ascii', errors='replace')
        if rsize > 0 and raw + rsize <= len(data):
            chunk = bytearray(data[raw:raw + rsize])
            if vsize > rsize:
                chunk.extend(b'\0' * (vsize - rsize))
            memory[va] = bytes(chunk)
            sections.append((va, vsize, name, flags))
        else:
            memory[va] = b'\0' * vsize
            sections.append((va, vsize, name, flags))

    print(f"Loaded {len(sections)} sections:")
    for va, vsize, name, flags in sections:
        executable = "X" if (flags & 0x00000004) else " "
        writable   = "W" if (flags & 0x00000002) else " "
        readable   = "R" if (flags & 0x00000001) else " "
        print(f"  0x{va:08X}  size={vsize:>10,}  [{readable}{writable}{executable}]  {name}")

    # Derive code regions from section flags (executable bit).  If the
    # image marks nothing executable (some XDK builds don't), fall back
    # to every loaded section so tracing still works.
    # .data/.data1 are flagged RWX in this build but are data — tracing
    # pointers into them produces garbage runs.
    NONCODE = ('.data', '.data1', '.rdata')
    CODE_REGIONS.clear()
    for va, vsize, name, flags in sections:
        if (flags & 0x00000004) and name not in NONCODE:
            CODE_REGIONS.append((va, va + vsize))
    if not CODE_REGIONS:
        print("(no sections flagged executable — treating all as code)")
        for va, vsize, name, flags in sections:
            CODE_REGIONS.append((va, va + vsize))

    return memory, entry, sections


# ── Memory reader ─────────────────────────────────────────────

def read_byte(memory, va):
    for base, chunk in memory.items():
        end = base + len(chunk)
        if base <= va < end:
            return chunk[va - base]
    return None


def read_dword(memory, va):
    b0 = read_byte(memory, va)
    b1 = read_byte(memory, va + 1)
    b2 = read_byte(memory, va + 2)
    b3 = read_byte(memory, va + 3)
    if b0 is None or b1 is None or b2 is None or b3 is None:
        return None
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)


def read_bytes(memory, va, count):
    result = bytearray()
    for i in range(count):
        b = read_byte(memory, va + i)
        if b is None:
            break
        result.append(b)
    return bytes(result)


# ── Disassembler ──────────────────────────────────────────────

def is_in_code(va):
    for start, end in CODE_REGIONS:
        if start <= va < end:
            return True
    return False


def is_kernel_thunk(va):
    return KERNEL_THUNK_BASE <= va < KERNEL_THUNK_END


def kernel_thunk_slot(va):
    return (va - KERNEL_THUNK_BASE) // 4


def is_call_insn(mnemonic):
    return mnemonic in ('call', 'jmp')


def is_ret_insn(mnemonic):
    return mnemonic in ('ret', 'retf', 'iret', 'iretd')


def is_unconditional_jmp(mnemonic, op_str):
    return mnemonic == 'jmp'


COND_JCC = ('ja','jae','jb','jbe','jc','jcxz','jecxz','je','jg','jge',
            'jl','jle','jna','jnae','jnb','jnbe','jnc','jne','jng','jnge',
            'jnl','jnle','jno','jnp','jns','jnz','jo','jp','jpe','jpo',
            'js','jz','loop','loope','loopne')


def disassemble_function(memory, start_va, max_insns=2000):
    """Recursive-descent within one function: follows BOTH arms of every
    conditional branch (worklist), so calls behind a jcc are not lost the
    way a linear first-ret scan loses them.

    Returns (insns, callees, terminal_type, fptrs) where fptrs are code
    pointers pushed as immediates (callbacks / thread start routines)."""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    insns = []
    callees = []
    fptrs = []
    seen_va = set()
    worklist = [start_va]
    term = 'ret'

    while worklist and len(insns) < max_insns:
        va = worklist.pop()
        while len(insns) < max_insns:
            if va in seen_va:
                break
            code = read_bytes(memory, va, 15)
            if len(code) == 0:
                term = 'fallthrough'
                break
            try:
                decoded = next(md.disasm(code, va))
            except StopIteration:
                term = 'unknown'
                break

            mnemonic = decoded.mnemonic
            op_str = decoded.op_str
            size = decoded.size
            seen_va.add(va)
            insns.append((va, mnemonic, op_str, size))

            target = None
            if decoded.operands:
                op0 = decoded.operands[0]
                if op0.type == X86_OP_IMM:
                    target = op0.imm
                elif op0.type == X86_OP_MEM:
                    if op0.mem.base == 0 and op0.mem.index == 0:
                        target = op0.mem.disp

            if mnemonic == 'push' and target is not None and \
                    is_in_code(target):
                fptrs.append(target)

            if target is not None:
                if is_kernel_thunk(target):
                    callees.append(target)
                elif is_call_insn(mnemonic):
                    if is_in_code(target):
                        callees.append(target)

            if is_ret_insn(mnemonic) or mnemonic in ('hlt', 'int3'):
                break
            if mnemonic in COND_JCC and target is not None:
                if target not in seen_va and is_in_code(target):
                    worklist.append(target)
                va += size
                continue
            if is_unconditional_jmp(mnemonic, op_str):
                if target is None or not is_in_code(target):
                    break
                # Tail-jump far away = tail call; nearby = intra-function.
                if abs(target - start_va) > 0x2000:
                    callees.append(target)
                    break
                va = target
                continue
            va += size

    insns.sort(key=lambda t: t[0])
    return insns, callees, term, fptrs


def trace_call_graph(memory, entry_va, max_depth=8, max_functions=200):
    functions = {}
    queue = [(entry_va, 0, 'entry')]
    visited = set()

    while queue and len(functions) < max_functions:
        va, depth, label = queue.pop(0)
        if va in visited:
            continue
        if not is_in_code(va):
            continue
        visited.add(va)

        insns, callees, term, fptrs = disassemble_function(memory, va)
        if not insns:
            continue

        kernel_calls = defaultdict(list)
        for callee in callees:
            if is_kernel_thunk(callee):
                slot = kernel_thunk_slot(callee)
                kernel_calls[slot].append(callee)

        kernel_ordinals = {}
        for slot in kernel_calls:
            ordinal_va = KERNEL_THUNK_BASE + slot * 4
            entry_val = read_dword(memory, ordinal_va)
            if entry_val is not None:
                if entry_val & 0x80000000:
                    kernel_ordinals[slot] = entry_val & 0x7FFFFFFF
                elif KDATA_BASE <= entry_val < KDATA_END:
                    kernel_ordinals[slot] = f"KDATA@0x{entry_val:08X}"
                else:
                    kernel_ordinals[slot] = f"VA=0x{entry_val:08X}"

        name = label if label != 'entry' else f"sub_0x{va:08X}"
        functions[va] = {
            'name': name,
            'start': va,
            'end': insns[-1][0] + insns[-1][3] if insns else va,
            'insn_count': len(insns),
            'term': term,
            'callees': [c for c in callees if not is_kernel_thunk(c)],
            'kernel_calls': kernel_calls,
            'kernel_ordinals': kernel_ordinals,
            'fptrs': fptrs,
            'depth': depth,
        }

        if depth < max_depth:
            for callee in functions[va]['callees']:
                if callee not in visited:
                    queue.append((callee, depth + 1, f"sub_0x{callee:08X}"))
            # Code pointers pushed as immediates are callbacks / thread
            # start routines — trace them as roots too.
            for fp in fptrs:
                if fp not in visited:
                    queue.append((fp, depth + 1, f"fptr_0x{fp:08X}"))

    return functions


def read_jump_table(memory, va, count=64):
    entries = []
    for i in range(count):
        ptr = read_dword(memory, va + i * 4)
        if ptr is None:
            break
        entries.append((i, ptr))
    return entries


def print_call_tree(functions, entry_va, indent=0, visited=None, max_depth=5):
    if visited is None:
        visited = set()
    if indent > max_depth * 2:
        return
    if entry_va not in functions:
        return
    f = functions[entry_va]

    prefix = "  " * indent
    kernel_str = ""
    if f['kernel_calls']:
        parts = []
        for slot in sorted(f['kernel_calls']):
            ordv = f['kernel_ordinals'].get(slot, '?')
            name = ORDINAL_NAMES.get(ordv, f"ord{ordv}") if isinstance(ordv, int) else str(ordv)
            parts.append(name)
        kernel_str = f"  [KERN: {', '.join(parts)}]"

    print(f"{prefix}├─ {f['name']} ({f['insn_count']} insns, →{f['term']}){kernel_str}")

    visited.add(entry_va)
    for callee in f['callees']:
        if callee not in visited and callee in functions:
            print_call_tree(functions, callee, indent + 1, visited, max_depth)


# ═══════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════

def main():
    if len(sys.argv) < 2:
        xbe = "game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe"
    else:
        xbe = sys.argv[1]

    print("═══ L.A. Rush XBE Disassembly Tracer ═══\n")
    memory, xbe_entry, sections = load_xbe(xbe)

    print("\n── Entry Point Analysis ──")
    print(f"XBE decoded entry: 0x{xbe_entry:08X}")

    ep_data = read_bytes(memory, xbe_entry, 64)
    if ep_data:
        print("Bytes at entry point:")
        for i in range(0, min(64, len(ep_data)), 16):
            hexstr = ' '.join(f'{b:02x}' for b in ep_data[i:i+16])
            print(f"  0x{xbe_entry + i:08X}: {hexstr}")

    # ── Hypothesis 1: the entry is direct code (unlike FlatOut). ──
    insns, callees, term, _ = disassemble_function(memory, xbe_entry, max_insns=5000)
    kernel_count = sum(1 for c in callees if is_kernel_thunk(c))
    sub_count = sum(1 for c in callees if not is_kernel_thunk(c))
    direct_score = (len(insns) + kernel_count * 20 + sub_count * 10) if insns else 0
    print(f"\nDirect-code probe: {len(insns)} insns, {kernel_count} kernel calls, "
          f"{sub_count} sub-calls, →{term}  (score={direct_score})")

    # ── Hypothesis 2: FlatOut-style init table of (func, type) pairs. ──
    jt = read_jump_table(memory, xbe_entry, 32)
    code_entries = [ptr for _, ptr in jt if ptr != 0 and is_in_code(ptr)]
    print(f"Init-table probe: {len(code_entries)} code pointers in first 32 dwords")

    # If the entry disassembles as a real function (terminates cleanly
    # in a reasonable span) it IS the CRT root — trust it over jump-table
    # pointers, which on L.A. Rush resolve into RWX .data and mis-decode
    # as giant garbage runs.  Only fall back to the init-table hypothesis
    # when the entry is not plausible code.
    entry_is_code = term in ('ret', 'jmp', 'thunk', 'jmp_outside') and \
                    len(insns) >= 10
    candidates = []
    if entry_is_code:
        candidates.append(xbe_entry)
    else:
        if direct_score >= 20:
            candidates.append(xbe_entry)
        candidates.extend(code_entries[:20])

    if not candidates:
        print("ERROR: entry is neither plausible code nor an init table!")
        return 1

    print("\n── Probing candidates for CRT startup ──")
    best_entry = None
    best_score = 0
    all_functions = {}

    for code_va in candidates:
        insns, callees, term, _ = disassemble_function(memory, code_va, max_insns=5000)
        if not insns or len(insns) < 5:
            continue
        kernel_count = sum(1 for c in callees if is_kernel_thunk(c))
        sub_count = sum(1 for c in callees if not is_kernel_thunk(c))
        score = len(insns) + kernel_count * 20 + sub_count * 10

        print(f"  probe 0x{code_va:08X}: {len(insns):4d} insns, "
              f"{kernel_count} kernel calls, {sub_count} sub-calls, "
              f"→{term}  (score={score})")

        if score > best_score:
            best_score = score
            best_entry = code_va
            all_functions = trace_call_graph(memory, code_va,
                                             max_depth=6, max_functions=150)

    if best_entry is None:
        print("ERROR: no viable CRT startup found!")
        return 1

    code_entry = best_entry
    functions = all_functions
    print(f"\nSelected CRT startup: 0x{code_entry:08X} (score={best_score})\n")

    sorted_funcs = sorted(functions.values(), key=lambda f: (f['depth'], f['start']))
    print(f"Found {len(functions)} functions:\n")
    for f in sorted_funcs:
        line = (f"{'  ' * f['depth']}{f['name']}  "
                f"0x{f['start']:08X}–0x{f['end']:08X}  "
                f"({f['insn_count']} insns, →{f['term']})")
        if f['kernel_calls']:
            parts = []
            for slot in sorted(f['kernel_calls']):
                ordv = f['kernel_ordinals'].get(slot, '?')
                name = ORDINAL_NAMES.get(ordv, f"ord{ordv}") if isinstance(ordv, int) else str(ordv)
                parts.append(name)
            line += f"  [KERN: {', '.join(parts)}]"
        print(line)

    print("\n── Call Tree ──\n")
    print_call_tree(functions, code_entry, max_depth=5)

    all_ordinals = set()
    for f in functions.values():
        for slot, ordv in f['kernel_ordinals'].items():
            if isinstance(ordv, int):
                all_ordinals.add(ordv)
    if all_ordinals:
        print(f"\n── Kernel Ordinals Referenced ({len(all_ordinals)}) ──")
        for o in sorted(all_ordinals):
            name = ORDINAL_NAMES.get(o, '')
            print(f"  ordinal {o:3d}  {name}")

    entry_func = functions.get(code_entry)
    if entry_func:
        main_candidates = []
        for callee in entry_func['callees']:
            if callee in functions:
                main_candidates.append((functions[callee]['insn_count'], callee))
        if main_candidates:
            main_candidates.sort(reverse=True)
            print("\n── Likely Main Candidates ──")
            for count, va in main_candidates[:3]:
                f = functions[va]
                print(f"  0x{va:08X}: {count} insns, depth={f['depth']}, "
                      f"calls {len(f['callees'])} sub-functions, "
                      f"ends with {f['term']}")

    print("\n── Done ──")
    return 0


if __name__ == '__main__':
    sys.exit(main())
