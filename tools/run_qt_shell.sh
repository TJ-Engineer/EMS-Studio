#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qt_root="${repository_root}/build/qt-sdk/usr"
library_root="${qt_root}/lib/x86_64-linux-gnu"

build_preset="${OEP_SHELL_PRESET:-qt-cad}"
workspace="${1:-cad}"
if [[ $# -gt 0 ]]; then
    shift
fi

case "${workspace}" in
    cad)
        executable="${repository_root}/build/${build_preset}/apps/cad_studio/cad-studio"
        ;;
    em)
        executable="${repository_root}/build/${build_preset}/apps/em_studio/em-studio"
        ;;
    *)
        echo "Usage: $0 [cad|em] [application arguments...]" >&2
        exit 2
        ;;
esac

if [[ ! -x "${executable}" ]]; then
    echo "Native shell is not built. Run cmake --build --preset ${build_preset} first." >&2
    exit 1
fi

export LD_LIBRARY_PATH="${repository_root}/build/occt-sdk/usr/lib/x86_64-linux-gnu:${library_root}:${library_root}/libproxy:${LD_LIBRARY_PATH:-}"
export QML2_IMPORT_PATH="${library_root}/qt6/qml${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"
export QT_PLUGIN_PATH="${library_root}/qt6/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"

exec "${executable}" "$@"
