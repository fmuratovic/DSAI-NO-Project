#include "opf_ref.cpp"
#include <cstdio>
#include <cmath>

using namespace ref;

static int failures = 0;
static int checks = 0;

static void chk(const char* what, double got, double want, double tol = 1e-3)
{
    ++checks;
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) { ++failures; printf("  FAIL %-34s got %14.6f  want %14.6f\n", what, got, want); }
    else       printf("  ok   %-34s %14.6f\n", what, got);
}

static void chkTrue(const char* what, bool cond)
{
    ++checks;
    if (!cond) { ++failures; printf("  FAIL %s\n", what); }
    else        printf("  ok   %s\n", what);
}

// ---- the validated 5-bus network from the session -----------------------
static std::vector<Bus> net5Buses(double windCapAtBus4 = 0.0)
{
    return {
        {0, true,  {0}, 0.0,   0.0},
        {1, false, {1}, 0.0,   0.0},
        {2, false, {2}, 0.0,   0.0},
        {3, false, {},  500.0, 0.0},
        {4, false, {},  0.0,   windCapAtBus4}
    };
}
static std::vector<Line> net5Lines(double rFrac = 0.0)
{
    // rFrac: resistance as a fraction of reactance (0 => lossless)
    std::vector<Line> L = {
        {0, 1, 0.10, 0.0, 250.0},
        {0, 4, 0.15, 0.0, 150.0},
        {1, 2, 0.10, 0.0, 200.0},
        {4, 2, 0.12, 0.0, 150.0},
        {2, 3, 0.08, 0.0, 180.0},
        {1, 3, 0.20, 0.0, 200.0}
    };
    for (auto& l : L) l.r = rFrac * l.x;
    return L;
}
static std::vector<Generator> net5Gens()
{
    return {
        {0.008, 8.0, 0.0, 50.0, 300.0},
        {0.009, 6.4, 0.0, 50.0, 400.0},
        {0.007, 7.9, 0.0, 30.0, 350.0}
    };
}

int main()
{
    Solver S(100.0);

    // ================= TEST 1: 2-bus unconstrained =======================
    printf("\n=== T1: 2-bus DC-OPF unconstrained (expect ED match) ===\n");
    {
        std::vector<Bus>  bs = {{0,true,{0},0.0,0.0},{1,false,{1},500.0,0.0}};
        std::vector<Line> ls = {{0,1,0.10,0.0,1e9}};
        std::vector<Generator> gs = {{0.008,8.0,0.0,50.0,300.0},{0.009,6.4,0.0,50.0,400.0}};
        auto r = S.solve(bs, ls, gs);
        chkTrue("feasible", r.feasible);
        chk("P[0]", r.P[0], 217.6471);
        chk("P[1]", r.P[1], 282.3529);
        chk("lambda[0]", r.lambda[0], 11.4824);
        chk("lambda[1]", r.lambda[1], 11.4824);
        chk("theta[1]", r.theta[1], -0.2176);
        chk("flow[0]", r.flow[0], 217.6471);
        chk("totalCost", r.totalCost, 4644.7059, 1e-2);
        chkTrue("residual < 1e-9", r.residual < 1e-9);
    }

    // ================= TEST 2: 2-bus line capped at 100 ==================
    printf("\n=== T2: 2-bus, line capped 100 MW (hand-derived) ===\n");
    {
        std::vector<Bus>  bs = {{0,true,{0},0.0,0.0},{1,false,{1},500.0,0.0}};
        std::vector<Line> ls = {{0,1,0.10,0.0,100.0}};
        std::vector<Generator> gs = {{0.008,8.0,0.0,50.0,300.0},{0.009,6.4,0.0,50.0,400.0}};
        auto r = S.solve(bs, ls, gs);
        chkTrue("feasible", r.feasible);
        chk("P[0]", r.P[0], 100.0);
        chk("P[1]", r.P[1], 400.0);
        chk("lambda[0]", r.lambda[0], 9.6);
        chk("lambda[1]", r.lambda[1], 13.6);
        chk("flow[0]", r.flow[0], 100.0);
        chk("mu[0]", r.mu[0], 4.0);
        chk("totalCost", r.totalCost, 4880.0, 1e-2);
    }

    // ================= TEST 3: 5-bus unconstrained =======================
    printf("\n=== T3: 5-bus DC-OPF unconstrained (expect ED match) ===\n");
    {
        auto bs = net5Buses(); auto ls = net5Lines(); auto gs = net5Gens();
        std::vector<bool> none(gs.size(), false);
        std::vector<int>  noact(ls.size(), 0);
        auto r = S.solveFixed(bs, ls, gs, none, none, noact, 0.0, {});
        chkTrue("feasible", r.feasible);
        chk("P[0]", r.P[0], 133.2461);
        chk("P[1]", r.P[1], 207.3298);
        chk("P[2]", r.P[2], 159.4241);
        chk("sum P", r.P[0]+r.P[1]+r.P[2], 500.0, 1e-2);
        chk("lambda[0]", r.lambda[0], 10.1319);
        chk("lambda[4]", r.lambda[4], 10.1319);
        chk("flow[4] (overloads)", r.flow[4], 321.6557, 1e-2);
        chk("totalCost", r.totalCost, 4359.1492, 1e-2);
    }

    // ================= TEST 4: 5-bus, line 4 pinned ======================
    printf("\n=== T4: 5-bus, line 4 pinned at 180 MW ===\n");
    {
        auto bs = net5Buses(); auto ls = net5Lines(); auto gs = net5Gens();
        std::vector<bool> none(gs.size(), false);
        std::vector<int>  act = {0,0,0,0,+1,0};
        auto r = S.solveFixed(bs, ls, gs, none, none, act, 0.0, {});
        chkTrue("feasible", r.feasible);
        chk("flow[4] == limit", r.flow[4], 180.0);
        chk("mu[4]", r.mu[4], 84.4561, 1e-2);
        chk("P[0]", r.P[0], 360.1052, 1e-2);
        chk("P[1]", r.P[1], 687.2746, 1e-2);
        chk("P[2]", r.P[2], -547.3798, 1e-2);
        chkTrue("lambda separated (not uniform)",
                std::fabs(r.lambda[0] - r.lambda[3]) > 1.0);
    }

    // ================= TEST 5: wind displaces generation =================
    printf("\n=== T5: wind injection at bus 4 (unconstrained network) ===\n");
    {
        auto gs = net5Gens();
        auto ls = net5Lines();
        for (auto& l : ls) l.limit = 1e9;      // remove limits to isolate wind effect
        std::vector<bool> none(gs.size(), false);
        std::vector<int>  noact(ls.size(), 0);

        auto r0  = S.solveFixed(net5Buses(150.0), ls, gs, none, none, noact, 0.0, {});
        auto r50 = S.solveFixed(net5Buses(150.0), ls, gs, none, none, noact, 0.5, {});
        auto r100= S.solveFixed(net5Buses(150.0), ls, gs, none, none, noact, 1.0, {});

        chkTrue("all feasible", r0.feasible && r50.feasible && r100.feasible);
        chk("wind used @0%",   r0.windUsed,   0.0);
        chk("wind used @50%",  r50.windUsed,  75.0);
        chk("wind used @100%", r100.windUsed, 150.0);

        const double sum0   = r0.P[0]+r0.P[1]+r0.P[2];
        const double sum50  = r50.P[0]+r50.P[1]+r50.P[2];
        const double sum100 = r100.P[0]+r100.P[1]+r100.P[2];
        chk("thermal sum @0%   == D",        sum0,   500.0, 1e-2);
        chk("thermal sum @50%  == D-75",     sum50,  425.0, 1e-2);
        chk("thermal sum @100% == D-150",    sum100, 350.0, 1e-2);

        chkTrue("cost falls as wind rises",
                r100.totalCost < r50.totalCost && r50.totalCost < r0.totalCost);
        chkTrue("lambda falls as wind rises",
                r100.lambda[0] < r50.lambda[0] && r50.lambda[0] < r0.lambda[0]);
        printf("       cost:   %.2f -> %.2f -> %.2f\n",
               r0.totalCost, r50.totalCost, r100.totalCost);
        printf("       lambda: %.4f -> %.4f -> %.4f\n",
               r0.lambda[0], r50.lambda[0], r100.lambda[0]);
    }

    // ================= TEST 6: losses ====================================
    printf("\n=== T6: DC-OPF with quadratic branch losses ===\n");
    {
        auto bs = net5Buses();
        auto gs = net5Gens();
        auto lsLossless = net5Lines(0.0);
        auto lsLossy     = net5Lines(0.10);   // r = 10% of x
        for (auto& l : lsLossless) l.limit = 1e9;
        for (auto& l : lsLossy)     l.limit = 1e9;

        auto rNo   = S.solveWithLosses(bs, lsLossless, gs, 0.0, 40, 1e-4, true);
        auto rLoss = S.solveWithLosses(bs, lsLossy,     gs, 0.0, 40, 1e-4, true);

        chkTrue("both feasible", rNo.feasible && rLoss.feasible);
        chk("lossless total loss", rNo.totalLoss, 0.0);
        chkTrue("lossy has positive loss", rLoss.totalLoss > 0.1);

        const double genNo   = rNo.P[0]+rNo.P[1]+rNo.P[2];
        const double genLoss = rLoss.P[0]+rLoss.P[1]+rLoss.P[2];
        chk("lossless gen == D", genNo, 500.0, 1e-2);
        chk("lossy gen == D + losses", genLoss, 500.0 + rLoss.totalLoss, 1e-2);
        chkTrue("lossy costs more", rLoss.totalCost > rNo.totalCost);
        printf("       total loss = %.4f MW, gen = %.4f MW, cost %.2f vs %.2f\n",
               rLoss.totalLoss, genLoss, rLoss.totalCost, rNo.totalCost);
    }

    // ================= TEST 7: generator bound binds =====================
    printf("\n=== T7: generator bound binds (tight Pmax) ===\n");
    {
        auto bs = net5Buses();
        auto ls = net5Lines();
        for (auto& l : ls) l.limit = 1e9;     // no line limits, isolate gen bound
        auto gs = net5Gens();
        gs[1].Pmax = 100.0;                   // was 400; forces G2 to its cap
        auto r = S.solve(bs, ls, gs, 0.0, {}, true);
        chkTrue("feasible", r.feasible);
        chk("P[1] pinned at Pmax", r.P[1], 100.0);
        chk("sum P == D", r.P[0]+r.P[1]+r.P[2], 500.0, 1e-2);
        chkTrue("P[0] within bounds", r.P[0] >= gs[0].Pmin-1e-6 && r.P[0] <= gs[0].Pmax+1e-6);
        chkTrue("P[2] within bounds", r.P[2] >= gs[2].Pmin-1e-6 && r.P[2] <= gs[2].Pmax+1e-6);
        printf("       P = [%.4f, %.4f, %.4f]\n", r.P[0], r.P[1], r.P[2]);
    }

    printf("\n==================================================\n");
    printf("  %d checks, %d failures\n", checks, failures);
    printf("==================================================\n");
    return failures == 0 ? 0 : 1;
}
