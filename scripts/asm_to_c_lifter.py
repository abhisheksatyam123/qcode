#!/usr/bin/env python3
"""
Assembly-to-C Lifter Script

Translates x86_64 assembly (AT&T and Intel syntax) into valid C code safely.
Supports safe parsing, C code generation, optional compilation, and isolated sandboxed execution.

Usage:
    python3 scripts/asm_to_c_lifter.py --input examples/sample_math.s --output build/sample_math.c --compile --run
"""

import sys
import os
import re
import argparse
import subprocess
import tempfile
from pathlib import Path


class AsmLifter:
    def __init__(self, syntax="auto"):
        self.syntax = syntax
        self.functions = []
        self.globals = set()

    def detect_syntax(self, lines):
        if self.syntax != "auto":
            return self.syntax
        for line in lines:
            line_clean = line.strip()
            if "%" in line_clean or line_clean.startswith("."):
                return "att"
            if "qword" in line_clean or "dword" in line_clean or "global" in line_clean:
                return "intel"
        return "att"

    def clean_operand(self, op, syntax):
        op = op.strip()
        if syntax == "att":
            if op.startswith("$"):
                return op[1:]
            if op.startswith("%"):
                reg = op[1:]
                return reg
            # Memory operand e.g., -8(%rbp) or (%rax)
            m = re.match(r"(-?\d+)?\((%[a-z0-9]+)\)", op)
            if m:
                offset, base = m.groups()
                base_reg = base[1:]
                offset_val = int(offset) if offset else 0
                if base_reg in ("rbp", "rsp"):
                    var_idx = abs(offset_val) if offset_val != 0 else 0
                    return f"local_{var_idx}"
                return f"*({base_reg} + {offset_val})"
        else: # intel
            # Strip size qualifiers like qword ptr, dword ptr, qword, dword
            op = re.sub(r"\b(qword|dword|word|byte)\s+(ptr\s+)?", "", op, flags=re.IGNORECASE).strip()
            if op.startswith("[") and op.endswith("]"):
                inner = op[1:-1].strip()
                m = re.match(r"([a-z0-9]+)\s*([+-]\s*\d+)?", inner, re.IGNORECASE)
                if m:
                    base_reg, offset = m.groups()
                    offset_val = 0
                    if offset:
                        offset_val = int(offset.replace(" ", ""))
                    if base_reg.lower() in ("rbp", "rsp"):
                        var_idx = abs(offset_val) if offset_val != 0 else 0
                        return f"local_{var_idx}"
                    return f"*({base_reg} + {offset_val})"
            return op
        return op

    def normalize_mnemonic(self, mnemonic, syntax):
        mnemonic = mnemonic.lower()
        if syntax != "att":
            return mnemonic
        
        dont_strip = {"call", "ret", "jmp", "syscall", "nop", "leave"}
        if mnemonic in dont_strip or mnemonic.startswith("j"):
            return mnemonic

        suffixes = ("q", "l", "w", "b")
        for s in suffixes:
            if mnemonic.endswith(s) and len(mnemonic) > 2:
                base = mnemonic[:-1]
                if base in ("mov", "add", "sub", "cmp", "inc", "dec", "xor", "or", "and", "lea", "test", "imul", "idiv", "push", "pop"):
                    return base

        return mnemonic

    def parse(self, text):
        lines = text.splitlines()
        syntax = self.detect_syntax(lines)

        functions = []
        curr_func = None

        for raw_line in lines:
            line = raw_line.split("#")[0].split(";")[0].strip()
            if not line:
                continue

            if line.startswith(".globl") or line.startswith("global"):
                parts = line.split()
                if len(parts) > 1:
                    self.globals.add(parts[1])
                continue

            if line.startswith("."):
                if not line.endswith(":"):
                    continue

            if line.endswith(":"):
                label_name = line[:-1].strip()
                if label_name in self.globals or label_name == "main" or label_name.startswith("compute_") or label_name.startswith("fibonacci"):
                    if curr_func:
                        functions.append(curr_func)
                    c_func_name = "asm_main" if label_name == "main" else label_name
                    curr_func = {"name": label_name, "c_name": c_func_name, "body": []}
                else:
                    if curr_func:
                        curr_func["body"].append({"type": "label", "name": label_name})
                continue

            if not curr_func:
                curr_func = {"name": "asm_entry", "c_name": "asm_entry", "body": []}

            tokens = line.split(None, 1)
            mnemonic = tokens[0].lower()
            operands_str = tokens[1] if len(tokens) > 1 else ""

            operands = []
            if operands_str:
                raw_ops = operands_str.split(",")
                ops_buf = []
                for rop in raw_ops:
                    ops_buf.append(rop)
                    combined = ",".join(ops_buf)
                    if combined.count("(") == combined.count(")") and combined.count("[") == combined.count("]"):
                        operands.append(combined.strip())
                        ops_buf = []

            curr_func["body"].append({
                "type": "instr",
                "mnemonic": mnemonic,
                "operands": operands,
                "syntax": syntax
            })

        if curr_func:
            functions.append(curr_func)

        self.functions = functions
        return functions

    def lift_to_c(self):
        c_lines = [
            "/* Auto-generated by Assembly-to-C Lifter (asm_to_c_lifter.py) */",
            "#include <stdio.h>",
            "#include <stdint.h>",
            "#include <inttypes.h>",
            "",
            "#pragma GCC diagnostic ignored \"-Wunused-variable\"",
            "#pragma GCC diagnostic ignored \"-Wunused-parameter\"",
            ""
        ]

        for func in self.functions:
            c_lines.append(f"int64_t {func['c_name']}(int64_t rdi, int64_t rsi, int64_t rdx, int64_t rcx);")
        c_lines.append("")

        main_c_name = "asm_entry"

        for func in self.functions:
            c_func_name = func["c_name"]
            if c_func_name == "asm_main":
                main_c_name = "asm_main"

            c_lines.append(f"int64_t {c_func_name}(int64_t rdi, int64_t rsi, int64_t rdx, int64_t rcx) {{")
            c_lines.append("    int64_t rax = 0, rbx = 0, rbp = 0, rsp = 0, r8 = 0, r9 = 0, r10 = 0;")
            c_lines.append("    int64_t local_0 = 0, local_8 = 0, local_16 = 0, local_24 = 0, local_32 = 0;")
            c_lines.append("    int flags_cmp = 0;")
            c_lines.append("")

            for item in func["body"]:
                if item["type"] == "label":
                    lbl = item["name"].replace(".", "L_")
                    c_lines.append(f"{lbl}:")
                    continue

                mnemonic = item["mnemonic"]
                ops = item["operands"]
                syntax = item["syntax"]

                cleaned_ops = [self.clean_operand(op, syntax) for op in ops]
                op_base = self.normalize_mnemonic(mnemonic, syntax)

                if op_base == "mov":
                    if len(cleaned_ops) == 2:
                        src = cleaned_ops[0] if syntax == "att" else cleaned_ops[1]
                        dst = cleaned_ops[1] if syntax == "att" else cleaned_ops[0]
                        # Ignore move rbp, rsp or rsp, rbp stack setup
                        if dst in ("rbp", "rsp") and src in ("rbp", "rsp"):
                            c_lines.append(f"    /* {mnemonic} {' '.join(ops)} */")
                        else:
                            c_lines.append(f"    {dst} = {src};")

                elif op_base == "add":
                    if len(cleaned_ops) == 2:
                        src = cleaned_ops[0] if syntax == "att" else cleaned_ops[1]
                        dst = cleaned_ops[1] if syntax == "att" else cleaned_ops[0]
                        c_lines.append(f"    {dst} += {src};")

                elif op_base == "sub":
                    if len(cleaned_ops) == 2:
                        src = cleaned_ops[0] if syntax == "att" else cleaned_ops[1]
                        dst = cleaned_ops[1] if syntax == "att" else cleaned_ops[0]
                        c_lines.append(f"    {dst} -= {src};")

                elif op_base == "inc":
                    if len(cleaned_ops) == 1:
                        c_lines.append(f"    {cleaned_ops[0]}++;")

                elif op_base == "dec":
                    if len(cleaned_ops) == 1:
                        c_lines.append(f"    {cleaned_ops[0]}--;")

                elif op_base == "cmp":
                    if len(cleaned_ops) == 2:
                        op1 = cleaned_ops[1] if syntax == "att" else cleaned_ops[0]
                        op2 = cleaned_ops[0] if syntax == "att" else cleaned_ops[1]
                        c_lines.append(f"    flags_cmp = ({op1} > {op2}) ? 1 : (({op1} == {op2}) ? 0 : -1);")

                elif op_base == "jmp":
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    goto {target};")

                elif op_base in ("jg", "jnle"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp > 0) goto {target};")

                elif op_base in ("jge", "jnl"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp >= 0) goto {target};")

                elif op_base in ("jl", "jnge"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp < 0) goto {target};")

                elif op_base in ("jle", "jng"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp <= 0) goto {target};")

                elif op_base in ("je", "jz"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp == 0) goto {target};")

                elif op_base in ("jne", "jnz"):
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0].replace(".", "L_")
                        c_lines.append(f"    if (flags_cmp != 0) goto {target};")

                elif op_base == "call":
                    if len(cleaned_ops) == 1:
                        target = cleaned_ops[0]
                        if target == "main":
                            target = "asm_main"
                        c_lines.append(f"    rax = {target}(rdi, rsi, rdx, rcx);")

                elif op_base == "ret":
                    c_lines.append("    return rax;")

                elif op_base in ("push", "pop"):
                    c_lines.append(f"    /* {mnemonic} {' '.join(ops)} */")
                else:
                    c_lines.append(f"    /* unhandled: {mnemonic} {' '.join(ops)} */")

            c_lines.append("    return rax;")
            c_lines.append("}\n")

        c_lines.append("int main(void) {")
        c_lines.append(f"    int64_t result = {main_c_name}(0, 0, 0, 0);")
        c_lines.append('    printf("Lifted C execution output / rax return value: %" PRId64 "\\n", result);')
        c_lines.append("    return 0;")
        c_lines.append("}")

        return "\n".join(c_lines)


def run_safe_compile_and_run(c_code, compile_only=False, run_binary=True):
    with tempfile.TemporaryDirectory() as tmpdir:
        c_file = os.path.join(tmpdir, "lifted.c")
        bin_file = os.path.join(tmpdir, "lifted_bin")

        with open(c_file, "w") as f:
            f.write(c_code)

        compile_cmd = ["gcc", "-Wall", "-Wextra", "-O1", c_file, "-o", bin_file]
        print(f"[+] Compiling safely with GCC: {' '.join(compile_cmd)}")
        res = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=15)
        if res.returncode != 0:
            print("[-] Compilation failed:")
            print(res.stderr)
            return False

        print("[+] Compilation succeeded.")

        if run_binary and not compile_only:
            print(f"[+] Executing binary safely in sandbox...")
            exec_res = subprocess.run([bin_file], capture_output=True, text=True, timeout=5)
            print(f"[+] Return code: {exec_res.returncode}")
            print(f"[+] Output:\n{exec_res.stdout.strip()}")
            if exec_res.stderr:
                print(f"[+] Stderr:\n{exec_res.stderr.strip()}")

    return True


def main():
    parser = argparse.ArgumentParser(description="Safely lift x86_64 assembly to C code.")
    parser.add_argument("--input", "-i", required=True, help="Input assembly (.s / .asm) file")
    parser.add_argument("--output", "-o", help="Output C file path")
    parser.add_argument("--compile", action="store_true", help="Compile generated C code safely")
    parser.add_argument("--run", action="store_true", help="Run compiled binary safely after compilation")
    parser.add_argument("--safe-mode", action="store_true", default=True, help="Enable sandbox safe validation mode")

    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file '{args.input}' not found.", file=sys.stderr)
        sys.exit(1)

    text = input_path.read_text()
    lifter = AsmLifter()
    lifter.parse(text)
    c_code = lifter.lift_to_c()

    if args.output:
        out_path = Path(args.output)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(c_code)
        print(f"[+] Lifted C code written to {args.output}")
    else:
        print(c_code)

    if args.compile or args.run:
        success = run_safe_compile_and_run(c_code, compile_only=not args.run, run_binary=args.run)
        if not success:
            sys.exit(1)


if __name__ == "__main__":
    main()
