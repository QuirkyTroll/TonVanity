#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define __device__
#define __forceinline__ inline
#define __constant__
#define __shared__
#define __restrict__
#define __global__ static
struct TnvDim3 { unsigned x, y, z; };
static TnvDim3 threadIdx = {0,0,0};
static TnvDim3 blockIdx = {0,0,0};
static TnvDim3 blockDim = {0,0,0};
#define __syncthreads() ((void)0)

#include "fe.cu"
#include "ge.cu"
#include "sha512.cu"

extern "C" void tnv_ed25519_pub_from_seed(const unsigned char seed[32], unsigned char pk[32]) {
    unsigned char h[64];
    ::sha512(seed, 32, h);
    h[0] &= 248;
    h[31] &= 63;
    h[31] |= 64;
    ge_p3 A;
    ge_scalarmult_base(&A, h);
    ge_p3_tobytes(pk, &A);
}
