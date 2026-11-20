#!/usr/bin/env python3
"""Build src/native/kernels_opencl.cl from the CUDA ed25519 sources.

The CUDA fe/ge code is plain C apart from ``__device__``; OpenCL 1.2 has no
generic address space, so the two places that read the constant base table
through a private pointer are rewritten to copy limb by limb.  The TON
pipeline lives in opencl_head.cl / opencl_tail.cl and is concatenated last.
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
native = root / "src" / "native"


def strip_cuda(text: str) -> str:
    out = []
    for line in text.splitlines():
        if re.match(r"\s*#include", line):
            continue
        if re.match(r"\s*#ifndef\s+ED25519", line):
            continue
        if re.match(r"\s*#define\s+ED25519", line):
            continue
        if re.match(r"\s*#endif\s*$", line):
            continue
        out.append(line)
    text = "\n".join(out)
    text = text.replace("__forceinline__", "inline")
    text = text.replace("__device__", "")
    text = text.replace("__constant__", "__constant")
    text = text.replace("__global__", "__kernel")
    # OpenCL scalar types keep the generated code free of long-long / stdint.
    for a, b in (
        ("unsigned long long", "ulong"),
        ("long long", "long"),
        ("unsigned short", "ushort"),
        ("unsigned int", "uint"),
        ("unsigned char", "uchar"),
        ("uint64_t", "ulong"),
        ("int64_t", "long"),
        ("uint32_t", "uint"),
        ("int32_t", "int"),
        ("uint16_t", "ushort"),
        ("int16_t", "short"),
        ("uint8_t", "uchar"),
        ("int8_t", "char"),
    ):
        text = re.sub(r"\b" + re.escape(a) + r"\b", b, text)
    return text


precomp = strip_cuda((native / "ed25519" / "precomp_data.h").read_text(encoding="utf-8"))
precomp = precomp.replace("static const ge_precomp", "__constant ge_precomp")
fe = strip_cuda((native / "ed25519" / "fe.cu").read_text(encoding="utf-8"))
ge = strip_cuda((native / "ed25519" / "ge_core.cu").read_text(encoding="utf-8"))

# Reading base[] through `const ge_precomp *` would be a private/constant
# address-space mismatch in OpenCL 1.2.  Copy the selected entry into the
# private temporary instead.
old = """    const ge_precomp *u = &base[pos][babs - 1];
    fe_copy(t->yplusx, u->yplusx);
    fe_copy(t->yminusx, u->yminusx);
    fe_copy(t->xy2d, u->xy2d);"""
new = """    __constant const ge_precomp *u = &base[pos][babs - 1];
    for (int k = 0; k < 10; ++k) {
        t->yplusx[k]  = u->yplusx[k];
        t->yminusx[k] = u->yminusx[k];
        t->xy2d[k]    = u->xy2d[k];
    }"""
if old not in ge:
    raise SystemExit("ge_select constant-table block not found")
ge = ge.replace(old, new)

parts = [
    (native / "opencl_head.cl").read_text(encoding="utf-8"),
    precomp,
    strip_cuda((native / "ed25519" / "common.cu").read_text(encoding="utf-8")),
    fe,
    ge,
    (native / "opencl_tail.cl").read_text(encoding="utf-8"),
]
out = "\n\n".join(parts)
(native / "kernels_opencl.cl").write_text(out, encoding="utf-8", newline="\n")
print(f"wrote {native / 'kernels_opencl.cl'} ({len(out)} bytes)")
