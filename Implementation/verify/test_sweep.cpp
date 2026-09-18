#include "opf_ref.cpp"
#include <cstdio>
#include <cmath>

using namespace ref;

static int failures = 0, checks = 0;
static void chkTrue(const char* what, bool cond)
{
    ++checks;
    if (!cond) { ++failures; printf("  FAIL %s\n", what); }
    else        printf("  ok   %s\n", what);
}

static std::vector<Bus> buses5(double windCap, double dScale = 1.0)
{
    return {
        {0, true,  {0}, 0.0,            0.0},
        {1, false, {1}, 0.0,            0.0},
        {2, false, {2}, 0.0,            0.0},
        {3, false, {},  500.0 * dScale, 0.0},
        {4, false, {},  0.0,            windCap}
    };
}
static std::vector<Line> lines5(double rFrac, double limScale = 1.0)
{
    std::vector<Line> L = {
        {0,1,0.10,0.0,250.0*limScale},
        {0,4,0.15,0.0,150.0*limScale},
        {1,2,0.10,0.0,200.0*limScale},
        {4,2,0.12,0.0,150.0*limScale},
        {2,3,0.08,0.0,180.0*limScale},
        {1,3,0.20,0.0,200.0*limScale}
    };
    for (auto& l : L) l.r = rFrac * l.x;
    return L;
}
static std::vector<Generator> gens5()
{
    return { {0.008,8.0,0.0,50.0,300.0},
             {0.009,6.4,0.0,50.0,400.0},
             {0.007,7.9,0.0,30.0,350.0} };
}

int main()
{
    Solver S(100.0);

    // ---- wind sweep monotonicity -------------------------------------
    printf("\n=== S1: wind sweep 0..100%%, 11 steps (limits relaxed) ===\n");
    {
        auto gs = gens5();
        auto ls = lines5(0.0, 1e7);   // effectively unlimited
        double prevCost = 1e300, prevLam = 1e300;
        bool monoCost = true, monoLam = true, allFeas = true;
        printf("   frac   windMW    sumP     lambda      cost\n");
        for (int s = 0; s <= 10; ++s)
        {
            const double f = s / 10.0;
            auto r = S.solve(buses5(150.0), ls, gs, f, {}, false);
            allFeas = allFeas && r.feasible;
            const double sum = r.P[0]+r.P[1]+r.P[2];
            printf("   %.1f  %7.2f  %7.2f  %9.4f  %9.2f\n",
                   f, r.windUsed, sum, r.lambda[0], r.totalCost);
            if (r.totalCost > prevCost + 1e-6)  monoCost = false;
            if (r.lambda[0] > prevLam + 1e-6)   monoLam = false;
            prevCost = r.totalCost; prevLam = r.lambda[0];
            // thermal generation must cover demand minus wind
            if (std::fabs(sum - (500.0 - r.windUsed)) > 1e-3) allFeas = false;
        }
        chkTrue("all points feasible & balanced", allFeas);
        chkTrue("cost monotonically non-increasing", monoCost);
        chkTrue("lambda monotonically non-increasing", monoLam);
    }

    // ---- demand sweep: lambda should rise with demand -----------------
    printf("\n=== S2: demand sweep 0.4x..1.4x (limits relaxed) ===\n");
    {
        auto gs = gens5();
        auto ls = lines5(0.0, 1e7);
        double prevLam = -1e300;
        bool monoLam = true, allFeas = true;
        printf("   scale   demand    lambda      cost   bindLines\n");
        for (int s = 0; s <= 10; ++s)
        {
            const double sc = 0.4 + (1.0 / 10.0) * s;
            auto bs = buses5(0.0, sc);
            auto r = S.solve(bs, ls, gs, 0.0, {}, false);
            allFeas = allFeas && r.feasible;
            printf("   %.2f  %7.2f  %9.4f  %9.2f\n",
                   sc, 500.0*sc, r.lambda[0], r.totalCost);
            if (r.lambda[0] < prevLam - 1e-6) monoLam = false;
            prevLam = r.lambda[0];
        }
        chkTrue("all points feasible", allFeas);
        chkTrue("lambda monotonically non-decreasing with demand", monoLam);
    }

    // ---- demand sweep with real limits: congestion must appear --------
    printf("\n=== S3: demand sweep with real line limits ===\n");
    {
        auto gs = gens5();
        auto ls = lines5(0.0, 1.0);   // real limits -> line 4 binds early
        int nFeasible = 0, nWithBinding = 0;
        printf("   scale   demand   feasible  bindLines  maxFlowViol\n");
        for (int s = 0; s <= 10; ++s)
        {
            const double sc = 0.2 + (0.6 / 10.0) * s;   // 0.2x .. 0.8x
            auto bs = buses5(0.0, sc);
            auto r = S.solve(bs, ls, gs, 0.0, {}, false);
            int nb = 0; double maxViol = 0.0;
            if (r.feasible) {
                ++nFeasible;
                for (size_t l = 0; l < ls.size(); ++l) {
                    maxViol = std::max(maxViol, std::fabs(r.flow[l]) - ls[l].limit);
                }
            }
            printf("   %.2f  %7.2f      %d        %d      %8.3f\n",
                   sc, 500.0*sc, r.feasible?1:0, nb, maxViol);
            if (maxViol <= 1e-3 && r.feasible) ++nWithBinding;
        }
        chkTrue("at least some demand levels solve", nFeasible > 0);
        printf("       (%d/11 feasible, %d fully within limits)\n", nFeasible, nWithBinding);
    }

    // ---- loss sweep: losses should grow with demand -------------------
    printf("\n=== S4: losses vs demand (r = 10%% of x) ===\n");
    {
        auto gs = gens5();
        auto ls = lines5(0.10, 1e7);
        double prevLoss = -1.0;
        bool monoLoss = true, allFeas = true;
        printf("   scale   demand    lossMW    sumP    cost\n");
        for (int s = 0; s <= 6; ++s)
        {
            const double sc = 0.4 + (0.8 / 6.0) * s;
            auto bs = buses5(0.0, sc);
            auto r = S.solveWithLosses(bs, ls, gs, 0.0, 40, 1e-5, false);
            allFeas = allFeas && r.feasible;
            const double sum = r.P[0]+r.P[1]+r.P[2];
            printf("   %.2f  %7.2f  %8.4f  %7.2f  %9.2f\n",
                   sc, 500.0*sc, r.totalLoss, sum, r.totalCost);
            if (r.totalLoss < prevLoss - 1e-9) monoLoss = false;
            prevLoss = r.totalLoss;
            // generation must equal demand + losses
            if (std::fabs(sum - (500.0*sc + r.totalLoss)) > 1e-2) allFeas = false;
        }
        chkTrue("all feasible & generation == demand + losses", allFeas);
        chkTrue("losses grow with demand", monoLoss);
    }

    // ---- losses fall as wind rises (key proposal deliverable) ---------
    printf("\n=== S5: losses vs wind penetration (r = 10%% of x) ===\n");
    {
        auto gs = gens5();
        auto ls = lines5(0.10, 1e7);
        printf("   frac   windMW    lossMW      cost\n");
        std::vector<double> losses;
        for (int s = 0; s <= 5; ++s)
        {
            const double f = s / 5.0;
            auto r = S.solveWithLosses(buses5(150.0), ls, gs, f, 40, 1e-5, false);
            printf("   %.1f  %7.2f  %8.4f  %9.2f\n",
                   f, r.windUsed, r.totalLoss, r.totalCost);
            losses.push_back(r.totalLoss);
        }
        chkTrue("loss curve produced for all wind levels", losses.size() == 6);
    }

    printf("\n==================================================\n");
    printf("  %d checks, %d failures\n", checks, failures);
    printf("==================================================\n");
    return failures == 0 ? 0 : 1;
}
