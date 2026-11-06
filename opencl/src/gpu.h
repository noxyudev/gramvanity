// EN: gpu.h - the OpenCL device layer: context and kernel setup, double-buffered PBKDF2/SLIP-10/Ed25519
// EN: job submission and the adaptive chunk tuning that keeps single launches below the Windows TDR.
// RU: gpu.h - слой устройств OpenCL: настройка контекста и ядер, конвейер заданий PBKDF2/SLIP-10/Ed25519
// RU: с двойной буферизацией и подстройка размера части, чтобы отдельные запуски укладывались в TDR Windows.
#ifndef SV_GPU_H
#define SV_GPU_H

#define CL_TARGET_OPENCL_VERSION 120
#define CL_SILENCE_DEPRECATION

#include <CL/cl.h>
#include <stdint.h>

#include "platform.h" // die()

// EN: OpenCL call guard: on an error code, print the call and abort (used by gpu.c and the self-tests).
// RU: Защита вызовов OpenCL: при коде ошибки печатает вызов и прерывает работу (gpu.c и self-тесты).
#define CK(x) do { cl_int e_ = (x); if (e_ != CL_SUCCESS) die("OpenCL error %d at line %d: %s", e_, __LINE__, #x); } while (0)

typedef struct {
    cl_device_id dev;
    cl_context ctx;
    cl_command_queue q;
    cl_program prog;
    cl_kernel kinit, kiter, kslip, ked;
    cl_mem salt_mnemonic, salt_ton_version, salt_ton_default, mkey, edtab;
    char name[256];
    size_t local;
} GpuState;

extern GpuState gpu;

typedef struct {
    cl_mem keys, state, out, pub;
    uint32_t n;
    cl_event ev;
    // EN: one pbkdf2_iter launch, profiled to tune chunk_iters
    // RU: один запуск pbkdf2_iter, измеряется для настройки chunk_iters
    cl_event kev;
    // EN: iterations in that launch | RU: итераций в этом запуске
    uint32_t kiters;
} Job;

// EN: Picks opt.device, builds the embedded kernels and uploads the constant salt/table buffers.
// RU: Выбирает opt.device, собирает встроенные ядра и загружает постоянные буферы солей и таблицы.
void gpu_init(void);
void job_release(Job *j);
// EN: Enqueues PBKDF2 over n key blocks; with slip10 != 0 also SLIP-10 (out = 4 words per thread),
// EN: otherwise out = the first out_words words of the PBKDF2 output. Reads out into host asynchronously.
// EN: With host_pub != NULL the GPU also derives the Ed25519 public key of each 32-byte private key
// EN: (from out, or from words 24..27 of the PBKDF2 state when slip10 == 0) and reads it back.
// RU: Ставит в очередь PBKDF2 по n блокам ключей; при slip10 != 0 ещё и SLIP-10 (out = 4 слова на поток),
// RU: иначе out = первые out_words слов вывода PBKDF2. Читает out в host асинхронно.
// RU: При host_pub != NULL GPU ещё и выводит публичный ключ Ed25519 каждого 32-байтного приватного
// RU: ключа (из out или из слов 24..27 состояния PBKDF2 при slip10 == 0) и читает его обратно.
void job_submit(Job *j, const uint64_t *keys, uint32_t n, cl_mem salt, uint32_t iters, int slip10,
                uint32_t out_words, uint64_t *host_out, uint8_t *host_pub);
void job_wait(Job *j);

// EN: Adaptive PBKDF2 chunk: iterations per kernel launch, tuned to keep each launch ~0.25 s.
// RU: Подстраиваемая часть PBKDF2: итераций на запуск ядра, подбирается под ~0.25 с на запуск.
// EN: until measured: 16384 threads x 64 iterations is short even on weak GPUs
// RU: до измерений: 16384 потока x 64 итерации — коротко даже на слабых GPU
extern uint32_t chunk_iters;
// EN: Measured GPU time of one pbkdf2_iter launch per iteration and thread batch (seconds), or 0.
// RU: Измеренное время GPU одного запуска pbkdf2_iter на итерацию и пакет потоков (секунды), или 0.
extern double last_iter_time;
// EN: seconds per PBKDF2 iteration per thread | RU: секунды на итерацию PBKDF2 на поток
extern double iter_cost;
void tune_chunk(uint32_t iters, uint32_t n, double loop_time);

// EN: Builds the constant SLIP-10 master-key ipad/opad SHA-512 states ("ed25519 seed") as a GPU buffer.
// RU: Строит постоянные состояния ipad/opad SHA-512 мастер-ключа SLIP-10 ("ed25519 seed") в буфер GPU.
void make_mkey(void);

#endif
