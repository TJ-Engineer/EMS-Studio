# ADR 0005: Versioned RMS power-wave port contract

**Status:** Accepted
**Date:** October 10, 2026

## Context

Reusable multiport response bases are only valid when every solver and results consumer agrees on phasor scaling, current direction, wave normalization, modal phase, inactive-port termination, and reference planes. A hidden sign, peak-versus-RMS, or phase choice can produce plausible but incorrect S-parameters and reconstructed fields. Eigenmode fields also carry an arbitrary complex phase unless the system removes it deterministically.

## Decision

The first response-basis contract is identified as `oep-rms-power-wave-1.0`. It uses RMS phasors with the physical-time convention

\[
x(t)=\operatorname{Re}\{\sqrt{2}\,x e^{+j\omega t}\}.
\]

Port voltage follows the declared modal voltage reference, and port current is positive into the computational domain. Every V1 propagating mode has a finite positive-real reference impedance. Its incident and outgoing waves are

\[
a_p=\frac{V_p+Z_{0,p}I_p}{2\sqrt{Z_{0,p}}},\qquad
b_p=\frac{V_p-Z_{0,p}I_p}{2\sqrt{Z_{0,p}}}.
\]

The amplitudes have units of `sqrt(W)`, terminal real power is `|a_p|² - |b_p|²`, and the multiport relation is `b = S a`. A basis run applies `a_p = 1 sqrt(W)` at one port and zero incident wave at every matched inactive port. Its outgoing vector becomes column `p` of `S`, and its normalized far field becomes column `p` of the field basis.

Each mode declares an inward unit orientation and a named transverse-field overlap functional plus fallback. The modal field is multiplied by the conjugate phase of a nonzero overlap so the anchored overlap is real and positive. Near-degenerate modes additionally require an explicit orthogonal basis and a sufficient tracked overlap across frequency; ambiguous modes are rejected.

For a positive distance `d_p` moving a reference plane inward along the port orientation, a propagating mode satisfies

\[
a'_p=a_p e^{-j\beta_p d_p},\qquad
b'_p=b_p e^{+j\beta_p d_p},\qquad
S'_{pq}=S_{pq}e^{+j(\beta_p d_p+\beta_q d_q)}.
\]

Signed shifts are allowed in the mathematical harness so the transformation can be inverted. ASIR de-embedding requests remain nonnegative. Directivity records radiated power as its denominator, gain records accepted power `aᴴa - bᴴb`, and realized gain records total incident power `aᴴa`.

The golden harness owns the independent four-port analytic fixture. It verifies RMS voltage/current round trips and power identity, matched inactive ports, complex-drive scattering and far-field reconstruction, passivity, reciprocity, modal phase anchoring, and reversible reference-plane shifts.

## Alternatives

- Use peak phasors. This inserts factors of two into power calculations and conflicts with the declared result convention.
- Use voltage waves. Their squared magnitude is not directly modal power when port impedances differ.
- Leave modal phase to the eigenmode solver. Repeated calibration runs could then rotate basis columns and corrupt reconstruction.
- Accept complex or non-positive reference impedance in V1. Generalized complex power waves require different power and conjugation rules and therefore a new contract version.
- Store simultaneous drive results without an independent port basis. This prevents cheap linear reconstruction and makes matched inactive-port behavior harder to audit.

## Consequences

- Solver calibration and normalized result writers must identify this contract version and preserve its orientation, phase-anchor, impedance, and reference-plane metadata.
- Frequencies at cutoff, evanescent modes, non-positive-real impedance, weak phase anchors, and ambiguous degenerate bases cannot enter a V1 response basis.
- Lossless reference-plane shifts preserve passivity and reciprocity while changing S-parameter phase.
- Future complex-power-wave or alternate-phasor conventions require an explicit version and conversion boundary.
