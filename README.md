# DSAI-NO-Project — Economic Dispatch and DC-OPF with Wind Integration

Course project for **Numerical Optimization** (Topic 11).

**Author:** Faris Muratović (19958)
**Instructor:** Prof. Dr. Izudin Džafić, Faculty of Electrical Engineering, University of Sarajevo

An active-set KKT solver for economic dispatch and DC optimal power flow with
generator bounds, line limits, wind injection and a quadratic loss model,
wrapped in a four-tab desktop application built with the natID framework.

## Main features

- economic dispatch and DC-OPF formulated directly from the KKT conditions;
- active-set handling of generator bounds and line-flow limits;
- wind injection at a chosen bus as a zero-marginal-cost source;
- transmission losses by successive linearisation;
- indefinite KKT system solved with natID's pivoting sparse LU, every solve residual-checked;
- 56-check validation suite (CLI) with analytically verified two-bus cases;
- GUI: Overview dashboard with scenario presets and KPI tiles, Parameters panel,
  annotated five-bus network schematic, four analytical charts, PDF export;
- builds on Windows, macOS and Linux from one CMake project.

## Repository structure

```
DSAI-NO-Project/
├── Docs/
│   ├── Images/
│   ├── EconomicDispatch_Report.tex
│   ├── EconomicDispatch_Report.pdf
│   └── IEEEtran.cls
└── Implementation/
    ├── CMakeLists.txt
    ├── dispatch.cmake
    ├── README.md
    ├── core/        solver library (Network, OPFSolver, Sweep, TestNetwork)
    ├── cli/         validation suite + CSV export
    ├── gui/         main.cpp — natGUI application
    ├── verify/      independent numerical checks
    ├── res/         DevRes.xml, application icon
    └── make_figures.py
```

Build instructions and implementation details are in [Implementation/README.md](Implementation/README.md).
