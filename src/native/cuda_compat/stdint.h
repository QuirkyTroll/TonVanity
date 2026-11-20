#ifndef TNV_CUDA_STDINT_H
#define TNV_CUDA_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long long intptr_t;
typedef unsigned long long uintptr_t;
#define UINT64_C(x) x##ULL
#define UINT32_C(x) x##U
#endif
