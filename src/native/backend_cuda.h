#pragma once
#include "backend_cpu.h"
#include <string>

struct CudaEngine;
CudaEngine* cuda_create(const TnvConfig& cfg, std::string& name, std::string& error);
void cuda_run(CudaEngine* e, CpuShared& sh);
void cuda_destroy(CudaEngine* e);
bool cuda_probe(std::string& name, std::string& error);
bool cuda_debug_selftest(std::string& report);


