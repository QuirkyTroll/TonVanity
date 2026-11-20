#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

typedef int nvrtcResult;
typedef void* nvrtcProgram;
typedef nvrtcResult (*PFN_create)(nvrtcProgram*, const char*, const char*, int, const char* const*, const char* const*);
typedef nvrtcResult (*PFN_compile)(nvrtcProgram, int, const char* const*);
typedef nvrtcResult (*PFN_getlogsize)(nvrtcProgram, size_t*);
typedef nvrtcResult (*PFN_getlog)(nvrtcProgram, char*);
typedef nvrtcResult (*PFN_getptxsize)(nvrtcProgram, size_t*);
typedef nvrtcResult (*PFN_getptx)(nvrtcProgram, char*);
typedef nvrtcResult (*PFN_destroy)(nvrtcProgram*);
typedef nvrtcResult (*PFN_version)(int*, int*);

static std::string read_file(const char* p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <nvrtc_dir> <include1> <include2> <source.cu> <out_prefix> [arch...]\n", argv[0]);
        return 2;
    }
    SetDllDirectoryA(argv[1]);
    std::string dll = std::string(argv[1]) + "\\nvrtc64_120_0.dll";
    HMODULE h = LoadLibraryA(dll.c_str());
    if (!h) h = LoadLibraryA("nvrtc64_120_0.dll");
    if (!h) { fprintf(stderr, "cannot load nvrtc: %lu\n", GetLastError()); return 3; }
    PFN_create create = (PFN_create)GetProcAddress(h, "nvrtcCreateProgram");
    PFN_compile compile = (PFN_compile)GetProcAddress(h, "nvrtcCompileProgram");
    PFN_getlogsize getlogsize = (PFN_getlogsize)GetProcAddress(h, "nvrtcGetProgramLogSize");
    PFN_getlog getlog = (PFN_getlog)GetProcAddress(h, "nvrtcGetProgramLog");
    PFN_getptxsize getptxsize = (PFN_getptxsize)GetProcAddress(h, "nvrtcGetPTXSize");
    PFN_getptx getptx = (PFN_getptx)GetProcAddress(h, "nvrtcGetPTX");
    PFN_destroy destroy = (PFN_destroy)GetProcAddress(h, "nvrtcDestroyProgram");
    PFN_version version = (PFN_version)GetProcAddress(h, "nvrtcVersion");
    if (!create || !compile || !getptxsize || !getptx || !destroy) { fprintf(stderr, "missing nvrtc symbols\n"); return 4; }
    int vmaj = 0, vmin = 0; if (version) version(&vmaj, &vmin);
    printf("NVRTC %d.%d\n", vmaj, vmin);
    std::string src = read_file(argv[4]);
    std::vector<std::string> archs;
    for (int i = 6; i < argc; ++i) archs.push_back(argv[i]);
    if (archs.empty()) { archs.push_back("compute_75"); archs.push_back("compute_86"); archs.push_back("compute_89"); archs.push_back("compute_120"); }
    for (auto& arch : archs) {
        nvrtcProgram prog = nullptr;
        nvrtcResult r = create(&prog, src.c_str(), "kernels.cu", 0, nullptr, nullptr);
        if (r != 0) { fprintf(stderr, "nvrtcCreateProgram failed %d\n", (int)r); return 5; }
        std::string inc1 = std::string("-I") + argv[2];
        std::string inc2 = std::string("-I") + argv[3];
        std::string archopt = "--gpu-architecture=" + arch;
        const char* opts[] = { archopt.c_str(), "--std=c++14", inc1.c_str(), inc2.c_str(),
                               "-D__STDC_VERSION__=199901L", "-D__STDC__=1", "--device-as-default-execution-space" };
        r = compile(prog, (int)(sizeof(opts)/sizeof(opts[0])), opts);
        if (r != 0) {
            size_t lsz = 0; if (getlogsize && getlogsize(prog, &lsz) == 0 && lsz > 0) {
                std::string log(lsz, 0); if (getlog(prog, &log[0]) == 0) fprintf(stderr, "%s\n", log.c_str());
            }
            fprintf(stderr, "nvrtcCompileProgram failed for %s: %d\n", arch.c_str(), (int)r);
            return 6;
        }
        size_t psz = 0; r = getptxsize(prog, &psz);
        if (r != 0) { fprintf(stderr, "getPTXSize failed %d\n", (int)r); return 7; }
        std::string ptx(psz, 0); r = getptx(prog, &ptx[0]);
        if (r != 0) { fprintf(stderr, "getPTX failed %d\n", (int)r); return 8; }
        std::string out = std::string(argv[5]) + "_" + arch + ".ptx";
        std::ofstream f(out, std::ios::binary); f.write(ptx.data(), (std::streamsize)ptx.size()); f.close();
        printf("wrote %s (%zu bytes)\n", out.c_str(), ptx.size());
        destroy(&prog);
    }
    return 0;
}
