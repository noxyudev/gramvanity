// EN: i18n.c - the message language of the program and tr(), the English/Russian selector.
// RU: i18n.c - язык сообщений программы и tr(), выбор между английским и русским вариантом.
#include "sv.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

// EN: English is the default for every run: scripts and the hit/stop formats it produces stay
// EN: stable unless the user explicitly asks for Russian.
// RU: Английский — значение по умолчанию для любого запуска: скрипты и форматы hit/stop-файлов
// RU: остаются стабильными, пока пользователь явно не попросит русский.
Lang g_lang = LANG_EN;

int lang_set(const char *code) {
    if (!code || !*code) return 0;
    if (!strcmp(code, "en")) { g_lang = LANG_EN; return 1; }
    if (!strcmp(code, "ru")) { g_lang = LANG_RU; return 1; }
    return 0;
}

const char *lang_init_env(void) {
    const char *v = getenv("GRAMVANITY_LANG");
    if (!v || !*v) return NULL;
    if (lang_set(v)) return NULL;
    return v;
}

const char *tr(const char *en, const char *ru) {
    return g_lang == LANG_RU ? ru : en;
}
