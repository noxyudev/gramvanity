FROM golang:1.26-bookworm

# EN: Alternative developer environment: build and CPU checks in a container, so no MSYS2, Go,
# EN: Python or GCC is ever installed on the developer's own machine. The repository is MOUNTED at
# EN: run time (not copied into the image), so the built binaries land on the host in the layout
# EN: the search expects: opencl/ + verify-go/ side by side.
# RU: Альтернативная среда разработчика: сборка и CPU-проверки в контейнере, чтобы на машине
# RU: разработчика не появлялись ни MSYS2, ни Go, ни Python, ни GCC. Репозиторий МОНТИРУЕТСЯ в
# RU: момент запуска (а не копируется в образ), поэтому собранные бинарники остаются на хосте в
# RU: ожидаемой раскладке: opencl/ и verify-go/ рядом.
#
# EN: Build and run (from the repository root):
# RU: Сборка и запуск (из корня репозитория):
#
#   docker build -t gramvanity-build ./docker
#   docker run --rm -v "$PWD":/src \
#     -v gramvanity-gomod:/go/pkg/mod -v gramvanity-gocache:/go/cache \
#     gramvanity-build
#
# EN: GPU: reachable from the container only on a Linux host - add `--device /dev/dri`
# EN: (AMD/Intel/Mesa) or use nvidia-container-toolkit (NVIDIA); the driver itself stays on the
# EN: host. On Windows and macOS hosts the GPU is unreachable from containers: this image then
# EN: serves builds and CPU checks only, and the entry point says so explicitly.
# EN: The full GPU suite (opencl/tests/test_det.sh) inside Docker is a best-effort scenario on a
# EN: Linux host.
# RU: GPU: контейнеру доступен только на Linux-хосте - добавьте `--device /dev/dri`
# RU: (AMD/Intel/Mesa) или используйте nvidia-container-toolkit (NVIDIA); сам драйвер остаётся
# RU: на хосте. На хостах Windows и macOS GPU из контейнера недоступен: образ годится для сборки
# RU: и CPU-проверок, и точка входа сообщит об этом явно. Полный GPU-набор
# RU: (opencl/tests/test_det.sh) в Docker - best-effort сценарий на Linux-хосте.
#
# EN: Go caches live in named volumes so repeated runs do not re-download modules or rebuild the
# EN: build cache. The Go version follows the image tag, so the "Go too old" problem does not
# EN: exist here by construction; keep the tag in sync with the `go` directive in go.mod.
# RU: Кэши Go живут в именованных томах, поэтому повторные запуски не скачивают модули заново и
# RU: не пересобирают кэш. Версия Go задаётся тегом образа, так что проблемы "Go слишком старый"
# RU: здесь нет по построению; держите тег согласованным с директивой `go` в go.mod.

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ocl-icd-opencl-dev \
        clinfo \
        python3 \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV GOMODCACHE=/go/pkg/mod \
    GOCACHE=/go/cache
VOLUME ["/go/pkg/mod", "/go/cache"]

WORKDIR /src
COPY build-check.sh /usr/local/bin/gramvanity-build-check
ENTRYPOINT ["/usr/local/bin/gramvanity-build-check"]
