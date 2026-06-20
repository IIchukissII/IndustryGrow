<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow — Research Trajectory

- **Status:** Living document (forward research view; non-normative)
- **Date:** 2026-06-20
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0014, ADR-0015, ADR-0016; `MOTIVATION.md`; `project/ROADMAP.md`

## What this document is

`MOTIVATION.md` states *why* IndustryGrow exists — the gap it is built to close.
This document is the *how*: the directions of research that gap opens, how those
directions depend on one another, and what each returns — first as a result on
the testbed (the strawberry cabinet, ADR-0001 decision 6), then as a feature of
an eventual commercial deployment.

It is non-normative. The ADRs remain the single source of truth for decisions and
rationale (ADR-0000); this document only **arranges** the research lines those
decisions make possible. Where a line rests on a decision, the governing ADR is
named. The architecture itself — the survey/identification lifecycle, reduced-order
state-space modeling, the profile as single mutation channel — is settled in
ADR-0016, ADR-0015, and ADR-0014. The value here is in the **interlock** between
the lines, not in any single one.

## The research lines

| Line | Title | Extends |
|---|---|---|
| **L1** | Survey & identification design — how to excite the system and place sensors so a valid model falls out | ADR-0016 (survey + identification phases) |
| **L2** | Field estimation — reconstruct the full climate field and its gradients from sparse sensing plus boundary/forcing terms (reduced-order model + observer) | ADR-0016 (state estimator, decisions 4–5) |
| **L3** | Coupling identification & model-based control — discover the MIMO coupling structure; design decoupling/feedforward that beats independent PID | ADR-0015, ADR-0016 (biological-subspace coupling) |
| **L4** | Residual-based fault detection — use the observer innovation as a fault- and model-aging signal; trigger re-identification | ADR-0016 (decision 7) |
| **L5** | Crop-response identification — identify models of plant response; use the staggered cultivation cadence as parallel experimental design | ADR-0001 (staggered cabinet), ADR-0003 |
| **L6** | Two-level integration — drive the biological objective through the profile onto the climate system; map where timescale separation holds | ADR-0016 (biological / apparatus partition) |
| **L7** | Scale & transfer — whether a model identified at one scale or deployment transfers to another (cabinet → greenhouse, deployment → deployment) | ADR-0001 (one architecture, all scales) |

## How the lines depend on one another

**L1 is the foundation.** No valid model exists without a well-posed survey; a
mis-posed survey poisons everything downstream. The first deliverable of the
research program is a well-designed survey, not a model.

**L2 and L3 are two uses of one identified model.** The observer L2 builds is the
state estimate L3's controller acts on. Estimation and control are not separate
programs — they are the read and write sides of the same identified system
(ADR-0016 decisions 4–6).

**L4 falls out of L2 almost for free.** The estimator innovation (measured minus
predicted) is already computed for the observer; monitoring it for sustained
drift gives fault detection and a re-identification trigger at near-zero marginal
cost (ADR-0016 decision 7).

**L6 is the membrane between biology and climate.** It drives the climate system
(L2 + L3) toward biological objectives and consumes residual diagnostics (L4) as
feedback. This is the three-layer partition of ADR-0016 — biological setpoints
over apparatus bounds over hardware survival — read as a control problem.

**L5 rides on top of L6.** Crop-response identification is the slow loop: it
operates through the integration membrane, and its experiments are paced by crop
cycles, not minutes.

**L7 asks whether the whole structure transfers** to other scales and
deployments — the central claim of ADR-0001 (one architecture, all scales),
tested rather than asserted.

The point is the interlock: **one survey (L1) and one identified model feed
estimation, control, fault detection, and biological optimization at once.**

## What each line yields — testbed vs deployment

This mirrors the research / commercial split already drawn in ADR-0016
("defensible IP is calibration and identification, not sensors", decision 14).

| Line | Testbed yield (research result) | Deployment yield (applied feature) |
|---|---|---|
| L1 | A repeatable identification methodology — the defensible method | Self-commissioning: a new unit identifies its own model from a short survey instead of months of manual tuning |
| L2 | Validated sparse field reconstruction; the sensor-economics result | Sensor-lean deployments with software-defined field awareness — lower BOM, virtual sensors (ADR-0016 decision 5) |
| L3 | Quantified gain of model-based decoupling over tuned PID; the coupling map | Tighter, more uniform, more energy-efficient climate control — less actuator fighting |
| L4 | Residual detection benchmarked against fixed thresholds; earlier, more specific faults | Self-monitoring and predictive maintenance — fewer crop losses, less downtime |
| L5 | Identified crop-response models (scientifically valuable where the agronomy is incomplete); cadence-as-DoE validated | Yield/quality optimization; a growing model library as a transferable asset |
| L6 | The integration validated; conditions for clean two-level separation established | Grow-by-objective instead of grow-by-setpoint — true autonomy |
| L7 | Scaling and transfer validity established | One platform, any scale; recipes and models that port across deployments |

The two highest-value lines are **L1+L2** (sensor-as-temporary-scaffold, the
defensible method) and **L5** (cadence-as-experimental-design, the model-library
asset). These are where the platform's commercial moat is actually built.

## Order of attack

1. **L1 first** — it gates everything. A clean survey of a bad design still
   yields a confident *wrong* model, so the survey design is the 90%.
2. **L2 + L3 together** on the first identified model — fast, since
   thermal/airflow experiments run in minutes to hours.
3. **L4** follows almost for free once L2's observer runs.
4. **L6, then L5** — cycle-bound: each crop-response validation is one crop
   cycle, and the staggered cadence is what parallelizes them.
5. **L7** whenever a second scale or deployment exists.

The fast lines (L1–L4) produce results in weeks. The slow lines (L5–L6) are
gated by biology and are the long pole — start them early **because** they are
slow.

## Theoretical foundations

Each line reduces to a well-posed object from system theory. This is the formal
spine of the trajectory; the algorithm choices (which estimator, which
identification method) remain implementation concerns per ADR-0016 decision 18.

**Plant model (shared by L2, L3, L4).** A deployment's climate dynamics are taken
as a finite-dimensional, sparsely-sensed linear time-invariant system (the working
regime; nonlinearity is handled by grey-box or local-linear extensions):

```
ẋ = A x + B u + E w        (state / forcing)
y = C x + D u + v          (sparse measurement)
```

where `x ∈ ℝⁿ` is the (reduced) field state, `u` the actuator vector, `w` the
boundary/forcing terms, `y` the sparse sensor outputs, and `v`, `w` noise.

**L1 — survey & identification design.** Sensor placement and excitation are an
optimal-experiment-design problem: choose inputs `u(t)` and the output map `C` to
maximize information in the Fisher information matrix

```
M(θ) = Σ_k  ψ_k(θ)ᵀ Σ⁻¹ ψ_k(θ),   ψ_k = ∂ŷ_k/∂θ
```

D-optimality maximizes `det M` (parameter-volume), A-optimality minimizes
`tr M⁻¹`. A persistently-exciting input of order `n` is the necessary condition
for identifiability — the formal statement of "a mis-posed survey poisons
everything."

**L2 — field estimation (ROM + observer).** The full PDE field is projected onto a
reduced basis (POD / balanced truncation) to get the tractable `n`-state model
above, then reconstructed from sparse `y` by an observer. The steady-state Kalman
gain solves the algebraic Riccati equation:

```
P = A P Aᵀ + Q − A P Cᵀ (C P Cᵀ + R)⁻¹ C P Aᵀ
L = A P Cᵀ (C P Cᵀ + R)⁻¹
x̂_{k+1} = A x̂_k + B u_k + L (y_k − C x̂_k)
```

Reconstruction is well-posed iff the observability matrix has full rank — the
control-theoretic restatement of ADR-0016's "minimum sensor placement"
(decision 2):

```
O = [ C ; CA ; CA² ; … ; CA^{n−1} ],   rank O = n
```

**L3 — coupling identification & model-based control.** The MIMO coupling
structure is read from the steady-state gain `G(0) = −CA⁻¹B + D`; the Relative
Gain Array

```
RGA = G ∘ (G⁻¹)ᵀ
```

quantifies loop interaction and tells you where independent PID will fight itself.
Model-based decoupling/feedforward (LQR, then MPC for the commercial control module
of ADR-0001) minimizes

```
J = Σ_k ( xₖᵀ Q xₖ + uₖᵀ R uₖ )
```

subject to the plant model and the apparatus-subspace bounds of ADR-0016.

**L4 — residual-based fault detection.** The observer innovation is already
computed:

```
e_k = y_k − C x̂_{k|k−1},   cov(e_k) = S_k = C P Cᵀ + R
```

Under the nominal model `e_k` is zero-mean white with covariance `S_k`. A
normalized squared innovation (chi-square / CUSUM on `e_kᵀ S_k⁻¹ e_k`) crossing
its envelope signals model drift, equipment fault, or stale calibration — the
formal content of ADR-0016 decision 7, at near-zero marginal cost.

**L5 — crop-response identification.** Slow biological response is a grey-box
input–output model identified across the staggered slots. With `m` slots offset by
the cadence `τ`, the slots form a parallel, time-shifted experimental design; the
effective sample rate of the cycle-bound learning loop improves ~`m`× over a
single-slot experiment — the formal basis for "cadence-as-DoE."

**L6 — two-level integration.** Validity of the two-level (biological / climate)
split rests on singular-perturbation theory: with slow states `x_s` and fast
states `x_f`,

```
ẋ_s = f(x_s, x_f, u),   ε ẋ_f = g(x_s, x_f, u),   ε ≪ 1
```

the timescale separation holds while `ε` (ratio of climate to biological time
constants) is small; it degrades exactly where fast biology (minutes) couples into
the climate loop — the contingency noted below.

**L7 — scale & transfer.** Transfer is a model-validity question across domains: a
model identified on deployment A retains predictive power on B only if the dominant
POD modes are preserved. New spatial modes appearing at scale (a partitioned
greenhouse) break the reduced basis — a falsifiable, testable claim, not an
assumption.

## Honest contingencies

- A valid model is not guaranteed from one survey. L1 is the dominant risk; a
  clean survey of a poorly-posed design yields a confident wrong model.
- L7 (transfer) may fail: a cabinet model may not generalize to a partitioned
  greenhouse if new spatial modes appear at scale (consistent with ADR-0016's
  "models are deployment-specific" decision driver).
- L6 separation degrades where fast biological responses (minutes) couple into the
  climate loop; the clean two-level picture holds for slow objectives — yield,
  morphology, quality.
- The components are individually established (system identification, Kalman
  estimation, MIMO control). The contribution is the **integration** and the
  L1/L5 methodology, not novelty in any single line.

## References

**System identification (L1, L3, L5).**
- Ljung, L. *System Identification: Theory for the User*, 2nd ed. — PEM, grey-box
  identification, persistent excitation, identifiability.
- Van Overschee, P. & De Moor, B. *Subspace Identification for Linear Systems* —
  N4SID / MOESP, the subspace methods named in ADR-0016 decision 18.
- Pukelsheim, F. *Optimal Design of Experiments* — D-/A-/E-optimality, Fisher
  information; the formal basis of survey design (L1).

**Estimation & reduced-order modeling (L2, L4).**
- Anderson, B.D.O. & Moore, J.B. *Optimal Filtering* — Kalman filter, Riccati
  equation, innovation process.
- Kailath, T. *Linear Systems* — observability matrix/Gramian, canonical forms.
- Antoulas, A.C. *Approximation of Large-Scale Dynamical Systems* — balanced
  truncation, model-order reduction.
- Berkooz, Holmes & Lumley, "The proper orthogonal decomposition in the analysis
  of turbulent flows," *Annu. Rev. Fluid Mech.* 25 (1993) — POD basis for L2's
  field reduction.
- Basseville, M. & Nikiforov, I. *Detection of Abrupt Changes* — CUSUM,
  chi-square residual tests for L4.

**Control (L3, L6).**
- Skogestad, S. & Postlethwaite, I. *Multivariable Feedback Control* — RGA, MIMO
  interaction analysis, decoupling.
- Rawlings, Mayne & Diehl, *Model Predictive Control: Theory, Computation, and
  Design* — MPC for the commercial control module (ADR-0001 decision 4).
- Kokotović, Khalil & O'Reilly, *Singular Perturbation Methods in Control* — the
  formal two-timescale separation underpinning L6.

**Domain (L5, L6).**
- Vanthoor et al., "A methodology for model-based greenhouse design," *Biosystems
  Engineering* (2011) — state-space approach to greenhouse climate.
- Van Straten et al., *Optimal Control of Greenhouse Cultivation* — biological
  objective driven onto the climate system; the L6 pattern in the literature.

**Architecture context.**
- ADR-0001 — IndustryGrow framing (platform-not-product; one architecture, all scales).
- ADR-0014 — sensor node taxonomy; time-series-over-spatial-redundancy.
- ADR-0015 — gateway control loops; profile as single mutation channel.
- ADR-0016 — empirical survey, state-space modeling, sensor-density lifecycle.
- `MOTIVATION.md` — the *why* this trajectory serves.
- `project/ROADMAP.md` — the build-order companion.

## Provenance

Forward research reading of the IndustryGrow architecture, June 2026. Not yet
implemented; superseded by the relevant ADR if any direction here is later
formalized into a decision.
