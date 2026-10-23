// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include "oep/app_shell/application.hpp"

int main(int argc, char** argv)
{
    return oep::app_shell::run_application(
        argc,
        argv,
        {.initial_workspace = oep::app_shell::Workspace::em,
         .product_name = "EM Studio",
         .version = OEP_VERSION});
}
