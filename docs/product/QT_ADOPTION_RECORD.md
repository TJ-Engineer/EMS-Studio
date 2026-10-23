# Qt 6.4.2 adoption record

<!--
SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
SPDX-License-Identifier: MIT
-->

**Status date:** October 10, 2026

**Engineering status:** Optional native-shell path in use. Binary distribution remains gated on artifact-level notices, source-access/relinking material, SBOM generation, and release review.

## Scope and boundary

Qt belongs to `oep::app_shell`. No Qt type appears in document, CAD, ASIR, solver, persistence, worker, or durable-schema APIs. The default preset remains headless, while `OEP_ENABLE_QT_SHELL` selects the native implementation behind the same application entry contract.

CMake requires exact Qt 6.4.2 and the compiled modules `Core`, `Gui`, `Qml`, `Quick`, and `QuickControls2`. The QML runtime allowlist contains `QtQml`, `QtQml.Models`, `QtQml.WorkerScript`, `QtQuick`, `QtQuick.Window`, `QtQuick.Controls`, `QtQuick.Templates`, and `QtQuick.Layouts`. Adding another Qt module requires updating this record and the dependency matrix first.

## Ubuntu 24.04 development artifact

`tools/bootstrap_qt_ubuntu_noble.sh` pins Ubuntu's Qt Base packages at `6.4.2+dfsg-21.1build5` and Qt Declarative packages at `6.4.2+dfsg-4build3`. It downloads the locked Noble dependency closure, verifies every package against `tools/qt_ubuntu_noble.SHA256SUMS`, and extracts the verified files under ignored `build/qt-sdk/usr`. The local SDK changes no system package.

The package lock covers the actual transitive development artifact, including QPA plugins and the libraries needed by Qt's host tools. It is specific to Ubuntu 24.04 amd64. Other platforms need their own pinned artifact, module verification, dependency inventory, and launch evidence.

## License and packaging disposition

The selected Qt modules are available under Qt's LGPL/GPL or commercial choices; the open OEP path uses the LGPL-available modules and excludes GPL-only add-ons. Development links dynamically to the extracted Ubuntu libraries. A distributable package must retain the applicable Qt and third-party notices, provide the required corresponding-source access and relinking path, avoid stripping the user's replacement rights, and generate its SBOM from the shipped files. A commercial Qt build is a separate packaging decision.

The repository's MIT license applies only to OEP-owned sources. It does not replace Qt, Ubuntu package, plugin, or transitive library terms. The extracted SDK retains package copyright files under `usr/share/doc`; those files are inputs to later artifact-level notice generation.

## Verification evidence

- Configuration prints the exact Qt version and allowlisted compiled modules.
- Strict compilation covers both shared desktop entry points and the QML resource.
- `qmllint` resolves the pinned runtime modules and typed C++ controller contract.
- CAD Studio and EM Studio load the shell with the offscreen platform and assert its navigation/accessibility contract.
- The isolated controller test proves bounded persistence and reset of the dock-style layout.
- The default non-Qt preset still passes its original twenty-three checks.

The platform/UI maintainers own version updates and adapter tests. Release owners remain responsible for vulnerability review, final notices, SBOM output, source-access instructions, and approval of the exact packaged artifact.
