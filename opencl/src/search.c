// EN: search.c - the candidate pipelines and the range bookkeeping (see search.h). The BIP39 pipeline
// EN: derives everything in one GPU pass; the TON pipeline keeps only the ~1/256 stage-1 survivors of
// EN: each batch and runs the expensive 100000-iteration PBKDF2 on them in stage 2.
// RU: search.c - конвейеры кандидатов и учёт диапазона (см. search.h). Конвейер BIP39 выводит всё
// RU: за один проход GPU; конвейер TON оставляет только ~1/256 выживших стадии 1 из каждого пакета
// RU: и запускает для них дорогой PBKDF2 на 100000 итераций на стадии 2.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "crypto.h"
#include "detsearch.h"
#include "gpu.h"
#include "stats.h"
#include "checkpoint.h"
#include "hits.h"
#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "third_party/monocypher/monocypher.h"

// ---------------------------------------------------------------- BIP39 pipeline

void bip_prep_fn(void *p, int tid, int nt) {
    BipBatch *b = (BipBatch *)p;
    int idx[24];
    char phrase[256];
    for (uint32_t i = (uint32_t)tid; i < b->n; i += (uint32_t)nt) {
        if (det.enabled) det_entropy(b->base + i, b->ent + (size_t)i * b->entlen, (size_t)b->entlen);
        bip39_indices(b->ent + (size_t)i * b->entlen, b->entlen, idx);
        size_t l = phrase_bytes(idx, b->nw, phrase);
        key_block((const uint8_t *)phrase, l, b->keys + (size_t)i * 16);
        crypto_wipe(phrase, sizeof phrase);
    }
}

void words_to_bytes(const uint64_t *w, int nwords, uint8_t *out) {
    for (int j = 0; j < nwords; j++)
        for (int k = 0; k < 8; k++) out[j * 8 + k] = (uint8_t)(w[j] >> (56 - 8 * k));
}

void bip_post_fn(void *p, int tid, int nt) {
    BipBatch *b = (BipBatch *)p;
    int idx[24];
    uint8_t priv[32];
    for (uint32_t i = (uint32_t)tid; i < b->n; i += (uint32_t)nt) {
        bip39_indices(b->ent + (size_t)i * b->entlen, b->entlen, idx);
        words_to_bytes(b->priv + (size_t)i * 4, 4, priv);
        finish(idx, b->nw, priv, b->pub + (size_t)i * 32, b->base + i);
    }
}

BipBatch *bip_new(uint32_t n, uint64_t base) {
    BipBatch *b = calloc(1, sizeof *b);
    b->n = n;
    b->base = base;
    b->entlen = (opt.mode == BIP12 || opt.mode == TG) ? 16 : 32;
    b->nw = (opt.mode == BIP12 || opt.mode == TG) ? 12 : 24;
    b->ent = malloc((size_t)n * b->entlen);
    b->keys = malloc((size_t)n * 128);
    b->priv = malloc((size_t)n * 32);
    b->pub = malloc((size_t)n * 32);
    if (!b->ent || !b->keys || !b->priv || !b->pub) die(tr("out of memory", "не хватает памяти"));
    if (!det.enabled) random_bytes(b->ent, (size_t)n * b->entlen);
    parallel(opt.threads, bip_prep_fn, b);
    job_submit(&b->job, b->keys, n, gpu.salt_mnemonic, 2048, 1, 0, b->priv, b->pub);
    return b;
}

void bip_free(BipBatch *b) {
    job_release(&b->job);
    crypto_wipe(b->ent, (size_t)b->n * b->entlen);
    free(b->ent); free(b->keys); free(b->priv); free(b->pub); free(b);
}

// ---------------------------------------------------------------- TON pipeline

void ton_prep_fn(void *p, int tid, int nt) {
    TonBatch *b = (TonBatch *)p;
    char phrase[256];
    for (uint32_t i = (uint32_t)tid; i < b->n; i += (uint32_t)nt) {
        TonCand *c = &b->cand[i];
        uint8_t r[33];
        if (det.enabled) det_entropy(b->base + i, r, 33);
        else memcpy(r, b->rnd + (size_t)i * 33, 33);
        indices11(r, 24, c->idx);
        crypto_wipe(r, sizeof r);
        c->index = b->base + i;
        size_t l = phrase_bytes(c->idx, 24, phrase);
        ton_entropy(phrase, l, c->ent);
        key_block(c->ent, 64, b->keys + (size_t)i * 16);
        crypto_wipe(phrase, sizeof phrase);
    }
}

TonCand *survivors;
uint32_t n_surv, cap_surv;

void ton_post_fn(void *p, int tid, int nt) {
    TonBatch *b = (TonBatch *)p;
    uint8_t priv[32];
    for (uint32_t i = (uint32_t)tid; i < b->n; i += (uint32_t)nt) {
        words_to_bytes(b->out + (size_t)i * 4, 4, priv);
        finish(b->cand[i].idx, 24, priv, b->pub + (size_t)i * 32, b->cand[i].index);
    }
}

void det_commit(uint64_t upto); // range bookkeeping, defined below

void ton_flush(uint32_t maxn) {
    if (!n_surv) return;
    uint32_t s2n = n_surv < maxn ? n_surv : maxn;
    TonBatch s2 = {0};
    s2.n = s2n;
    s2.cand = survivors;
    s2.keys = malloc((size_t)s2n * 128);
    s2.out = malloc((size_t)s2n * 32);
    s2.pub = malloc((size_t)s2n * 32);
    if (!s2.keys || !s2.out || !s2.pub) die(tr("out of memory", "не хватает памяти"));
    for (uint32_t i = 0; i < s2n; i++) key_block(survivors[i].ent, 64, s2.keys + (size_t)i * 16);
    Job j;
    job_submit(&j, s2.keys, s2n, gpu.salt_ton_default, TON_ITERS, 0, 4, s2.out, s2.pub);
    job_wait(&j);
    job_release(&j);
    parallel(opt.threads, ton_post_fn, &s2);
    mutex_lock(&stat_lock);
    total_done += s2.n;
    mutex_unlock(&stat_lock);
    det_commit(survivors[s2n - 1].index + 1);
    crypto_wipe(survivors, (size_t)s2n * sizeof(TonCand));
    memmove(survivors, survivors + s2n, (size_t)(n_surv - s2n) * sizeof(TonCand));
    n_surv -= s2n;
    free(s2.keys); free(s2.out); free(s2.pub);
}

// ---------------------------------------------------------------- range bookkeeping

uint64_t det_alloc(uint32_t want, uint32_t *got) {
    *got = want;
    if (!det.enabled) return 0;
    uint64_t rem;
    if (det.has_end) rem = det.end > det.cursor ? det.end - det.cursor : 0;
    else rem = UINT64_MAX - det.cursor; // EN: unlimited: covers every index except the very last one
                                        // RU: без ограничения: покрывает все индексы, кроме самого последнего
    if (rem == 0) { *got = 0; return 0; }
    if ((uint64_t)want > rem) *got = (uint32_t)rem;
    uint64_t base = det.cursor;
    det.cursor += *got;
    return base;
}

void det_commit_s1(uint64_t upto) {
    if (!det.enabled || opt.dump || opt.bench > 0) return;
    if (upto > det.s1_next) det.s1_next = upto;
    if (now_sec() - det.last_save >= (double)opt.checkpoint_interval) checkpoint_save();
}

void det_commit(uint64_t upto) {
    if (!det.enabled || opt.dump || opt.bench > 0) return;
    if (upto > det.next) det.next = upto;
    if (opt.mode != TON24 && upto > det.s1_next) det.s1_next = upto;
    if (now_sec() - det.last_save >= (double)opt.checkpoint_interval) checkpoint_save();
}
