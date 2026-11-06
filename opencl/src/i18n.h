// EN: i18n.h - the message language of the program (--lang / GRAMVANITY_LANG) and tr().
// RU: i18n.h - язык сообщений программы (--lang / GRAMVANITY_LANG) и функция tr().
//
// EN: This module is a leaf: it depends only on libc, so every other module may include it without
// EN: creating a dependency cycle. English is the default; there is no locale auto-detection.
// RU: Модуль листовой: зависит только от libc, поэтому любой другой модуль может его подключать,
// RU: не создавая цикла зависимостей. По умолчанию английский; автоопределения локали нет.
#ifndef SV_I18N_H
#define SV_I18N_H

typedef enum { LANG_EN = 0, LANG_RU = 1 } Lang;

extern Lang g_lang;

// EN: Selects the adapter language from a two-letter code. Returns 1 on success, 0 if the code is
// EN: not "en" or "ru" (the current language is then left unchanged).
// RU: Выбирает язык по двухбуквенному коду. Возвращает 1 при успехе, 0 если код не "en" и не "ru"
// RU: (текущий язык при этом не меняется).
int lang_set(const char *code);

// EN: Applies GRAMVANITY_LANG before the command line is parsed (--lang may override it). Returns
// EN: the unsupported value, or NULL when the variable is unset, empty or valid.
// RU: Применяет GRAMVANITY_LANG до разбора командной строки (--lang может его переопределить).
// RU: Возвращает неподдерживаемое значение или NULL, если переменная не задана, пуста или корректна.
const char *lang_init_env(void);

// EN: Picks the message variant for the current language: tr("English", "Русский"). Format strings
// EN: must keep identical conversion specifiers in both variants; args are passed by the caller.
// RU: Выбирает вариант сообщения для текущего языка: tr("English", "Русский"). Спецификаторы
// RU: формата в обоих вариантах должны совпадать; аргументы передаёт вызывающая сторона.
const char *tr(const char *en, const char *ru);

#endif
