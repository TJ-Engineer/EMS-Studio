// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace oep::test {

using GeometryKernelFactory =
    std::function<std::unique_ptr<modeler::GeometryKernel>()>;

struct ModelingThreadCaseResult {
    std::string case_id;
    bool passed{};
    std::string detail;
};

struct ModelingThreadReport {
    modeler::KernelIdentity kernel;
    std::vector<ModelingThreadCaseResult> cases;

    [[nodiscard]] bool passed() const noexcept;
};

[[nodiscard]] ModelingThreadReport run_modeling_thread_corpus(
    const GeometryKernelFactory& make_kernel);

} // namespace oep::test
