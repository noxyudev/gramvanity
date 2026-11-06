// EN: sv.h - shared types and constants of the gramvanity host program. Include it first in every
// EN: source file: it selects the platform feature macros and the path constants.
// RU: sv.h - общие типы и константы основной программы gramvanity. Включается первым в каждом
// RU: исходнике: он выбирает макросы возможностей платформы и константы путей.
#ifndef SV_H
#define SV_H

#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#ifdef _WIN32
#define PATH_SEP '/'
#define W5ADDR_CLI "../verify-go/w5addr.exe"
#else
#define PATH_SEP '/'
#define W5ADDR_CLI "../verify-go/w5addr"
#endif

// EN: Search modes (--mode).
// RU: Режимы поиска (--mode).
enum { BIP12, BIP24, TON24, TG };
extern const char *MODE_NAME[4];

// EN: Entropy domain (det_entropy_ex mode byte) of the tg-wallet signing half, words 13-24. It is not a
// EN: search mode: the anchor half alone is searched, the signing half is generated for hits only.
// RU: Домен энтропии (байт режима det_entropy_ex) подписывающей половины tg-wallet, слова 13-24. Это
// RU: не режим поиска: ищется только половина-якорь, подписывающая половина создаётся лишь для хитов.
enum { DETSIGN = 16 };

#endif
