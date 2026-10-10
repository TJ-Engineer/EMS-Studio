// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <string>
#include <vector>

namespace oep::test {

struct GoldenCaseResult {
    std::string case_id;
    std::string category;
    bool passed{};
    std::string detail;
};

struct KernelCorpusReport {
    modeler::KernelIdentity kernel;
    std::vector<GoldenCaseResult> cases;

    [[nodiscard]] bool passed() const noexcept;
};

[[nodiscard]] KernelCorpusReport run_kernel_corpus(modeler::GeometryKernel& kernel);

} // namespace oep::test
