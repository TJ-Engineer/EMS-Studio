#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_directory="${repository_root}/build/qt-packages"
sdk_directory="${repository_root}/build/qt-sdk"
manifest_file="${package_directory}/SHA256SUMS"
locked_manifest="${repository_root}/tools/qt_ubuntu_noble.SHA256SUMS"

mkdir -p "${package_directory}" "${sdk_directory}"

qt_base_version="6.4.2+dfsg-21.1build5"
qt_declarative_version="6.4.2+dfsg-4build3"
requested_packages=(
    "qt6-base-dev=${qt_base_version}"
    "qt6-declarative-dev=${qt_declarative_version}"
    "qt6-declarative-dev-tools=${qt_declarative_version}"
    "qml6-module-qtqml=${qt_declarative_version}"
    "qml6-module-qtqml-models=${qt_declarative_version}"
    "qml6-module-qtquick=${qt_declarative_version}"
    "qml6-module-qtquick-controls=${qt_declarative_version}"
    "qml6-module-qtquick-layouts=${qt_declarative_version}"
    "qml6-module-qtquick-templates=${qt_declarative_version}"
    "qml6-module-qtquick-window=${qt_declarative_version}"
    "qml6-module-qtqml-workerscript=${qt_declarative_version}"
)
local_link_support=(
    "libgl1=1.7.0-1build1"
    "libglx0=1.7.0-1build1"
    "libxkbcommon-dev=1.6.0-1build1"
    "libxkbcommon0=1.6.0-1build1"
)

mapfile -t locked_files < <(awk '!/^#/ && NF >= 2 {print $2}' "${locked_manifest}")
package_specs=()
declare -A locked_spec_set=()
for locked_path in "${locked_files[@]}"; do
    package_file="${locked_path#./}"
    package_name="${package_file%%_*}"
    version_and_architecture="${package_file#*_}"
    package_version="${version_and_architecture%_*}"
    package_version="${package_version//%3a/:}"
    package_spec="${package_name}=${package_version}"
    package_specs+=("${package_spec}")
    locked_spec_set["${package_spec}"]=true
done

for required_spec in "${requested_packages[@]}" "${local_link_support[@]}"; do
    if [[ ! -v "locked_spec_set[${required_spec}]" ]]; then
        echo "Qt package lock is missing ${required_spec}" >&2
        exit 1
    fi
done

cd "${package_directory}"
for index in "${!package_specs[@]}"; do
    package_file="${locked_files[${index}]#./}"
    if [[ ! -f "${package_file}" ]]; then
        apt-get download "${package_specs[${index}]}"
    fi
done

sha256sum --check "${locked_manifest}" >/dev/null
cp "${locked_manifest}" "${manifest_file}"
for locked_path in "${locked_files[@]}"; do
    dpkg-deb -x "${locked_path}" "${sdk_directory}"
done

qt_config="${sdk_directory}/usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake"
qml_runtime="${sdk_directory}/usr/lib/x86_64-linux-gnu/qt6/qml/QtQuick/qmldir"
qmllint_tool="${sdk_directory}/usr/lib/qt6/bin/qmllint"
for required_file in "${qt_config}" "${qml_runtime}" "${qmllint_tool}"; do
    if [[ ! -f "${required_file}" ]]; then
        echo "Incomplete Qt SDK: missing ${required_file}" >&2
        exit 1
    fi
done

echo "Qt 6.4.2 SDK ready at ${sdk_directory}/usr"
echo "Downloaded package checksums recorded at ${manifest_file}"
