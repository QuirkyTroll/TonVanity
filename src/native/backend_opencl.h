#pragma once
#include "tonvanity.h"
#include "backend_cpu.h"
#include <string>

struct OpenCLEngine;

OpenCLEngine* opencl_create(const TnvConfig& cfg, std::string& name, std::string& error);
void opencl_run(OpenCLEngine* e, CpuShared& sh);
void opencl_destroy(OpenCLEngine* e);
bool opencl_probe(std::string& name, std::string& error);
