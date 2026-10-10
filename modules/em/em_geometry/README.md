# EM geometry preparation

This module owns the first traceable boundary between validated ASIR intent and solver-specific geometry. `GeometryPreparer` resolves every material, surface-role, port-face, and reference-plane identity against a CAD snapshot carrying the exact evaluated geometry hash.

The spike accepts world-space axis-aligned volume boxes and planar patches. It preserves:

- the ASIR fingerprint and exact CAD geometry hash;
- role-assignment, material, port, and mode identifiers;
- persistent feature/local topology IDs for every resolved volume and face;
- independent port-face and reference-plane identities, orientation, impedance, de-embedding, and termination metadata.

Input collections are emitted in stable-ID order, so equivalent ASIR or snapshot ordering cannot change the computational model. Stale hashes, unresolved or duplicate IDs, topology-kind mismatches, invalid geometry, missing materials, ambiguous backgrounds, misoriented ports, and incompatible reference planes produce typed errors before meshing.

This is a geometry-transfer spike rather than a general B-rep voxelizer. A CAD adapter must apply component transforms before supplying primitives. Curved surfaces, rotated regions, healing, defeaturing, exact B-rep intersection, coatings, and thin-sheet/wire models remain future preparation work.
