#include "Network.h"
#include <stdexcept>

namespace core
{

Network::Network(std::vector<Bus> buses, std::vector<Line> lines)
    : _buses(std::move(buses)), _lines(std::move(lines))
{
    for (const auto& b : _buses)
    {
        if (b.isSlack)
        {
            _slackBus = b.id;
            break;
        }
    }
    if (_slackBus < 0)
        throw std::runtime_error("Network: no slack bus defined");
}

double Network::getTotalWindCapacity() const
{
    double total = 0.0;
    for (const auto& b : _buses)
        total += b.windCap;
    return total;
}

double Network::lineConductance(const Line& line)
{
    if (line.r <= 0.0)
        return 0.0;
    double denom = line.r * line.r + line.x * line.x;
    if (denom <= 0.0)
        return 0.0;
    return line.r / denom;
}

dense::Matrix<double> Network::buildBMatrix() const
{
    td::UINT4 n = static_cast<td::UINT4>(_buses.size());
    dense::Matrix<double> B(n, n, nullptr, true);
    auto Bio = B.getManipulator();

    for (const auto& line : _lines)
    {
        double bLine = 1.0 / line.x;
        td::UINT4 i = static_cast<td::UINT4>(line.from);
        td::UINT4 j = static_cast<td::UINT4>(line.to);

        Bio(i, j) -= bLine;
        Bio(j, i) -= bLine;
        Bio(i, i) += bLine;
        Bio(j, j) += bLine;
    }
    return B;
}

}
