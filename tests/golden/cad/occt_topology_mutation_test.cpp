// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#include <oep/modeler/occt_geometry_kernel.hpp>
#include <oep/test/topology_mutation_corpus.hpp>

#include <iostream>

int main()
{
    oep::modeler::OcctGeometryKernel kernel;
    const auto report = oep::test::run_topology_mutation_corpus(kernel);
    for (const auto& result : report.cases) {
        if (!result.passed) {
            std::cerr << report.kernel.name << ' ' << result.case_id << ": "
                      << result.detail << '\n';
        }
    }
    if (!report.passed()) {
        return 1;
    }
    std::cout << report.kernel.name << " passed " << report.cases.size()
              << " topology mutation cases\n";
    return 0;
}
