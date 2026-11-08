# gramvanity (OpenCL)
#
# EN: Linux:    make
# EN: Windows:  make   (in an MSYS2 UCRT64 or MINGW64 shell, see README)
# RU: Linux:    make
# RU: Windows:  make   (в оболочке MSYS2 UCRT64 или MINGW64, см. README)
#
# EN: embedded.h (kernels + wordlist) is generated from sha512_consts.h, kernel.cl, ed25519_gpu.cl
# EN: and english.txt; regenerate it with `make embed` (needs python3, or python on Windows).
# RU: embedded.h (ядра + словарь) генерируется из sha512_consts.h, kernel.cl, ed25519_gpu.cl и
# RU: english.txt; перегенерировать можно командой `make embed` (нужен python3 или python на Windows).
SRC = src/main.c src/platform.c src/options.c src/pattern.c src/crypto.c src/detsearch.c src/addresses.c src/gpu.c src/stats.c src/checkpoint.c src/hits.c src/search.c src/selftest.c src/i18n.c ed25519_fast.c sha256.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
CFLAGS ?= -O3 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation

# EN: Platform-specific link libraries and executable suffix. The Makefile does not create object
# EN: files: every build compiles the whole SRC list into a single binary.
# RU: Библиотеки линковки и суффикс исполняемого файла для каждой платформы. Makefile не создаёт
# RU: объектных файлов: каждая сборка компилирует весь список SRC в один бинарник.
# EN: Windows when the environment declares it (build-windows.sh exports OS=Windows_NT) or when
# EN: uname reports an MSYS2/MINGW host, so a plain `make` works in a clean MSYS2 shell too;
# EN: everything else takes the Linux branch.
# RU: Windows, когда об этом говорит окружение (build-windows.sh экспортирует OS=Windows_NT),
# RU: или когда uname сообщает о хосте MSYS2/MINGW, поэтому обычный `make` работает и в чистой
# RU: оболочке MSYS2; всё остальное собирается по Linux-ветке.
UNAME := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  WINBUILD = 1
else ifneq (,$(findstring NT,$(UNAME)))
  WINBUILD = 1
endif

ifeq ($(WINBUILD),1)
  ifeq ($(origin CC),default)
    CC = gcc
  endif
  LIBS = -lOpenCL -lbcrypt -static-libgcc
  EXE = .exe
else
  LIBS = -lOpenCL -lpthread -lm
endif

# EN: -I.: src/*.c resolve "embedded.h", "sha256.h" and the third_party headers relative to opencl/.
# RU: -I.: файлы src/*.c находят "embedded.h", "sha256.h" и заголовки third_party относительно opencl/.
gramvanity$(EXE): $(SRC) embedded.h sha256.h
	$(CC) $(CFLAGS) -I. -o $@ $(SRC) $(LIBS)

# EN: Regenerate embedded.h after editing kernel.cl, ed25519_gpu.cl, sha512_consts.h or english.txt.
# RU: Перегенерировать embedded.h после правки kernel.cl, ed25519_gpu.cl, sha512_consts.h или english.txt.
embed:
	python3 tools/embed.py

# EN: Remove the built binary; embedded.h stays because it is committed.
# RU: Удалить собранный бинарник; embedded.h остаётся, потому что хранится в репозитории.
clean:
	rm -f gramvanity gramvanity.exe

.PHONY: embed clean
