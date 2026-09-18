#include "OPFSolver.h"
#include <sparse/ISolver.h>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace core
{

OPFResult OPFSolver::solveFixed(const Network& net,
                                const std::vector<Generator>& gens,
                                const std::vector<bool>& genAtMin,
                                const std::vector<bool>& genAtMax,
                                const std::vector<int>& activeSign,
                                double windFrac,
                                const std::vector<double>& lossInject) const
{
    const auto& buses = net.getBuses();
    const auto& lines = net.getLines();
    td::UINT4 nBus  = static_cast<td::UINT4>(buses.size());
    td::UINT4 nGen  = static_cast<td::UINT4>(gens.size());
    td::UINT4 nLine = static_cast<td::UINT4>(lines.size());
    int slack = net.getSlackBus();

    OPFResult res;
    res.P.assign(nGen, 0.0);
    res.theta.assign(nBus, 0.0);
    res.lambda.assign(nBus, 0.0);
    res.flow.assign(nLine, 0.0);
    res.mu.assign(nLine, 0.0);
    res.lineLoss.assign(nLine, 0.0);
    res.genAtMin = genAtMin;
    res.genAtMax = genAtMax;
    res.activeLines = activeSign;

    std::vector<double> Pfixed(nGen, 0.0);
    std::vector<bool>   genFree(nGen, true);
    for (td::UINT4 g = 0; g < nGen; ++g)
    {
        if (genAtMin[g])      { Pfixed[g] = gens[g].Pmin; genFree[g] = false; }
        else if (genAtMax[g]) { Pfixed[g] = gens[g].Pmax; genFree[g] = false; }
    }

    std::vector<int> pIdx(nGen, -1);
    td::UINT4 nFreeGen = 0;
    for (td::UINT4 g = 0; g < nGen; ++g)
        if (genFree[g]) pIdx[g] = nFreeGen++;

    std::vector<int> thIdx(nBus, -1);
    td::UINT4 nTh = 0;
    for (td::UINT4 i = 0; i < nBus; ++i)
        if (static_cast<int>(i) != slack) thIdx[i] = nTh++;

    std::vector<int> muIdx(nLine, -1);
    td::UINT4 nMu = 0;
    for (td::UINT4 l = 0; l < nLine; ++l)
        if (activeSign[l] != 0) muIdx[l] = nMu++;

    dense::Matrix<double> B = net.buildBMatrix();
    auto Bio = B.getManipulator();

    int offP  = 0;
    int offTh = static_cast<int>(nFreeGen);
    int offLm = offTh + static_cast<int>(nTh);
    int offMu = offLm + static_cast<int>(nBus);
    int dim   = offMu + static_cast<int>(nMu);

    struct Entry { int r, c; double v; };
    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(dim) * 6);
    auto addEntry = [&](int r, int c, double v)
    {
        if (v == 0.0) return;
        entries.push_back({ r, c, v });
    };

    std::vector<double> rhs(dim, 0.0);

    for (td::UINT4 g = 0; g < nGen; ++g)
    {
        if (!genFree[g]) continue;
        int r = offP + pIdx[g];
        addEntry(r, r, 2.0 * gens[g].a);
        rhs[r] = -gens[g].b;
    }
    for (td::UINT4 i = 0; i < nBus; ++i)
        for (int g : buses[i].genIndices)
            if (genFree[g])
                addEntry(offP + pIdx[g], offLm + static_cast<int>(i), -1.0);

    for (td::UINT4 k = 0; k < nBus; ++k)
    {
        if (static_cast<int>(k) == slack) continue;
        int r = offTh + thIdx[k];
        for (td::UINT4 i = 0; i < nBus; ++i)
            addEntry(r, offLm + static_cast<int>(i), Bio(k, i));
    }
    for (td::UINT4 l = 0; l < nLine; ++l)
    {
        if (activeSign[l] == 0) continue;
        const auto& ln = lines[l];
        double bLine = 1.0 / ln.x;
        int mc = offMu + muIdx[l];
        if (ln.from != slack) addEntry(offTh + thIdx[ln.from], mc,  bLine);
        if (ln.to   != slack) addEntry(offTh + thIdx[ln.to],   mc, -bLine);
    }

    double windUsed = 0.0;
    for (td::UINT4 i = 0; i < nBus; ++i)
    {
        int r = offLm + static_cast<int>(i);
        double fixedGen = 0.0;
        for (int g : buses[i].genIndices)
        {
            if (genFree[g]) addEntry(r, offP + pIdx[g], 1.0 / _sBase);
            else            fixedGen += Pfixed[g] / _sBase;
        }
        for (td::UINT4 j = 0; j < nBus; ++j)
        {
            if (static_cast<int>(j) == slack) continue;
            addEntry(r, offTh + thIdx[j], -Bio(i, j));
        }
        double wind = buses[i].windCap * windFrac;
        windUsed += wind;
        double loss = lossInject.empty() ? 0.0 : lossInject[i];
        rhs[r] = (buses[i].demand + loss - wind) / _sBase - fixedGen;
    }
    res.windUsed = windUsed;

    for (td::UINT4 l = 0; l < nLine; ++l)
    {
        if (activeSign[l] == 0) continue;
        const auto& ln = lines[l];
        double bLine = 1.0 / ln.x;
        int r = offMu + muIdx[l];
        if (ln.from != slack) addEntry(r, offTh + thIdx[ln.from],  bLine);
        if (ln.to   != slack) addEntry(r, offTh + thIdx[ln.to],   -bLine);
        rhs[r] = activeSign[l] * ln.limit / _sBase;
    }

    int nz = static_cast<int>(entries.size());
    sparse::DblSolverReleaser guard(
        sparse::createDblSolver(dim, nz,
                                sparse::Symmetry::NonSymmetric,
                                sparse::SolverType::LU,
                                sparse::Pivoting::DiagonalMultiPass));
    sparse::DblSolver& solver = *(guard.ptr());

    solver.populateDiagonals(0.0);
    for (const auto& e : entries)
        solver.addTriple(e.r, e.c, e.v);

    if (!solver.factorize())
    {
        res.feasible = false;
        return res;
    }

    for (int r = 0; r < dim; ++r)
        solver.setRHS(r, rhs[r]);

    if (!solver.solve())
    {
        res.feasible = false;
        return res;
    }

    {
        std::vector<double> Ax(dim, 0.0);
        for (const auto& e : entries)
            Ax[e.r] += e.v * solver.x(e.c);
        double maxRes = 0.0;
        for (int r = 0; r < dim; ++r)
            maxRes = std::max(maxRes, std::fabs(Ax[r] - rhs[r]));
        res.residual = maxRes;
        if (maxRes > 1e-4)
        {
            res.feasible = false;
            return res;
        }
    }

    for (td::UINT4 g = 0; g < nGen; ++g)
        res.P[g] = genFree[g] ? solver.x(offP + pIdx[g]) : Pfixed[g];
    for (td::UINT4 i = 0; i < nBus; ++i)
        res.theta[i] = (static_cast<int>(i) == slack) ? 0.0 : solver.x(offTh + thIdx[i]);
    for (td::UINT4 i = 0; i < nBus; ++i)
        res.lambda[i] = solver.x(offLm + i);
    for (td::UINT4 l = 0; l < nLine; ++l)
        if (activeSign[l] != 0) res.mu[l] = solver.x(offMu + muIdx[l]);

    for (td::UINT4 l = 0; l < nLine; ++l)
    {
        const auto& ln = lines[l];
        res.flow[l] = (1.0 / ln.x) * (res.theta[ln.from] - res.theta[ln.to]) * _sBase;
    }

    res.totalCost = 0.0;
    for (td::UINT4 g = 0; g < nGen; ++g)
        res.totalCost += gens[g].a * res.P[g] * res.P[g]
                       + gens[g].b * res.P[g] + gens[g].c;

    res.feasible = true;
    return res;
}

OPFResult OPFSolver::solveActiveSet(const Network& net,
                                    const std::vector<Generator>& gens,
                                    double windFrac,
                                    const std::vector<double>& lossInject,
                                    bool verbose) const
{
    const auto& lines = net.getLines();
    td::UINT4 nGen  = static_cast<td::UINT4>(gens.size());
    td::UINT4 nLine = static_cast<td::UINT4>(lines.size());

    std::vector<bool> atMin(nGen, false), atMax(nGen, false);
    std::vector<int>  act(nLine, 0);

    OPFResult res = solveFixed(net, gens, atMin, atMax, act, windFrac, lossInject);
    if (!res.feasible)
        return res;

    double tol = 1e-6;
    int maxIt = static_cast<int>(nGen + nLine) + 3;

    for (int it = 0; it < maxIt; ++it)
    {
        int kind = 0, idx = -1;
        double worst = tol;

        for (td::UINT4 g = 0; g < nGen; ++g)
        {
            if (atMin[g] || atMax[g]) continue;
            if (res.P[g] < gens[g].Pmin - tol)
            {
                double v = gens[g].Pmin - res.P[g];
                if (v > worst) { worst = v; kind = 1; idx = static_cast<int>(g); }
            }
            else if (res.P[g] > gens[g].Pmax + tol)
            {
                double v = res.P[g] - gens[g].Pmax;
                if (v > worst) { worst = v; kind = 2; idx = static_cast<int>(g); }
            }
        }
        for (td::UINT4 l = 0; l < nLine; ++l)
        {
            if (act[l] != 0) continue;
            if (res.flow[l] > lines[l].limit + tol)
            {
                double v = res.flow[l] - lines[l].limit;
                if (v > worst) { worst = v; kind = 3; idx = static_cast<int>(l); }
            }
            else if (res.flow[l] < -lines[l].limit - tol)
            {
                double v = -lines[l].limit - res.flow[l];
                if (v > worst) { worst = v; kind = 4; idx = static_cast<int>(l); }
            }
        }

        if (kind == 0)
        {
            if (verbose) std::cout << "converged\n";
            break;
        }

        if      (kind == 1) atMin[idx] = true;
        else if (kind == 2) atMax[idx] = true;
        else if (kind == 3) act[idx]   = +1;
        else                act[idx]   = -1;

        if (verbose)
            std::cout << "activate kind=" << kind << " idx=" << idx << " viol=" << worst << "\n";

        OPFResult attempt = solveFixed(net, gens, atMin, atMax, act, windFrac, lossInject);
        if (!attempt.feasible)
        {
            if      (kind == 1) atMin[idx] = false;
            else if (kind == 2) atMax[idx] = false;
            else                act[idx]   = 0;
            if (verbose) std::cout << "reverted, stopping\n";
            break;
        }
        res = attempt;
    }

    res.genAtMin = atMin;
    res.genAtMax = atMax;
    res.activeLines = act;
    return res;
}

OPFResult OPFSolver::solve(const Network& net,
                           const std::vector<Generator>& gens,
                           double windFrac,
                           bool verbose) const
{
    return solveActiveSet(net, gens, windFrac, {}, verbose);
}

OPFResult OPFSolver::solveWithLosses(const Network& net,
                                     const std::vector<Generator>& gens,
                                     double windFrac,
                                     int maxOuter,
                                     double tolMW,
                                     bool verbose) const
{
    const auto& buses = net.getBuses();
    const auto& lines = net.getLines();
    size_t nBus = buses.size();

    std::vector<double> inject(nBus, 0.0);
    OPFResult res;

    for (int outer = 0; outer < maxOuter; ++outer)
    {
        res = solveActiveSet(net, gens, windFrac, inject, false);
        if (!res.feasible) return res;
        res.lossIters = outer + 1;

        std::vector<double> next(nBus, 0.0);
        double total = 0.0;
        for (size_t l = 0; l < lines.size(); ++l)
        {
            const auto& ln = lines[l];
            double g = Network::lineConductance(ln);
            double dth = res.theta[ln.from] - res.theta[ln.to];
            double lossMW = g * dth * dth * _sBase;
            res.lineLoss[l] = lossMW;
            total += lossMW;
            next[ln.from] += 0.5 * lossMW;
            next[ln.to]   += 0.5 * lossMW;
        }
        res.totalLoss = total;

        double delta = 0.0;
        for (size_t i = 0; i < nBus; ++i)
            delta = std::max(delta, std::fabs(next[i] - inject[i]));

        if (verbose)
            std::cout << "loss iter " << outer << " total=" << total << " delta=" << delta << "\n";

        inject = next;
        if (delta < tolMW) break;
    }
    return res;
}

}
