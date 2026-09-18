#include <vector>
#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

namespace ref {

struct Generator { double a, b, c, Pmin, Pmax; };

struct Bus {
    int id;
    bool isSlack = false;
    std::vector<int> genIndices;
    double demand = 0.0;
    double windCap = 0.0;
};

struct Line {
    int from, to;
    double x;
    double r;
    double limit;
};

struct Result {
    std::vector<double> P, theta, lambda, flow, mu;
    std::vector<double> lineLoss;
    double windUsed = 0.0;
    double totalLoss = 0.0;
    double totalCost = 0.0;
    bool feasible = false;
    double residual = 0.0;
    int iterations = 0;
};

static bool luSolve(std::vector<std::vector<double>> A,
                    std::vector<double> b,
                    std::vector<double>& x)
{
    int n = (int)b.size();
    std::vector<int> piv(n);
    for (int i = 0; i < n; ++i) piv[i] = i;

    for (int k = 0; k < n; ++k) {
        int best = k; double bestVal = std::fabs(A[k][k]);
        for (int i = k + 1; i < n; ++i)
            if (std::fabs(A[i][k]) > bestVal) { bestVal = std::fabs(A[i][k]); best = i; }
        if (bestVal < 1e-12) return false;
        if (best != k) { std::swap(A[k], A[best]); std::swap(b[k], b[best]); }

        for (int i = k + 1; i < n; ++i) {
            double f = A[i][k] / A[k][k];
            if (f == 0.0) continue;
            A[i][k] = 0.0;
            for (int j = k + 1; j < n; ++j) A[i][j] -= f * A[k][j];
            b[i] -= f * b[k];
        }
    }
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
        x[i] = s / A[i][i];
    }
    return true;
}

static std::vector<std::vector<double>>
buildB(const std::vector<Bus>& buses, const std::vector<Line>& lines)
{
    int n = (int)buses.size();
    std::vector<std::vector<double>> B(n, std::vector<double>(n, 0.0));
    for (const auto& ln : lines) {
        double bl = 1.0 / ln.x;
        B[ln.from][ln.to]   -= bl;
        B[ln.to][ln.from]   -= bl;
        B[ln.from][ln.from] += bl;
        B[ln.to][ln.to]     += bl;
    }
    return B;
}

class Solver {
public:
    explicit Solver(double sBase = 100.0) : _sBase(sBase) {}

    Result solveFixed(const std::vector<Bus>& buses,
                      const std::vector<Line>& lines,
                      const std::vector<Generator>& gens,
                      const std::vector<bool>& genAtMin,
                      const std::vector<bool>& genAtMax,
                      const std::vector<int>&  activeSign,
                      double windFrac,
                      const std::vector<double>& lossInject) const
    {
        int nBus = (int)buses.size();
        int nGen = (int)gens.size();
        int nLine = (int)lines.size();
        int slack = -1;
        for (const auto& bs : buses) if (bs.isSlack) { slack = bs.id; break; }

        Result res;
        res.P.assign(nGen, 0.0);
        res.theta.assign(nBus, 0.0);
        res.lambda.assign(nBus, 0.0);
        res.flow.assign(nLine, 0.0);
        res.mu.assign(nLine, 0.0);
        res.lineLoss.assign(nLine, 0.0);

        std::vector<double> Pfixed(nGen, 0.0);
        std::vector<bool>   genFree(nGen, true);
        for (int g = 0; g < nGen; ++g) {
            if (genAtMin[g])      { Pfixed[g] = gens[g].Pmin; genFree[g] = false; }
            else if (genAtMax[g]) { Pfixed[g] = gens[g].Pmax; genFree[g] = false; }
        }

        std::vector<int> pIdx(nGen, -1); int nFreeGen = 0;
        for (int g = 0; g < nGen; ++g) if (genFree[g]) pIdx[g] = nFreeGen++;

        std::vector<int> thIdx(nBus, -1); int nTh = 0;
        for (int i = 0; i < nBus; ++i) if (i != slack) thIdx[i] = nTh++;

        std::vector<int> muIdx(nLine, -1); int nMu = 0;
        for (int l = 0; l < nLine; ++l) if (activeSign[l] != 0) muIdx[l] = nMu++;

        auto B = buildB(buses, lines);

        int offP = 0;
        int offTh = nFreeGen;
        int offLm = offTh + nTh;
        int offMu = offLm + nBus;
        int dim   = offMu + nMu;

        std::vector<std::vector<double>> A(dim, std::vector<double>(dim, 0.0));
        std::vector<double> rhs(dim, 0.0);

        for (int g = 0; g < nGen; ++g) {
            if (!genFree[g]) continue;
            A[offP + pIdx[g]][offP + pIdx[g]] = 2.0 * gens[g].a;
            rhs[offP + pIdx[g]] = -gens[g].b;
        }
        for (int i = 0; i < nBus; ++i)
            for (int g : buses[i].genIndices)
                if (genFree[g]) A[offP + pIdx[g]][offLm + i] = -1.0;

        for (int k = 0; k < nBus; ++k) {
            if (k == slack) continue;
            for (int i = 0; i < nBus; ++i)
                if (B[k][i] != 0.0) A[offTh + thIdx[k]][offLm + i] = B[k][i];
        }
        for (int l = 0; l < nLine; ++l) {
            if (activeSign[l] == 0) continue;
            double bl = 1.0 / lines[l].x;
            int mc = offMu + muIdx[l];
            if (lines[l].from != slack) A[offTh + thIdx[lines[l].from]][mc] += bl;
            if (lines[l].to   != slack) A[offTh + thIdx[lines[l].to]][mc]   -= bl;
        }

        double windUsed = 0.0;
        for (int i = 0; i < nBus; ++i) {
            int r = offLm + i;
            double fixedGen = 0.0;
            for (int g : buses[i].genIndices) {
                if (genFree[g]) A[r][offP + pIdx[g]] = 1.0 / _sBase;
                else            fixedGen += Pfixed[g] / _sBase;
            }
            for (int j = 0; j < nBus; ++j) {
                if (j == slack) continue;
                if (B[i][j] != 0.0) A[r][offTh + thIdx[j]] = -B[i][j];
            }
            double wind = buses[i].windCap * windFrac;
            windUsed += wind;
            double loss = lossInject.empty() ? 0.0 : lossInject[i];
            rhs[r] = (buses[i].demand + loss - wind) / _sBase - fixedGen;
        }
        res.windUsed = windUsed;

        for (int l = 0; l < nLine; ++l) {
            if (activeSign[l] == 0) continue;
            double bl = 1.0 / lines[l].x;
            int r = offMu + muIdx[l];
            if (lines[l].from != slack) A[r][offTh + thIdx[lines[l].from]] += bl;
            if (lines[l].to   != slack) A[r][offTh + thIdx[lines[l].to]]   -= bl;
            rhs[r] = activeSign[l] * lines[l].limit / _sBase;
        }

        std::vector<double> x;
        auto Acopy = A; auto bcopy = rhs;
        if (!luSolve(A, rhs, x)) { res.feasible = false; return res; }

        double maxRes = 0.0;
        for (int i = 0; i < dim; ++i) {
            double s = 0.0;
            for (int j = 0; j < dim; ++j) s += Acopy[i][j] * x[j];
            maxRes = std::max(maxRes, std::fabs(s - bcopy[i]));
        }
        res.residual = maxRes;
        if (maxRes > 1e-6) { res.feasible = false; return res; }

        for (int g = 0; g < nGen; ++g)
            res.P[g] = genFree[g] ? x[offP + pIdx[g]] : Pfixed[g];
        for (int i = 0; i < nBus; ++i)
            res.theta[i] = (i == slack) ? 0.0 : x[offTh + thIdx[i]];
        for (int i = 0; i < nBus; ++i)
            res.lambda[i] = x[offLm + i];
        for (int l = 0; l < nLine; ++l)
            if (activeSign[l] != 0) res.mu[l] = x[offMu + muIdx[l]];

        for (int l = 0; l < nLine; ++l) {
            const auto& ln = lines[l];
            res.flow[l] = (1.0 / ln.x) * (res.theta[ln.from] - res.theta[ln.to]) * _sBase;
        }

        res.totalCost = 0.0;
        for (int g = 0; g < nGen; ++g)
            res.totalCost += gens[g].a * res.P[g] * res.P[g]
                           + gens[g].b * res.P[g] + gens[g].c;

        res.feasible = true;
        return res;
    }

    Result solve(const std::vector<Bus>& buses,
                 const std::vector<Line>& lines,
                 const std::vector<Generator>& gens,
                 double windFrac = 0.0,
                 const std::vector<double>& lossInject = {},
                 bool verbose = false) const
    {
        int nGen = (int)gens.size();
        int nLine = (int)lines.size();
        std::vector<bool> atMin(nGen, false), atMax(nGen, false);
        std::vector<int>  act(nLine, 0);

        Result res = solveFixed(buses, lines, gens, atMin, atMax, act, windFrac, lossInject);
        if (!res.feasible) return res;

        double tol = 1e-6;
        int maxIt = nGen + nLine + 3;
        for (int it = 0; it < maxIt; ++it) {
            res.iterations = it;
            int kind = 0, idx = -1;
            double worst = tol;

            for (int g = 0; g < nGen; ++g) {
                if (atMin[g] || atMax[g]) continue;
                if (res.P[g] < gens[g].Pmin - tol) {
                    double v = gens[g].Pmin - res.P[g];
                    if (v > worst) { worst = v; kind = 1; idx = g; }
                } else if (res.P[g] > gens[g].Pmax + tol) {
                    double v = res.P[g] - gens[g].Pmax;
                    if (v > worst) { worst = v; kind = 2; idx = g; }
                }
            }
            for (int l = 0; l < nLine; ++l) {
                if (act[l] != 0) continue;
                if (res.flow[l] > lines[l].limit + tol) {
                    double v = res.flow[l] - lines[l].limit;
                    if (v > worst) { worst = v; kind = 3; idx = l; }
                } else if (res.flow[l] < -lines[l].limit - tol) {
                    double v = -lines[l].limit - res.flow[l];
                    if (v > worst) { worst = v; kind = 4; idx = l; }
                }
            }
            if (kind == 0) break;

            if (kind == 1) atMin[idx] = true;
            else if (kind == 2) atMax[idx] = true;
            else if (kind == 3) act[idx] = +1;
            else act[idx] = -1;

            if (verbose)
                printf("    [activate %s %d]\n",
                       kind==1?"genMin":kind==2?"genMax":kind==3?"line+":"line-", idx);

            Result att = solveFixed(buses, lines, gens, atMin, atMax, act, windFrac, lossInject);
            if (!att.feasible) {
                if (kind == 1) atMin[idx] = false;
                else if (kind == 2) atMax[idx] = false;
                else act[idx] = 0;
                if (verbose) printf("    [revert -- infeasible]\n");
                break;
            }
            res = att;
        }
        return res;
    }

    Result solveWithLosses(const std::vector<Bus>& buses,
                           const std::vector<Line>& lines,
                           const std::vector<Generator>& gens,
                           double windFrac = 0.0,
                           int maxOuter = 40,
                           double tolMW = 1e-4,
                           bool verbose = false) const
    {
        int nBus = (int)buses.size();
        std::vector<double> inject(nBus, 0.0), prev(nBus, 0.0);
        Result res;

        for (int outer = 0; outer < maxOuter; ++outer) {
            res = solve(buses, lines, gens, windFrac, inject, false);
            if (!res.feasible) return res;

            std::vector<double> next(nBus, 0.0);
            double total = 0.0;
            for (size_t l = 0; l < lines.size(); ++l) {
                const auto& ln = lines[l];
                double g = (ln.r <= 0.0) ? 0.0 : ln.r / (ln.r * ln.r + ln.x * ln.x);
                double dth = res.theta[ln.from] - res.theta[ln.to];
                double lossMW = g * dth * dth * _sBase;
                res.lineLoss[l] = lossMW;
                total += lossMW;
                next[ln.from] += 0.5 * lossMW;
                next[ln.to]   += 0.5 * lossMW;
            }
            res.totalLoss = total;

            double delta = 0.0;
            for (int i = 0; i < nBus; ++i) delta = std::max(delta, std::fabs(next[i] - inject[i]));
            if (verbose) printf("    [loss outer %d: total=%.4f MW, delta=%.2e]\n",
                                outer, total, delta);
            inject = next;
            if (delta < tolMW) break;
        }
        return res;
    }

private:
    double _sBase;
};

}
