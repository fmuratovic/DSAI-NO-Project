# Economic Dispatch and DC Optimal Power Flow with Wind Integration

**Numerical Optimization — Topic 11**
Faris Muratović, Index 19958
Faculty of Electrical Engineering, University of Sarajevo
Instructor: Izudin Džafić, prof.

---

## 1. Problem statement

Each thermal generator *i* has a quadratic fuel cost in its active power
output,

  C_i(P_i) = a_i P_i² + b_i P_i + c_i

and the task is to minimise total fuel cost subject to the system meeting
demand and respecting operating limits. Two models are implemented.

**Economic dispatch** (the network-free baseline):

  min Σ C_i(P_i)   s.t.  Σ P_i = D,   P_i,min ≤ P_i ≤ P_i,max

**DC optimal power flow**, which replaces the single system-wide balance
equation with one balance equation per bus and adds transmission limits:

  min Σ C_i(P_i)
  s.t.  Σ_{g at i} P_g − Σ_j B_ij θ_j = D_i        for every bus i
        |B_l (θ_from − θ_to)| ≤ F_l,max             for every line l
        P_i,min ≤ P_i ≤ P_i,max
        θ_slack = 0

Here θ is the vector of bus voltage angles and **B** is the network
susceptance matrix, B_ij = −1/x_ij off-diagonal and B_ii = Σ 1/x over the
lines touching bus i. DC-OPF is the standard linearisation of the AC power
flow equations under the assumptions that voltage magnitudes are near 1 p.u.
and angle differences across lines are small; power flow on a line is then
proportional to the angle difference across it divided by its reactance.

The model is extended in two directions:

* **Wind** is injected at a designated bus as a zero-marginal-cost source
  that reduces the demand the thermal units must cover at that bus.
* **Losses** are modelled per line as g_l (Δθ_l)², with
  g_l = r_l /(r_l² + x_l²), half of each line's loss injected as additional
  demand at each of its end buses.

---

## 2. Method

### 2.1 KKT formulation

Forming the Lagrangian of the DC-OPF problem and differentiating gives three
families of stationarity conditions, which are assembled into a single linear
system per active-set iteration.

For each generator, marginal cost equals the price **at its own bus**:

  2 a_g P_g + b_g = λ_bus(g)

For each non-slack bus, the prices themselves satisfy a network equation:

  (B λ)_k + Σ_{active l} ± B_l μ_l = 0

together with the per-bus power balance equations, and one equality per
binding line forcing its flow to the limit. The unknown vector is

  [ P (free generators) | θ (non-slack buses) | λ (all buses) | μ (binding lines) ]

Counting for N buses and n_g generators: the unknowns number
n_g + (N−1) + N + n_active, and the equations match exactly.

Two points are worth emphasising because they were the source of real
difficulty during implementation:

1. **θ and λ are different objects.** θ is a primal variable appearing in the
   physical flow equations; λ is the dual variable (the locational marginal
   price) arising from the balance constraint. An early version of the solver
   conflated them, which produces a system that looks plausible and is wrong.
2. **The KKT matrix is not symmetric.** The P–λ coupling has coefficient −1 in
   the cost block but +1/S_base in the balance block, because P is expressed
   in MW in the cost equations and in per-unit in the balance equations.
   Declaring the matrix symmetric to the solver and supplying only one
   triangle causes factorisation to fail outright.

### 2.2 Active-set method

Inequality constraints — generator bounds and line limits — are handled by an
active-set scheme. Starting from no active constraints, the equality-constrained
KKT system is solved, the result is checked against every inequality, and the
**single worst violation** is added to the active set before re-solving.

Adding one constraint per iteration matters. An earlier version added every
currently-violated constraint from a single intermediate solve; because those
intermediate iterates are themselves infeasible, this cascades, and in the
5-bus case ended with all six lines pinned simultaneously — more equality
constraints than the network has degrees of freedom, and therefore a singular
system.

Generator bounds and line limits are **not independent**. Pinning a line
changes the whole solution including every generator's output, and pinning a
generator changes every line flow. The loop therefore re-checks both kinds
after every single activation rather than handling them in separate passes.

If an activation makes the system unsolvable, it is reverted and the search
stops, returning the last feasible iterate.

### 2.3 Losses by successive linearisation

Pure DC-OPF is lossless by construction, since losses are a second-order
effect. The loss model is therefore applied as an outer iteration: solve the
lossless problem, compute each line's loss from the resulting angles, reinject
half of each loss as extra demand at each end bus, and repeat until the
injections stop changing. Convergence is fast — five outer iterations to a
tolerance of 10⁻⁴ MW in the test case.

### 2.4 Linear algebra

The KKT system is an indefinite saddle-point system: the λ and μ rows have no
diagonal entries, so the matrix has structural zeros on the diagonal. This
matters in practice.

`dense::Matrix::solve` was used initially. It reports success while returning
a solution with a residual of order 10², i.e. it silently does not solve the
system. The final implementation uses `sparse::createDblSolver` with
`Symmetry::NonSymmetric`, `SolverType::LU` and `Pivoting::DiagonalMultiPass`,
which handles the zero diagonal correctly.

Because a direct solver can fail silently on such a system, **every solve is
followed by an explicit residual check**: A·x is recomputed from the stored
triplets and compared against b, and a residual above 10⁻⁴ is treated as
infeasible rather than trusted. This check is what made the original failure
visible, and it is retained permanently.

---

## 3. Test systems

### 3.1 Two-bus system (analytically verifiable)

Two generators, 500 MW load at bus 1, one line with x = 0.10 p.u.

| Generator | a | b | P_min | P_max |
|---|---|---|---|---|
| G1 (bus 0, slack) | 0.008 | 8.0 | 50 | 300 |
| G2 (bus 1) | 0.009 | 6.4 | 50 | 400 |

### 3.2 Five-bus system

```
  bus0 (slack, G1) --- bus1 (G2)
    |                    |    |
    |                    |    +---- bus3 (load, 500 MW)
    |                    |                  |
  bus4 (wind) -------- bus2 (G3) -----------+
```

| Generator | Bus | a | b | P_min | P_max |
|---|---|---|---|---|---|
| G1 | 0 | 0.008 | 8.0 | 50 | 300 |
| G2 | 1 | 0.009 | 6.4 | 50 | 400 |
| G3 | 2 | 0.007 | 7.9 | 30 | 350 |

| Line | From–To | x (p.u.) | Limit (MW) |
|---|---|---|---|
| L0 | 0–1 | 0.10 | 250 |
| L1 | 0–4 | 0.15 | 150 |
| L2 | 1–2 | 0.10 | 200 |
| L3 | 4–2 | 0.12 | 150 |
| **L4** | **2–3** | **0.08** | **180** |
| L5 | 1–3 | 0.20 | 200 |

S_base = 100 MVA. Installed wind capacity at bus 4 is 150 MW (30 % of demand).
The loop formed by L5 means the flow split is decided by the optimisation
rather than forced by topology. L4 is deliberately tight: the unconstrained
optimum pushes 321.7 MW through it.

---

## 4. Results

All results below are produced by the `cli` target and are reproduced by its
built-in validation suite (**56 checks, 0 failures**). Residuals are reported
for every KKT solve.

### 4.1 Economic dispatch baseline — two generators, D = 500 MW

| Quantity | Value |
|---|---|
| P₁ | 217.6471 MW |
| P₂ | 282.3529 MW |
| λ | 11.4824 |
| Total cost | 4644.7059 |

Verification of the equal-incremental-cost condition:
2(0.008)(217.6471) + 8 = 11.4824 and 2(0.009)(282.3529) + 6.4 = 11.4824. ✓

### 4.2 DC-OPF reduces to economic dispatch when nothing binds

Two-bus network, line unconstrained. Result is **identical** to 4.1:
P = [217.6471, 282.3529], λ = 11.4824 at both buses, θ₁ = −0.2176 rad,
flow = 217.6471 MW, cost 4644.7059, residual 0.

This is the central correctness test: with no binding transmission
constraint, the network model must add nothing, and it does not.

The five-bus case gives the same result: P = [133.2461, 207.3298, 159.4241],
λ = 10.1319 **uniform across all five buses**, cost 4359.1492, residual
5.7 × 10⁻¹⁴ — matching the three-generator economic dispatch solution exactly.
A uniform λ is the correct signature of an unconstrained network.

### 4.3 Congestion separates locational prices

Two-bus network with the line capped at 100 MW, below the 217.6 MW it
naturally carries.

| Quantity | Value | Hand-derived |
|---|---|---|
| P₁ | 100.0000 MW | 100 |
| P₂ | 400.0000 MW | 400 |
| λ bus 0 | 9.6000 | 9.6 |
| λ bus 1 | 13.6000 | 13.6 |
| flow | 100.0000 MW | 100 (at limit) |
| μ (congestion price) | 4.0000 | λ₁ − λ₀ = 4.0 |
| Total cost | 4880.0000 | — |

Residual 7.1 × 10⁻¹⁵. Every value matches the independent hand derivation.
The cheap generator is held back by the line, the expensive one makes up the
difference, cost rises from 4644.71 to 4880.00, and the price splits by
exactly the congestion price. This is the textbook mechanism by which
locational marginal prices arise.

### 4.4 Generator bounds bind correctly

Five-bus network with G2's capacity reduced to 100 MW. The active-set method
detects the 107.33 MW violation, pins G2 at its maximum, and re-solves:

P = [183.3333, 100.0000, 216.6667] MW, λ = 10.9333 uniform, cost 4505.8333,
residual 2.8 × 10⁻¹⁴. Generation still meets demand exactly, and both free
generators remain within their own bounds. The price rises from 10.1319 to
10.9333 because cheap capacity has been withdrawn.

### 4.5 Wind displaces thermal generation

Five-bus network, 150 MW installed wind at bus 4, line limits relaxed to
isolate the wind effect.

| Wind | Wind injected | Thermal total | λ | Total cost |
|---|---|---|---|---|
| 0 % | 0 MW | 500.00 MW | 10.1319 | 4359.15 |
| 50 % | 75 MW | 425.00 MW | 9.7361 | 3614.10 |
| 100 % | 150 MW | 350.00 MW | 9.3403 | 2898.73 |

Thermal output falls exactly one-for-one with wind injection, and both the
marginal price and total cost fall monotonically — wind with zero marginal
cost displaces the most expensive thermal increment first, so the system
marginal price drops. Over a 21-point sweep the cost falls from 4359.15 to
2898.73, a 33.5 % reduction at 30 % penetration.

### 4.6 Transmission losses

Five-bus network with r = 10 % of x on every line.

The outer loss iteration converges in five passes:

| Iteration | Total loss (MW) | Change |
|---|---|---|
| 0 | 17.0179 | 7.2467 |
| 1 | 17.5767 | 0.2121 |
| 2 | 17.5928 | 0.0062 |
| 3 | 17.5933 | 0.000183 |
| 4 | 17.5933 | 5.4 × 10⁻⁶ |

At convergence: P = [139.0491, 212.4881, 166.0561] MW, total generation
517.5933 MW = 500 MW demand + 17.5933 MW losses (exactly), cost 4538.22
versus 4359.15 lossless — losses add 4.1 % to the fuel bill. λ rises from
10.1319 to 10.2248, correctly reflecting that serving one more MW of load now
also requires covering its marginal losses.

### 4.7 Losses versus wind penetration — an unexpected result

| Wind penetration | Losses (MW) | Total cost |
|---|---|---|
| 0 % | 17.5933 | 4538.22 |
| 6 % | 17.5998 | 4233.92 |
| 12 % | 17.7310 | 3935.60 |
| 18 % | 17.9862 | 3643.19 |
| 24 % | 18.3646 | 3356.64 |
| 30 % | 18.8654 | 3075.88 |

Losses **increase** slightly, by 7 % across the full wind range, rather than
decreasing as one might expect from reducing thermal output. The reason is
locational: the wind bus (bus 4) is electrically distant from the load at
bus 3, reachable only via L3 and L4. Displacing nearby thermal generation
with distant wind lengthens the average path power travels, and the
quadratic loss term grows faster with flow than the reduction in thermal
output reduces it.

This is a genuine and useful finding: the economic benefit of the wind
resource (cost down 32 %) is unambiguous, but its effect on network losses
depends on **where** it is connected, not just how much it produces. It would
be a mistake to assume renewable penetration always reduces losses, and this
result makes the case for treating siting as part of the optimisation.

---

## 5. Figures

Generated from the CSV output by `make_figures.py`.

1. **fig1_cost_curves** — cost and incremental cost per generator, with the
   operating λ drawn as a horizontal line. The three incremental-cost curves
   cross the λ line at exactly the dispatched outputs (133.2, 207.3, 159.4 MW),
   which is the equal-incremental-cost condition made visible.
2. **fig2_dispatch** — dispatch bar chart against each generator's capacity.
3. **fig3_lambda_vs_demand** — λ against total demand over a 0.4×–1.4× sweep
   (λ from 8.4250 to 11.1874), plus the dispatch of each generator. The
   piecewise character where a generator hits a bound is visible.
4. **fig4_wind_sweep** — total cost and λ against wind penetration.
5. **fig5_wind_with_losses** — losses and cost against wind penetration, with
   the loss model active.

---

## 6. Limitations and future work

These are stated plainly because they bound what the present implementation
can be claimed to do.

**Simultaneous multi-constraint active sets.** The method activates one
constraint at a time and reverts any activation that makes the system
unsolvable. It does **not** implement constraint-dropping: a proper
active-set method also removes a constraint whose multiplier has the wrong
sign, and detects linear dependence among candidate constraints so it can try
a different one instead of stopping. In the five-bus case, pinning L4 alone
produces a mathematically exact solution (residual 2.3 × 10⁻¹³) in which
G3 is dispatched to −547 MW — a valid solution of the stated equalities but
not a physical one, since nothing in that constraint set forbids negative
generation. Reaching a physical solution requires a generator bound to bind
*simultaneously*, and the combination of L4, L2 and two generator bounds
turns out to be over-determined for this topology. The correct response is
constraint-dependency detection, which is the natural next step.

**Degenerate (purely linear) costs.** The formulation assumes a > 0 for every
generator. With linear costs (a = 0) and two generators at the same bus, the
stationarity conditions demand that the bus price equal two different
constants simultaneously, which has no solution — such problems are linear
programs with corner solutions and need a simplex or interior-point method
rather than this smooth KKT approach. This was confirmed by attempting the
standard PJM 5-bus benchmark, whose published data uses linear costs and two
generators at bus 1.

**DC approximation.** Voltage magnitudes, reactive power and voltage
stability are outside the model by construction. Angle differences in the
test cases reach 0.43 rad (about 25°), which is at the edge of where the
small-angle assumption is comfortable; a full AC-OPF would be needed for
voltage-related studies.

**Loss model.** Losses are approximated as g(Δθ)² per line and split evenly
between end buses. This is the standard first-order treatment but is not
exact, and the loss term is not included in the optimisation's own
stationarity conditions — it enters only through the outer iteration, so the
result is a fixed point rather than a true optimum of the lossy problem.

**GUI.** The solver library and console driver are compiled and verified. The
natGUI front end is written against the SDK headers but has not been built
against the framework; it is staged so that each layer can be validated
independently. See `README.md`.

---

## 7. Conclusion

The project implements economic dispatch and DC-OPF from first principles via
the KKT conditions, with an active-set treatment of generator bounds and line
limits, extended with wind injection and a quadratic loss model. Correctness
is established by a 56-check validation suite: the DC-OPF solution reduces
exactly to economic dispatch when no transmission constraint binds, the
congested two-bus case reproduces an independent hand derivation to every
printed digit, and every linear solve is residual-checked.

Two findings are worth carrying forward. First, the indefinite saddle-point
structure of the KKT system genuinely requires a pivoting solver; a
non-pivoting direct solve fails **silently**, returning plausible-looking
numbers with a residual of order 10², which is why the residual check is now
a permanent part of the solver rather than a debugging aid. Second, wind
integration reduced fuel cost by a third but slightly *increased* network
losses, because the wind resource sits far from the load — a result that
argues for treating renewable siting, not just capacity, as an optimisation
variable.
