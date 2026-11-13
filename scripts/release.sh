#!/usr/bin/env bash
# Build, test and package a release of open-deck-export.
#
#   ./scripts/release.sh [build-dir]        # build-dir por defecto: build-release
#   GENERATOR=DEB ./scripts/release.sh      # elige el generador CPack
#
# El script trabaja siempre desde la raíz del repositorio, así que puede
# invocarse desde cualquier directorio.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd -- "$(dirname -- "$SCRIPT_DIR")"

BUILD="${1:-build-release}"

echo "==> [1/4] Configurar (Release, Ninja) en '${BUILD}'"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> [2/4] Compilar"
cmake --build "$BUILD"

echo "==> [3/4] Ejecutar pruebas"
ctest --test-dir "$BUILD" --output-on-failure

echo "==> [4/4] Generar paquetes (generador: ${GENERATOR:-TGZ}) en 'dist'"
cpack --config "$BUILD/CPackConfig.cmake" -G "${GENERATOR:-TGZ}" -B dist

echo
echo "==> Paquetes generados en dist/:"
mapfile -t packages < <(find dist -maxdepth 1 -type f -print | sort)
if ((${#packages[@]} == 0)); then
    echo "  (ninguno)"
else
    printf '  %s\n' "${packages[@]}"
fi
