// EN: selftest.c - startup self-test and deterministic-search unit tests (see selftest.h). The unit
// EN: tests exercise determinism, checkpoints (roundtrip, corruption, legacy formats), target matching
// EN: and the --all-patterns bookkeeping; they run without a GPU.
// RU: selftest.c - стартовый self-test и unit-тесты детерминированного поиска (см. selftest.h).
// RU: Тесты проверяют детерминизм, checkpoint (roundtrip, повреждение, старые форматы), совпадение
// RU: целей и учёт --all-patterns; GPU для них не нужен.
// EN: The assertion labels and the "SELF-TEST FAILED" tag stay English: they are stable identifiers
// EN: that scripts and the AGENTS.md safety rule refer to, not prose for the end user.
// RU: Метки проверок и тег "SELF-TEST FAILED" остаются английскими: это стабильные идентификаторы,
// RU: на которые ссылаются скрипты и правило безопасности из AGENTS.md, а не текст для пользователя.
#include "sv.h"
#include "platform.h"
#include "options.h"
#include "pattern.h"
#include "stats.h"
#include "detsearch.h"
#include "crypto.h"
#include "addresses.h"
#include "gpu.h"
#include "checkpoint.h"
#include "search.h"
#include "selftest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "third_party/monocypher/monocypher.h"

static int fails;
static void tcheck(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
void self_test(void) {
    // EN: 1. CPU primitives against published vectors
    // RU: 1. примитивы CPU против опубликованных векторов
    uint8_t h[32], seed[64];
    sha256((const uint8_t *)"abc", 3, h);
    char hx[129];
    hexs(h, 32, hx);
    if (strcmp(hx, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) die("SELF-TEST FAILED: SHA-256");
    const char *abandon = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    cpu_pbkdf2((const uint8_t *)abandon, strlen(abandon), SALT_MNEMONIC, 8, 2048, seed);
    hexs(seed, 64, hx);
    if (strcmp(hx, "5eb00bbddcf069084889a8ab9155568165f5c453ccb85e70811aaed6f6da5fc19a5ac40b389cd370d086206dec8aa6c43daea6690f20ad3d8d48b2d2ce9e38e4"))
        die("SELF-TEST FAILED: CPU PBKDF2 vs BIP39 test vector");
    // EN: 2. full derivation -> W5 address vs vectors produced by @ton/ton (throwaway phrases, never
    // EN: fund them)
    // RU: 2. полный вывод -> адрес W5 против векторов из @ton/ton (одноразовые фразы, не пополняйте их)
    int saved_testnet = opt.testnet; // EN: the W5 vectors below are mainnet address strings
                                     // RU: векторы W5 ниже — адресные строки mainnet
    opt.testnet = 0;
    struct { int mode; const char *phrase, *want; } vec[3] = {
        {BIP12, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
         "UQBHyu-oZVDHRYQ1-rKlGqpHy5yAqanPBirEQNMNOmfHLtaT"},
        {BIP24, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon "
                "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art",
         "UQC020bHeiUqqyw8BB4EttblmRidKkT_hnINJ-8rZCP0L1Dw"},
        {TON24, "exercise lawn pole excite tape shy judge episode twice crazy pudding unfold unique fatal scrub parade "
                "cover pretty payment shrimp ahead humor shoe merit",
         "UQBDJxXesUgF7mvh4aoc0aa5kjKFE1Pquz1T-u9Y_e0VQk41"}};
    for (int v = 0; v < 3; v++) {
        uint8_t priv[32], pub[32], id[32], s[64];
        size_t l = strlen(vec[v].phrase);
        if (vec[v].mode == TON24) {
            uint8_t e[64];
            ton_entropy(vec[v].phrase, l, e);
            cpu_pbkdf2(e, 64, SALT_TON_DEFAULT, 16, TON_ITERS, s);
            memcpy(priv, s, 32);
        } else {
            cpu_pbkdf2((const uint8_t *)vec[v].phrase, l, SALT_MNEMONIC, 8, 2048, s);
            cpu_slip10(s, priv);
        }
        ed25519_public(priv, pub);
        w5_account_id(pub, id);
        char uq[49];
        friendly(id, 0, uq);
        if (strcmp(uq, vec[v].want)) die("SELF-TEST FAILED: %s W5 address %s != %s", MODE_NAME[vec[v].mode], uq, vec[v].want);
    }
    if (!ton_valid(vec[2].phrase, strlen(vec[2].phrase))) die("SELF-TEST FAILED: TON validity");
    // EN: 2b. WalletTg (Telegram Wallet) addresses: trampoline code hash + BIP39 anchor key, both nets
    // RU: 2b. адреса WalletTg (Telegram Wallet): хеш кода trampoline + якорный ключ BIP39, обе сети
    struct { const char *anchor; int testnet; const char *want; } tgv[2] = {
        {"abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", 0,
         "UQD4d2pzr-W4jzh1wOd52HrsH7rRagzkSUPC3_jCgMxmKz_L"},
        {"legal winner thank year wave sausage worth useful legal winner thank yellow", 1,
         "0QBwqZDGPaXv1fkv3l373LWH8CGQh_UGZgHHcwy1N0XEjdJV"}};
    for (int v = 0; v < 2; v++) {
        uint8_t priv[32], pub[32], id[32];
        bip39_key(tgv[v].anchor, strlen(tgv[v].anchor), priv);
        ed25519_public(priv, pub);
        opt.testnet = tgv[v].testnet;
        tg_account_id(pub, tg_subwallet(), id);
        char uq[49];
        friendly(id, 0, uq);
        if (strcmp(uq, tgv[v].want))
            die("SELF-TEST FAILED: WalletTg %s address %s != %s", tgv[v].testnet ? "testnet" : "mainnet", uq, tgv[v].want);
    }
    opt.testnet = saved_testnet;
    { // EN: the signing half is an ordinary BIP39-12 phrase; check one known key
      // RU: подписывающая половина — обычная фраза BIP39-12; проверяем один известный ключ
        const char *signing = "letter advice cage absurd amount doctor acoustic avoid letter advice cage above";
        uint8_t priv[32], pub[32];
        bip39_key(signing, strlen(signing), priv);
        ed25519_public(priv, pub);
        hexs(pub, 32, hx);
        if (strcmp(hx, "9c3927b809a6c8d1459cdf08ca97657bab56bee90556d92c19e8d424da2e77f9"))
            die("SELF-TEST FAILED: WalletTg signing key");
    }
    // EN: 3. GPU PBKDF2 + SLIP-10 == CPU, incl. >128-byte keys and chunked iterations
    // RU: 3. GPU PBKDF2 + SLIP-10 == CPU, включая ключи >128 байт и разбитые на части итерации
    enum { N = 40 };
    uint64_t keys[N * 16], out[N * 4], outt[N * 8];
    uint8_t gpub[N * 32];
    uint8_t pws[N][200];
    size_t lens[N];
    uint32_t saved = chunk_iters;
    chunk_iters = 700; // EN: forces 3 chunks for 2048 iterations
                       // RU: даёт 3 части для 2048 итераций
    for (int i = 0; i < N; i++) {
        lens[i] = (size_t)(i % 3 == 0 ? 150 + i : 40 + i);
        random_bytes(pws[i], lens[i]);
        key_block(pws[i], lens[i], keys + i * 16);
    }
    Job j;
    job_submit(&j, keys, N, gpu.salt_mnemonic, 2048, 1, 0, out, gpub);
    job_wait(&j);
    job_release(&j);
    for (int i = 0; i < N; i++) {
        uint8_t ref[64], rp[32], got[32], rpub[32];
        cpu_pbkdf2(pws[i], lens[i], SALT_MNEMONIC, 8, 2048, ref);
        cpu_slip10(ref, rp);
        words_to_bytes(out + i * 4, 4, got);
        if (memcmp(rp, got, 32)) die("SELF-TEST FAILED: GPU PBKDF2+SLIP-10 != CPU (thread %d)", i);
        ed25519_public(rp, rpub);
        if (memcmp(rpub, gpub + i * 32, 32)) die("SELF-TEST FAILED: GPU Ed25519 != CPU (thread %d)", i);
    }
    { // EN: RFC 8032 test vector 1: seed -> public key, straight through the GPU kernel
      // RU: тестовый вектор 1 RFC 8032: seed -> публичный ключ, напрямую через ядро GPU
        static const uint8_t tv_seed[32] = {
            0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
            0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
        static const uint8_t tv_pub[32] = {
            0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
            0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
        uint64_t words[4];
        for (int w = 0; w < 4; w++) {
            uint64_t v = 0;
            for (int k = 0; k < 8; k++) v = (v << 8) | tv_seed[w * 8 + k];
            words[w] = v;
        }
        cl_int e2;
        cl_mem pin = clCreateBuffer(gpu.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32, words, &e2); CK(e2);
        cl_mem pout = clCreateBuffer(gpu.ctx, CL_MEM_WRITE_ONLY, 32, NULL, &e2); CK(e2);
        cl_uint stride = 4, offset = 0, one = 1;
        CK(clSetKernelArg(gpu.ked, 0, sizeof(cl_mem), &pin));
        CK(clSetKernelArg(gpu.ked, 1, sizeof(cl_uint), &stride));
        CK(clSetKernelArg(gpu.ked, 2, sizeof(cl_uint), &offset));
        CK(clSetKernelArg(gpu.ked, 3, sizeof(cl_mem), &gpu.edtab));
        CK(clSetKernelArg(gpu.ked, 4, sizeof(cl_mem), &pout));
        CK(clSetKernelArg(gpu.ked, 5, sizeof(cl_uint), &one));
        size_t gg = gpu.local;
        CK(clEnqueueNDRangeKernel(gpu.q, gpu.ked, 1, NULL, &gg, &gpu.local, 0, NULL, NULL));
        uint8_t got[32];
        CK(clEnqueueReadBuffer(gpu.q, pout, CL_TRUE, 0, 32, got, 0, NULL, NULL));
        clReleaseMemObject(pin);
        clReleaseMemObject(pout);
        if (memcmp(got, tv_pub, 32)) die("SELF-TEST FAILED: GPU Ed25519 RFC 8032 test vector");
    }
    job_submit(&j, keys, N, gpu.salt_ton_version, TON_BASIC_ITERS, 0, 8, outt, NULL);
    job_wait(&j);
    job_release(&j);
    for (int i = 0; i < N; i++) {
        uint8_t ref[64], got[64];
        cpu_pbkdf2(pws[i], lens[i], SALT_TON_VERSION, 16, TON_BASIC_ITERS, ref);
        words_to_bytes(outt + i * 8, 8, got);
        if (memcmp(ref, got, 64)) die("SELF-TEST FAILED: GPU PBKDF2 (TON salt) != CPU (thread %d)", i);
    }
    { // EN: TON stage 2: private key at words 24..27 of the PState, public key on the GPU
      // RU: стадия 2 TON: приватный ключ в словах 24..27 PState, публичный ключ на GPU
        uint64_t tout[4 * 4];
        uint8_t tpub[4 * 32];
        job_submit(&j, keys, 2, gpu.salt_ton_default, TON_ITERS, 0, 4, tout, tpub);
        job_wait(&j);
        job_release(&j);
        for (int i = 0; i < 2; i++) {
            uint8_t ref[64], priv[32], rpub[32], got[32];
            cpu_pbkdf2(pws[i], lens[i], SALT_TON_DEFAULT, 16, TON_ITERS, ref);
            words_to_bytes(tout + i * 4, 4, got);
            if (memcmp(ref, got, 32)) die("SELF-TEST FAILED: GPU PBKDF2 (TON stage 2) != CPU (thread %d)", i);
            memcpy(priv, ref, 32);
            ed25519_public(priv, rpub);
            if (memcmp(rpub, tpub + i * 32, 32)) die("SELF-TEST FAILED: GPU Ed25519 (TON stage 2) != CPU (thread %d)", i);
        }
    }
    chunk_iters = saved;
    // EN: 4. BIP39 checksum helper
    // RU: 4. помощник проверки контрольной суммы BIP39
    uint8_t ent[32];
    int idx[24];
    random_bytes(ent, 32);
    bip39_indices(ent, 16, idx);
    if (!bip39_valid(idx, 12)) die("SELF-TEST FAILED: BIP39 checksum (12)");
    bip39_indices(ent, 32, idx);
    if (!bip39_valid(idx, 24)) die("SELF-TEST FAILED: BIP39 checksum (24)");
}

static void det_unit_tests(void) {
    // EN: 1. determinism: key+index -> same entropy every time, different key/index -> different
    // RU: 1. детерминизм: ключ+индекс -> одна и та же энтропия, другой ключ/индекс -> другая
    uint8_t k1[32], k2[32], a[33], b[33], c[33];
    memset(k1, 0x11, 32);
    memset(k2, 0x22, 32);
    uint64_t idxs[] = {0, 1, 2, 1000000000ULL, 0xFFFFFFFFULL, 0x8000000000000000ULL};
    int det_ok = 1, diff_ok = 1;
    for (size_t i = 0; i < sizeof idxs / sizeof idxs[0]; i++) {
        det_entropy_ex(k1, BIP12, idxs[i], a, 16);
        det_entropy_ex(k1, BIP12, idxs[i], b, 16);
        if (memcmp(a, b, 16)) det_ok = 0;
        det_entropy_ex(k1, BIP24, idxs[i], c, 32);
        if (!memcmp(a, c, 16)) diff_ok = 0; // EN: mode separation (trivially true by construction, but cheap)
                                            // RU: разделение режимов (тривиально по построению, но дёшево)
        det_entropy_ex(k1, TG, idxs[i], c, 16);
        if (!memcmp(a, c, 16)) diff_ok = 0; // EN: tg-wallet has its own entropy space too
                                            // RU: у tg-wallet тоже своё пространство энтропии
        det_entropy_ex(k1, DETSIGN, idxs[i], c, 16);
        if (!memcmp(a, c, 16)) diff_ok = 0; // EN: and the signing half another one
                                            // RU: а у подписывающей половины — ещё одно
        det_entropy_ex(k2, BIP12, idxs[i], b, 16);
        if (!memcmp(a, b, 16)) diff_ok = 0;
    }
    for (size_t i = 1; i < sizeof idxs / sizeof idxs[0]; i++) {
        det_entropy_ex(k1, BIP12, idxs[0], a, 16);
        det_entropy_ex(k1, BIP12, idxs[i], b, 16);
        if (!memcmp(a, b, 16)) diff_ok = 0;
    }
    tcheck(det_ok, "det: same key + index = same entropy");
    tcheck(diff_ok, "det: different key/index/mode = different entropy");
    // EN: 2. derived BIP39 candidates are valid phrases (checksum) and derivable addresses
    // RU: 2. выведенные кандидаты BIP39 — корректные фразы (контрольная сумма) с выводимыми адресами
    int v12 = 1, v24 = 1, addr_det = 1;
    char first_uq[49] = {0}, second_uq[49] = {0};
    for (uint64_t i = 0; i < 50; i++) {
        Derived d;
        derive_candidate(k1, BIP12, i, &d);
        if (!bip39_valid(d.idx, 12)) v12 = 0;
        if (i == 0) friendly(d.id, 0, first_uq);
        if (i == 1) friendly(d.id, 0, second_uq);
        derive_candidate(k1, BIP24, i + 7777, &d);
        if (!bip39_valid(d.idx, 24)) v24 = 0;
        crypto_wipe(&d, sizeof d);
    }
    {
        Derived d;
        char uq[49];
        derive_candidate(k1, BIP12, 0, &d);
        friendly(d.id, 0, uq);
        if (strcmp(uq, first_uq)) addr_det = 0;   // EN: same key + index = same address
                                                  // RU: тот же ключ + индекс = тот же адрес
        if (!strcmp(first_uq, second_uq)) addr_det = 0; // EN: neighbouring indices differ
                                                        // RU: соседние индексы различаются
        crypto_wipe(&d, sizeof d);
    }
    tcheck(v12, "det: bip39-12 candidates pass the BIP39 checksum");
    tcheck(v24, "det: bip39-24 candidates pass the BIP39 checksum");
    tcheck(addr_det, "det: index 0 reproduces the same address, index 1 differs");
    // EN: 2b. tg-wallet candidates: both halves pass the BIP39 checksum, addresses are deterministic
    // RU: 2b. кандидаты tg-wallet: обе половины проходят контрольную сумму BIP39, адреса детерминированы
    int tg_ok = 1;
    char tg_first[49] = {0}, tg_second[49] = {0};
    for (uint64_t i = 0; i < 8; i++) {
        Derived d;
        derive_candidate(k1, TG, i, &d);
        if (d.nw != 24 || !bip39_valid(d.idx, 12) || !bip39_valid(d.idx + 12, 12)) tg_ok = 0;
        if (i == 0) friendly(d.id, 0, tg_first);
        if (i == 1) friendly(d.id, 0, tg_second);
        crypto_wipe(&d, sizeof d);
    }
    if (!strcmp(tg_first, tg_second)) tg_ok = 0;
    tcheck(tg_ok, "det: tg-wallet halves pass the BIP39 checksum, index 0 and 1 differ");
    // EN: 3. parse_u64 and overflow
    // RU: 3. parse_u64 и переполнение
    uint64_t v;
    tcheck(parse_u64("0", &v) == 0 && v == 0, "parse_u64 zero");
    tcheck(parse_u64("18446744073709551615", &v) == 0 && v == UINT64_MAX, "parse_u64 max");
    tcheck(parse_u64("18446744073709551616", &v) == -1, "parse_u64 overflow rejected");
    tcheck(parse_u64("-1", &v) == -1 && parse_u64("12x", &v) == -1 && parse_u64("", &v) == -1, "parse_u64 junk rejected");
    uint64_t r;
    tcheck(add_ovf_u64(UINT64_MAX - 1, 1, &r) == 0 && r == UINT64_MAX, "add_ovf_u64 boundary ok");
    tcheck(add_ovf_u64(UINT64_MAX, 1, &r) == -1, "add_ovf_u64 overflow detected");
    // EN: 4. range bookkeeping: allocation never crosses the end, clamps to the remainder
    // RU: 4. учёт диапазона: выделение не выходит за конец и урезается до остатка
    det.enabled = 1;
    memcpy(det.key, k1, 32);
    det.start = 1000;
    det.cursor = det.next = 1000;
    det.end = 1100;
    det.has_end = 1;
    uint32_t got;
    uint64_t total = 0, base;
    int range_ok = 1, monotone = 1;
    uint64_t monotone_base = det.start; // EN: first allocation must begin exactly at start
                                        // RU: первое выделение должно начинаться ровно с start
    while ((base = det_alloc(64, &got)) || got) {
        if (got == 0) break;
        if (base != monotone_base) monotone = 0;
        if (base + got > 1100) range_ok = 0;
        monotone_base = base + got;
        total += got;
    }
    uint32_t got0;
    det_alloc(64, &got0);
    tcheck(total == 100 && got0 == 0 && range_ok && monotone, "det: batches cover the range exactly, no overrun");
    // EN: 5. checkpoint roundtrip + corruption detection (mutexes are initialized in main)
    // RU: 5. roundtrip checkpoint + обнаружение повреждения (мьютексы инициализируются в main)
    opt.mode = TG;
    opt.bounceable = 1;
    opt.ignore_case = 0;
    opt.testnet = 1;
    n_targets = 0;
    Target *ta = new_target();
    snprintf(ta->name, sizeof ta->name, "A");
    snprintf(ta->given_name, sizeof ta->given_name, "A");
    snprintf(ta->label, sizeof ta->label, "%s", "\xD0\x9F\xD0\xB0\xD1\x88\xD0\xB0");
    ta->name_given = 1;
    ta->has_suffix = 1;
    strlist_add(&ta->suffix, "[a-z_]OK", strlen("[a-z_]OK"));
    ta->count_given = 1;
    ta->count = 1;
    ta->hits = 1;
    ta->all_patterns = 1;
    snprintf(ta->out_dir, sizeof ta->out_dir, "%s%cresults%cout-A", root_dir, PATH_SEP, PATH_SEP);
    target_compile(ta);
    ta->suf_hits[0] = 1; // EN: the one alternative already has its hit
                         // RU: у единственной альтернативы уже есть свой хит
    Target *tb = new_target();
    snprintf(tb->name, sizeof tb->name, "B");
    snprintf(tb->given_name, sizeof tb->given_name, "B");
    snprintf(tb->label, sizeof tb->label, "B");
    tb->name_given = 1;
    tb->has_prefix = tb->has_suffix = 1;
    strlist_add(&tb->prefix, "MOON", 4);
    strlist_add(&tb->suffix, "HELLO", 5);
    tb->hits = 3;
    tb->ignore_case = 1;
    snprintf(tb->out_dir, sizeof tb->out_dir, "%s%cresults%cout-B", root_dir, PATH_SEP, PATH_SEP);
    target_compile(tb);
    tb->pre_hits[0] = 2;
    tb->suf_hits[0] = 1;
    det.next = 1050;
    det.s1_next = 1050;
    total_done = 123456;
    hits_found = 4;
    verified = 3;
    snprintf(job_dir, sizeof job_dir, "%s%cresults%cjob-selftest", root_dir, PATH_SEP, PATH_SEP);
    char path[4096];
    mkdir_p(job_dir);
    snprintf(path, sizeof path, "%s%cselftest.ckpt", job_dir, PATH_SEP);
    remove(path);
    snprintf(det.ckpt_path, sizeof det.ckpt_path, "%s", path);
    tcheck(checkpoint_save() == 0, "ckpt: saved");
    struct CkptIn in;
    char err[256];
    tcheck(ckpt_load(path, &in, err, sizeof err) == 0, "ckpt: loaded");
    int same = in.mode == TG && (in.flags & 1) && (in.flags & 4) && in.n_targets == 2 &&
               !strcmp(in.targets[0].name, "A") && !strcmp(in.targets[0].label, ta->label) &&
               in.targets[0].n_suffix == 1 &&
               !strcmp(in.targets[0].suffix[0], "[a-z_]OK") && (in.targets[0].flags & 2) &&
               !(in.targets[0].flags & 1) && in.targets[0].count == 1 && in.targets[0].hits == 1 &&
               (in.targets[0].flags & 16) && in.targets[0].suf_hits && in.targets[0].suf_hits[0] == 1 &&
               !strcmp(in.targets[0].out, ta->out_dir) &&
               !strcmp(in.targets[1].name, "B") && !strcmp(in.targets[1].label, "B") &&
               in.targets[1].n_prefix == 1 &&
               !strcmp(in.targets[1].prefix[0], "MOON") && in.targets[1].n_suffix == 1 &&
               !strcmp(in.targets[1].suffix[0], "HELLO") && (in.targets[1].flags & 1) &&
               !(in.targets[1].flags & 2) && in.targets[1].hits == 3 &&
               !(in.targets[1].flags & 16) && in.targets[1].pre_hits && in.targets[1].pre_hits[0] == 2 &&
               in.targets[1].suf_hits && in.targets[1].suf_hits[0] == 1 &&
               !strcmp(in.targets[1].out, tb->out_dir) &&
               !strcmp(in.job_dir, job_dir) && in.start == 1000 && in.next == 1050 && in.s1_next == 1050 &&
               in.end == 1100 && in.done == 123456 && in.unique_hits == 4 && in.verified == 3 &&
               !memcmp(in.key, k1, 32);
    tcheck(same, "ckpt: all target fields survive the roundtrip (v8)");
    ckpt_in_free(&in);
    // EN: corrupt one byte -> must be rejected
    // RU: портим один байт -> файл должен быть отвергнут
    FILE *f = fopen(path, "r+b");
    if (f) {
        fseek(f, 40, SEEK_SET);
        int ch = fgetc(f);
        fseek(f, 40, SEEK_SET);
        fputc(ch ^ 0x40, f);
        fclose(f);
    }
    tcheck(ckpt_load(path, &in, err, sizeof err) == -1, "ckpt: corrupted file rejected");
    // EN: truncated file -> must be rejected
    // RU: обрезанный файл -> должен быть отвергнут
    f = fopen(path, "rb");
    uint8_t buf[65536];
    size_t len = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, len - 10, f); fclose(f); }
    tcheck(ckpt_load(path, &in, err, sizeof err) == -1, "ckpt: truncated file rejected");
    // EN: hundreds of patterns: no fixed count and no fixed-size checkpoint buffer
    // RU: сотни шаблонов: нет фиксированного числа и буфера checkpoint фиксированного размера
    n_targets = 0;
    Target *tmany = new_target();
    snprintf(tmany->name, sizeof tmany->name, "many");
    tmany->has_suffix = 1;
    for (int k = 0; k < 700; k++) strlist_add(&tmany->suffix, "[A-Z0-9_-]", strlen("[A-Z0-9_-]"));
    target_compile(tmany);
    remove(path);
    tcheck(checkpoint_save() == 0, "ckpt: saved with 700 patterns");
    tcheck(ckpt_load(path, &in, err, sizeof err) == 0 && in.n_targets == 1 && in.targets[0].n_suffix == 700,
           "ckpt: 700 patterns survive the roundtrip");
    ckpt_in_free(&in);
    remove(path);
    // EN: more targets than the old fixed 256-entry table: the list grows on demand
    // RU: целей больше, чем в старой фиксированной таблице на 256: список растёт по необходимости
    n_targets = 0;
    for (int k = 0; k < 300; k++) {
        Target *tk = new_target();
        snprintf(tk->name, sizeof tk->name, "t%d", k + 1);
        tk->has_suffix = 1;
        char pat[16];
        snprintf(pat, sizeof pat, "N%d", k);
        strlist_add(&tk->suffix, pat, strlen(pat));
        target_compile(tk);
    }
    remove(path);
    tcheck(checkpoint_save() == 0, "ckpt: saved with 300 targets");
    tcheck(ckpt_load(path, &in, err, sizeof err) == 0 && in.n_targets == 300 &&
           !strcmp(in.targets[299].name, "t300") && in.targets[299].n_suffix == 1 &&
           !strcmp(in.targets[299].suffix[0], "N299"),
           "ckpt: 300 targets survive the roundtrip");
    ckpt_in_free(&in);
    remove(path);
    // EN: a version 4 checkpoint still loads as one legacy OR target
    // RU: checkpoint версии 4 по-прежнему загружается как одна legacy OR-цель
    {
        uint8_t old[4096];
        size_t o = 0;
        memset(old, 0, sizeof old);
        memcpy(old, CKPT_MAGIC "\0", 8); o = 8;
        le32p(old + o, 4); o += 4;
        le32p(old + o, TG); o += 4;
        le32p(old + o, 2); o += 4; // EN: old: ignore-case | RU: старый вариант: ignore-case
        le32p(old + o, 2); o += 4; // EN: two endings | RU: два окончания
        const char *ends[2] = {"ONE", "TWO"};
        for (int k = 0; k < 2; k++) {
            uint32_t l = (uint32_t)strlen(ends[k]);
            le32p(old + o, l); o += 4; memcpy(old + o, ends[k], l); o += l;
        }
        le32p(old + o, 1); o += 4; // one prefix
        le32p(old + o, 3); o += 4; memcpy(old + o, "PRE", 3); o += 3;
        le64p(old + o, 10); o += 8;
        le64p(old + o, 20); o += 8;
        le64p(old + o, 20); o += 8;
        le64p(old + o, 30); o += 8;
        le64p(old + o, 10); o += 8;
        le64p(old + o, 1); o += 8;
        memcpy(old + o, k1, 32); o += 32;
        sha256(old, o, old + o); o += 32;
        tcheck(ckpt_parse(old, o, &in, err, sizeof err) == 0 && in.n_targets == 1 &&
               in.targets[0].n_suffix == 2 && in.targets[0].n_prefix == 1 &&
               (in.targets[0].flags & 4) && in.unique_hits == 1,
               "ckpt: v4 file loads as one legacy OR target");
        ckpt_in_free(&in);
    }
    // EN: 6. target matching: alternatives inside a group, AND between groups
    // RU: 6. совпадение целей: альтернативы внутри группы, AND между группами
    n_targets = 0;
    Target *t = new_target();
    snprintf(t->name, sizeof t->name, "T");
    t->has_suffix = 1;
    strlist_add(&t->suffix, "ZZZZZ", 5);
    strlist_add(&t->suffix, "[0-9]", 5);
    target_compile(t);
    char addr[49];
    memset(addr, 'A', 48);
    addr[48] = 0;
    strcpy(addr + 47, "7");
    tcheck(target_matches(t, addr), "targets: second suffix alternative matches");
    strcpy(addr + 43, "ZZZZZ");
    tcheck(target_matches(t, addr), "targets: first suffix alternative matches");
    strcpy(addr + 43, "QQQQQ");
    addr[47] = 'X';
    tcheck(!target_matches(t, addr), "targets: no suffix matches");
    n_targets = 0;
    t = new_target();
    snprintf(t->name, sizeof t->name, "AND");
    t->has_prefix = t->has_suffix = 1;
    strlist_add(&t->prefix, "QQ", 2);
    strlist_add(&t->suffix, "7", 1);
    target_compile(t);
    memset(addr, 'A', 48);
    strcpy(addr + 2, "QQ");
    addr[47] = '7';
    tcheck(target_matches(t, addr), "targets: prefix AND suffix match");
    addr[47] = '8';
    tcheck(!target_matches(t, addr), "targets: prefix without suffix does not match");
    addr[47] = '7';
    memset(addr + 2, 'A', 2);
    tcheck(!target_matches(t, addr), "targets: suffix without prefix does not match");
    addr[0] = 'Q'; addr[1] = 'Q'; addr[2] = 'A'; addr[3] = 'A';
    tcheck(!target_matches(t, addr), "targets: prefix does not match the UQ/EQ form tag itself");
    t->legacy_or = 1;
    memset(addr, 'A', 48);
    strcpy(addr + 2, "QQ");
    tcheck(target_matches(t, addr), "targets: legacy OR semantics still resume old jobs");
    // EN: --match or: one group (prefix or suffix) suffices
    // RU: --match or: достаточно одной группы (префикса или окончания)
    n_targets = 0;
    t = new_target();
    snprintf(t->name, sizeof t->name, "ORMODE");
    t->has_prefix = t->has_suffix = 1;
    t->match_or = 1;
    strlist_add(&t->prefix, "QQ", 2);
    strlist_add(&t->suffix, "7", 1);
    target_compile(t);
    double p_or = target_probability(t);
    t->match_or = 0;
    double p_and = target_probability(t);
    t->match_or = 1;
    tcheck(p_or > p_and && p_or < 1, "stats: --match or has a higher hit probability than AND");
    memset(addr, 'A', 48);
    strcpy(addr + 2, "QQ");
    addr[47] = '7';
    tcheck(target_matches(t, addr), "targets: --match or matches prefix and suffix");
    addr[47] = '8';
    tcheck(target_matches(t, addr), "targets: --match or matches the prefix alone");
    addr[47] = '7';
    memset(addr + 2, 'A', 2);
    tcheck(target_matches(t, addr), "targets: --match or matches the suffix alone");
    addr[47] = '8';
    tcheck(!target_matches(t, addr), "targets: --match or still needs one group to match");
    // EN: the global --match default applies where the target has no own --match
    // RU: глобальное значение --match действует там, где у цели нет своего --match
    n_targets = 0;
    opt.match_or = 1;
    Target *gm1 = new_target(); // EN: no own --match: takes the global default
                                // RU: своего --match нет: берёт глобальное значение
    gm1->has_suffix = 1;
    strlist_add(&gm1->suffix, "X", 1);
    Target *gm2 = new_target();
    gm2->match_given = 1; // EN: own --match and overrides the global or
                          // RU: свой --match, перекрывает глобальный or
    gm2->match_or = 0;
    gm2->has_suffix = 1;
    strlist_add(&gm2->suffix, "X", 1);
    targets_prepare();
    tcheck(gm1->match_or == 1 && gm2->match_or == 0, "targets: global --match default, per-target override");
    opt.match_or = 0;
    n_targets = 0;
    // EN: --ignore-case is applied when the target is compiled
    // RU: --ignore-case применяется при компиляции цели
    n_targets = 0;
    t = new_target();
    snprintf(t->name, sizeof t->name, "CASE");
    t->has_prefix = 1;
    t->ignore_case = 1;
    strlist_add(&t->prefix, "ab", 2);
    target_compile(t);
    memset(addr, 'A', 48);
    addr[2] = 'A'; addr[3] = 'B';
    tcheck(target_matches(t, addr), "targets: ignore-case applies to the target patterns");
    // EN: the all-targets bitset matcher must agree with the per-target scan on every address: a fixed
    // EN: LCG generates addresses over the full base64url alphabet against hundreds of mixed targets
    // RU: битсет-матчер всех целей обязан совпадать с перебором целей на каждом адресе: фиксированный
    // RU: LCG генерирует адреса по всему алфавиту base64url против сотен смешанных целей
    {
        static const char *const PRE[] = {"QQ", "U[0-9]", "_[Pp]a", "AB[C-F]", "Z"};
        static const char *const SUF[] = {"ZZZZZ", "[0-9]", "_[Pp]auper", "7", "[a-m][n-z]A", "TELEGRAM", "_PASHA"};
        static const char ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int k = 0; k < 300; k++) {
            Target *t = new_target();
            snprintf(t->name, sizeof t->name, "M%d", k);
            if (k % 3 != 2 || k % 4 == 3) { // EN: keep at least one pattern per target | RU: у каждой цели остаётся хотя бы один шаблон
                t->has_prefix = 1;
                const char *s = PRE[k % 5];
                strlist_add(&t->prefix, s, strlen(s));
            }
            if (k % 4 != 3) {
                t->has_suffix = 1;
                const char *s = SUF[k % 7];
                strlist_add(&t->suffix, s, strlen(s));
            }
            t->match_or = k % 7 == 0;
            t->legacy_or = k == 299;
            t->ignore_case = k % 11 == 0;
            target_compile(t);
        }
        targets_match_build();
        uint64_t rng = 0x9E3779B97F4A7C15ull;
        int mism = 0;
        uint8_t m1[300], m2[300];
        for (int it = 0; it < 8000 && !mism; it++) {
            char a[49];
            for (int j = 0; j < 48; j++) {
                rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                a[j] = ALPHA[(rng >> 56) & 63];
            }
            a[48] = 0;
            memset(m1, 0, sizeof m1);
            int any1 = targets_match_all(a, m1);
            memset(m2, 0, sizeof m2);
            int any2 = 0;
            for (int i = 0; i < n_targets; i++)
                if (target_matches(&targets[i], a)) { m2[i] = 1; any2 = 1; }
            if (any1 != any2 || memcmp(m1, m2, sizeof m1)) mism = 1;
        }
        tcheck(!mism, "matcher: bitset NFA agrees with the per-target scan on 8000 addresses x 300 targets");
        n_targets = 0;
        targets_match_build();
    }
    // EN: 7. target names: unnamed targets become t1, t2, ...; duplicates get a numeric suffix
    // RU: 7. имена целей: безымянные становятся t1, t2, ...; дубликаты получают числовой суффикс
    n_targets = 0;
    Target *n1 = new_target();
    snprintf(n1->given_name, sizeof n1->given_name, "A");
    snprintf(n1->label, sizeof n1->label, "A");
    n1->name_given = 1;
    Target *n2 = new_target();
    Target *n3 = new_target();
    snprintf(n3->given_name, sizeof n3->given_name, "A");
    snprintf(n3->label, sizeof n3->label, "A");
    n3->name_given = 1;
    target_names_resolve();
    tcheck(!strcmp(n1->name, "A") && !strcmp(n2->name, "t1") && !strcmp(n3->name, "A2"),
           "targets: names are resolved and deduplicated");
    char cyr_label[MAX_TARGET_LABEL], cyr_slug[MAX_TARGET_NAME];
    target_label_normalize("\xD0\x9F\xD0\xB0\xD1\x88\xD0\xB0", cyr_label, sizeof cyr_label);
    target_slug(cyr_label, cyr_slug, sizeof cyr_slug);
    tcheck(!strcmp(cyr_slug, "Pasha"), "targets: Cyrillic labels transliterate to a safe id");
    char cp1251_label[MAX_TARGET_LABEL];
    target_label_normalize("\xCF\xE0\xF8\xE0", cp1251_label, sizeof cp1251_label);
    tcheck(!strcmp(cp1251_label, cyr_label), "targets: CP1251 labels are normalized to UTF-8");
    // EN: 8. progress odds: a target that reached its --count no longer counts toward "expected"
    // RU: 8. оценка прогресса: цель, набравшая свой --count, больше не входит в "ожидаемое"
    n_targets = 0;
    Target *sa = new_target();
    sa->count_given = 1;
    sa->count = 1;
    sa->has_suffix = 1;
    strlist_add(&sa->suffix, "7", 1);
    target_compile(sa);
    Target *sb = new_target();
    sb->has_suffix = 1;
    strlist_add(&sb->suffix, "Q", 1);
    target_compile(sb);
    int stats_active = 0;
    double stats_both = expected_tries_remaining(&stats_active);
    sa->hits = 1; // EN: A reached --count 1 | RU: A достигла --count 1
    double stats_one = expected_tries_remaining(&stats_active);
    tcheck(stats_active == 1 && stats_both > 0 && stats_one > stats_both * 1.5,
           "stats: a finished target is dropped from the expected tries");
    // EN: 9. --all-patterns: per-pattern hit counts and the done/fill rules
    // RU: 9. --all-patterns: счётчики хитов по шаблонам и правила done/fill
    n_targets = 0;
    Target *ap = new_target();
    snprintf(ap->name, sizeof ap->name, "AP");
    ap->count_given = 1;
    ap->count = 1;
    ap->all_patterns = 1;
    ap->has_suffix = 1;
    strlist_add(&ap->suffix, "ZZZZZ", 5);
    strlist_add(&ap->suffix, "[0-9]", 5);
    target_compile(ap);
    char ap_addr[49];
    memset(ap_addr, 'A', 48);
    ap_addr[48] = 0;
    strcpy(ap_addr + 47, "7"); // EN: matches the numeric alternative only
                               // RU: совпадает только с числовой альтернативой
    mutex_lock(&stat_lock);
    int fills7 = target_fills_pattern_locked(ap, ap_addr);
    target_count_pattern_hits_locked(ap, ap_addr);
    int refills7 = target_fills_pattern_locked(ap, ap_addr);
    strcpy(ap_addr + 43, "ZZZZZ"); // EN: matches the ZZZZZ alternative only
                                   // RU: совпадает только с альтернативой ZZZZZ
    int fillsZ = target_fills_pattern_locked(ap, ap_addr);
    int done_one = target_done_locked(ap);
    target_count_pattern_hits_locked(ap, ap_addr);
    int done_both = target_done_locked(ap);
    mutex_unlock(&stat_lock);
    tcheck(fills7 && !refills7 && fillsZ && !done_one && done_both,
           "all-patterns: a hit fills only its own variant; the target is done when all are full");
    // EN: one address that matches two variants counts for both of them
    // RU: один адрес, совпавший с двумя вариантами, засчитывается обоим
    n_targets = 0;
    Target *ap2 = new_target();
    snprintf(ap2->name, sizeof ap2->name, "AP2");
    ap2->count_given = 1;
    ap2->count = 1;
    ap2->all_patterns = 1;
    ap2->has_suffix = 1;
    strlist_add(&ap2->suffix, "7", 1);
    strlist_add(&ap2->suffix, "[0-9]", 5);
    target_compile(ap2);
    memset(ap_addr, 'A', 48);
    ap_addr[47] = '7'; // EN: matches both alternatives at once | RU: совпадает сразу с обеими альтернативами
    mutex_lock(&stat_lock);
    target_count_pattern_hits_locked(ap2, ap_addr);
    int done_overlap = target_done_locked(ap2);
    mutex_unlock(&stat_lock);
    tcheck(done_overlap, "all-patterns: one address matching two variants finishes both");
    // EN: without the flag the target-level count is unchanged
    // RU: без флага счётчик уровня цели не меняется
    ap2->all_patterns = 0;
    ap2->suf_hits[0] = 0;
    ap2->suf_hits[1] = 0;
    ap2->hits = 1;
    mutex_lock(&stat_lock);
    int done_plain = target_done_locked(ap2);
    int fills_plain = target_fills_pattern_locked(ap2, ap_addr);
    mutex_unlock(&stat_lock);
    tcheck(done_plain && fills_plain,
           "all-patterns: without the flag the target-level --count still applies");
    // EN: unlimited count: never done, every matching hit is saved
    // RU: без ограничения: цель никогда не завершается, каждый подходящий хит сохраняется
    ap2->all_patterns = 1;
    ap2->count_given = 0;
    ap2->count = 0;
    ap2->hits = 5;
    mutex_lock(&stat_lock);
    int done_unlimited = target_done_locked(ap2);
    int fills_unlimited = target_fills_pattern_locked(ap2, ap_addr);
    mutex_unlock(&stat_lock);
    tcheck(!done_unlimited && fills_unlimited,
           "all-patterns: an unlimited target never finishes and saves every matching hit");
    printf("%s: %d failure(s)\n", fails ? "SELF-TEST FAILED" : "deterministic-search self-test passed", fails);
}

int selftest_det(void) {
    det_unit_tests();
    return fails ? 1 : 0;
}
