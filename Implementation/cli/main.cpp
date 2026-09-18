#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "Network.h"
#include "OPFSolver.h"
#include "DispatchSolver.h"
#include "TestNetwork.h"
#include "Sweep.h"

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(const std::string& what, double got, double want, double tol = 1e-3)
{
    ++g_checks;
    bool ok = std::fabs(got - want) <= tol;
    if (!ok) ++g_failures;
    std::cout << (ok ? "  ok   " : "  FAIL ")
              << std::left << std::setw(34) << what << std::right
              << std::fixed << std::setprecision(4) << std::setw(14) << got;
    if (!ok) std::cout << "   want " << std::setw(14) << want;
    std::cout << "\n";
}

void checkTrue(const std::string& what, bool cond)
{
    ++g_checks;
    if (!cond) ++g_failures;
    std::cout << (cond ? "  ok   " : "  FAIL ") << what << "\n";
}

void printResult(const core::OPFResult& r)
{
    std::cout << std::fixed << std::setprecision(4);
    for (size_t g = 0; g < r.P.size(); ++g)
        std::cout << "    P[" << g << "] = " << std::setw(12) << r.P[g] << " MW\n";
    for (size_t i = 0; i < r.theta.size(); ++i)
        std::cout << "    theta[" << i << "] = " << std::setw(10) << r.theta[i]
                  << " rad    lambda[" << i << "] = " << std::setw(10) << r.lambda[i] << "\n";
    for (size_t l = 0; l < r.flow.size(); ++l)
    {
        std::cout << "    flow[" << l << "] = " << std::setw(12) << r.flow[l] << " MW";
        if (r.mu[l] != 0.0) std::cout << "    mu = " << r.mu[l];
        std::cout << "\n";
    }
    if (r.totalLoss > 0.0)
        std::cout << "    total losses = " << r.totalLoss << " MW ("
                  << r.lossIters << " loss iterations)\n";
    if (r.windUsed > 0.0)
        std::cout << "    wind injected = " << r.windUsed << " MW\n";
    std::cout << "    total cost = " << r.totalCost << "\n";
    std::cout << "    residual   = " << std::scientific << r.residual
              << std::fixed << "\n";
}

}

int main()
{
    core::OPFSolver opf;
    core::DispatchSolver ed;

    std::cout << "\n============================================================\n";
    std::cout << "  PART A -- VALIDATION SUITE\n";
    std::cout << "============================================================\n";

    std::cout << "\n[A1] Economic dispatch, 2 generators, D = 500 MW\n";
    {
        auto gens = core::build2BusGenerators();
        auto r = ed.solve(gens, 500.0);
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "    P = [" << r.P[0] << ", " << r.P[1] << "]  lambda = "
                  << r.lambda << "  cost = " << r.totalCost << "\n";
        checkTrue("feasible", r.feasible);
        check("P[0]", r.P[0], 217.6471);
        check("P[1]", r.P[1], 282.3529);
        check("lambda", r.lambda, 11.4824);
        check("total cost", r.totalCost, 4644.7059, 1e-2);
    }

    std::cout << "\n[A2] 2-bus DC-OPF, line unconstrained (must match A1)\n";
    {
        auto net = core::build2BusNetwork(1e9);
        auto gens = core::build2BusGenerators();
        auto r = opf.solve(net, gens);
        printResult(r);
        checkTrue("feasible", r.feasible);
        check("P[0]", r.P[0], 217.6471);
        check("P[1]", r.P[1], 282.3529);
        check("lambda[0]", r.lambda[0], 11.4824);
        check("lambda[1] (uniform)", r.lambda[1], 11.4824);
        check("theta[1]", r.theta[1], -0.2176);
        check("total cost", r.totalCost, 4644.7059, 1e-2);
        checkTrue("residual below 1e-6", r.residual < 1e-6);
    }

    std::cout << "\n[A3] 2-bus DC-OPF, line capped at 100 MW (hand-derived)\n";
    {
        auto net = core::build2BusNetwork(100.0);
        auto gens = core::build2BusGenerators();
        auto r = opf.solve(net, gens);
        printResult(r);
        checkTrue("feasible", r.feasible);
        check("P[0] (import limited)", r.P[0], 100.0);
        check("P[1] (covers rest)", r.P[1], 400.0);
        check("lambda[0] cheap bus", r.lambda[0], 9.6);
        check("lambda[1] expensive bus", r.lambda[1], 13.6);
        check("flow pinned at limit", r.flow[0], 100.0);
        check("mu[0] congestion price", r.mu[0], 4.0);
        check("total cost", r.totalCost, 4880.0, 1e-2);
        checkTrue("prices separated by congestion",
                  std::fabs(r.lambda[1] - r.lambda[0]) > 1.0);
    }

    std::cout << "\n[A4] 5-bus DC-OPF, no binding limits (must match ED)\n";
    {
        auto net = core::buildTestNetwork(0.0, 0.0, 1e7);
        auto gens = core::buildTestGenerators();
        auto r = opf.solve(net, gens);
        printResult(r);

        auto edr = ed.solve(gens, 500.0);
        std::cout << "    economic dispatch cross-check: P = ["
                  << edr.P[0] << ", " << edr.P[1] << ", " << edr.P[2]
                  << "]  lambda = " << edr.lambda << "\n";

        checkTrue("feasible", r.feasible);
        check("P[0] == ED", r.P[0], edr.P[0]);
        check("P[1] == ED", r.P[1], edr.P[1]);
        check("P[2] == ED", r.P[2], edr.P[2]);
        check("lambda == ED lambda", r.lambda[0], edr.lambda);
        check("lambda uniform across buses", r.lambda[4], r.lambda[0]);
        check("cost == ED cost", r.totalCost, edr.totalCost, 1e-2);
        check("generation meets demand", r.P[0] + r.P[1] + r.P[2], 500.0, 1e-2);
        checkTrue("residual below 1e-6", r.residual < 1e-6);
    }

    std::cout << "\n[A5] 5-bus DC-OPF, real limits (line 4 congests)\n";
    {
        auto net = core::buildTestNetwork();
        auto gens = core::buildTestGenerators();

        auto relaxed = core::buildTestNetwork(0.0, 0.0, 1e7);
        auto r0 = opf.solve(relaxed, gens);
        std::cout << "    unconstrained flow on line 4 = " << r0.flow[4]
                  << " MW  (limit 180)\n";
        checkTrue("line 4 overloads without limits", r0.flow[4] > 180.0);

        std::vector<bool> noBounds(gens.size(), false);
        std::vector<int> act = { 0, 0, 0, 0, +1, 0 };
        auto r = opf.solveManual(net, gens, noBounds, noBounds, act);
        printResult(r);
        checkTrue("feasible", r.feasible);
        check("flow[4] pinned at limit", r.flow[4], 180.0);
        checkTrue("mu[4] positive (congestion has a price)", r.mu[4] > 0.0);
        checkTrue("prices separate across buses",
                  std::fabs(r.lambda[3] - r.lambda[2]) > 1.0);
        checkTrue("residual below 1e-6", r.residual < 1e-6);
        std::cout << "    note: P[2] = " << r.P[2] << " MW is outside its bounds, "
                     "see A6 and report\n";
    }

    std::cout << "\n[A6] 5-bus DC-OPF, generator bound binds (G2 Pmax = 100)\n";
    {
        auto net = core::buildTestNetwork(0.0, 0.0, 1e7);
        auto gens = core::buildTestGenerators();
        gens[1].Pmax = 100.0;
        auto r = opf.solve(net, gens, 0.0, true);
        printResult(r);
        checkTrue("feasible", r.feasible);
        check("P[1] pinned at its Pmax", r.P[1], 100.0);
        check("generation meets demand", r.P[0] + r.P[1] + r.P[2], 500.0, 1e-2);
        checkTrue("P[0] within bounds",
                  r.P[0] >= gens[0].Pmin - 1e-6 && r.P[0] <= gens[0].Pmax + 1e-6);
        checkTrue("P[2] within bounds",
                  r.P[2] >= gens[2].Pmin - 1e-6 && r.P[2] <= gens[2].Pmax + 1e-6);
        checkTrue("G2 recorded as at-max", r.genAtMax.size() > 1 && r.genAtMax[1]);
    }

    std::cout << "\n[A7] Wind injection displaces thermal generation\n";
    {
        auto net = core::buildTestNetwork(0.0, 150.0, 1e7);
        auto gens = core::buildTestGenerators();

        auto r0   = opf.solve(net, gens, 0.0);
        auto r50  = opf.solve(net, gens, 0.5);
        auto r100 = opf.solve(net, gens, 1.0);

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "    wind   0%: thermal = " << r0.P[0]+r0.P[1]+r0.P[2]
                  << " MW  lambda = " << r0.lambda[0] << "  cost = " << r0.totalCost << "\n";
        std::cout << "    wind  50%: thermal = " << r50.P[0]+r50.P[1]+r50.P[2]
                  << " MW  lambda = " << r50.lambda[0] << "  cost = " << r50.totalCost << "\n";
        std::cout << "    wind 100%: thermal = " << r100.P[0]+r100.P[1]+r100.P[2]
                  << " MW  lambda = " << r100.lambda[0] << "  cost = " << r100.totalCost << "\n";

        checkTrue("all feasible", r0.feasible && r50.feasible && r100.feasible);
        check("wind used at 50%", r50.windUsed, 75.0);
        check("wind used at 100%", r100.windUsed, 150.0);
        check("thermal at 50% = D - wind", r50.P[0]+r50.P[1]+r50.P[2], 425.0, 1e-2);
        check("thermal at 100% = D - wind", r100.P[0]+r100.P[1]+r100.P[2], 350.0, 1e-2);
        checkTrue("cost falls as wind rises",
                  r100.totalCost < r50.totalCost && r50.totalCost < r0.totalCost);
        checkTrue("lambda falls as wind rises",
                  r100.lambda[0] < r50.lambda[0] && r50.lambda[0] < r0.lambda[0]);
    }

    std::cout << "\n[A8] DC-OPF with quadratic branch losses (r = 10% of x)\n";
    {
        auto gens = core::buildTestGenerators();
        auto lossless = core::buildTestNetwork(0.00, 0.0, 1e7);
        auto lossy    = core::buildTestNetwork(0.10, 0.0, 1e7);

        auto rNo   = opf.solveWithLosses(lossless, gens, 0.0, 40, 1e-4, false);
        auto rLoss = opf.solveWithLosses(lossy,    gens, 0.0, 40, 1e-4, true);
        printResult(rLoss);

        double genLoss = rLoss.P[0] + rLoss.P[1] + rLoss.P[2];
        checkTrue("both feasible", rNo.feasible && rLoss.feasible);
        check("lossless case has zero losses", rNo.totalLoss, 0.0);
        checkTrue("lossy case has positive losses", rLoss.totalLoss > 0.1);
        check("generation = demand + losses", genLoss, 500.0 + rLoss.totalLoss, 1e-2);
        checkTrue("losses raise total cost", rLoss.totalCost > rNo.totalCost);
        checkTrue("loss iteration converged", rLoss.lossIters < 40);
    }

    std::cout << "\n------------------------------------------------------------\n";
    std::cout << "  VALIDATION: " << g_checks << " checks, "
              << g_failures << " failures\n";
    std::cout << "------------------------------------------------------------\n";

    std::cout << "\n============================================================\n";
    std::cout << "  PART B -- PARAMETER SWEEPS (figure data)\n";
    std::cout << "============================================================\n";

    core::Sweep sweep;
    auto gens = core::buildTestGenerators();

    {
        bool ok = core::Sweep::writeCostCurvesCsv("fig1_cost_curves.csv", gens, 41);
        std::cout << "\n[B1] fig1_cost_curves.csv  " << (ok ? "written" : "FAILED") << "\n";
    }

    {
        auto net = core::buildTestNetwork(0.0, 0.0, 1e7);
        auto r = opf.solve(net, gens);
        bool ok = core::Sweep::writeDispatchCsv("fig2_dispatch.csv", r, gens);
        std::cout << "\n[B2] fig2_dispatch.csv  " << (ok ? "written" : "FAILED") << "\n";
        std::cout << "     P = [" << r.P[0] << ", " << r.P[1] << ", " << r.P[2]
                  << "]  at lambda = " << r.lambda[0] << "\n";
    }

    {
        auto net = core::buildTestNetwork(0.0, 0.0, 1e7);
        auto pts = sweep.demandSweep(net, gens, 0.4, 1.4, 21, false);
        bool ok = core::Sweep::writeDemandSweepCsv("fig3_lambda_vs_demand.csv", pts);
        std::cout << "\n[B3] fig3_lambda_vs_demand.csv  " << (ok ? "written" : "FAILED") << "\n";
        int nFeas = 0;
        for (const auto& p : pts) if (p.feasible) ++nFeas;
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "     " << nFeas << "/" << pts.size() << " points feasible;  lambda "
                  << pts.front().systemLambda << " -> " << pts.back().systemLambda << "\n";
    }

    {
        auto net = core::buildTestNetwork(0.0, 150.0, 1e7);
        auto pts = sweep.windSweep(net, gens, 21, false);
        bool ok = core::Sweep::writeWindSweepCsv("fig4_wind_sweep.csv", pts);
        std::cout << "\n[B4] fig4_wind_sweep.csv  " << (ok ? "written" : "FAILED") << "\n";
        std::cout << "     cost " << pts.front().totalCost << " -> " << pts.back().totalCost
                  << ",  lambda " << pts.front().systemLambda << " -> "
                  << pts.back().systemLambda << "\n";
    }

    {
        auto net = core::buildTestNetwork(0.10, 150.0, 1e7);
        auto pts = sweep.windSweep(net, gens, 11, true);
        bool ok = core::Sweep::writeWindSweepCsv("fig5_wind_with_losses.csv", pts);
        std::cout << "\n[B5] fig5_wind_with_losses.csv  " << (ok ? "written" : "FAILED") << "\n";
        std::cout << "     losses " << pts.front().totalLoss << " -> "
                  << pts.back().totalLoss << " MW across the wind range\n";
    }

    std::cout << "\n============================================================\n";
    std::cout << "  DONE.  " << g_failures << " validation failure(s).\n";
    std::cout << "============================================================\n\n";

    return g_failures == 0 ? 0 : 1;
}
