/* TonVanity OpenCL preamble.
   Shared by the runtime-compiled OpenCL program. Kept C-like on purpose:
   OpenCL 1.2 has no C++ features and no generic address space. */

typedef uchar  u8;
typedef ushort u16;
typedef uint   u32;
typedef ulong  u64;
typedef char   i8;
typedef short  i16;
typedef int    i32;
typedef long   i64;

typedef uchar  uint8_t;
typedef ushort uint16_t;
typedef uint   uint32_t;
typedef ulong  uint64_t;
typedef char   int8_t;
typedef short  int16_t;
typedef int    int32_t;
typedef long   int64_t;

typedef int fe[10];

typedef struct { fe X; fe Y; fe Z; }        ge_p2;
typedef struct { fe X; fe Y; fe Z; fe T; }  ge_p3;
typedef struct { fe X; fe Y; fe Z; fe T; }  ge_p1p1;
typedef struct { fe yplusx; fe yminusx; fe xy2d; }   ge_precomp;
typedef struct { fe yplusx; fe yminusx; fe z; fe t2d; } ge_niels;
typedef struct { fe yplusx; fe yminusx; fe t2d; }     ge_affine;

#define COMB_W 8
#define COMB_WINDOWS ((256 + COMB_W - 1) / COMB_W)
#define COMB_POS (1 << (COMB_W - 1))
#define COMB_TABLE_LEN (COMB_WINDOWS * COMB_POS)
#define KP_BATCH_MAX 32

typedef struct { int32_t limbs[COMB_TABLE_LEN * 3 * 10]; } ge_comb_table;

/* fe.cu needs these before its own definitions (fe_invert uses fe_sq/fe_mul). */
void fe_0(fe h);
void fe_1(fe h);
void fe_frombytes(fe h, const unsigned char *s);
void fe_tobytes(unsigned char *s, const fe h);
void fe_copy(fe h, const fe f);
int  fe_isnegative(const fe f);
int  fe_isnonzero(const fe f);
void fe_cmov(fe f, const fe g, unsigned int b);
void fe_neg(fe h, const fe f);
void fe_add(fe h, const fe f, const fe g);
void fe_sub(fe h, const fe f, const fe g);
void fe_mul(fe h, const fe f, const fe g);
void fe_sq(fe h, const fe f);
void fe_sq2(fe h, const fe f);
void fe_invert(fe out, const fe z);
void fe_pow22523(fe out, const fe z);

void ge_p3_tobytes(unsigned char *s, const ge_p3 *h);
void ge_p3_tobytes_inv(unsigned char *s, const fe X, const fe Y, const fe zinv);
void fe_batch_invert(fe zs[KP_BATCH_MAX], int n);
void ge_scalarmult_base(ge_p3 *h, const unsigned char *a);
void ge_scalarmult_base_comb(ge_p3 *h, const unsigned char *a, const ge_comb_table *table);

