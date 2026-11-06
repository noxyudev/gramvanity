// EN: Ed25519 public-key derivation on the GPU: [s]B with s = clamp(SHA-512(seed)).
// RU: Вывод публичного ключа Ed25519 на GPU: [s]B, где s = clamp(SHA-512(seed)).
//
// EN: Fixed base, signed 4-bit windows: s = sum d[i]*16^i with d[i] in [-8,8], so
// EN: [s]B = sum d[i]*[16^i]B. The host precomputes T[i][j] = [j*16^i]B for j = 1..8 in
// EN: affine coordinates and uploads (x, y, c = 2*d*x*y) as three 10-limb field elements
// EN: per point, 512 points. Each digit is one mixed addition (8 multiplications) and
// EN: there are no doublings; the final affine y needs one inversion.
// RU: Фиксированная база, знаковые 4-битные окна: s = sum d[i]*16^i, d[i] из [-8,8], поэтому
// RU: [s]B = sum d[i]*[16^i]B. Хост заранее считает T[i][j] = [j*16^i]B для j = 1..8 в
// RU: аффинных координатах и загружает (x, y, c = 2*d*x*y) как три 10-лимбовых элемента поля
// RU: на точку, 512 точек. Каждая цифра — одно смешанное сложение (8 умножений), удвоений
// RU: нет; для итогового аффинного y нужно одно обращение.
//
// EN: Field: radix 2^25.5, ten signed 32-bit limbs with 64-bit products (the ref10 layout,
// EN: public domain by D. J. Bernstein et al.). The 5x51-bit donna layout used on the CPU
// EN: needs 64x64-bit products, which are slow on consumer GPUs.
// RU: Поле: radix 2^25.5, десять знаковых 32-битных лимбов с 64-битными произведениями (раскладка
// RU: ref10, public domain Д. Д. Бернштейна и др.). Раскладка donna 5x51 бит, применяемая на CPU,
// RU: требует 64x64-битных произведений, а они медленны на потребительских GPU.
//
// EN: table: [0..9] = 2d, then point (i*8 + j-1) at 10 + (i*8 + j-1)*30: x, y, 2d*x*y.
// EN: All limbs are ints in [-2^25, 2^25); the host serialises them as int32.
// RU: table: [0..9] = 2d, затем точка (i*8 + j-1) по смещению 10 + (i*8 + j-1)*30: x, y, 2d*x*y.
// RU: Все лимбы — int в [-2^25, 2^25); хост сериализует их как int32.

typedef struct { int v[10]; } fe10;

static inline void fe10_copy(fe10 *r, const fe10 *a) { for (int i = 0; i < 10; i++) r->v[i] = a->v[i]; }
static inline void fe10_zero(fe10 *r) { for (int i = 0; i < 10; i++) r->v[i] = 0; }
static inline void fe10_one(fe10 *r) { r->v[0] = 1; for (int i = 1; i < 10; i++) r->v[i] = 0; }
static inline void fe10_add(fe10 *r, const fe10 *a, const fe10 *b) { for (int i = 0; i < 10; i++) r->v[i] = a->v[i] + b->v[i]; }
static inline void fe10_sub(fe10 *r, const fe10 *a, const fe10 *b) { for (int i = 0; i < 10; i++) r->v[i] = a->v[i] - b->v[i]; }
static inline void fe10_neg(fe10 *r, const fe10 *a) { for (int i = 0; i < 10; i++) r->v[i] = -a->v[i]; }

// EN: h = f*g mod 2^255-19. Inputs: |f_i|,|g_i| < 1.1*2^26 (ref10's bound); output: |h_i| < 2^25.
// RU: h = f*g mod 2^255-19. Входы: |f_i|,|g_i| < 1.1*2^26 (граница ref10); выход: |h_i| < 2^25.
static void fe10_mul(fe10 *h, const fe10 *f, const fe10 *g) {
    long f0 = f->v[0], f1 = f->v[1], f2 = f->v[2], f3 = f->v[3], f4 = f->v[4];
    long f5 = f->v[5], f6 = f->v[6], f7 = f->v[7], f8 = f->v[8], f9 = f->v[9];
    long g0 = g->v[0], g1 = g->v[1], g2 = g->v[2], g3 = g->v[3], g4 = g->v[4];
    long g5 = g->v[5], g6 = g->v[6], g7 = g->v[7], g8 = g->v[8], g9 = g->v[9];
    long g1_19 = 19 * g1, g2_19 = 19 * g2, g3_19 = 19 * g3, g4_19 = 19 * g4, g5_19 = 19 * g5;
    long g6_19 = 19 * g6, g7_19 = 19 * g7, g8_19 = 19 * g8, g9_19 = 19 * g9;
    long f1_2 = 2 * f1, f3_2 = 2 * f3, f5_2 = 2 * f5, f7_2 = 2 * f7, f9_2 = 2 * f9;

    long h0 = f0*g0 + f1_2*g9_19 + f2*g8_19 + f3_2*g7_19 + f4*g6_19 + f5_2*g5_19 + f6*g4_19 + f7_2*g3_19 + f8*g2_19 + f9_2*g1_19;
    long h1 = f0*g1 + f1*g0 + f2*g9_19 + f3*g8_19 + f4*g7_19 + f5*g6_19 + f6*g5_19 + f7*g4_19 + f8*g3_19 + f9*g2_19;
    long h2 = f0*g2 + f1_2*g1 + f2*g0 + f3_2*g9_19 + f4*g8_19 + f5_2*g7_19 + f6*g6_19 + f7_2*g5_19 + f8*g4_19 + f9_2*g3_19;
    long h3 = f0*g3 + f1*g2 + f2*g1 + f3*g0 + f4*g9_19 + f5*g8_19 + f6*g7_19 + f7*g6_19 + f8*g5_19 + f9*g4_19;
    long h4 = f0*g4 + f1_2*g3 + f2*g2 + f3_2*g1 + f4*g0 + f5_2*g9_19 + f6*g8_19 + f7_2*g7_19 + f8*g6_19 + f9_2*g5_19;
    long h5 = f0*g5 + f1*g4 + f2*g3 + f3*g2 + f4*g1 + f5*g0 + f6*g9_19 + f7*g8_19 + f8*g7_19 + f9*g6_19;
    long h6 = f0*g6 + f1_2*g5 + f2*g4 + f3_2*g3 + f4*g2 + f5_2*g1 + f6*g0 + f7_2*g9_19 + f8*g8_19 + f9_2*g7_19;
    long h7 = f0*g7 + f1*g6 + f2*g5 + f3*g4 + f4*g3 + f5*g2 + f6*g1 + f7*g0 + f8*g9_19 + f9*g8_19;
    long h8 = f0*g8 + f1_2*g7 + f2*g6 + f3_2*g5 + f4*g4 + f5_2*g3 + f6*g2 + f7_2*g1 + f8*g0 + f9_2*g9_19;
    long h9 = f0*g9 + f1*g8 + f2*g7 + f3*g6 + f4*g5 + f5*g4 + f6*g3 + f7*g2 + f8*g1 + f9*g0;

    long c;
    c = (h0 + (1L << 25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + (1L << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + (1L << 24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + (1L << 24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + (1L << 25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + (1L << 25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + (1L << 24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + (1L << 24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + (1L << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + (1L << 25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + (1L << 24)) >> 25; h0 += 19 * c; h9 -= c << 25;
    c = (h0 + (1L << 25)) >> 26; h1 += c; h0 -= c << 26;

    h->v[0] = (int)h0; h->v[1] = (int)h1; h->v[2] = (int)h2; h->v[3] = (int)h3; h->v[4] = (int)h4;
    h->v[5] = (int)h5; h->v[6] = (int)h6; h->v[7] = (int)h7; h->v[8] = (int)h8; h->v[9] = (int)h9;
}

static void fe10_sq(fe10 *h, const fe10 *f) { fe10_mul(h, f, f); }

// EN: Canonical 32-byte little-endian encoding (ref10 fe_tobytes).
// RU: Каноническое 32-байтное little-endian кодирование (ref10 fe_tobytes).
static void fe10_tobytes(uchar *s, const fe10 *h) {
    long h0 = h->v[0], h1 = h->v[1], h2 = h->v[2], h3 = h->v[3], h4 = h->v[4];
    long h5 = h->v[5], h6 = h->v[6], h7 = h->v[7], h8 = h->v[8], h9 = h->v[9];
    long q, c;
    q = (19 * h9 + (1L << 24)) >> 25;
    q = (h0 + q) >> 26;
    q = (h1 + q) >> 25;
    q = (h2 + q) >> 26;
    q = (h3 + q) >> 25;
    q = (h4 + q) >> 26;
    q = (h5 + q) >> 25;
    q = (h6 + q) >> 26;
    q = (h7 + q) >> 25;
    q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;
    h0 += 19 * q;

    c = h0 >> 26; h1 += c; h0 -= c << 26;
    c = h1 >> 25; h2 += c; h1 -= c << 25;
    c = h2 >> 26; h3 += c; h2 -= c << 26;
    c = h3 >> 25; h4 += c; h3 -= c << 25;
    c = h4 >> 26; h5 += c; h4 -= c << 26;
    c = h5 >> 25; h6 += c; h5 -= c << 25;
    c = h6 >> 26; h7 += c; h6 -= c << 26;
    c = h7 >> 25; h8 += c; h7 -= c << 25;
    c = h8 >> 26; h9 += c; h8 -= c << 26;
    c = h9 >> 25;         h9 -= c << 25;

    s[0]  = (uchar)h0;
    s[1]  = (uchar)(h0 >> 8);
    s[2]  = (uchar)(h0 >> 16);
    s[3]  = (uchar)((h0 >> 24) | (h1 << 2));
    s[4]  = (uchar)(h1 >> 6);
    s[5]  = (uchar)(h1 >> 14);
    s[6]  = (uchar)((h1 >> 22) | (h2 << 3));
    s[7]  = (uchar)(h2 >> 5);
    s[8]  = (uchar)(h2 >> 13);
    s[9]  = (uchar)((h2 >> 21) | (h3 << 5));
    s[10] = (uchar)(h3 >> 3);
    s[11] = (uchar)(h3 >> 11);
    s[12] = (uchar)((h3 >> 19) | (h4 << 6));
    s[13] = (uchar)(h4 >> 2);
    s[14] = (uchar)(h4 >> 10);
    s[15] = (uchar)(h4 >> 18);
    s[16] = (uchar)h5;
    s[17] = (uchar)(h5 >> 8);
    s[18] = (uchar)(h5 >> 16);
    s[19] = (uchar)((h5 >> 24) | (h6 << 1));
    s[20] = (uchar)(h6 >> 7);
    s[21] = (uchar)(h6 >> 15);
    s[22] = (uchar)((h6 >> 23) | (h7 << 3));
    s[23] = (uchar)(h7 >> 5);
    s[24] = (uchar)(h7 >> 13);
    s[25] = (uchar)((h7 >> 21) | (h8 << 4));
    s[26] = (uchar)(h8 >> 4);
    s[27] = (uchar)(h8 >> 12);
    s[28] = (uchar)((h8 >> 20) | (h9 << 6));
    s[29] = (uchar)(h9 >> 2);
    s[30] = (uchar)(h9 >> 10);
    s[31] = (uchar)(h9 >> 18);
}

static inline int fe10_isnegative(const fe10 *f) {
    uchar s[32];
    fe10_tobytes(s, f);
    return s[0] & 1;
}

// EN: 1/z mod 2^255-19, z^(p-2) (ref10's addition chain: 254 squarings, 11 multiplications).
// RU: 1/z mod 2^255-19, z^(p-2) (цепочка сложений ref10: 254 возведения в квадрат, 11 умножений).
static void fe10_invert(fe10 *out, const fe10 *z) {
    fe10 t0, t1, t2, t3;
    fe10_sq(&t0, z);
    fe10_sq(&t1, &t0);
    fe10_sq(&t1, &t1);
    fe10_mul(&t1, z, &t1);
    fe10_mul(&t0, &t0, &t1);
    fe10_sq(&t2, &t0);
    fe10_mul(&t1, &t1, &t2);
    fe10_sq(&t2, &t1);
    for (int i = 1; i < 5; i++) fe10_sq(&t2, &t2);
    fe10_mul(&t1, &t2, &t1);
    fe10_sq(&t2, &t1);
    for (int i = 1; i < 10; i++) fe10_sq(&t2, &t2);
    fe10_mul(&t2, &t2, &t1);
    fe10_sq(&t3, &t2);
    for (int i = 1; i < 20; i++) fe10_sq(&t3, &t3);
    fe10_mul(&t2, &t3, &t2);
    fe10_sq(&t2, &t2);
    for (int i = 1; i < 10; i++) fe10_sq(&t2, &t2);
    fe10_mul(&t1, &t2, &t1);
    fe10_sq(&t2, &t1);
    for (int i = 1; i < 50; i++) fe10_sq(&t2, &t2);
    fe10_mul(&t2, &t2, &t1);
    fe10_sq(&t3, &t2);
    for (int i = 1; i < 100; i++) fe10_sq(&t3, &t3);
    fe10_mul(&t2, &t3, &t2);
    fe10_sq(&t2, &t2);
    for (int i = 1; i < 50; i++) fe10_sq(&t2, &t2);
    fe10_mul(&t1, &t2, &t1);
    fe10_sq(&t1, &t1);
    for (int i = 1; i < 5; i++) fe10_sq(&t1, &t1);
    fe10_mul(out, &t1, &t0); // EN: t0 = z^11, so out = z^(2^255-32+11) = z^(p-2)
                             // RU: t0 = z^11, значит out = z^(2^255-32+11) = z^(p-2)
}

// EN: Mixed addition: (X:Y:Z:T) += (+-x2, y2) with the table's c = 2*d*x2*y2 (add-2008-hwcd-3, a=-1).
// RU: Смешанное сложение: (X:Y:Z:T) += (+-x2, y2) с c = 2*d*x2*y2 из таблицы (add-2008-hwcd-3, a=-1).
static void ed10_add_mixed(fe10 *X, fe10 *Y, fe10 *Z, fe10 *T,
                           const fe10 *x2, const fe10 *y2, const fe10 *c2, int neg) {
    fe10 yx, yy, cc;
    if (neg) {
        fe10_sub(&yx, y2, x2); // EN: y2 + (s*x2), s = -1 | RU: y2 + (s*x2), s = -1
        fe10_add(&yy, y2, x2); // EN: y2 - (s*x2) | RU: y2 - (s*x2)
        fe10_neg(&cc, c2);
    } else {
        fe10_add(&yx, y2, x2);
        fe10_sub(&yy, y2, x2);
        fe10_copy(&cc, c2);
    }
    fe10 t1, t2, A, B, C, D, E, F, G, H;
    fe10_sub(&t1, Y, X);
    fe10_add(&t2, Y, X);
    fe10_mul(&A, &t1, &yy);
    fe10_mul(&B, &t2, &yx);
    fe10_mul(&C, T, &cc);
    fe10_add(&D, Z, Z);
    fe10_sub(&E, &B, &A);
    fe10_sub(&F, &D, &C);
    fe10_add(&G, &D, &C);
    fe10_add(&H, &B, &A);
    fe10_mul(X, &E, &F);
    fe10_mul(Y, &G, &H);
    fe10_mul(T, &E, &H);
    fe10_mul(Z, &F, &G);
}

// EN: out = compressed [clamp(SHA-512(seed))]B from the four big-endian private-key words H[0..3].
// RU: out = сжатая точка [clamp(SHA-512(seed))]B по четырём big-endian словам приватного ключа H[0..3].
static void ed10_public(ulong *H, __global const int *tab, uchar out[32]) {
    H[0] &= 0xF8FFFFFFFFFFFFFFUL; // EN: h[0] &= 248 | RU: h[0] &= 248
    H[3] = (H[3] & ~0xFFUL) | ((H[3] & 0x3FUL) | 0x40UL); // EN: h[31] &= 63; h[31] |= 64
                                                          // RU: h[31] &= 63; h[31] |= 64

    fe10 X, Y, Z, T;
    fe10_zero(&X); fe10_one(&Y); fe10_one(&Z); fe10_zero(&T);
    int carry = 0;
    for (int i = 0; i < 64; i++) {
        // EN: nibble i of the scalar, then the signed radix-16 recoding (digits in [-8,7],
        // EN: the top one in [0,8]); done in one pass so no digit array is needed
        // RU: ниббл i скаляра, затем знаковое перекодирование radix-16 (цифры в [-8,7],
        // RU: старшая в [0,8]); всё за один проход, поэтому массив цифр не нужен
        uint by = (uint)(H[i >> 4] >> (56 - 8 * ((i >> 1) & 7))) & 0xFFu;
        uint nib = (i & 1) ? (by >> 4) : (by & 15u);
        int t = (int)nib + carry, dd;
        if (i == 63) {
            dd = t;
        } else {
            carry = (t + 8) >> 4;
            dd = t - (carry << 4);
        }
        if (!dd) continue;
        int neg = dd < 0, j = neg ? -dd : dd;
        __global const int *p = tab + 10 + (i * 8 + (j - 1)) * 30;
        fe10 x2, y2, c2;
        for (int k = 0; k < 10; k++) {
            x2.v[k] = p[k];
            y2.v[k] = p[10 + k];
            c2.v[k] = p[20 + k];
        }
        ed10_add_mixed(&X, &Y, &Z, &T, &x2, &y2, &c2, neg);
    }

    fe10 zi, ax, ay;
    fe10_invert(&zi, &Z);
    fe10_mul(&ax, &X, &zi);
    fe10_mul(&ay, &Y, &zi);
    fe10_tobytes(out, &ay);
    out[31] ^= (uchar)(fe10_isnegative(&ax) << 7);
}

// EN: priv: stride words per candidate, 32 key bytes at words [offset, offset+4).
// EN: pub: 32 bytes per candidate.
// RU: priv: stride слов на кандидата, 32 байта ключа в словах [offset, offset+4).
// RU: pub: 32 байта на кандидата.
__kernel void edpub(__global const ulong *priv, uint stride, uint offset,
                    __global const int *tab, __global uchar *pub, uint n) {
    uint gid = get_global_id(0);
    if (gid >= n) return;
    size_t base = (size_t)gid * stride + offset;
    ulong w[16], H[8];
    w[0] = priv[base + 0];
    w[1] = priv[base + 1];
    w[2] = priv[base + 2];
    w[3] = priv[base + 3];
    w[4] = 0x8000000000000000UL;
    for (int j = 5; j < 15; j++) w[j] = 0;
    w[15] = 256; // EN: SHA-512 of a 32-byte message | RU: SHA-512 от 32-байтного сообщения
    for (int j = 0; j < 8; j++) H[j] = IV512[j];
    compress(H, w);

    uchar out[32];
    ed10_public(H, tab, out);
    for (int k = 0; k < 32; k++) pub[(size_t)gid * 32 + k] = out[k];
}
