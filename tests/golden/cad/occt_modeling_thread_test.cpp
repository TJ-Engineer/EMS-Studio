#include <oep/modeler/occt_geometry_kernel.hpp>
#include <oep/test/modeling_thread_corpus.hpp>

#include <iostream>
#include <memory>

int main()
{
    const auto report = oep::test::run_modeling_thread_corpus([] {
        return std::make_unique<oep::modeler::OcctGeometryKernel>();
    });
    for (const auto& result : report.cases) {
        if (!result.passed) {
            std::cerr << report.kernel.name << ' ' << result.case_id << ": "
                      << result.detail << '\n';
        }
    }
    if (!report.passed()) {
        return 1;
    }
    std::cout << report.kernel.name << " passed the Reel 2 modeling thread\n";
}
