#pragma once
#include <vector>
#include <dense/Matrix.h>

namespace core
{

struct Generator
{
    double a, b, c;
    double Pmin, Pmax;
};

struct DispatchResult
{
    std::vector<double> P;
    double lambda = 0.0;
    double totalCost = 0.0;
    bool feasible = false;
};

class DispatchSolver
{
public:
    DispatchResult solve(const std::vector<Generator>& gens, double demand) const;

private:
    DispatchResult solveKKT(const std::vector<Generator>& gens,
                            const std::vector<bool>& atMin,
                            const std::vector<bool>& atMax,
                            double demand) const;
};

}
