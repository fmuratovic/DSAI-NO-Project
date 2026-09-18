#pragma once
#include <vector>
#include <dense/Matrix.h>

namespace core
{

struct Bus
{
    int id;
    bool isSlack = false;
    std::vector<int> genIndices;
    double demand = 0.0;
    double windCap = 0.0;
};

struct Line
{
    int from, to;
    double x;
    double r = 0.0;
    double limit;
};

class Network
{
public:
    Network(std::vector<Bus> buses, std::vector<Line> lines);

    const std::vector<Bus>&  getBuses() const { return _buses; }
    const std::vector<Line>& getLines() const { return _lines; }
    int getSlackBus() const { return _slackBus; }

    double getTotalWindCapacity() const;
    dense::Matrix<double> buildBMatrix() const;
    static double lineConductance(const Line& line);

private:
    std::vector<Bus> _buses;
    std::vector<Line> _lines;
    int _slackBus = -1;
};

}
