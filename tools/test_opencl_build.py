#!/usr/bin/env python3
"""Compile kernels_opencl.cl on every OpenCL GPU and run one BIP-39 vector."""
import ctypes
import os
import sys

CL_SUCCESS = 0
CL_DEVICE_TYPE_GPU = 1 << 2
CL_DEVICE_TYPE_ALL = 0xFFFFFFFF
CL_DEVICE_NAME = 0x102B
CL_DEVICE_VERSION = 0x102F
CL_PLATFORM_NAME = 0x0902
CL_MEM_READ_ONLY = 1 << 2
CL_MEM_WRITE_ONLY = 1 << 1
CL_MEM_COPY_HOST_PTR = 1 << 5
CL_PROGRAM_BUILD_LOG = 0x1183
CL_TRUE = 1

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
clGetPlatformInfo = sig("clGetPlatformInfo", [P, U, S, P, ctypes.POINTER(S)])
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
clEnqueueWriteBuffer = sig("clEnqueueWriteBuffer", [P, P, U, S, S, P, U, P, P])
clEnqueueReadBuffer = sig("clEnqueueReadBuffer", [P, P, U, S, S, P, U, P, P])
clFinish = sig("clFinish", [P])
clReleaseMemObject = sig("clReleaseMemObject", [P])
clReleaseKernel = sig("clReleaseKernel", [P])
clReleaseProgram = sig("clReleaseProgram", [P])
clReleaseCommandQueue = sig("clReleaseCommandQueue", [P])
clReleaseContext = sig("clReleaseContext", [P])


def get_str(fn, handle, param):
    n = S()
    fn(handle, param, S(0), None, ctypes.byref(n))
    buf = ctypes.create_string_buffer(n.value)
    fn(handle, param, n, buf, None)
    return buf.value.decode("utf-8", "replace").rstrip("\x00")


def main():
    n = U()
    if clGetPlatformIDs(0, None, ctypes.byref(n)) != 0 or not n.value:
        print("no platforms")
        return 1
    plats = (P * n.value)()
    clGetPlatformIDs(n.value, plats, None)
    src = open(sys.argv[1] if len(sys.argv) > 1 else "kernels_opencl.cl", "rb").read()
    for pi, plat in enumerate(plats):
        plat = P(plat)
        pname = get_str(clGetPlatformInfo, plat, CL_PLATFORM_NAME)
        nd = U()
        if clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, None, ctypes.byref(nd)) != 0 or not nd.value:
            continue
        devs = (P * nd.value)()
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, nd.value, devs, None)
        for di, dev in enumerate(devs):
            dev = P(dev)
            dname = get_str(clGetDeviceInfo, dev, CL_DEVICE_NAME)
            dver = get_str(clGetDeviceInfo, dev, CL_DEVICE_VERSION)
            print(f"[{pi}:{di}] {dname}  {dver}")
            err = I()
            ctx = clCreateContext(None, 1, ctypes.byref(dev), None, None, ctypes.byref(err))
            if not ctx:
                print(f"  context failed {err.value}")
                continue
            queue = clCreateCommandQueue(ctx, dev, 0, ctypes.byref(err))
            prog = clCreateProgramWithSource(ctx, 1, ctypes.byref(ctypes.c_char_p(src)), None, ctypes.byref(err))
            if not prog:
                print(f"  program failed {err.value}")
                continue
            r = clBuildProgram(prog, 1, ctypes.byref(dev), b"-cl-std=CL1.2", None, None)
            nlog = S()
            clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, S(0), None, ctypes.byref(nlog))
            log = ctypes.create_string_buffer(nlog.value + 1)
            clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, nlog, log, None)
            text = log.value.decode("utf-8", "replace").strip()
            print(f"  build rc={r} log={text[:4000] if text else '(empty)'}")
            if r != 0:
                continue
            ker = clCreateKernel(prog, b"kn_bip39_12", ctypes.byref(err))
            print(f"  kn_bip39_12 kernel={bool(ker)} err={err.value}")
            if ker:
                # 16 zero bytes -> "abandon ... about"; W5R1 UQ.
                ent = (ctypes.c_ubyte * 16)()
                addrs = (ctypes.c_ubyte * 48)()
                here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
                words = None
                for cand in (os.path.join(here, "wordlist.bin"),
                             os.path.join(here, "..", ".work", "wordlist.bin")):
                    if os.path.exists(cand):
                        words = open(cand, "rb").read()
                        break
                if words is None:
                    print("  (wordlist.bin missing; kernel-only build check)")
                else:
                    code = bytes.fromhex("20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f")
                    m_words = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, len(words), ctypes.c_char_p(words), ctypes.byref(err))
                    m_ent = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 16, ent, ctypes.byref(err))
                    m_out = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, 48, None, ctypes.byref(err))
                    m_code = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32, ctypes.c_char_p(code), ctypes.byref(err))
                    for idx, mem in [(0, m_ent), (2, m_out), (3, m_words), (4, m_code)]:
                        p = P(mem)
                        clSetKernelArg(ker, idx, ctypes.sizeof(P()), ctypes.byref(p))
                    for idx, val in [(1, 1), (5, 1), (6, 0)]:
                        v = I(val)
                        clSetKernelArg(ker, idx, ctypes.sizeof(I), ctypes.byref(v))
                    g = S(1)
                    rc = clEnqueueNDRangeKernel(queue, ker, 1, None, ctypes.byref(g), None, 0, None, None)
                    clFinish(queue)
                    rc2 = clEnqueueReadBuffer(queue, m_out, 1, 0, 48, addrs, 0, None, None)
                    got = bytes(addrs).decode("latin1")
                    want = "UQBHyu-oZVDHRYQ1-rKlGqpHy5yAqanPBirEQNMNOmfHLtaT"
                    print(f"  launch rc={rc} read rc={rc2} addr={got} match={got == want}")
            clReleaseProgram(prog)
            clReleaseCommandQueue(queue)
            clReleaseContext(ctx)
    return 0


if __name__ == "__main__":
    sys.exit(main())
