// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Shared desktop entry-point contract. The executable selects the initial
// workspace explicitly; the build chooses the Qt host or headless fallback.
// Business state and editing commands belong to the document/controller layers.

#pragma once

#include <string>

namespace oep::app_shell {

enum class Workspace {
    cad,
    em,
};

struct ApplicationOptions {
    Workspace initial_workspace{Workspace::cad};
    std::string product_name;
    std::string version;
};

[[nodiscard]] int run_application(
    int argc,
    char** argv,
    const ApplicationOptions& options);

} // namespace oep::app_shell
