// EN: selftest.h - startup crypto/address/GPU self-test and the deterministic-search unit tests.
// RU: selftest.h - стартовый self-test криптографии/адресов/GPU и unit-тесты детерминированного поиска.
#ifndef SV_SELFTEST_H
#define SV_SELFTEST_H

// EN: Checks CPU primitives, address constructions and the GPU kernels against published vectors.
// EN: Dies with "SELF-TEST FAILED" on the first mismatch; gates every run.
// RU: Проверяет примитивы CPU, построение адресов и ядра GPU против опубликованных векторов.
// RU: При первом расхождении завершается с "SELF-TEST FAILED"; это условие любого запуска.
void self_test(void);

// EN: Runs the deterministic-search unit tests and returns the process exit code (0 = all passed).
// RU: Запускает unit-тесты детерминированного поиска и возвращает код завершения (0 = всё прошло).
int selftest_det(void);

#endif
