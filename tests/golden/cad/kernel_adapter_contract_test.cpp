#include <oep/test/fake_geometry_kernel.hpp>
#include <oep/test/kernel_corpus.hpp>

#include <iostream>

int main()
{
    oep::test::FakeGeometryKernel kernel;
    const auto report = oep::test::run_kernel_corpus(kernel);
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
              << " CAD kernel corpus cases\n";
    return 0;
}
