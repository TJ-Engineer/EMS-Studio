#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_directory="${repository_root}/build/occt-packages"
sdk_directory="${repository_root}/build/occt-sdk"
source_archive="${package_directory}/opencascade_7.6.3+dfsg1.orig.tar.xz"

mkdir -p "${package_directory}" "${sdk_directory}"

occt_package_version="7.6.3+dfsg1-7.1build1"
tbb_package_version="2021.11.0-2ubuntu2"
package_specs=(
    "libocct-foundation-dev=${occt_package_version}"
    "libocct-modeling-data-dev=${occt_package_version}"
    "libocct-modeling-algorithms-dev=${occt_package_version}"
    "libocct-foundation-7.6t64=${occt_package_version}"
    "libocct-modeling-data-7.6t64=${occt_package_version}"
    "libocct-modeling-algorithms-7.6t64=${occt_package_version}"
    "libtbb12=${tbb_package_version}"
    "libtbbmalloc2=${tbb_package_version}"
)

cd "${package_directory}"
for package_spec in "${package_specs[@]}"; do
    package_name="${package_spec%%=*}"
    if ! compgen -G "${package_name}_*.deb" >/dev/null; then
        apt-get download "${package_spec}"
    fi
done

for package_file in ./*.deb; do
    dpkg-deb -x "${package_file}" "${sdk_directory}"
done

if [[ ! -f "${source_archive}" ]]; then
    curl -L --fail --output "${source_archive}" \
        'https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/opencascade/7.6.3+dfsg1-7.1build1/opencascade_7.6.3+dfsg1.orig.tar.xz'
fi

expected_source_sha256="03967cfbcbc4b2439b0c7605599f2ae0770b539d937ef0c0c0d3c0006d51d50e"
actual_source_sha256="$(sha256sum "${source_archive}" | cut -d' ' -f1)"
if [[ "${actual_source_sha256}" != "${expected_source_sha256}" ]]; then
    echo "OCCT source archive checksum mismatch" >&2
    exit 1
fi

# Ubuntu 24.04's development package omits this header even though another
# packaged header includes it. Recover the exact matching header from Ubuntu's
# checksummed source archive.
tar -xOf "${source_archive}" \
    occt-V7_6_3/src/NCollection/NCollection_AliasedArray.hxx \
    > "${sdk_directory}/usr/include/opencascade/NCollection_AliasedArray.hxx"

echo "OCCT SDK ready at ${sdk_directory}/usr"
