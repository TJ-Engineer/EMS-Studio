// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <oep/modeler/kernel_adapter.hpp>

#include <string>
#include <vector>

namespace oep::test {

struct TopologyMutationCaseResult {
    std::string case_id;
    bool passed{};
    std::string detail;
};

struct TopologyMutationReport {
    modeler::KernelIdentity kernel;
    std::vector<TopologyMutationCaseResult> cases;

    [[nodiscard]] bool passed() const noexcept;
};

[[nodiscard]] TopologyMutationReport run_topology_mutation_corpus(
    modeler::GeometryKernel& kernel);

} // namespace oep::test
