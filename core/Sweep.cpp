#include "Sweep.h"
#include <fstream>
#include <iomanip>

namespace core
{

Network Sweep::scaledDemand(const Network& net, double scale)
{
    std::vector<Bus> buses = net.getBuses();
    for (auto& b : buses)
        b.demand *= scale;
    return Network(std::move(buses), net.getLines());
}

std::vector<WindPoint> Sweep::windSweep(const Network& net,
                                        const std::vector<Generator>& gens,
                                        int steps,
                                        bool withLosses) const
{
    std::vector<WindPoint> out;
    if (steps < 2) steps = 2;

    double totalDemand = 0.0;
    for (const auto& b : net.getBuses())
        totalDemand += b.demand;

    OPFSolver opf(_sBase);

    for (int s = 0; s < steps; ++s)
    {
        double frac = static_cast<double>(s) / static_cast<double>(steps - 1);

        OPFResult r = withLosses
            ? opf.solveWithLosses(net, gens, frac)
            : opf.solve(net, gens, frac);

        WindPoint p;
        p.windFrac = frac;
        p.windMW = r.windUsed;
        p.penetrationPct = (totalDemand > 0.0) ? 100.0 * r.windUsed / totalDemand : 0.0;
        p.P = r.P;
        p.systemLambda = r.lambda.empty() ? 0.0 : r.lambda[net.getSlackBus()];
        p.totalCost = r.totalCost;
        p.totalLoss = r.totalLoss;
        p.feasible = r.feasible;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<DemandPoint> Sweep::demandSweep(const Network& net,
                                            const std::vector<Generator>& gens,
                                            double scaleFrom,
                                            double scaleTo,
                                            int steps,
                                            bool withLosses) const
{
    std::vector<DemandPoint> out;
    if (steps < 2) steps = 2;

    OPFSolver opf(_sBase);

    for (int s = 0; s < steps; ++s)
    {
        double t = static_cast<double>(s) / static_cast<double>(steps - 1);
        double scale = scaleFrom + t * (scaleTo - scaleFrom);

        Network scaled = scaledDemand(net, scale);

        double totalDemand = 0.0;
        for (const auto& b : scaled.getBuses())
            totalDemand += b.demand;

        OPFResult r = withLosses
            ? opf.solveWithLosses(scaled, gens, 0.0)
            : opf.solve(scaled, gens, 0.0);

        DemandPoint p;
        p.demandScale = scale;
        p.totalDemand = totalDemand;
        p.P = r.P;
        p.lambda = r.lambda;
        p.systemLambda = r.lambda.empty() ? 0.0 : r.lambda[scaled.getSlackBus()];
        p.totalCost = r.totalCost;
        p.nBindingLines = 0;
        for (int a : r.activeLines) if (a != 0) ++p.nBindingLines;
        p.feasible = r.feasible;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<CostCurvePoint> Sweep::costCurve(const Generator& g, int steps)
{
    std::vector<CostCurvePoint> out;
    if (steps < 2) steps = 2;
    for (int s = 0; s < steps; ++s)
    {
        double t = static_cast<double>(s) / static_cast<double>(steps - 1);
        double P = g.Pmin + t * (g.Pmax - g.Pmin);
        CostCurvePoint p;
        p.P = P;
        p.cost = g.a * P * P + g.b * P + g.c;
        p.incrementalCost = 2.0 * g.a * P + g.b;
        out.push_back(p);
    }
    return out;
}

bool Sweep::writeWindSweepCsv(const std::string& path, const std::vector<WindPoint>& pts)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << std::fixed << std::setprecision(6);

    size_t nGen = 0;
    for (const auto& p : pts) nGen = std::max(nGen, p.P.size());

    f << "wind_frac,wind_MW,penetration_pct,system_lambda,total_cost,total_loss_MW,feasible";
    for (size_t g = 0; g < nGen; ++g) f << ",P" << g;
    f << "\n";

    for (const auto& p : pts)
    {
        f << p.windFrac << ',' << p.windMW << ',' << p.penetrationPct << ','
          << p.systemLambda << ',' << p.totalCost << ',' << p.totalLoss << ','
          << (p.feasible ? 1 : 0);
        for (size_t g = 0; g < nGen; ++g)
            f << ',' << (g < p.P.size() ? p.P[g] : 0.0);
        f << "\n";
    }
    return true;
}

bool Sweep::writeDemandSweepCsv(const std::string& path, const std::vector<DemandPoint>& pts)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << std::fixed << std::setprecision(6);

    size_t nGen = 0, nBus = 0;
    for (const auto& p : pts)
    {
        nGen = std::max(nGen, p.P.size());
        nBus = std::max(nBus, p.lambda.size());
    }

    f << "demand_scale,total_demand_MW,system_lambda,total_cost,binding_lines,feasible";
    for (size_t g = 0; g < nGen; ++g) f << ",P" << g;
    for (size_t b = 0; b < nBus; ++b) f << ",lambda" << b;
    f << "\n";

    for (const auto& p : pts)
    {
        f << p.demandScale << ',' << p.totalDemand << ',' << p.systemLambda << ','
          << p.totalCost << ',' << p.nBindingLines << ',' << (p.feasible ? 1 : 0);
        for (size_t g = 0; g < nGen; ++g)
            f << ',' << (g < p.P.size() ? p.P[g] : 0.0);
        for (size_t b = 0; b < nBus; ++b)
            f << ',' << (b < p.lambda.size() ? p.lambda[b] : 0.0);
        f << "\n";
    }
    return true;
}

bool Sweep::writeCostCurvesCsv(const std::string& path,
                               const std::vector<Generator>& gens, int steps)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << std::fixed << std::setprecision(6);

    f << "gen,P_MW,cost,incremental_cost\n";
    for (size_t g = 0; g < gens.size(); ++g)
    {
        auto curve = costCurve(gens[g], steps);
        for (const auto& p : curve)
            f << g << ',' << p.P << ',' << p.cost << ',' << p.incrementalCost << "\n";
    }
    return true;
}

bool Sweep::writeDispatchCsv(const std::string& path, const OPFResult& res,
                             const std::vector<Generator>& gens)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << std::fixed << std::setprecision(6);

    f << "gen,P_MW,Pmin,Pmax,incremental_cost,at_min,at_max\n";
    for (size_t g = 0; g < gens.size() && g < res.P.size(); ++g)
    {
        double inc = 2.0 * gens[g].a * res.P[g] + gens[g].b;
        f << g << ',' << res.P[g] << ',' << gens[g].Pmin << ',' << gens[g].Pmax
          << ',' << inc << ','
          << ((g < res.genAtMin.size() && res.genAtMin[g]) ? 1 : 0) << ','
          << ((g < res.genAtMax.size() && res.genAtMax[g]) ? 1 : 0) << "\n";
    }
    return true;
}

}
