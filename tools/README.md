# Engineering tools

- `corpus/`: golden-model and mutation-corpus management.
- `migration/`: document and schema migration utilities.
- `benchmark/`: reproducible CAD, solver, and hardware benchmark drivers.

Tools may consume public module APIs and versioned schemas. They must not link to module-private implementation directories.
