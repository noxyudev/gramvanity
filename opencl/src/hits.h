// EN: hits.h - hit handling: CPU re-derivation and verification of every match, hit file writing, the
// EN: end-of-session (stop) report and the final save-and-report path shared by the main loop and the
// EN: console-control handler.
// RU: hits.h - работа с хитами: повторный вывод и проверка каждого совпадения на CPU, запись hit-файлов,
// RU: итоговый (stop) отчёт и общий путь сохранения и отчёта для главного цикла и обработчика консоли.
#ifndef SV_HITS_H
#define SV_HITS_H

#include <stddef.h>
#include <stdint.h>

#include "platform.h" // mutex_t
#include "pattern.h"  // Target

// EN: Guards the end-of-session report so it is written exactly once (main loop or console handler).
// RU: Защищает итоговый отчёт, чтобы он записывался ровно один раз (главный цикл или обработчик консоли).
extern mutex_t finish_lock;
// EN: Last stop report written (for the final console message).
// RU: Последний записанный stop-отчёт (для финального сообщения в консоли).
extern char g_stop_path[4096];

// EN: Human-readable effective pattern groups of one target for hit/stop files and --checkpoint-info.
// RU: Читаемые эффективные группы шаблонов цели для hit/stop-файлов и --checkpoint-info.
size_t target_desc(const Target *t, char *buf, size_t cap, size_t o);

// EN: Final CPU part for one derived private key: wallet address and target check. pub is the
// EN: Ed25519 public key computed on the GPU (handle_hit re-derives it on the CPU and compares).
// RU: Финальная часть на CPU для одного выведенного приватного ключа: адрес кошелька и проверка целей.
// RU: pub — публичный ключ Ed25519, вычисленный на GPU (handle_hit выводит его заново на CPU и сверяет).
void finish(const int *idx, int nw, const uint8_t priv[32], const uint8_t pub_gpu[32], uint64_t cand_index);

// EN: Saves the checkpoint and the stop report once (guarded by finish_lock), then, outside a signal
// EN: handler, prints a summary and exits. in_handler != 0 skips printing and exiting (console handler).
// RU: Один раз сохраняет checkpoint и stop-отчёт (под finish_lock), затем, вне обработчика сигналов,
// RU: печатает сводку и выходит. in_handler != 0 пропускает печать и выход (обработчик консоли).
void save_all_and_report(const char *reason, int in_handler);

#endif
