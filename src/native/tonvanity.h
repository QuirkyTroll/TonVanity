#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define TNV_VERSION "1.0.0"

typedef struct TnvConfig {
    int wallet_version;      /* 0=V4R2, 1=W5R1, 2=TGWallet */
    int mnemonic_scheme;     /* 0=12 words, 1=24 words */
    int bounceable;          /* 1=EQ, 0=UQ */
    int compute_mode;        /* 0=CPU only, 1=CPU+GPU, 2=GPU only */
    int case_sensitive;      /* 1=exact, 0=case-insensitive */
    int cpu_threads;         /* 0=all logical cores */
    char suffix[64];         /* base64url ending, 1..48 chars */
} TnvConfig;

typedef struct TnvStats {
    unsigned long long attempts;
    unsigned long long candidates;
    unsigned long long found;
    double elapsed_ms;
    double attempts_per_sec;
    double candidates_per_sec;
    int backend;             /* 0=CPU, 1=CUDA, 2=OpenCL */
    int backend_state;       /* 0=init, 1=running, 2=fallback, 3=error */
    char backend_name[64];
    char message[256];
} TnvStats;

typedef struct TnvMatch {
    char address[64];
    char mnemonic[512];
    int wallet_version;
    int mnemonic_scheme;
    int bounceable;
    int backend;
    unsigned long long attempts;
} TnvMatch;

void* tnv_create(const TnvConfig* cfg);
int   tnv_start(void* handle);
void  tnv_stop(void* handle);
int   tnv_poll(void* handle, TnvStats* stats, TnvMatch* matches, int max_matches);
void  tnv_destroy(void* handle);
const char* tnv_backend_info(void);
int   tnv_selftest(char* report, int cap);

#ifdef __cplusplus
}
#endif
