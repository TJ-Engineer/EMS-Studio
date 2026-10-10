# Cartesian EM grid spike

`CartesianGridBuilder` maps prepared EM geometry onto a uniform three-dimensional Cartesian grid while retaining the CAD identity behind every derived artifact.

The grid stores the full material catalog and one dominant material assignment per cell. Separate region mappings preserve every exact box-cell intersection fraction even when a material is not dominant. Surface and port mappings retain their role or port ID, persistent CAD entity, and per-cell area fraction. The dominant-material threshold is an explicit grid policy and defaults to `0.5`; ties follow stable role-ID order and mixed cells remain visible through their subcell mappings and diagnostics.

For each volume or planar patch, the builder reports:

- exact geometric volume or area;
- subcell-integrated measure;
- binary cell-center measure and relative error;
- minimum feature width measured in cells;
- cut-cell count and low, moderate, or high staircasing risk.

Invalid grid dimensions, hard cell-limit violations, geometry outside the domain, overlapping non-background material volumes, and unmappable faces fail with typed errors. Under-resolved and mixed cells produce warnings without discarding traceability.

The current reference FDTD engine does not yet consume these heterogeneous cell or subcell records. The spike establishes the mapping and diagnostic contract before a production B-rep intersector, material update path, conformal treatment, and calibrated port implementation are added.
