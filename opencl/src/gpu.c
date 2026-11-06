// EN: gpu.c - the OpenCL device layer (see gpu.h). Builds the embedded kernel sources, manages the
// EN: double-buffered job pipeline and keeps single launches short enough for Windows TDR.
// RU: gpu.c - слой устройств OpenCL (см. gpu.h). Собирает встроенные исходники ядер, управляет
// RU: конвейером заданий с двойной буферизацией и держит отдельные запуски достаточно короткими
// RU: для Windows TDR.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "crypto.h"
#include "gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embedded.h"

GpuState gpu;

static int list_gpus(cl_device_id *devs, int max) {
    cl_platform_id plats[16];
    cl_uint np = 0;
    if (clGetPlatformIDs(16, plats, &np) != CL_SUCCESS) return 0;
    int n = 0;
    for (cl_uint p = 0; p < np && n < max; p++) {
        cl_uint nd = 0;
        if (clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU, (cl_uint)(max - n), devs + n, &nd) == CL_SUCCESS) n += (int)nd;
    }
    return n;
}

static cl_mem const_buffer(const void *data, size_t size) {
    cl_int e;
    cl_mem m = clCreateBuffer(gpu.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, (void *)data, &e);
    CK(e);
    return m;
}

void gpu_init(void) {
    cl_device_id devs[32];
    int n = list_gpus(devs, 32);
    if (opt.list_devices) {
        for (int i = 0; i < n; i++) {
            char name[256] = {0}, ver[128] = {0};
            cl_uint cu = 0;
            clGetDeviceInfo(devs[i], CL_DEVICE_NAME, sizeof name, name, NULL);
            clGetDeviceInfo(devs[i], CL_DEVICE_VERSION, sizeof ver, ver, NULL);
            clGetDeviceInfo(devs[i], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof cu, &cu, NULL);
            printf(tr("[%d] %s, %u compute units, %s\n", "[%d] %s, %u вычислительных блоков, %s\n"),
                   i, name, cu, ver);
        }
        if (!n) printf(tr("no OpenCL GPU found (is the GPU driver with OpenCL support installed?)\n",
                          "GPU с OpenCL не найден (установлен ли драйвер GPU с поддержкой OpenCL?)\n"));
        exit(0);
    }
    if (!n) die(tr("no OpenCL GPU found: install the GPU driver with OpenCL support (see README)",
                   "GPU с OpenCL не найден: установите драйвер GPU с поддержкой OpenCL (см. README)"));
    if (opt.device < 0 || opt.device >= n)
        die(tr("--device %d: only %d GPU(s), see --list-devices",
               "--device %d: доступно GPU: %d, см. --list-devices"), opt.device, n);
    gpu.dev = devs[opt.device];
    clGetDeviceInfo(gpu.dev, CL_DEVICE_NAME, sizeof gpu.name, gpu.name, NULL);
    cl_int e;
    gpu.ctx = clCreateContext(NULL, 1, &gpu.dev, NULL, NULL, &e);
    CK(e);
    gpu.q = clCreateCommandQueue(gpu.ctx, gpu.dev, CL_QUEUE_PROFILING_ENABLE, &e);
    CK(e);
    gpu.prog = clCreateProgramWithSource(gpu.ctx, (cl_uint)KERNEL_SRC_LINES, (const char **)KERNEL_SRC, NULL, &e);
    CK(e);
    if (clBuildProgram(gpu.prog, 1, &gpu.dev, "-cl-std=CL1.2", NULL, NULL) != CL_SUCCESS) {
        size_t len = 0;
        clGetProgramBuildInfo(gpu.prog, gpu.dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
        char *log = malloc(len + 1);
        clGetProgramBuildInfo(gpu.prog, gpu.dev, CL_PROGRAM_BUILD_LOG, len, log, NULL);
        log[len] = 0;
        die(tr("OpenCL kernel build failed:\n%s", "сборка ядра OpenCL не удалась:\n%s"), log);
    }
    gpu.kinit = clCreateKernel(gpu.prog, "pbkdf2_init", &e); CK(e);
    gpu.kiter = clCreateKernel(gpu.prog, "pbkdf2_iter", &e); CK(e);
    gpu.kslip = clCreateKernel(gpu.prog, "slip10", &e); CK(e);
    gpu.ked = clCreateKernel(gpu.prog, "edpub", &e); CK(e);
    size_t wg = 64, maxwg = 0;
    clGetKernelWorkGroupInfo(gpu.kiter, gpu.dev, CL_KERNEL_WORK_GROUP_SIZE, sizeof maxwg, &maxwg, NULL);
    gpu.local = maxwg && maxwg < wg ? maxwg : wg;
    uint64_t w[16];
    salt_block(SALT_MNEMONIC, 8, w);     gpu.salt_mnemonic = const_buffer(w, 128);
    salt_block(SALT_TON_VERSION, 16, w); gpu.salt_ton_version = const_buffer(w, 128);
    salt_block(SALT_TON_DEFAULT, 16, w); gpu.salt_ton_default = const_buffer(w, 128);
    { // EN: fixed-base table for the GPU Ed25519 public-key kernel (a few ms, once)
      // RU: таблица фиксированной базы для GPU-ядра публичных ключей Ed25519 (несколько мс, один раз)
        int32_t *tab = malloc((size_t)ED_TAB_WORDS * 4);
        if (!tab) die(tr("out of memory", "не хватает памяти"));
        ed25519_gpu_table(tab);
        gpu.edtab = const_buffer(tab, (size_t)ED_TAB_WORDS * 4);
        free(tab);
    }
    gpu.mkey = NULL; // set by make_mkey()
}

static size_t round_up(size_t n) { return (n + gpu.local - 1) / gpu.local * gpu.local; }

uint32_t chunk_iters = 64;

void job_release(Job *j) {
    if (j->keys) clReleaseMemObject(j->keys);
    if (j->state) clReleaseMemObject(j->state);
    if (j->out) clReleaseMemObject(j->out);
    if (j->pub) clReleaseMemObject(j->pub);
    if (j->ev) clReleaseEvent(j->ev);
    if (j->kev) clReleaseEvent(j->kev);
    memset(j, 0, sizeof *j);
}

void job_submit(Job *j, const uint64_t *keys, uint32_t n, cl_mem salt, uint32_t iters, int slip10,
                uint32_t out_words, uint64_t *host_out, uint8_t *host_pub) {
    cl_int e;
    memset(j, 0, sizeof *j);
    j->n = n;
    j->keys = clCreateBuffer(gpu.ctx, CL_MEM_READ_ONLY, (size_t)n * 128, NULL, &e); CK(e);
    j->state = clCreateBuffer(gpu.ctx, CL_MEM_READ_WRITE, (size_t)n * 256, NULL, &e); CK(e);
    CK(clEnqueueWriteBuffer(gpu.q, j->keys, CL_FALSE, 0, (size_t)n * 128, keys, 0, NULL, NULL));
    size_t g = round_up(n);
    CK(clSetKernelArg(gpu.kinit, 0, sizeof(cl_mem), &j->keys));
    CK(clSetKernelArg(gpu.kinit, 1, sizeof(cl_mem), &salt));
    CK(clSetKernelArg(gpu.kinit, 2, sizeof(cl_mem), &j->state));
    CK(clSetKernelArg(gpu.kinit, 3, sizeof(cl_uint), &n));
    CK(clEnqueueNDRangeKernel(gpu.q, gpu.kinit, 1, NULL, &g, &gpu.local, 0, NULL, NULL));
    for (uint32_t done = 1; done < iters;) {
        uint32_t k = iters - done < chunk_iters ? iters - done : chunk_iters;
        CK(clSetKernelArg(gpu.kiter, 0, sizeof(cl_mem), &j->state));
        CK(clSetKernelArg(gpu.kiter, 1, sizeof(cl_uint), &k));
        CK(clSetKernelArg(gpu.kiter, 2, sizeof(cl_uint), &n));
        int profile = !j->kev && (done + k >= iters || done > 1); // EN: the second launch, or the only one
                                                                  // RU: второй запуск или единственный
        CK(clEnqueueNDRangeKernel(gpu.q, gpu.kiter, 1, NULL, &g, &gpu.local, 0, NULL, profile ? &j->kev : NULL));
        if (profile) j->kiters = k;
        CK(clFlush(gpu.q));
        done += k;
    }
    cl_mem priv = j->state;
    cl_uint stride = 32, offset = 24; // EN: PBKDF2 output at words 24..27 of each 32-word PState
                                      // RU: вывод PBKDF2 в словах 24..27 каждого 32-словного PState
    if (slip10) {
        j->out = clCreateBuffer(gpu.ctx, CL_MEM_WRITE_ONLY, (size_t)n * 32, NULL, &e); CK(e);
        CK(clSetKernelArg(gpu.kslip, 0, sizeof(cl_mem), &j->state));
        CK(clSetKernelArg(gpu.kslip, 1, sizeof(cl_mem), &gpu.mkey));
        CK(clSetKernelArg(gpu.kslip, 2, sizeof(cl_mem), &j->out));
        CK(clSetKernelArg(gpu.kslip, 3, sizeof(cl_uint), &n));
        CK(clEnqueueNDRangeKernel(gpu.q, gpu.kslip, 1, NULL, &g, &gpu.local, 0, NULL, NULL));
        priv = j->out;
        stride = 4;
        offset = 0;
    }
    if (host_pub) {
        j->pub = clCreateBuffer(gpu.ctx, CL_MEM_WRITE_ONLY, (size_t)n * 32, NULL, &e); CK(e);
        CK(clSetKernelArg(gpu.ked, 0, sizeof(cl_mem), &priv));
        CK(clSetKernelArg(gpu.ked, 1, sizeof(cl_uint), &stride));
        CK(clSetKernelArg(gpu.ked, 2, sizeof(cl_uint), &offset));
        CK(clSetKernelArg(gpu.ked, 3, sizeof(cl_mem), &gpu.edtab));
        CK(clSetKernelArg(gpu.ked, 4, sizeof(cl_mem), &j->pub));
        CK(clSetKernelArg(gpu.ked, 5, sizeof(cl_uint), &n));
        CK(clEnqueueNDRangeKernel(gpu.q, gpu.ked, 1, NULL, &g, &gpu.local, 0, NULL, NULL));
    }
    // EN: the queue is in order, so the host wait only needs the last read
    // RU: очередь упорядочена, поэтому ожиданию хоста достаточно последнего чтения
    cl_event *ev = host_pub ? NULL : &j->ev;
    if (slip10) {
        CK(clEnqueueReadBuffer(gpu.q, j->out, CL_FALSE, 0, (size_t)n * 32, host_out, 0, NULL, ev));
    } else {
        // EN: t starts at word 24 of each 32-word PState; read it with a rectangular copy
        // RU: t начинается со слова 24 каждого 32-словного PState; читаем прямоугольной копией
        size_t origin[3] = {24 * 8, 0, 0}, horigin[3] = {0, 0, 0}, region[3] = {(size_t)out_words * 8, n, 1};
        CK(clEnqueueReadBufferRect(gpu.q, j->state, CL_FALSE, origin, horigin, region, 256, 0,
                                   (size_t)out_words * 8, 0, host_out, 0, NULL, ev));
    }
    if (host_pub)
        CK(clEnqueueReadBuffer(gpu.q, j->pub, CL_FALSE, 0, (size_t)n * 32, host_pub, 0, NULL, &j->ev));
    CK(clFlush(gpu.q));
}

double last_iter_time;

void job_wait(Job *j) {
    CK(clWaitForEvents(1, &j->ev));
    last_iter_time = 0;
    if (j->kev && j->kiters) {
        cl_ulong t0 = 0, t1 = 0;
        if (clGetEventProfilingInfo(j->kev, CL_PROFILING_COMMAND_START, sizeof t0, &t0, NULL) == CL_SUCCESS &&
            clGetEventProfilingInfo(j->kev, CL_PROFILING_COMMAND_END, sizeof t1, &t1, NULL) == CL_SUCCESS && t1 > t0)
            last_iter_time = (double)(t1 - t0) / 1e9 / j->kiters;
    }
}

double iter_cost; // EN: seconds per PBKDF2 iteration per thread
                  // RU: секунды на итерацию PBKDF2 на поток

void tune_chunk(uint32_t iters, uint32_t n, double loop_time) {
    double est = loop_time > 0 ? loop_time / ((double)iters * n) : 0;
    if (last_iter_time > 0) {
        double prof = last_iter_time / n;
        if (prof > est) est = prof;
    }
    if (est <= 0) return;
    iter_cost = est > iter_cost * 0.8 ? est : iter_cost * 0.8;
    double c = 0.25 / (iter_cost * n);
    chunk_iters = (uint32_t)(c < 8 ? 8 : c > 4096 ? 4096 : c);
}

// EN: SHA-512 compression on the CPU, used once: the ipad/opad states of the constant SLIP-10 master
// EN: key "ed25519 seed" (Monocypher does not expose intermediate SHA-512 states).
// RU: Сжатие SHA-512 на CPU, используется один раз: состояния ipad/opad постоянного мастер-ключа
// RU: SLIP-10 "ed25519 seed" (Monocypher не предоставляет промежуточные состояния SHA-512).
static const uint64_t K512C[80] = {
#define X(v) v##ULL
    X(0x428a2f98d728ae22), X(0x7137449123ef65cd), X(0xb5c0fbcfec4d3b2f), X(0xe9b5dba58189dbbc), X(0x3956c25bf348b538),
    X(0x59f111f1b605d019), X(0x923f82a4af194f9b), X(0xab1c5ed5da6d8118), X(0xd807aa98a3030242), X(0x12835b0145706fbe),
    X(0x243185be4ee4b28c), X(0x550c7dc3d5ffb4e2), X(0x72be5d74f27b896f), X(0x80deb1fe3b1696b1), X(0x9bdc06a725c71235),
    X(0xc19bf174cf692694), X(0xe49b69c19ef14ad2), X(0xefbe4786384f25e3), X(0x0fc19dc68b8cd5b5), X(0x240ca1cc77ac9c65),
    X(0x2de92c6f592b0275), X(0x4a7484aa6ea6e483), X(0x5cb0a9dcbd41fbd4), X(0x76f988da831153b5), X(0x983e5152ee66dfab),
    X(0xa831c66d2db43210), X(0xb00327c898fb213f), X(0xbf597fc7beef0ee4), X(0xc6e00bf33da88fc2), X(0xd5a79147930aa725),
    X(0x06ca6351e003826f), X(0x142929670a0e6e70), X(0x27b70a8546d22ffc), X(0x2e1b21385c26c926), X(0x4d2c6dfc5ac42aed),
    X(0x53380d139d95b3df), X(0x650a73548baf63de), X(0x766a0abb3c77b2a8), X(0x81c2c92e47edaee6), X(0x92722c851482353b),
    X(0xa2bfe8a14cf10364), X(0xa81a664bbc423001), X(0xc24b8b70d0f89791), X(0xc76c51a30654be30), X(0xd192e819d6ef5218),
    X(0xd69906245565a910), X(0xf40e35855771202a), X(0x106aa07032bbd1b8), X(0x19a4c116b8d2d0c8), X(0x1e376c085141ab53),
    X(0x2748774cdf8eeb99), X(0x34b0bcb5e19b48a8), X(0x391c0cb3c5c95a63), X(0x4ed8aa4ae3418acb), X(0x5b9cca4f7763e373),
    X(0x682e6ff3d6b2b8a3), X(0x748f82ee5defb2fc), X(0x78a5636f43172f60), X(0x84c87814a1f0ab72), X(0x8cc702081a6439ec),
    X(0x90befffa23631e28), X(0xa4506cebde82bde9), X(0xbef9a3f7b2c67915), X(0xc67178f2e372532b), X(0xca273eceea26619c),
    X(0xd186b8c721c0c207), X(0xeada7dd6cde0eb1e), X(0xf57d4f7fee6ed178), X(0x06f067aa72176fba), X(0x0a637dc5a2c898a6),
    X(0x113f9804bef90dae), X(0x1b710b35131c471b), X(0x28db77f523047d84), X(0x32caab7b40c72493), X(0x3c9ebe0a15c9bebc),
    X(0x431d67c49c100d4c), X(0x4cc5d4becb3e42b6), X(0x597f299cfc657e2a), X(0x5fcb6fab3ad6faec), X(0x6c44198c4a475817)
#undef X
};
#define R64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void cpu_compress(uint64_t st[8], const uint64_t blk[16]) {
    uint64_t w[80], a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
    for (int i = 0; i < 16; i++) w[i] = blk[i];
    for (int i = 16; i < 80; i++)
        w[i] = w[i - 16] + (R64(w[i - 15], 1) ^ R64(w[i - 15], 8) ^ (w[i - 15] >> 7)) + w[i - 7] +
               (R64(w[i - 2], 19) ^ R64(w[i - 2], 61) ^ (w[i - 2] >> 6));
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + (R64(e, 14) ^ R64(e, 18) ^ R64(e, 41)) + ((e & f) ^ (~e & g)) + K512C[i] + w[i];
        uint64_t t2 = (R64(a, 28) ^ R64(a, 34) ^ R64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

void make_mkey(void) {
    static const uint64_t IV[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    uint64_t kb[16], blk[16], st[16];
    key_block((const uint8_t *)"ed25519 seed", 12, kb);
    memcpy(st, IV, 64);
    for (int j = 0; j < 16; j++) blk[j] = kb[j] ^ 0x3636363636363636ULL;
    cpu_compress(st, blk);
    memcpy(st + 8, IV, 64);
    for (int j = 0; j < 16; j++) blk[j] = kb[j] ^ 0x5c5c5c5c5c5c5c5cULL;
    cpu_compress(st + 8, blk);
    gpu.mkey = const_buffer(st, 128);
}
