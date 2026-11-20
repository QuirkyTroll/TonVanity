#!/usr/bin/env python3
"""Run the TON-24 OpenCL phase A/B against a known @ton/crypto vector."""
import ctypes
import os
import sys

CL_SUCCESS = 0
CL_DEVICE_TYPE_GPU = 1 << 2
CL_DEVICE_NAME = 0x102B
CL_MEM_READ_ONLY = 1 << 2
CL_MEM_READ_WRITE = 1 << 0
CL_MEM_WRITE_ONLY = 1 << 1
CL_MEM_COPY_HOST_PTR = 1 << 5
CL_PROGRAM_BUILD_LOG = 0x1183

P = ctypes.c_void_p
S = ctypes.c_size_t
U = ctypes.c_uint
I = ctypes.c_int
ULL = ctypes.c_ulonglong

lib = ctypes.WinDLL("OpenCL.dll")


def sig(name, args, rest=I):
    fn = getattr(lib, name)
    fn.argtypes = args
    fn.restype = rest
    return fn


clGetPlatformIDs = sig("clGetPlatformIDs", [U, ctypes.POINTER(P), ctypes.POINTER(U)])
clGetDeviceIDs = sig("clGetDeviceIDs", [P, ULL, U, ctypes.POINTER(P), ctypes.POINTER(U)])
clGetDeviceInfo = sig("clGetDeviceInfo", [P, U, S, P, ctypes.POINTER(S)])
clCreateContext = sig("clCreateContext", [P, U, ctypes.POINTER(P), P, P, ctypes.POINTER(I)], P)
clCreateCommandQueue = sig("clCreateCommandQueue", [P, P, ULL, ctypes.POINTER(I)], P)
clCreateProgramWithSource = sig("clCreateProgramWithSource", [P, U, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(S), ctypes.POINTER(I)], P)
clBuildProgram = sig("clBuildProgram", [P, U, ctypes.POINTER(P), ctypes.c_char_p, P, P])
clGetProgramBuildInfo = sig("clGetProgramBuildInfo", [P, P, U, S, P, ctypes.POINTER(S)])
clCreateBuffer = sig("clCreateBuffer", [P, ULL, S, P, ctypes.POINTER(I)], P)
clCreateKernel = sig("clCreateKernel", [P, ctypes.c_char_p, ctypes.POINTER(I)], P)
clSetKernelArg = sig("clSetKernelArg", [P, U, S, P])
clEnqueueNDRangeKernel = sig("clEnqueueNDRangeKernel", [P, P, U, ctypes.POINTER(S), ctypes.POINTER(S), ctypes.POINTER(S), U, P, P])
clEnqueueReadBuffer = sig("clEnqueueReadBuffer", [P, P, U, S, S, P, U, P, P])
clFinish = sig("clFinish", [P])


def get_str(fn, handle, param):
    n = S()
    fn(handle, param, S(0), None, ctypes.byref(n))
    buf = ctypes.create_string_buffer(n.value)
    fn(handle, param, n, buf, None)
    return buf.value.decode("utf-8", "replace").rstrip("\x00")


def set_buf(ker, idx, mem):
    p = P(mem)
    assert clSetKernelArg(ker, idx, ctypes.sizeof(P()), ctypes.byref(p)) == 0


def set_int(ker, idx, val):
    v = I(val)
    assert clSetKernelArg(ker, idx, ctypes.sizeof(I), ctypes.byref(v)) == 0


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = open(os.path.join(here, "src", "native", "kernels_opencl.cl"), "rb").read()
    words_blob = open(os.path.join(here, "wordlist.bin"), "rb").read()
    phrase = ("hospital stove relief fringe tongue always charge angry urge sentence again "
              "match nerve inquiry senior coconut label tumble carry category beauty bean road solution")
    index_of = {}
    for i in range(2048):
        index_of[words_blob[i * 9:(i + 1) * 9].rstrip(b"\0").decode()] = i
    idx = [index_of[w] for w in phrase.split()]
    bits = "".join(format(i, "011b") for i in idx)
    raw = bytes(int(bits[i:i + 8], 2) for i in range(0, 264, 8))
    assert len(raw) == 33

    vectors = [
        (1, "20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f", "UQDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw3Pg53h3h"),
        (0, "feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0", "UQAYZ-rchbaHK4IUkCvcyl3V8spcAnO8IBRCNvS3are-CbyQ"),
        (2, "9149ae51c1e4689710cebf7830297b16acfbadb363a920a537893e7ffeeca768", "UQCPnDc5RmfJM9AWefWJxjVjihzXC6889VZ-4YYN1QQ0eshX"),
    ]

    n = U()
    clGetPlatformIDs(0, None, ctypes.byref(n))
    plats = (P * n.value)()
    clGetPlatformIDs(n.value, plats, None)
    for pi, plat in enumerate(plats):
        plat = P(plat)
        nd = U()
        if clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, None, ctypes.byref(nd)) != 0 or not nd.value:
            continue
        devs = (P * nd.value)()
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, nd.value, devs, None)
        for di, dev in enumerate(devs):
            dev = P(dev)
            name = get_str(clGetDeviceInfo, dev, CL_DEVICE_NAME)
            print(f"[{pi}:{di}] {name}")
            err = I()
            ctx = clCreateContext(None, 1, ctypes.byref(dev), None, None, ctypes.byref(err))
            q = clCreateCommandQueue(ctx, dev, 0, ctypes.byref(err))
            prog = clCreateProgramWithSource(ctx, 1, ctypes.byref(ctypes.c_char_p(src)), None, ctypes.byref(err))
            rc = clBuildProgram(prog, 1, ctypes.byref(dev), b"-cl-std=CL1.2", None, None)
            if rc != 0:
                print("  build failed", rc)
                continue
            ka = clCreateKernel(prog, b"kn_ton24_a", ctypes.byref(err))
            kb = clCreateKernel(prog, b"kn_ton24_b", ctypes.byref(err))
            m_words = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, len(words_blob), ctypes.c_char_p(words_blob), ctypes.byref(err))
            m_raw = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 33, ctypes.c_char_p(raw), ctypes.byref(err))
            m_ve = clCreateBuffer(ctx, CL_MEM_READ_WRITE, 64, None, ctypes.byref(err))
            m_vi = clCreateBuffer(ctx, CL_MEM_READ_WRITE, 33, None, ctypes.byref(err))
            zero = (I * 1)(0)
            m_vc = clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, 4, zero, ctypes.byref(err))
            m_out = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, 48, None, ctypes.byref(err))

            set_buf(ka, 0, m_raw)
            set_int(ka, 1, 1)
            set_buf(ka, 2, m_words)
            set_buf(ka, 3, m_ve)
            set_buf(ka, 4, m_vi)
            set_buf(ka, 5, m_vc)
            g = S(1)
            rc = clEnqueueNDRangeKernel(q, ka, 1, None, ctypes.byref(g), None, 0, None, None)
            clFinish(q)
            vc = I()
            clEnqueueReadBuffer(q, m_vc, 1, 0, 4, ctypes.byref(vc), 0, None, None)
            print(f"  phase A rc={rc} valid={vc.value}")
            if vc.value != 1:
                continue
            for version, code_hex, want in vectors:
                code = bytes.fromhex(code_hex)
                m_code = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32, ctypes.c_char_p(code), ctypes.byref(err))
                set_buf(kb, 0, m_ve)
                set_buf(kb, 1, m_vi)
                set_int(kb, 2, 1)
                set_buf(kb, 3, m_out)
                set_buf(kb, 4, m_words)
                set_buf(kb, 5, m_code)
                set_int(kb, 6, version)
                set_int(kb, 7, 0)
                rc = clEnqueueNDRangeKernel(q, kb, 1, None, ctypes.byref(g), None, 0, None, None)
                clFinish(q)
                out = (ctypes.c_ubyte * 48)()
                clEnqueueReadBuffer(q, m_out, 1, 0, 48, out, 0, None, None)
                got = bytes(out).decode("latin1")
                print(f"  B v{version} rc={rc} addr={got} match={got == want}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
