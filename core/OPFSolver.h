#pragma once
#include <vector>
#include <dense/Matrix.h>
#include "Network.h"
#include "DispatchSolver.h"

namespace core
{

struct OPFResult
{
    std::vector<double> P;
    std::vector<double> theta;
    std::vector<double> lambda;
    std::vector<double> flow;
    std::vector<double> mu;
    std::vector<double> lineLoss;

    double windUsed  = 0.0;
    double totalLoss = 0.0;
    double totalCost = 0.0;
    double residual  = 0.0;
    bool   feasible  = false;
    int    lossIters = 0;

    std::vector<bool> genAtMin, genAtMax;
    std::vector<int>  activeLines;
};

class OPFSolver
{
public:
    explicit OPFSolver(double sBase = 100.0) : _sBase(sBase) {}

    OPFResult solve(const Network& net,
                    const std::vector<Generator>& gens,
                    double windFrac = 0.0,
                    bool verbose = false) const;

    OPFResult solveWithLosses(const Network& net,
                              const std::vector<Generator>& gens,
                              double windFrac = 0.0,
                              int maxOuter = 40,
                              double tolMW = 1e-4,
                              bool verbose = false) const;

    OPFResult solveManual(const Network& net,
                          const std::vector<Generator>& gens,
                          const std::vector<bool>& genAtMin,
                          const std::vector<bool>& genAtMax,
                          const std::vector<int>& activeSign,
                          double windFrac = 0.0) const
    {
        return solveFixed(net, gens, genAtMin, genAtMax, activeSign, windFrac, {});
    }

private:
    OPFResult solveFixed(const Network& net,
                         const std::vector<Generator>& gens,
                         const std::vector<bool>& genAtMin,
                         const std::vector<bool>& genAtMax,
                         const std::vector<int>& activeSign,
                         double windFrac,
                         const std::vector<double>& lossInject) const;

    OPFResult solveActiveSet(const Network& net,
                             const std::vector<Generator>& gens,
                             double windFrac,
                             const std::vector<double>& lossInject,
                             bool verbose) const;

    double _sBase;
};

}
