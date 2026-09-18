#pragma once
#include "Network.h"
#include "DispatchSolver.h"
#include <vector>

namespace core
{

// 5-bus network: bus0 slack+G1, bus1 G2, bus2 G3, bus3 load 500MW, bus4 wind
// lines: 0-1, 0-4, 1-2, 4-2, 2-3(tight, index 4), 1-3
inline Network buildTestNetwork(double rFrac = 0.0,
                                double windCap = 0.0,
                                double limitScale = 1.0)
{
    std::vector<Bus> buses = {
        { 0, true,  { 0 }, 0.0,   0.0     },
        { 1, false, { 1 }, 0.0,   0.0     },
        { 2, false, { 2 }, 0.0,   0.0     },
        { 3, false, { },   500.0, 0.0     },
        { 4, false, { },   0.0,   windCap }
    };

    std::vector<Line> lines = {
        { 0, 1, 0.10, 0.0, 250.0 },
        { 0, 4, 0.15, 0.0, 150.0 },
        { 1, 2, 0.10, 0.0, 200.0 },
        { 4, 2, 0.12, 0.0, 150.0 },
        { 2, 3, 0.08, 0.0, 180.0 },
        { 1, 3, 0.20, 0.0, 200.0 }
    };

    for (auto& l : lines)
    {
        l.r = rFrac * l.x;
        l.limit *= limitScale;
    }

    return Network(std::move(buses), std::move(lines));
}

inline std::vector<Generator> buildTestGenerators()
{
    return {
        { 0.008, 8.0, 0.0, 50.0, 300.0 },
        { 0.009, 6.4, 0.0, 50.0, 400.0 },
        { 0.007, 7.9, 0.0, 30.0, 350.0 }
    };
}

inline Network build2BusNetwork(double lineLimit = 1e9, double rFrac = 0.0)
{
    std::vector<Bus> buses = {
        { 0, true,  { 0 }, 0.0,   0.0 },
        { 1, false, { 1 }, 500.0, 0.0 }
    };
    std::vector<Line> lines = { { 0, 1, 0.10, rFrac * 0.10, lineLimit } };
    return Network(std::move(buses), std::move(lines));
}

inline std::vector<Generator> build2BusGenerators()
{
    return {
        { 0.008, 8.0, 0.0, 50.0, 300.0 },
        { 0.009, 6.4, 0.0, 50.0, 400.0 }
    };
}

}
