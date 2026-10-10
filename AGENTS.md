# Project instructions

## Living development storyboard

`docs/DEVELOPMENT_STORYBOARD.md` is the shared, living view of project progress for the user and development agents.

- Read the storyboard before selecting the next implementation slice.
- Update it in the same change whenever implementation work changes completed evidence, the current development frame, a gate, a blocker, or the planned dependency order.
- Mark a panel complete only after its stated acceptance evidence passes.
- Link completed panels to their implementation and validating tests.
- Keep the status date and progress snapshot current.
- State unfinished capabilities plainly and do not treat panel count as percent complete.
- Keep detailed architecture in the implementation plan and decision records; keep the storyboard visual and outcome-focused.

## License headers

Project-owned software and documentation use the MIT License in the root `LICENSE` file.

- Begin every new C, C++, CMake, and similar commentable source or build file with `SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors` and `SPDX-License-Identifier: MIT`, using that language's comment syntax.
- Put SPDX lines immediately after the interpreter line in executable scripts.
- Markdown and JSON files are covered by the root license and do not require inline license comments.
- Preserve third-party copyright and license notices. Do not replace them with the project header.
