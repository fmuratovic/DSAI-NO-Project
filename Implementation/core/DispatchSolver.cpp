#include "DispatchSolver.h"

namespace core
{

DispatchResult DispatchSolver::solveKKT(const std::vector<Generator>& gens,
                                        const std::vector<bool>& atMin,
                                        const std::vector<bool>& atMax,
                                        double demand) const
{
    td::UINT4 n = static_cast<td::UINT4>(gens.size());
    DispatchResult res;
    res.P.assign(n, 0.0);

    std::vector<td::UINT4> freeIdx;
    double fixedPower = 0.0;
    for (td::UINT4 i = 0; i < n; ++i)
    {
        if (atMin[i])       { res.P[i] = gens[i].Pmin; fixedPower += gens[i].Pmin; }
        else if (atMax[i])  { res.P[i] = gens[i].Pmax; fixedPower += gens[i].Pmax; }
        else                { freeIdx.push_back(i); }
    }

    td::UINT4 m = static_cast<td::UINT4>(freeIdx.size());
    double freeDemand = demand - fixedPower;

    if (m == 0)
    {
        res.feasible = true;
        return res;
    }

    td::UINT4 dim = m + 1;
    dense::Matrix<double> A(dim, dim, nullptr, true);
    dense::Matrix<double> rhs(dim, 1, nullptr, true);

    auto Aio   = A.getManipulator();
    auto rhsio = rhs.getManipulator();

    for (td::UINT4 k = 0; k < m; ++k)
    {
        const Generator& g = gens[freeIdx[k]];
        Aio(k, k) = 2.0 * g.a;
        Aio(k, m) = 1.0;
        Aio(m, k) = 1.0;
        rhsio(k, 0) = -g.b;
    }
    rhsio(m, 0) = freeDemand;

    if (!A.solve(rhs))
    {
        res.feasible = false;
        return res;
    }

    auto sol = rhs.getManipulator();
    for (td::UINT4 k = 0; k < m; ++k)
        res.P[freeIdx[k]] = sol(k, 0);

    res.lambda = -sol(m, 0);
    res.feasible = true;
    return res;
}

DispatchResult DispatchSolver::solve(const std::vector<Generator>& gens, double demand) const
{
    size_t n = gens.size();
    std::vector<bool> atMin(n, false), atMax(n, false);

    DispatchResult res;
    int maxIters = static_cast<int>(n) + 2;
    for (int iter = 0; iter < maxIters; ++iter)
    {
        res = solveKKT(gens, atMin, atMax, demand);
        if (!res.feasible) break;

        bool changed = false;
        for (size_t i = 0; i < n; ++i)
        {
            if (atMin[i] || atMax[i]) continue;
            if (res.P[i] < gens[i].Pmin)      { atMin[i] = true; changed = true; }
            else if (res.P[i] > gens[i].Pmax) { atMax[i] = true; changed = true; }
        }
        if (!changed) break;
    }

    if (res.feasible)
    {
        res.totalCost = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const Generator& g = gens[i];
            res.totalCost += g.a * res.P[i] * res.P[i] + g.b * res.P[i] + g.c;
        }
    }
    return res;
}

}
