# ADR 0003: MIT license for project-owned work

**Status:** Accepted
**Date:** October 9, 2026

## Context

The project is intended to provide an open CAD and electromagnetic-simulation baseline while retaining clean adapter boundaries for dependencies and optional commercial components. The implementation plan left the application license open between permissive and copyleft choices. Project files also lacked a consistent license notice.

## Decision

Project-owned software and documentation are licensed under the MIT License. The root `LICENSE` file contains the controlling license text.

C++, CMake, and shell files carry an SPDX copyright and license header using `SPDX-License-Identifier: MIT`. Shell headers follow the interpreter line. Markdown and JSON files are covered by the root license and do not require inline comments.

Third-party software, generated vendor material, and imported datasets retain their own licenses and notices. Adding a dependency or vendored artifact requires recording and checking its distribution terms; the MIT license does not relicense it.

## Alternatives

- GPL-3.0-or-later would require a stronger source-sharing commitment for distributed derivative works, but it would narrow compatibility with the planned optional proprietary adapters and some embedding scenarios.
- Deferring the choice would leave contributions and redistribution ambiguous while implementation continued.
- A custom or dual license would add legal and administrative work without a current product requirement.

## Consequences

- Users may use, modify, redistribute, sublicense, and sell project-owned work subject to retaining the MIT copyright and permission notice.
- Distributed modifications are not required by the project license to publish their source.
- Dependency compatibility and notice obligations remain separate release requirements.
- New project source and build scripts must carry the repository's SPDX header.
- Contribution policy and the dependency/license register remain open product-lane work.
