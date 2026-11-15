#!/bin/sh
# gramvanity - container entry point: build both programs and run the CPU checks.
#
# EN: This is the documented command list of the project, nothing more; the build logic itself
# EN: lives in the Makefile and build-windows.sh and is not duplicated here. Sources are mounted
# EN: at /src, so the binaries land on the host in the expected opencl/ + verify-go/ layout.
# RU: Это документированный список команд проекта и ничего больше; сама логика сборки живёт в
# RU: Makefile и build-windows.sh и здесь не дублируется. Исходники смонтированы в /src, поэтому
# RU: бинарники остаются на хосте в ожидаемой раскладке opencl/ + verify-go/.

set -e
cd /src

echo "=== building the Go verifier ==="
go build -trimpath -o verify-go/w5addr ./verify-go
echo "verify-go/w5addr: OK"

echo "=== building the OpenCL search ==="
make -C opencl
echo "opencl/gramvanity: OK"

echo "=== CPU checks ==="
./opencl/gramvanity --selftest-det
go vet ./verify-go
go test -count=1 ./verify-go
python3 tools/check_i18n.py
python3 tools/check_tr.py

echo "=== OpenCL devices visible inside the container ==="
DEVICES=$(./opencl/gramvanity --list-devices --lang en 2>&1 || true)
printf '%s\n' "$DEVICES"
if printf '%s\n' "$DEVICES" | grep -qi 'no OpenCL GPU found'; then
    echo "NOTE: no OpenCL device inside the container. The build and the CPU checks are fine,"
    echo "but the search will not run here. On a Linux host add --device /dev/dri (AMD/Intel/Mesa)"
    echo "or use nvidia-container-toolkit (NVIDIA); on Windows and macOS hosts the GPU is not"
    echo "reachable from containers."
fi

echo "=== done: binaries are on the host in opencl/ and verify-go/ ==="
