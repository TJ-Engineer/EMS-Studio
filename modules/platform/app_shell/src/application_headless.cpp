// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Headless fallback keeps desktop entry points buildable without Qt. It
// reports application identity only; interactive editing requires the Qt host.

#include "oep/app_shell/application.hpp"

#include <iostream>

namespace oep::app_shell {

int run_application(
    const int argc,
    char** const argv,
    const ApplicationOptions& options)
{
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cout << "Open Engineering Platform " << options.product_name << ' '
              << options.version << '\n';
    return 0;
}

} // namespace oep::app_shell
