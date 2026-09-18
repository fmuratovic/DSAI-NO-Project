#pragma once
#include <vector>
#include <string>
#include "Network.h"
#include "OPFSolver.h"
#include "DispatchSolver.h"

namespace core
{

struct WindPoint
{
    double windFrac = 0.0;
    double windMW = 0.0;
    double penetrationPct = 0.0;
    std::vector<double> P;
    double systemLambda = 0.0;
    double totalCost = 0.0;
    double totalLoss = 0.0;
    bool feasible = false;
};

struct DemandPoint
{
    double demandScale = 1.0;
    double totalDemand = 0.0;
    std::vector<double> P;
    std::vector<double> lambda;
    double systemLambda = 0.0;
    double totalCost = 0.0;
    int nBindingLines = 0;
    bool feasible = false;
};

struct CostCurvePoint
{
    double P = 0.0;
    double cost = 0.0;
    double incrementalCost = 0.0;
};

class Sweep
{
public:
    explicit Sweep(double sBase = 100.0) : _sBase(sBase) {}

    std::vector<WindPoint> windSweep(const Network& net,
                                     const std::vector<Generator>& gens,
                                     int steps = 21,
                                     bool withLosses = false) const;

    std::vector<DemandPoint> demandSweep(const Network& net,
                                         const std::vector<Generator>& gens,
                                         double scaleFrom = 0.4,
                                         double scaleTo = 1.4,
                                         int steps = 21,
                                         bool withLosses = false) const;

    static std::vector<CostCurvePoint> costCurve(const Generator& g, int steps = 41);

    static bool writeWindSweepCsv(const std::string& path, const std::vector<WindPoint>& pts);
    static bool writeDemandSweepCsv(const std::string& path, const std::vector<DemandPoint>& pts);
    static bool writeCostCurvesCsv(const std::string& path,
                                   const std::vector<Generator>& gens, int steps = 41);
    static bool writeDispatchCsv(const std::string& path, const OPFResult& res,
                                 const std::vector<Generator>& gens);

private:
    static Network scaledDemand(const Network& net, double scale);
    double _sBase;
};

}
