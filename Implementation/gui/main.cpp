// =====================================================================
//  Economic Dispatch / DC-OPF with wind -- complete application
//
//  Header strip + four tabs (gui::StandardTabView):
//    Overview   -- scenario selector, status, KPI tiles, compact dispatch,
//                  mini network, energy balance, KKT residual. One canvas.
//    Parameters -- generator table, demand, wind, losses, Solve; results
//                  drawn as compact cards on a canvas.
//    Network    -- the 5-bus schematic, full size, annotated after Solve.
//    Charts     -- the four deliverable charts, Export PDF.
//
//  Every natID drawing/text call lives in namespace prim. Widget calls are
//  taken from Common/Include headers (see the STAGE3/STAGE4 notes).
//  natGUI draws native controls: fields, buttons, sliders, tabs and their
//  fonts cannot be restyled; everything else here is canvas-drawn.
// =====================================================================

#include <gui/WinMain.h>       // /entry:mainCRTStartup so main() links under /SUBSYSTEM:WINDOWS
#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Canvas.h>
#include <gui/Shape.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/Slider.h>
#include <gui/CheckBox.h>
#include <gui/ComboBox.h>
#include <gui/LineEdit.h>
#include <gui/NumericEdit.h>
#include <gui/VerticalLayout.h>
#include <gui/HorizontalLayout.h>
#include <gui/FileDialog.h>
#include <gui/StandardTabView.h>

#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <cmath>

#include "Network.h"
#include "OPFSolver.h"
#include "Sweep.h"
#include "TestNetwork.h"

// ---------------------------------------------------------------------
//  Case constants, scenarios and the parameter bundle shared by the tabs
// ---------------------------------------------------------------------
static const int    N_GEN             = 3;
static const char* const GEN_NAME[N_GEN] = { "G1", "G2", "G3" };
static const double TOTAL_DEMAND_MW   = 500.0;    // nominal, scaled by CaseParams::demandScale
static const double WIND_INSTALLED_MW = 150.0;
static const double LOSS_R_OVER_X     = 0.10;
static const double LIMIT_RELAX       = 1e7;      // line limits relaxed, as in cli Part B

enum DlgID : td::UINT4 { DLG_EXPORT_OVERVIEW = 100, DLG_EXPORT_CHARTS = 101 };

enum class Scenario : int { Base = 0, HighWind, LowWind, HighDemand, LowDemand, Custom };
static const char* const SCENARIO_NAME[6] =
    { "Base case", "High wind", "Low wind", "High demand", "Low demand", "Custom" };

struct ScenarioPreset { double windFrac; double demandScale; };
static const ScenarioPreset SCENARIO_PRESET[5] =
    { { 0.0, 1.0 }, { 1.0, 1.0 }, { 0.25, 1.0 }, { 0.0, 1.3 }, { 0.0, 0.7 } };

struct CaseParams
{
    std::vector<core::Generator> gens;
    double windFrac    = 0.0;
    double demandScale = 1.0;
    bool   useLosses   = false;
    Scenario scenario  = Scenario::Base;

    double demandMW() const { return TOTAL_DEMAND_MW * demandScale; }
};

static std::string fmt(double v, int prec = 2)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

static std::string fmtThousands(double v, int prec)
{
    std::string s = fmt(std::fabs(v), prec);
    const size_t dot = s.find('.');
    size_t intLen = (dot == std::string::npos) ? s.size() : dot;
    for (size_t i = intLen; i > 3; i -= 3) s.insert(i - 3, ",");
    return (v < 0 ? "-" : "") + s;
}

// Network for a case: line resistance for the loss model, wind at bus 4,
// limits relaxed, every bus load scaled.
static core::Network buildCaseNetwork(const CaseParams& cp, bool forceLossless = false)
{
    core::Network base = core::buildTestNetwork(
        (cp.useLosses && !forceLossless) ? LOSS_R_OVER_X : 0.0, WIND_INSTALLED_MW, LIMIT_RELAX);
    std::vector<core::Bus> buses = base.getBuses();
    for (auto& b : buses) b.demand *= cp.demandScale;
    return core::Network(std::move(buses), base.getLines());
}

static core::OPFResult solveCase(const CaseParams& cp)
{
    core::Network net = buildCaseNetwork(cp);
    core::OPFSolver opf;
    return cp.useLosses ? opf.solveWithLosses(net, cp.gens, cp.windFrac)
                        : opf.solve(net, cp.gens, cp.windFrac);
}

// =====================================================================
//  DRAWING PRIMITIVES -- the only natID drawing calls in the file
// =====================================================================
namespace prim
{

inline void line(const gui::Point& a, const gui::Point& b, td::ColorID c,
                 float w = 1.0f, td::LinePattern p = td::LinePattern::Solid)
{
    gui::Shape::drawLine(a, b, c, w, p);
}

inline void polyline(const std::vector<gui::Point>& pts, td::ColorID c,
                     float w = 2.0f, td::LinePattern p = td::LinePattern::Solid)
{
    if (pts.size() < 2) return;
    gui::Shape s;
    s.createPolyLine(pts.data(), pts.size(), w, p);
    s.drawWire(c);
}

inline void polygon(const std::vector<gui::Point>& pts, td::ColorID fill, td::ColorID edge)
{
    if (pts.size() < 3) return;
    gui::Shape s;
    s.createPolygon(pts.data(), pts.size(), 1.0f);
    s.drawFillAndWire(fill, edge);
}

inline void fillRect(const gui::Rect& r, td::ColorID fill) { gui::Shape::drawRect(r, fill); }

inline void frameRect(const gui::Rect& r, td::ColorID lineColor, float w = 1.0f)
{
    gui::Shape::drawRect(r, lineColor, w);
}

inline void roundedPanel(const gui::Rect& r, double radius, td::ColorID fill, td::ColorID edge)
{
    gui::Shape s;
    s.createRoundedRect(r, radius, 1.0f);
    s.drawFillAndWire(fill, edge);
}

inline void dot(const gui::Point& center, double radius, td::ColorID fill, td::ColorID edge)
{
    gui::Shape s;
    s.createCircle(gui::Circle(center, radius), 1.0f);
    s.drawFillAndWire(fill, edge);
}

inline void text(const gui::Rect& r, const std::string& str, gui::Font::ID font,
                 td::ColorID c,
                 td::TextAlignment h = td::TextAlignment::Left,
                 td::VAlignment v = td::VAlignment::Center)
{
    td::String s(str.c_str());
    gui::DrawableString::draw(s, r, font, c, h, v);
}

} // namespace prim

// =====================================================================
//  Theme and chart helpers -- pure arithmetic
// =====================================================================
namespace chart
{

struct Palette
{
    td::ColorID text, muted, grid, axis, bg, card, accent, ok, warn, err;
};

inline Palette palette(bool forExport)
{
    Palette p;
    if (forExport)
    {
        p.text = td::ColorID::Black;      p.muted = td::ColorID::DimGray;
        p.grid = td::ColorID::LightGray;  p.axis  = td::ColorID::DimGray;
        p.bg   = td::ColorID::White;      p.card  = td::ColorID::WhiteSmoke;
    }
    else
    {
        const bool dark = gui::Application::isDarkMode();
        p.text = td::ColorID::SysText;
        p.muted = dark ? td::ColorID::Silver    : td::ColorID::DimGray;
        p.grid  = dark ? td::ColorID::DimGray   : td::ColorID::LightGray;
        p.axis  = dark ? td::ColorID::Gray      : td::ColorID::DimGray;
        p.bg    = td::ColorID::SysCtrlBack;
        p.card  = dark ? td::ColorID::ObsidianGray : td::ColorID::SysBackAlt1;
    }
    p.accent = td::ColorID::DodgerBlue;
    p.ok     = td::ColorID::MediumSeaGreen;
    p.warn   = td::ColorID::DarkOrange;
    p.err    = td::ColorID::Crimson;
    return p;
}

static const td::ColorID GEN_COLOR[3] = {
    td::ColorID::SteelBlue, td::ColorID::DarkOrange, td::ColorID::ForestGreen
};
inline td::ColorID genColor(size_t g) { return GEN_COLOR[g % 3]; }
static const td::ColorID WIND_COLOR = td::ColorID::MediumSeaGreen;

struct Series
{
    std::vector<double> x, y;
    td::ColorID color = td::ColorID::SteelBlue;
    float width = 2.0f;
    td::LinePattern pattern = td::LinePattern::Solid;
    std::string legend;
};

inline std::string num(double v, int prec) { return fmt(v, prec); }

inline double niceStep(double span, int nTarget)
{
    if (span <= 0.0) return 1.0;
    const double raw = span / nTarget;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / mag;
    double nf;
    if      (f < 1.5) nf = 1.0;
    else if (f < 3.0) nf = 2.0;
    else if (f < 7.0) nf = 5.0;
    else              nf = 10.0;
    return nf * mag;
}

inline int decimalsFor(double step)
{
    if (step <= 0.0 || step >= 1.0) return 0;
    return std::min(4, static_cast<int>(std::ceil(-std::log10(step))));
}

struct Axes
{
    gui::Rect plot;
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    double px(double x) const { return plot.left + (x - xMin) / (xMax - xMin) * plot.width(); }
    double py(double y) const { return plot.bottom - (y - yMin) / (yMax - yMin) * plot.height(); }
    gui::Point p(double x, double y) const { return gui::Point(px(x), py(y)); }
};

inline void niceRange(double& lo, double& hi, double& step, bool zeroBased, int nTicks = 5)
{
    if (zeroBased) lo = std::min(lo, 0.0);
    if (hi <= lo) hi = lo + 1.0;
    step = niceStep(hi - lo, nTicks);
    lo = std::floor(lo / step) * step;
    hi = std::ceil (hi / step) * step;
    if (hi - lo < step) hi = lo + step;
}

struct Margins { double left = 64, right = 18, top = 38, bottom = 46; };

// Panel border, title, frame, gridlines, ticks, axis labels. Fills ax.plot.
inline void frame(const gui::Rect& panel, const std::string& title,
                  const std::string& xLabel, const std::string& yLabel,
                  Axes& ax, double xStep, double yStep, const Palette& pal,
                  const Margins& m = Margins())
{
    using namespace prim;

    roundedPanel(panel, 8.0, pal.bg, pal.grid);

    ax.plot = gui::Rect(panel.left + m.left, panel.top + m.top,
                        panel.right - m.right, panel.bottom - m.bottom);
    if (ax.plot.width() < 20 || ax.plot.height() < 20) return;

    text(gui::Rect(ax.plot.left, panel.top + 6, panel.right - m.right, panel.top + m.top - 6),
         title, gui::Font::ID::SystemBold, pal.text);

    const int yDec = decimalsFor(yStep);
    for (double v = ax.yMin; v <= ax.yMax + yStep * 1e-6; v += yStep)
    {
        const double y = ax.py(v);
        line(gui::Point(ax.plot.left, y), gui::Point(ax.plot.right, y),
             pal.grid, 1.0f, td::LinePattern::Dot);
        text(gui::Rect(panel.left + 4, y - 10, ax.plot.left - 7, y + 10),
             num(v, yDec), gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Right);
    }

    const int xDec = decimalsFor(xStep);
    for (double v = ax.xMin; xStep > 0.0 && v <= ax.xMax + xStep * 1e-6; v += xStep)
    {
        const double x = ax.px(v);
        line(gui::Point(x, ax.plot.bottom), gui::Point(x, ax.plot.bottom + 4), pal.axis);
        text(gui::Rect(x - 34, ax.plot.bottom + 4, x + 34, ax.plot.bottom + 22),
             num(v, xDec), gui::Font::ID::SystemNormal, pal.muted,
             td::TextAlignment::Center, td::VAlignment::Top);
    }

    frameRect(ax.plot, pal.axis);

    text(gui::Rect(ax.plot.left, ax.plot.bottom + 22, ax.plot.right, panel.bottom - 4),
         xLabel, gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Center);
    text(gui::Rect(panel.left + 4, panel.top + 6, ax.plot.left - 7, panel.top + m.top - 6),
         yLabel, gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Right);
}

inline void drawSeries(const Axes& ax, const Series& s)
{
    std::vector<gui::Point> pts;
    pts.reserve(s.x.size());
    for (size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
        pts.push_back(ax.p(s.x[i], s.y[i]));
    prim::polyline(pts, s.color, s.width, s.pattern);
}

inline void legend(const Axes& ax, const std::vector<Series>& series, const Palette& pal)
{
    double y = ax.plot.top + 8;
    for (const auto& s : series)
    {
        if (s.legend.empty()) continue;
        const double x0 = ax.plot.left + 10;
        prim::line(gui::Point(x0, y + 8), gui::Point(x0 + 24, y + 8), s.color, s.width, s.pattern);
        prim::text(gui::Rect(x0 + 30, y, x0 + 260, y + 16), s.legend, gui::Font::ID::SystemNormal, pal.text);
        y += 18;
    }
}

} // namespace chart

// =====================================================================
//  The four deliverable charts
// =====================================================================
namespace figs
{

using namespace chart;

inline void costCurves(const gui::Rect& panel, const std::vector<core::Generator>& gens,
                       const core::OPFResult& op, const Palette& pal)
{
    Axes ax;
    double xLo = 0.0, xHi = 0.0, yLo = 1e300, yHi = -1e300;
    for (const auto& g : gens)
    {
        const double i0 = 2.0 * g.a * g.Pmin + g.b, i1 = 2.0 * g.a * g.Pmax + g.b;
        xHi = std::max(xHi, g.Pmax);
        yLo = std::min(yLo, std::min(i0, i1));
        yHi = std::max(yHi, std::max(i0, i1));
    }
    const double lam = (op.feasible && !op.lambda.empty()) ? op.lambda[0] : 0.0;
    if (op.feasible) { yLo = std::min(yLo, lam); yHi = std::max(yHi, lam); }
    if (gens.empty()) { yLo = 0; yHi = 1; }

    double xStep, yStep;
    niceRange(xLo, xHi, xStep, true);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Incremental cost curves, \xce\xbb marked", "P [MW]", "$/MWh", ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;

    std::vector<Series> all;
    for (size_t g = 0; g < gens.size(); ++g)
    {
        auto curve = core::Sweep::costCurve(gens[g], 41);
        Series s;
        s.color = genColor(g);
        s.legend = std::string(GEN_NAME[g % N_GEN]) + "   a = " + num(gens[g].a, 4) + ",  b = " + num(gens[g].b, 2);
        for (const auto& pt : curve) { s.x.push_back(pt.P); s.y.push_back(pt.incrementalCost); }
        drawSeries(ax, s);
        all.push_back(std::move(s));
    }

    if (op.feasible)
    {
        prim::line(ax.p(ax.xMin, lam), ax.p(ax.xMax, lam), pal.err, 1.5f, td::LinePattern::Dash);
        prim::text(gui::Rect(ax.plot.right - 200, ax.py(lam) - 20, ax.plot.right - 6, ax.py(lam) - 2),
                   "\xce\xbb = " + num(lam, 3) + " $/MWh", gui::Font::ID::SystemBold, pal.err,
                   td::TextAlignment::Right);
        for (size_t g = 0; g < gens.size() && g < op.P.size(); ++g)
            prim::dot(ax.p(op.P[g], 2.0 * gens[g].a * op.P[g] + gens[g].b), 5.0, genColor(g), pal.text);
    }
    legend(ax, all, pal);
}

inline void dispatchBars(const gui::Rect& panel, const std::vector<core::Generator>& gens,
                         const core::OPFResult& op, const Palette& pal)
{
    Axes ax;
    double yLo = 0.0, yHi = 0.0, yStep;
    for (const auto& g : gens) yHi = std::max(yHi, g.Pmax);
    niceRange(yLo, yHi, yStep, true);
    ax.xMin = 0; ax.xMax = std::max<double>(1.0, static_cast<double>(gens.size()));
    ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Optimal dispatch vs generator capacity", "", "MW", ax, 0.0, yStep, pal);
    if (ax.plot.width() < 20 || gens.empty()) return;

    const double slot = ax.plot.width() / static_cast<double>(gens.size());
    const double bw = slot * 0.45;

    for (size_t g = 0; g < gens.size(); ++g)
    {
        const double cx = ax.plot.left + slot * (static_cast<double>(g) + 0.5);
        prim::frameRect(gui::Rect(cx - bw / 2, ax.py(gens[g].Pmax), cx + bw / 2, ax.py(0.0)), pal.axis);

        const double P = (op.feasible && g < op.P.size()) ? std::max(0.0, op.P[g]) : 0.0;
        prim::fillRect(gui::Rect(cx - bw / 2, ax.py(P), cx + bw / 2, ax.py(0.0)), genColor(g));

        std::string tag = GEN_NAME[g % N_GEN];
        if (op.feasible && g < op.genAtMax.size() && op.genAtMax[g]) tag += "  (at Pmax)";
        if (op.feasible && g < op.genAtMin.size() && op.genAtMin[g]) tag += "  (at Pmin)";
        prim::text(gui::Rect(cx - slot / 2, ax.plot.bottom + 4, cx + slot / 2, ax.plot.bottom + 22),
                   tag, gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Center, td::VAlignment::Top);
        prim::text(gui::Rect(cx - slot / 2, ax.py(P) - 22, cx + slot / 2, ax.py(P) - 2),
                   num(P, 1) + " MW", gui::Font::ID::SystemBold, pal.text,
                   td::TextAlignment::Center, td::VAlignment::Bottom);
    }

    if (!op.feasible)
        prim::text(ax.plot, "no feasible dispatch for these parameters",
                   gui::Font::ID::SystemBold, pal.err, td::TextAlignment::Center);
}

inline void lambdaVsDemand(const gui::Rect& panel, const std::vector<core::DemandPoint>& pts,
                           double nominalDemand, const Palette& pal)
{
    Series s;
    s.color = pal.err;
    s.legend = "system \xce\xbb";
    for (const auto& p : pts)
        if (p.feasible) { s.x.push_back(p.totalDemand); s.y.push_back(p.systemLambda); }

    Axes ax;
    double xLo = 0, xHi = 1, yLo = 0, yHi = 1;
    if (s.x.size() >= 2)
    {
        xLo = *std::min_element(s.x.begin(), s.x.end()); xHi = *std::max_element(s.x.begin(), s.x.end());
        yLo = *std::min_element(s.y.begin(), s.y.end()); yHi = *std::max_element(s.y.begin(), s.y.end());
    }
    double xStep, yStep;
    niceRange(xLo, xHi, xStep, false);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Marginal price vs total demand", "demand [MW]", "$/MWh", ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;
    if (s.x.size() < 2)
    {
        prim::text(ax.plot, "demand sweep infeasible", gui::Font::ID::SystemBold, pal.err, td::TextAlignment::Center);
        return;
    }

    if (nominalDemand >= ax.xMin && nominalDemand <= ax.xMax)
    {
        prim::line(ax.p(nominalDemand, ax.yMin), ax.p(nominalDemand, ax.yMax), pal.axis, 1.0f, td::LinePattern::Dash);
        prim::text(gui::Rect(ax.px(nominalDemand) + 5, ax.plot.bottom - 20, ax.px(nominalDemand) + 160, ax.plot.bottom - 4),
                   "current " + num(nominalDemand, 0) + " MW", gui::Font::ID::SystemNormal, pal.muted);
    }
    drawSeries(ax, s);
    legend(ax, { s }, pal);
}

inline void costVsWind(const gui::Rect& panel, const std::vector<core::WindPoint>& lossless,
                       const std::vector<core::WindPoint>& lossy, const Palette& pal)
{
    Series a, b;
    a.color = WIND_COLOR;              a.legend = "lossless";
    b.color = td::ColorID::SteelBlue;  b.legend = "with losses (r = 10% of x)";
    b.pattern = td::LinePattern::Dash;
    for (const auto& p : lossless) if (p.feasible) { a.x.push_back(p.penetrationPct); a.y.push_back(p.totalCost); }
    for (const auto& p : lossy)    if (p.feasible) { b.x.push_back(p.penetrationPct); b.y.push_back(p.totalCost); }

    double xLo = 0.0, xHi = 0.0, yLo = 1e300, yHi = -1e300;
    for (const Series* s : { &a, &b })
    {
        for (double v : s->x) xHi = std::max(xHi, v);
        for (double v : s->y) { yLo = std::min(yLo, v); yHi = std::max(yHi, v); }
    }
    if (yLo > yHi) { yLo = 0; yHi = 1; }
    Axes ax;
    double xStep, yStep;
    niceRange(xLo, xHi, xStep, true);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Total fuel cost vs wind penetration", "wind [% of demand]", "$/h", ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;

    if (a.x.size() >= 2) drawSeries(ax, a);
    if (b.x.size() >= 2) drawSeries(ax, b);
    if (a.x.size() < 2 && b.x.size() < 2)
        prim::text(ax.plot, "wind sweep infeasible", gui::Font::ID::SystemBold, pal.err, td::TextAlignment::Center);
    legend(ax, { a, b }, pal);
}

} // namespace figs

// =====================================================================
//  Network schematic drawing (shared by the Network tab and the Overview)
// =====================================================================
namespace netdraw
{

using chart::Palette;

inline gui::Point busPos(size_t bus, const gui::Rect& area)
{
    static const double NX[5] = { 0.18, 0.18, 0.82, 0.50, 0.82 };
    static const double NY[5] = { 0.14, 0.58, 0.58, 0.94, 0.14 };
    const size_t b = std::min<size_t>(bus, 4);
    return gui::Point(area.left + NX[b] * area.width(), area.top + NY[b] * area.height());
}

inline void arrowHead(const gui::Point& a, const gui::Point& b, double t, td::ColorID c, double h = 10, double w = 5.5)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return;
    const double ux = dx / len, uy = dy / len;
    const gui::Point tip(a.x + dx * t, a.y + dy * t);
    prim::polygon({ tip,
                    gui::Point(tip.x - ux * h + uy * w, tip.y - uy * h - ux * w),
                    gui::Point(tip.x - ux * h - uy * w, tip.y - uy * h + ux * w) }, c, c);
}

inline void turbine(const gui::Point& c, double r, td::ColorID color, td::ColorID hub)
{
    for (int k = 0; k < 3; ++k)
    {
        const double ang = -1.5708 + k * 2.0944;
        prim::line(c, gui::Point(c.x + r * std::cos(ang), c.y + r * std::sin(ang)), color, 3.0f);
    }
    prim::dot(c, r * 0.25, hub, hub);
}

// compact=true draws only nodes, lines and a few labels (Overview preview)
inline void draw(const gui::Rect& area, const core::Network& net, const core::OPFResult& res,
                 bool hasResult, double windFrac, const Palette& pal, bool compact)
{
    const auto& buses = net.getBuses();
    const auto& lines = net.getLines();
    const double R  = compact ? 11 : 17;          // bus radius
    const double GR = compact ? 9  : 14;          // generator disc radius
    const double GD = compact ? 34 : 56;          // generator offset from bus
    const gui::Font::ID fSmall = compact ? gui::Font::ID::SystemSmaller : gui::Font::ID::SystemNormal;
    const gui::Font::ID fBold  = compact ? gui::Font::ID::SystemSmallerBold : gui::Font::ID::SystemBold;

    for (size_t l = 0; l < lines.size(); ++l)
    {
        const auto& ln = lines[l];
        const gui::Point a = busPos(ln.from, area), b = busPos(ln.to, area);
        const bool atLimit = hasResult && l < res.mu.size() && res.mu[l] != 0.0;
        const td::ColorID c = atLimit ? pal.err : pal.axis;
        prim::line(a, b, c, atLimit ? 3.5f : (compact ? 1.5f : 2.5f));

        if (hasResult && l < res.flow.size())
        {
            const double f = res.flow[l];
            if (f >= 0) arrowHead(a, b, 0.62, c); else arrowHead(b, a, 0.62, c);
            if (compact) continue;
            const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
            const double dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            const double ox = -dy / len * 15, oy = dx / len * 15;
            prim::text(gui::Rect(mx + ox - 60, my + oy - 9, mx + ox + 60, my + oy + 9),
                       "L" + std::to_string(l) + "   " + fmt(std::fabs(f), 1) + " MW",
                       atLimit ? fBold : fSmall, atLimit ? pal.err : pal.text, td::TextAlignment::Center);
        }
        else if (!compact)
            prim::text(gui::Rect((a.x + b.x) / 2 - 20, (a.y + b.y) / 2 - 9, (a.x + b.x) / 2 + 20, (a.y + b.y) / 2 + 9),
                       "L" + std::to_string(l), fSmall, pal.muted, td::TextAlignment::Center);
    }

    for (size_t i = 0; i < buses.size(); ++i)
    {
        const auto& bus = buses[i];
        const gui::Point p = busPos(i, area);
        const bool left = p.x < area.left + area.width() / 2;

        for (int g : bus.genIndices)
        {
            const double gx = p.x + (left ? -GD : GD);
            prim::line(p, gui::Point(gx, p.y), pal.axis, 2.0f);
            prim::dot(gui::Point(gx, p.y), GR, chart::genColor(static_cast<size_t>(g)), pal.text);
            prim::text(gui::Rect(gx - GR, p.y - GR, gx + GR, p.y + GR), "G" + std::to_string(g + 1),
                       fBold, td::ColorID::White, td::TextAlignment::Center);
            if (hasResult && static_cast<size_t>(g) < res.P.size())
            {
                const gui::Rect r = left ? gui::Rect(gx - 100, p.y + GR + 2, gx + GR + 4, p.y + GR + 18)
                                         : gui::Rect(gx - GR - 4, p.y + GR + 2, gx + 100, p.y + GR + 18);
                prim::text(r, fmt(res.P[g], 1) + " MW", fSmall, pal.text,
                           left ? td::TextAlignment::Right : td::TextAlignment::Left);
            }
        }

        if (bus.windCap > 0.0)
        {
            const double wx = p.x + (left ? -GD : GD), wy = p.y;
            prim::line(p, gui::Point(wx, wy), pal.axis, 2.0f);
            turbine(gui::Point(wx, wy), GR + 2, chart::WIND_COLOR, pal.text);
            if (!compact || hasResult)
            {
                const double windMW = hasResult ? res.windUsed : windFrac * bus.windCap;
                prim::text(gui::Rect(wx - 90, wy - GR - 24, wx + 90, wy - GR - 6),
                           "wind " + fmt(windMW, 1) + " / " + fmt(bus.windCap, 0) + " MW",
                           fSmall, pal.text, td::TextAlignment::Center);
            }
        }

        if (bus.demand > 0.0)
        {
            prim::line(p, gui::Point(p.x, p.y + R + 14), pal.axis, 2.0f);
            arrowHead(p, gui::Point(p.x, p.y + R + 22), 1.0, pal.axis);
            prim::text(gui::Rect(p.x + 12, p.y + R - 2, p.x + 160, p.y + R + 16),
                       "load " + fmt(bus.demand, 0) + " MW", fSmall, pal.text);
        }

        prim::dot(p, R, pal.bg, pal.text);
        prim::text(gui::Rect(p.x - R, p.y - R, p.x + R, p.y + R), std::to_string(i), fBold, pal.text, td::TextAlignment::Center);
        if (bus.isSlack && !compact)
            prim::text(gui::Rect(p.x - 40, p.y - R - 20, p.x + 40, p.y - R - 4), "slack", fSmall, pal.muted, td::TextAlignment::Center);

        if (hasResult && !compact && i < res.lambda.size())
        {
            const std::string lam = "\xce\xbb = " + fmt(res.lambda[i], 3);
            gui::Rect r; td::TextAlignment al;
            if (bus.demand > 0.0) { r = gui::Rect(p.x - 150, p.y - 9, p.x - R - 6, p.y + 9);      al = td::TextAlignment::Right; }
            else if (left)        { r = gui::Rect(p.x - 10, p.y + R + 2, p.x + 130, p.y + R + 18); al = td::TextAlignment::Left; }
            else                  { r = gui::Rect(p.x - 130, p.y + R + 2, p.x + 10, p.y + R + 18); al = td::TextAlignment::Right; }
            prim::text(r, lam, fBold, pal.err, al);
        }
    }
}

inline void legend(const gui::Rect& r, const Palette& pal)
{
    double x = r.left;
    const double y = (r.top + r.bottom) / 2;
    auto item = [&](std::function<void()> glyph, const std::string& label, double w)
    {
        glyph();
        prim::text(gui::Rect(x + 22, y - 9, x + 22 + w, y + 9), label, gui::Font::ID::SystemSmaller, pal.muted);
        x += 22 + w + 16;
    };
    item([&] { prim::dot(gui::Point(x + 8, y), 7, chart::genColor(0), pal.text); }, "generator", 66);
    item([&] { turbine(gui::Point(x + 8, y), 8, chart::WIND_COLOR, pal.text); }, "wind", 40);
    item([&] { prim::line(gui::Point(x, y), gui::Point(x + 16, y), pal.err, 3.5f); }, "line at its limit", 100);
    item([&] { prim::line(gui::Point(x, y), gui::Point(x + 16, y), pal.axis, 2.0f); arrowHead(gui::Point(x, y), gui::Point(x + 16, y), 1.0, pal.axis, 8, 4); },
         "direction of flow", 100);
    item([&] { prim::text(gui::Rect(x, y - 9, x + 18, y + 9), "\xce\xbb", gui::Font::ID::SystemBold, pal.err); },
         "locational marginal price", 150);
}

} // namespace netdraw

// =====================================================================
//  Canvases
// =====================================================================

// ---- small drawn icons (no image resources needed) -------------------
class IconCanvas : public gui::Canvas
{
public:
    enum class Kind { Dot, Gear, Turbine, Bars, Network, Divider };
private:
    Kind        _kind;
    td::ColorID _color;
protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        const chart::Palette pal = chart::palette(false);
        const gui::Point c(sz.width / 2, sz.height / 2);
        const double r = std::min(sz.width, sz.height) / 2 - 2;
        switch (_kind)
        {
        case Kind::Dot:
            prim::dot(c, r, _color, _color);
            break;
        case Kind::Gear:
            for (int k = 0; k < 8; ++k)
            {
                const double a = k * 0.7854;
                prim::line(gui::Point(c.x + std::cos(a) * r * 0.55, c.y + std::sin(a) * r * 0.55),
                           gui::Point(c.x + std::cos(a) * r,        c.y + std::sin(a) * r), _color, 3.0f);
            }
            prim::dot(c, r * 0.55, _color, _color);
            prim::dot(c, r * 0.22, pal.bg, pal.bg);
            break;
        case Kind::Turbine:
            netdraw::turbine(c, r, _color, pal.text);
            break;
        case Kind::Bars:
        {
            const double w = sz.width / 5.0;
            const double hs[3] = { 0.45, 0.8, 0.6 };
            for (int k = 0; k < 3; ++k)
                prim::fillRect(gui::Rect(2 + k * (w + 2), sz.height - 2 - hs[k] * (sz.height - 4),
                                         2 + k * (w + 2) + w, sz.height - 2), _color);
            break;
        }
        case Kind::Network:
        {
            const gui::Point a(c.x - r * 0.8, c.y - r * 0.6), b(c.x + r * 0.8, c.y - r * 0.6), d(c.x, c.y + r * 0.8);
            prim::line(a, b, _color, 2.0f); prim::line(b, d, _color, 2.0f); prim::line(d, a, _color, 2.0f);
            for (const auto& p : { a, b, d }) prim::dot(p, 3.5, _color, _color);
            break;
        }
        case Kind::Divider:
            prim::line(gui::Point(0, sz.height / 2), gui::Point(sz.width, sz.height / 2), pal.grid, 1.0f);
            break;
        }
    }
public:
    static const td::WORD SIZE = 22;
    IconCanvas(Kind kind, td::ColorID color = td::ColorID::DodgerBlue, td::WORD w = SIZE, td::WORD h = SIZE)
        : gui::Canvas(), _kind(kind), _color(color)
    {
        setFixedWidth(w);
        setFixedHeight(h);
        setSizeLimits(w, gui::Control::Limit::Fixed, h, gui::Control::Limit::Fixed);
    }
};

// ---- application header strip ----------------------------------------
class HeaderCanvas : public gui::Canvas
{
    std::string _scenario = SCENARIO_NAME[0];
    std::string _status   = "not solved";
    td::ColorID _statusColor = td::ColorID::Gray;
protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        const chart::Palette pal = chart::palette(false);
        const double h = sz.height;

        // bolt icon in a rounded accent square
        const gui::Rect box(14, h / 2 - 17, 48, h / 2 + 17);
        prim::roundedPanel(box, 7, pal.accent, pal.accent);
        prim::polygon({ gui::Point(33, box.top + 5), gui::Point(24, box.top + 20), gui::Point(31, box.top + 20),
                        gui::Point(29, box.bottom - 5), gui::Point(38, box.top + 14), gui::Point(31, box.top + 14) },
                      td::ColorID::White, td::ColorID::White);

        prim::text(gui::Rect(60, 4, 400, h / 2 + 2), "Economic Dispatch", gui::Font::ID::SystemLargerBold, pal.text,
                   td::TextAlignment::Left, td::VAlignment::Bottom);
        prim::text(gui::Rect(60, h / 2 + 1, 500, h - 4), "DC-OPF with Wind Integration", gui::Font::ID::SystemNormal, pal.muted,
                   td::TextAlignment::Left, td::VAlignment::Top);

        // right side: scenario | status
        const double xr = sz.width - 16;
        prim::text(gui::Rect(xr - 420, 0, xr - 200, h), "Scenario:  " + _scenario, gui::Font::ID::SystemNormal, pal.muted,
                   td::TextAlignment::Right);
        prim::dot(gui::Point(xr - 180, h / 2), 5, _statusColor, _statusColor);
        prim::text(gui::Rect(xr - 170, 0, xr, h), "Status:  " + _status, gui::Font::ID::SystemBold, pal.text);

        prim::line(gui::Point(0, h - 1), gui::Point(sz.width, h - 1), pal.grid, 1.0f);
    }
public:
    static const td::WORD HEIGHT = 58;
    HeaderCanvas() : gui::Canvas()
    {
        setFixedHeight(HEIGHT);
        setSizeLimits(0, gui::Control::Limit::None, HEIGHT, gui::Control::Limit::Fixed);
    }
    void set(const std::string& scenario, const std::string& status, td::ColorID c)
    {
        _scenario = scenario; _status = status; _statusColor = c;
        reDraw();
    }
};

// ---- results cards on the Parameters tab -----------------------------
class ResultsCanvas : public gui::Canvas
{
    CaseParams      _case;
    core::OPFResult _res;
    bool _hasResult = false;
    std::string _message = "Press Solve to compute the optimal dispatch.";
    td::ColorID _messageColor = td::ColorID::Gray;

    static void card(const gui::Rect& r, const std::string& title, const std::string& value,
                     const std::string& sub, const chart::Palette& pal, td::ColorID valueColor)
    {
        prim::roundedPanel(r, 8, pal.card, pal.grid);
        prim::text(gui::Rect(r.left + 12, r.top + 6, r.right - 12, r.top + 24), title, gui::Font::ID::SystemSmaller, pal.muted);
        prim::text(gui::Rect(r.left + 12, r.top + 24, r.right - 12, r.top + 50), value, gui::Font::ID::SystemLargerBold, valueColor);
        if (!sub.empty())
            prim::text(gui::Rect(r.left + 12, r.top + 50, r.right - 12, r.bottom - 6), sub, gui::Font::ID::SystemSmaller, pal.muted,
                       td::TextAlignment::Left, td::VAlignment::Top);
    }

protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        const chart::Palette pal = chart::palette(false);

        // status line
        prim::dot(gui::Point(12, 14), 6, _messageColor, _messageColor);
        prim::text(gui::Rect(26, 2, sz.width, 26), _message, gui::Font::ID::SystemBold, pal.text);

        if (!_hasResult) return;

        const double gap = 10, top = 36;
        const double cw = (sz.width - gap) / 2;
        const double ch = (sz.height - top - 2 * gap) / 3;
        auto cell = [&](int col, int row)
        {
            const double x = col * (cw + gap), y = top + row * (ch + gap);
            return gui::Rect(x, y, x + cw, y + ch);
        };

        // dispatch
        {
            std::string v, sub;
            for (size_t i = 0; i < _res.P.size(); ++i)
            {
                v += std::string(i ? "   " : "") + GEN_NAME[i % N_GEN] + " " + fmt(_res.P[i], 1);
                if (i < _res.genAtMax.size() && _res.genAtMax[i]) sub += std::string(GEN_NAME[i % N_GEN]) + " at Pmax  ";
                if (i < _res.genAtMin.size() && _res.genAtMin[i]) sub += std::string(GEN_NAME[i % N_GEN]) + " at Pmin  ";
            }
            card(cell(0, 0), "OPTIMAL DISPATCH  [MW]", v, sub.empty() ? "no generator at a bound" : sub, pal, pal.text);
        }
        // energy balance
        {
            double sum = 0.0;
            for (double p : _res.P) sum += p;
            std::string sub = "thermal " + fmt(sum, 1) + " + wind " + fmt(_res.windUsed, 1) + " = demand " + fmt(_case.demandMW(), 0);
            if (_case.useLosses) sub += " + losses " + fmt(_res.totalLoss, 2);
            card(cell(1, 0), "ENERGY BALANCE  [MW]", fmt(sum + _res.windUsed, 1) + " MW supplied", sub, pal, pal.text);
        }
        // marginal price
        {
            bool uniform = true;
            for (size_t i = 1; i < _res.lambda.size(); ++i)
                if (std::fabs(_res.lambda[i] - _res.lambda[0]) > 1e-6) { uniform = false; break; }
            std::string sub;
            if (uniform) sub = "uniform at every bus (no congestion)";
            else for (size_t i = 0; i < _res.lambda.size(); ++i) sub += "bus" + std::to_string(i) + " " + fmt(_res.lambda[i], 3) + "  ";
            card(cell(0, 1), "SYSTEM MARGINAL PRICE  \xce\xbb", "$" + fmt(_res.lambda.empty() ? 0.0 : _res.lambda[0], 3) + " / MWh", sub, pal, pal.err);
        }
        // total fuel cost
        card(cell(1, 1), "TOTAL FUEL COST", "$" + fmtThousands(_res.totalCost, 1) + " / h",
             _case.useLosses ? "with quadratic branch losses" : "lossless DC-OPF", pal, pal.accent);
        // KKT residual
        {
            std::ostringstream os;
            os << std::scientific << std::setprecision(2) << _res.residual;
            card(cell(0, 2), "KKT RESIDUAL  max |A x - b|", os.str(),
                 _res.lossIters > 0 ? std::to_string(_res.lossIters) + " loss-linearisation iterations" : "direct sparse LU, active-set", pal, pal.text);
        }
        // wind
        {
            const double pct = _case.demandMW() > 0 ? 100.0 * _res.windUsed / _case.demandMW() : 0.0;
            card(cell(1, 2), "WIND GENERATION", fmt(_res.windUsed, 1) + " MW",
                 fmt(pct, 1) + " % of demand,  " + fmt(100.0 * _case.windFrac, 0) + " % of installed " + fmt(WIND_INSTALLED_MW, 0) + " MW",
                 pal, chart::WIND_COLOR);
        }
    }
public:
    ResultsCanvas() : gui::Canvas() {}

    void setMessage(const std::string& m, td::ColorID c) { _message = m; _messageColor = c; _hasResult = false; reDraw(); }
    void setResult(const CaseParams& cp, const core::OPFResult& r)
    {
        _case = cp; _res = r; _hasResult = r.feasible;
        const chart::Palette pal = chart::palette(false);
        if (r.feasible) { _message = "Optimal solution found"; _messageColor = pal.ok; }
        else            { _message = "No feasible dispatch";    _messageColor = pal.err; }
        reDraw();
    }
};

// ---- Network tab canvas ----------------------------------------------
class NetworkCanvas : public gui::Canvas
{
    CaseParams      _case;
    core::Network   _net;
    core::OPFResult _res;
    bool _hasResult = false;
protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        if (sz.width < 200 || sz.height < 200) return;
        const chart::Palette pal = chart::palette(false);

        prim::text(gui::Rect(12, 6, sz.width / 2, 30),
                   "5-bus DC network" + std::string(_hasResult ? (_case.useLosses ? "   \xc2\xb7   with losses" : "   \xc2\xb7   lossless") : ""),
                   gui::Font::ID::SystemLargerBold, pal.text);
        prim::text(gui::Rect(12, 30, sz.width - 12, 50),
                   _hasResult ? "demand " + fmt(_case.demandMW(), 0) + " MW   \xc2\xb7   wind " + fmt(_res.windUsed, 1) + " MW   \xc2\xb7   total cost $"
                                + fmtThousands(_res.totalCost, 1) + " / h"
                              : "press Solve on the Parameters tab to annotate flows and prices",
                   gui::Font::ID::SystemNormal, pal.muted);

        const double side = std::min(sz.width - 160.0, sz.height - 140.0);
        const double cx = sz.width / 2, cy = 60 + (sz.height - 100) / 2;
        const gui::Rect area(cx - side * 0.62, cy - side * 0.45, cx + side * 0.62, cy + side * 0.45);
        netdraw::draw(area, _net, _res, _hasResult, _case.windFrac, pal, false);

        netdraw::legend(gui::Rect(12, sz.height - 30, sz.width - 12, sz.height - 6), pal);
    }
public:
    NetworkCanvas() : gui::Canvas(), _net(core::buildTestNetwork(0.0, WIND_INSTALLED_MW, LIMIT_RELAX)) {}

    void setCase(const CaseParams& cp) { _case = cp; _net = buildCaseNetwork(cp); _hasResult = false; reDraw(); }
    void setResult(const CaseParams& cp, const core::OPFResult& r)
    {
        _case = cp; _net = buildCaseNetwork(cp); _res = r; _hasResult = r.feasible;
        reDraw();
    }
};

// ---- Overview dashboard ----------------------------------------------
class OverviewCanvas : public gui::Canvas
{
    CaseParams      _case;
    core::Network   _net;
    core::OPFResult _res;
    bool _hasResult = false;
    bool _forExport = false;

    static void kpi(const gui::Rect& r, const std::string& title, const std::string& value, const std::string& unit,
                    const chart::Palette& pal, td::ColorID valueColor)
    {
        prim::roundedPanel(r, 10, pal.card, pal.grid);
        prim::text(gui::Rect(r.left + 14, r.top + 8, r.right - 14, r.top + 26), title, gui::Font::ID::SystemSmaller, pal.muted);
        prim::text(gui::Rect(r.left + 14, r.top + 26, r.right - 14, r.top + 60), value, gui::Font::ID::SystemLargestBold, valueColor,
                   td::TextAlignment::Left, td::VAlignment::Center);
        prim::text(gui::Rect(r.left + 14, r.top + 60, r.right - 14, r.bottom - 6), unit, gui::Font::ID::SystemSmaller, pal.muted,
                   td::TextAlignment::Left, td::VAlignment::Top);
    }

protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        if (sz.width < 300 || sz.height < 300) return;
        const chart::Palette pal = chart::palette(_forExport);
        if (_forExport) prim::fillRect(gui::Rect(0, 0, sz.width, sz.height), pal.bg);

        const double M = 14;

        // ---- status banner
        const gui::Rect banner(M, M, sz.width - M, M + 52);
        prim::roundedPanel(banner, 10, pal.card, _hasResult ? pal.ok : pal.grid);
        const td::ColorID sc = _hasResult ? pal.ok : pal.muted;
        prim::dot(gui::Point(banner.left + 26, (banner.top + banner.bottom) / 2), 9, sc, sc);
        if (_hasResult)
        {
            const gui::Point c(banner.left + 26, (banner.top + banner.bottom) / 2);
            prim::polyline({ gui::Point(c.x - 4.5, c.y), gui::Point(c.x - 1.5, c.y + 3.5), gui::Point(c.x + 5, c.y - 4) },
                           td::ColorID::White, 2.5f);
        }
        prim::text(gui::Rect(banner.left + 46, banner.top, banner.right - 300, banner.bottom),
                   _hasResult ? "Optimal solution found" : "No solution yet \xe2\x80\x94 choose a scenario or press Solve on the Parameters tab",
                   gui::Font::ID::SystemLargerBold, pal.text);
        prim::text(gui::Rect(banner.right - 300, banner.top, banner.right - 16, banner.bottom),
                   "Scenario:  " + std::string(SCENARIO_NAME[static_cast<int>(_case.scenario)]) +
                   (_case.useLosses ? "   \xc2\xb7   with losses" : "   \xc2\xb7   lossless"),
                   gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Right);

        if (!_hasResult) return;

        // ---- KPI tiles
        double sumP = 0.0;
        for (double p : _res.P) sumP += p;
        const double kTop = banner.bottom + 12, kH = 86;
        const int nK = 5;
        const double kW = (sz.width - 2 * M - (nK - 1) * 10) / nK;
        auto tile = [&](int i) { const double x = M + i * (kW + 10); return gui::Rect(x, kTop, x + kW, kTop + kH); };
        kpi(tile(0), "OPTIMAL FUEL COST", "$" + fmtThousands(_res.totalCost, 1), "per hour", pal, pal.accent);
        kpi(tile(1), "TOTAL GENERATION", fmt(sumP + _res.windUsed, 1) + " MW",
            "thermal " + fmt(sumP, 1) + "  +  wind " + fmt(_res.windUsed, 1), pal, pal.text);
        kpi(tile(2), "WIND GENERATION", fmt(_res.windUsed, 1) + " MW",
            fmt(_case.demandMW() > 0 ? 100.0 * _res.windUsed / _case.demandMW() : 0.0, 1) + " % of demand", pal, chart::WIND_COLOR);
        kpi(tile(3), "SYSTEM MARGINAL PRICE  \xce\xbb", "$" + fmt(_res.lambda.empty() ? 0.0 : _res.lambda[0], 3), "per MWh, slack bus", pal, pal.err);
        kpi(tile(4), "TRANSMISSION LOSSES", fmt(_res.totalLoss, 2) + " MW",
            _case.useLosses ? "quadratic branch losses, r = 10% of x" : "loss model off", pal, pal.text);

        // ---- lower row: dispatch bars (left) + network preview (right)
        const double rTop = kTop + kH + 12, rBot = sz.height - M - 40;
        const gui::Rect leftR(M, rTop, M + (sz.width - 2 * M) * 0.44 - 5, rBot);
        const gui::Rect rightR(leftR.right + 10, rTop, sz.width - M, rBot);

        // dispatch bars incl. wind
        {
            prim::roundedPanel(leftR, 10, pal.card, pal.grid);
            prim::text(gui::Rect(leftR.left + 14, leftR.top + 8, leftR.right - 14, leftR.top + 28), "OPTIMAL DISPATCH", gui::Font::ID::SystemSmaller, pal.muted);
            const int n = static_cast<int>(_res.P.size()) + 1;
            double maxV = 1.0;
            for (size_t g = 0; g < _case.gens.size(); ++g) maxV = std::max(maxV, _case.gens[g].Pmax);
            maxV = std::max(maxV, WIND_INSTALLED_MW);
            const gui::Rect plot(leftR.left + 20, leftR.top + 40, leftR.right - 20, leftR.bottom - 34);
            const double slot = plot.width() / n, bw = slot * 0.5;
            prim::line(gui::Point(plot.left, plot.bottom), gui::Point(plot.right, plot.bottom), pal.axis, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const bool wind = (i == n - 1);
                const double v   = wind ? _res.windUsed : std::max(0.0, _res.P[i]);
                const double cap = wind ? WIND_INSTALLED_MW : (static_cast<size_t>(i) < _case.gens.size() ? _case.gens[i].Pmax : maxV);
                const td::ColorID c = wind ? chart::WIND_COLOR : chart::genColor(static_cast<size_t>(i));
                const double cx = plot.left + slot * (i + 0.5);
                const double yv = plot.bottom - v / maxV * plot.height();
                prim::frameRect(gui::Rect(cx - bw / 2, plot.bottom - cap / maxV * plot.height(), cx + bw / 2, plot.bottom), pal.axis);
                prim::fillRect(gui::Rect(cx - bw / 2, yv, cx + bw / 2, plot.bottom), c);
                prim::text(gui::Rect(cx - slot / 2, yv - 20, cx + slot / 2, yv - 2),
                           fmt(v, 1), gui::Font::ID::SystemBold, pal.text, td::TextAlignment::Center, td::VAlignment::Bottom);
                prim::text(gui::Rect(cx - slot / 2, plot.bottom + 4, cx + slot / 2, plot.bottom + 22),
                           wind ? "Wind" : GEN_NAME[i % N_GEN], gui::Font::ID::SystemNormal, pal.muted, td::TextAlignment::Center);
            }
            prim::text(gui::Rect(leftR.left + 14, leftR.bottom - 22, leftR.right - 14, leftR.bottom - 6),
                       "bars: output [MW], outline: capacity", gui::Font::ID::SystemSmaller, pal.muted, td::TextAlignment::Right);
        }
        // network preview
        {
            prim::roundedPanel(rightR, 10, pal.card, pal.grid);
            prim::text(gui::Rect(rightR.left + 14, rightR.top + 8, rightR.right - 14, rightR.top + 28), "NETWORK", gui::Font::ID::SystemSmaller, pal.muted);
            const gui::Rect area(rightR.left + 70, rightR.top + 50, rightR.right - 70, rightR.bottom - 40);
            netdraw::draw(area, _net, _res, true, _case.windFrac, pal, true);
            prim::text(gui::Rect(rightR.left + 14, rightR.bottom - 22, rightR.right - 14, rightR.bottom - 6),
                       "see the Network tab for flows and prices", gui::Font::ID::SystemSmaller, pal.muted, td::TextAlignment::Right);
        }

        // ---- footer: energy balance + residual
        std::ostringstream res;
        res << std::scientific << std::setprecision(2) << _res.residual;
        std::string bal = "Energy balance:   thermal " + fmt(sumP, 1) + " MW  +  wind " + fmt(_res.windUsed, 1)
                        + " MW  =  demand " + fmt(_case.demandMW(), 0) + " MW";
        if (_case.useLosses) bal += "  +  losses " + fmt(_res.totalLoss, 2) + " MW";
        prim::text(gui::Rect(M + 4, rBot + 8, sz.width / 2 + 120, sz.height - M), bal, gui::Font::ID::SystemBold, pal.text);
        prim::text(gui::Rect(sz.width / 2 + 120, rBot + 8, sz.width - M - 4, sz.height - M),
                   "KKT residual " + res.str() + "   \xc2\xb7   sparse LU, active-set", gui::Font::ID::SystemSmaller, pal.muted,
                   td::TextAlignment::Right);
    }
public:
    OverviewCanvas() : gui::Canvas(), _net(core::buildTestNetwork(0.0, WIND_INSTALLED_MW, LIMIT_RELAX)) {}

    void setCase(const CaseParams& cp) { _case = cp; _net = buildCaseNetwork(cp); _hasResult = false; reDraw(); }
    void setResult(const CaseParams& cp, const core::OPFResult& r)
    {
        _case = cp; _net = buildCaseNetwork(cp); _res = r; _hasResult = r.feasible;
        reDraw();
    }
    bool exportPDF(const td::String& fileName)
    {
        _forExport = true;
        const bool ok = exportToPDF(fileName);
        _forExport = false;
        reDraw();
        return ok;
    }
};

// ---- Charts canvas ---------------------------------------------------
class ChartCanvas : public gui::Canvas
{
    CaseParams                     _case;
    core::OPFResult                _op;
    std::vector<core::DemandPoint> _demand;
    std::vector<core::WindPoint>   _windLossless;
    std::vector<core::WindPoint>   _windLossy;
    bool _forExport = false;

    void compute()
    {
        _op = solveCase(_case);
        core::Network lossless = buildCaseNetwork(_case, true);
        CaseParams lossyCase = _case; lossyCase.useLosses = true;
        core::Network lossy = buildCaseNetwork(lossyCase);
        core::Sweep sweep;
        _demand       = sweep.demandSweep(_case.useLosses ? lossy : lossless, _case.gens, 0.4, 1.4, 21, _case.useLosses);
        _windLossless = sweep.windSweep(lossless, _case.gens, 21, false);
        _windLossy    = sweep.windSweep(lossy, _case.gens, 11, true);
    }
protected:
    void onDraw(const gui::Rect&) override
    {
        gui::Size sz;
        getSize(sz);
        if (sz.width < 120 || sz.height < 120) return;
        const chart::Palette pal = chart::palette(_forExport);
        if (_forExport) prim::fillRect(gui::Rect(0, 0, sz.width, sz.height), pal.bg);

        const double gap = 12;
        const double w = (sz.width - 3 * gap) / 2, h = (sz.height - 3 * gap) / 2;
        auto cell = [&](int col, int row)
        {
            const double x = gap + col * (w + gap), y = gap + row * (h + gap);
            return gui::Rect(x, y, x + w, y + h);
        };
        figs::costCurves    (cell(0, 0), _case.gens, _op, pal);
        figs::dispatchBars  (cell(1, 0), _case.gens, _op, pal);
        figs::lambdaVsDemand(cell(0, 1), _demand, _case.demandMW(), pal);
        figs::costVsWind    (cell(1, 1), _windLossless, _windLossy, pal);
    }
public:
    explicit ChartCanvas(const CaseParams& cp) : gui::Canvas(), _case(cp) { compute(); }
    void setCase(const CaseParams& cp) { _case = cp; compute(); reDraw(); }
    bool exportPDF(const td::String& fileName)
    {
        _forExport = true;
        const bool ok = exportToPDF(fileName);
        _forExport = false;
        reDraw();
        return ok;
    }
};

// =====================================================================
//  Tab views
// =====================================================================

// ---- Overview --------------------------------------------------------
class OverviewView : public gui::View
{
    gui::Label     _scenarioLbl;
    gui::ComboBox  _scenario;
    gui::Label     _hint;
    gui::Button    _btnExport;
    OverviewCanvas _canvas;
    std::function<void(Scenario)> _onScenario;

    void onExport()
    {
        gui::SaveFileDialog::show(this, "Export overview to PDF", "pdf", DLG_EXPORT_OVERVIEW,
            [this](gui::FileDialog* dlg)
            {
                if (dlg->getStatus() != gui::FileDialog::Status::OK) return;
                const bool ok = _canvas.exportPDF(dlg->getFileName());
                _hint.setTitle(ok ? "PDF written" : "PDF export failed");
                _hint.setTextColor(ok ? td::Accent::Success : td::Accent::Error);
            },
            "Export", "dispatch_overview.pdf");
    }
public:
    OverviewView()
        : gui::View(10, 8, 10, 10)
        , _scenarioLbl("Scenario")
        , _scenario("Preset operating scenarios; edits on the Parameters tab switch to Custom")
        , _hint("")
        , _btnExport("Export PDF...")
    {
        _scenarioLbl.setBold();
        for (const char* n : SCENARIO_NAME) _scenario.addItem(n);
        _scenario.selectIndex(0, false);
        _scenario.setSizeLimits(180, gui::Control::Limit::Fixed);
        _scenario.onChangedSelection([this]()
        {
            const int i = _scenario.getSelectedIndex();
            if (_onScenario && i >= 0) _onScenario(static_cast<Scenario>(i));
        });
        _btnExport.onClick([this]() { onExport(); });

        gui::HorizontalLayout* row = new gui::HorizontalLayout(5);
        row->append(_scenarioLbl, td::HAlignment::Left);
        row->append(_scenario,    td::HAlignment::Left);
        row->append(_hint,        td::HAlignment::Left);
        row->appendSpacer();
        row->append(_btnExport,   td::HAlignment::Right);
        row->setSpaceBetweenCells(10);

        gui::VerticalLayout* main = new gui::VerticalLayout(2);
        main->appendLayout(*row);
        main->append(_canvas);
        main->setSpaceBetweenCells(6);
        setLayout(main);
    }

    void onScenario(std::function<void(Scenario)> fn) { _onScenario = std::move(fn); }
    void showScenario(Scenario s) { _scenario.selectIndex(static_cast<int>(s), false); }
    OverviewCanvas& canvas() { return _canvas; }
};

// ---- Parameters ------------------------------------------------------
class ParametersView : public gui::View
{
    gui::Label _tableHeader;
    gui::Label _colDot, _colName, _colA, _colB, _colC, _colPmin, _colPmax;
    IconCanvas _icoGens, _icoWind, _icoDemand, _icoResults;
    IconCanvas* _genDot[N_GEN];

    gui::Label*       _rowLbl[N_GEN];
    gui::NumericEdit* _a[N_GEN];
    gui::NumericEdit* _b[N_GEN];
    gui::NumericEdit* _c[N_GEN];
    gui::NumericEdit* _pmin[N_GEN];
    gui::NumericEdit* _pmax[N_GEN];

    gui::Label        _demandHeader;
    gui::Label        _demandLbl;
    gui::NumericEdit* _demand;
    gui::Label        _demandUnit;

    gui::Label    _windHeader;
    gui::Label    _windLabel;
    gui::Slider   _windSlider;
    gui::Label    _windPct;
    gui::CheckBox _lossesBox;
    gui::Button   _btnSolve;
    gui::Button   _btnCharts;

    gui::Label    _resultsHeader;
    ResultsCanvas _results;

    double _windFrac  = 0.0;
    bool   _useLosses = false;

    // set by MainView
    std::function<void(const CaseParams&, const core::OPFResult&, bool showCharts)> _onSolved;
    std::function<void()> _onUserEdit;

    static const td::UINT2 FIELD_PX = 104;
    static const td::UINT2 NAME_PX  = 34;

    static bool variantToDouble(const td::Variant& v, double& out)
    {
        switch (v.getType())
        {
            case td::decimal0: out = v.dec0Val().getAsFloat(); return true;
            case td::decimal1: out = v.dec1Val().getAsFloat(); return true;
            case td::decimal2: out = v.dec2Val().getAsFloat(); return true;
            case td::decimal3: out = v.dec3Val().getAsFloat(); return true;
            case td::decimal4: out = v.dec4Val().getAsFloat(); return true;
            case td::real8:    out = v.r8Val();                 return true;
            case td::real4:    out = v.r4Val();                 return true;
            case td::int4:     out = v.i4Val();                 return true;
            default:           return false;
        }
    }

    static double readField(const gui::NumericEdit* field, double fallback)
    {
        td::Variant v;
        if (!field->getValue(v)) return fallback;
        double out = fallback;
        if (!variantToDouble(v, out)) return fallback;
        return out;
    }

    static gui::NumericEdit* makeField(td::DataType dt, const char* toolTip, td::UINT2 px)
    {
        gui::NumericEdit* e = new gui::NumericEdit(dt, gui::LineEdit::Messages::DoNotSend, false, toolTip);
        e->setMinValue(0.0);
        e->setHAlignment(td::HAlignment::Right);
        e->setSizeLimits(px, gui::Control::Limit::Fixed);
        return e;
    }

    void updateWindLabel()
    {
        std::ostringstream os;
        os << "penetration " << fmt(_windFrac * 100.0, 1) << " %   \xc2\xb7   installed " << fmt(WIND_INSTALLED_MW, 0)
           << " MW   \xc2\xb7   output " << fmt(_windFrac * WIND_INSTALLED_MW, 1) << " MW";
        _windLabel.setTitle(os.str().c_str());
        _windPct.setTitle((fmt(_windFrac * 100.0, 0) + " %").c_str());
    }

    void stale()
    {
        _results.setMessage("Parameters changed \xe2\x80\x94 press Solve to recompute.", td::ColorID::DarkOrange);
        if (_onUserEdit) _onUserEdit();
    }

    bool validate(const CaseParams& cp, std::string& err) const
    {
        for (int i = 0; i < N_GEN; ++i)
        {
            if (cp.gens[i].a <= 0.0)               { err = std::string(GEN_NAME[i]) + ": coefficient a must be > 0"; return false; }
            if (cp.gens[i].Pmin > cp.gens[i].Pmax) { err = std::string(GEN_NAME[i]) + ": Pmin cannot exceed Pmax"; return false; }
        }
        double totalPmax = 0.0, totalPmin = 0.0;
        for (const auto& g : cp.gens) { totalPmax += g.Pmax; totalPmin += g.Pmin; }
        const double netDemand = cp.demandMW() - cp.windFrac * WIND_INSTALLED_MW;
        if (cp.demandMW() <= 0.0)  { err = "demand must be positive"; return false; }
        if (totalPmax < netDemand) { err = "total capacity below demand \xe2\x80\x94 raise a Pmax or lower demand"; return false; }
        if (totalPmin > netDemand) { err = "total minimum output exceeds demand \xe2\x80\x94 lower a Pmin"; return false; }
        return true;
    }

    void solve(bool showCharts)
    {
        CaseParams cp = currentCase();
        std::string err;
        if (!validate(cp, err))
        {
            _results.setMessage(err, td::ColorID::Crimson);
            return;
        }
        core::OPFResult r = solveCase(cp);
        _results.setResult(cp, r);
        if (_onSolved) _onSolved(cp, r, showCharts);
    }

public:
    CaseParams currentCase() const
    {
        CaseParams cp;
        auto fallback = core::buildTestGenerators();
        for (int i = 0; i < N_GEN; ++i)
        {
            core::Generator g{};
            g.a    = readField(_a[i],    fallback[i].a);
            g.b    = readField(_b[i],    fallback[i].b);
            g.c    = readField(_c[i],    fallback[i].c);
            g.Pmin = readField(_pmin[i], fallback[i].Pmin);
            g.Pmax = readField(_pmax[i], fallback[i].Pmax);
            cp.gens.push_back(g);
        }
        cp.windFrac    = _windFrac;
        cp.demandScale = readField(_demand, TOTAL_DEMAND_MW) / TOTAL_DEMAND_MW;
        cp.useLosses   = _useLosses;
        return cp;
    }

    // Apply a preset (wind fraction, demand scale) without firing edit callbacks, then solve.
    void applyPreset(const ScenarioPreset& p)
    {
        _windFrac = p.windFrac;
        _windSlider.setValue(_windFrac * 100.0, false);
        _demand->setText(fmt(TOTAL_DEMAND_MW * p.demandScale, 1).c_str());
        updateWindLabel();
        solve(false);
    }

    void onSolved(std::function<void(const CaseParams&, const core::OPFResult&, bool)> fn) { _onSolved = std::move(fn); }
    void onUserEdit(std::function<void()> fn) { _onUserEdit = std::move(fn); }

    ParametersView()
        : gui::View(14, 12, 14, 12)
        , _tableHeader("Generator cost and capacity")
        , _colDot(""), _colName(""), _colA("a  [$/MW\xc2\xb2h]"), _colB("b  [$/MWh]"), _colC("c  [$/h]")
        , _colPmin("Pmin  [MW]"), _colPmax("Pmax  [MW]")
        , _icoGens(IconCanvas::Kind::Gear)
        , _icoWind(IconCanvas::Kind::Turbine, chart::WIND_COLOR)
        , _icoDemand(IconCanvas::Kind::Network)
        , _icoResults(IconCanvas::Kind::Bars)
        , _demandHeader("System demand")
        , _demandLbl("total load at bus 3")
        , _demandUnit("MW")
        , _windHeader("Wind generation")
        , _windLabel("")
        , _windPct("0 %")
        , _lossesBox("Include transmission losses (r = 10% of x)")
        , _btnSolve("Solve")
        , _btnCharts("Solve & show charts")
        , _resultsHeader("Results")
    {
        for (gui::Label* l : { &_tableHeader, &_demandHeader, &_windHeader, &_resultsHeader }) l->setBold();
        for (gui::Label* l : { &_colA, &_colB, &_colC, &_colPmin, &_colPmax })
        {
            l->setFont(gui::Font::ID::SystemSmallerBold);
            l->setSizeLimits(FIELD_PX, gui::Control::Limit::Fixed);
        }
        _colName.setSizeLimits(NAME_PX, gui::Control::Limit::Fixed);
        _colDot.setSizeLimits(IconCanvas::SIZE, gui::Control::Limit::Fixed);
        _windPct.setSizeLimits(48, gui::Control::Limit::Fixed);
        const td::UINT2 tableW = IconCanvas::SIZE + NAME_PX + 5 * FIELD_PX + 6 * 6;
        _windSlider.setSizeLimits(tableW - 56, gui::Control::Limit::Fixed);
        _btnSolve.setSizeLimits(150, gui::Control::Limit::Fixed, 40, gui::Control::Limit::Fixed);
        _btnCharts.setSizeLimits(210, gui::Control::Limit::Fixed, 40, gui::Control::Limit::Fixed);
        _btnSolve.setAsDefault();
        _results.setFixedWidth(560);
        _results.setFixedHeight(360);
        _results.setSizeLimits(560, gui::Control::Limit::Fixed, 360, gui::Control::Limit::Fixed);

        _demand = makeField(td::decimal1, "total system demand [MW]", FIELD_PX);

        _btnSolve.onClick([this]() { solve(false); });
        _btnCharts.onClick([this]() { solve(true); });
        _windSlider.onChangedValue([this]()
        {
            _windFrac = std::min(1.0, std::max(0.0, _windSlider.getValue() / 100.0));
            updateWindLabel();
            stale();
        });
        _lossesBox.onClick([this]() { _useLosses = _lossesBox.isChecked(); stale(); });

        auto headerRow = [](IconCanvas& ico, gui::Label& lbl) -> gui::HorizontalLayout*
        {
            gui::HorizontalLayout* h = new gui::HorizontalLayout(3);
            h->append(ico, td::HAlignment::Left, td::VAlignment::Center);
            h->append(lbl, td::HAlignment::Left, td::VAlignment::Center);
            h->appendSpacer();
            h->setSpaceBetweenCells(8);
            return h;
        };

        // ---- left column (15 cells)
        gui::VerticalLayout* left = new gui::VerticalLayout(15);
        left->setSpaceBetweenCells(4);
        left->appendLayout(*headerRow(_icoGens, _tableHeader));                 // 1

        gui::HorizontalLayout* colRow = new gui::HorizontalLayout(7);
        colRow->append(_colDot,  td::HAlignment::Left);
        colRow->append(_colName, td::HAlignment::Left);
        colRow->append(_colA,    td::HAlignment::Center);
        colRow->append(_colB,    td::HAlignment::Center);
        colRow->append(_colC,    td::HAlignment::Center);
        colRow->append(_colPmin, td::HAlignment::Center);
        colRow->append(_colPmax, td::HAlignment::Center);
        colRow->setSpaceBetweenCells(6);
        left->appendLayout(*colRow);                                            // 2

        for (int i = 0; i < N_GEN; ++i)                                         // 3..5
        {
            _genDot[i] = new IconCanvas(IconCanvas::Kind::Dot, chart::genColor(static_cast<size_t>(i)));
            _rowLbl[i] = new gui::Label(GEN_NAME[i]);
            _rowLbl[i]->setBold();
            _rowLbl[i]->setSizeLimits(NAME_PX, gui::Control::Limit::Fixed);
            _a[i]    = makeField(td::decimal4, "quadratic cost coefficient a", FIELD_PX);
            _b[i]    = makeField(td::decimal4, "linear cost coefficient b", FIELD_PX);
            _c[i]    = makeField(td::decimal2, "fixed cost coefficient c", FIELD_PX);
            _pmin[i] = makeField(td::decimal2, "minimum output [MW]", FIELD_PX);
            _pmax[i] = makeField(td::decimal2, "maximum output [MW]", FIELD_PX);

            gui::HorizontalLayout* row = new gui::HorizontalLayout(7);
            row->append(*_genDot[i], td::HAlignment::Left, td::VAlignment::Center);
            row->append(*_rowLbl[i], td::HAlignment::Left);
            row->append(*_a[i]);
            row->append(*_b[i]);
            row->append(*_c[i]);
            row->append(*_pmin[i]);
            row->append(*_pmax[i]);
            row->setSpaceBetweenCells(6);
            left->appendLayout(*row);
        }

        left->appendSpace(6);                                                   // 6
        left->appendLayout(*headerRow(_icoDemand, _demandHeader));              // 7
        gui::HorizontalLayout* demandRow = new gui::HorizontalLayout(4);
        demandRow->append(_demandLbl, td::HAlignment::Left);
        demandRow->append(*_demand,   td::HAlignment::Left);
        demandRow->append(_demandUnit, td::HAlignment::Left);
        demandRow->appendSpacer();
        demandRow->setSpaceBetweenCells(8);
        left->appendLayout(*demandRow);                                         // 8

        left->appendSpace(6);                                                   // 9
        left->appendLayout(*headerRow(_icoWind, _windHeader));                  // 10
        left->append(_windLabel, td::HAlignment::Left);                         // 11
        gui::HorizontalLayout* sliderRow = new gui::HorizontalLayout(2);
        sliderRow->append(_windSlider, td::HAlignment::Left, td::VAlignment::Center);
        sliderRow->append(_windPct,    td::HAlignment::Right, td::VAlignment::Center);
        sliderRow->setSpaceBetweenCells(8);
        left->appendLayout(*sliderRow);                                         // 12
        left->append(_lossesBox, td::HAlignment::Left);                         // 13

        gui::HorizontalLayout* btnRow = new gui::HorizontalLayout(3);
        btnRow->append(_btnSolve, td::HAlignment::Left);
        btnRow->append(_btnCharts, td::HAlignment::Left);
        btnRow->appendSpacer();
        btnRow->setSpaceBetweenCells(10);
        left->appendLayout(*btnRow);                                            // 14
        left->appendSpacer();                                                   // 15

        // ---- right column: results header + cards (3 cells)
        gui::VerticalLayout* right = new gui::VerticalLayout(3);
        right->appendLayout(*headerRow(_icoResults, _resultsHeader));
        right->append(_results, td::HAlignment::Left, td::VAlignment::Top);
        right->appendSpacer();
        right->setSpaceBetweenCells(6);

        gui::HorizontalLayout* columns = new gui::HorizontalLayout(3);
        columns->appendLayout(*left);
        columns->appendLayout(*right);
        columns->appendSpacer();
        columns->setSpaceBetweenCells(28);

        gui::VerticalLayout* main = new gui::VerticalLayout(2);
        main->appendLayout(*columns);
        main->appendSpacer();
        setLayout(main);

        // initial widget state (this view is inside the tab view, so onInitialAppearance would not fire)
        auto gens = core::buildTestGenerators();
        for (int i = 0; i < N_GEN; ++i)
        {
            _a[i]->setText(fmt(gens[i].a, 4).c_str());
            _b[i]->setText(fmt(gens[i].b, 4).c_str());
            _c[i]->setText(fmt(gens[i].c, 2).c_str());
            _pmin[i]->setText(fmt(gens[i].Pmin, 2).c_str());
            _pmax[i]->setText(fmt(gens[i].Pmax, 2).c_str());
        }
        _demand->setText(fmt(TOTAL_DEMAND_MW, 1).c_str());
        _windSlider.setRange(0, 100);
        _windSlider.setValue(0.0, false);
        updateWindLabel();
    }
};

// ---- Network ---------------------------------------------------------
class NetworkView : public gui::View
{
    NetworkCanvas _canvas;
public:
    NetworkView() : gui::View(10, 8, 10, 10)
    {
        gui::VerticalLayout* main = new gui::VerticalLayout(1);
        main->append(_canvas);
        setLayout(main);
    }
    NetworkCanvas& canvas() { return _canvas; }
};

// ---- Charts ----------------------------------------------------------
class ChartsView : public gui::View
{
    gui::Label  _title;
    gui::Label  _caseLabel;
    gui::Label  _status;
    gui::Button _btnExport;
    ChartCanvas _canvas;

    void onExport()
    {
        gui::SaveFileDialog::show(this, "Export charts to PDF", "pdf", DLG_EXPORT_CHARTS,
            [this](gui::FileDialog* dlg)
            {
                if (dlg->getStatus() != gui::FileDialog::Status::OK) return;
                const bool ok = _canvas.exportPDF(dlg->getFileName());
                _status.setTitle(ok ? "PDF written" : "PDF export failed");
                _status.setTextColor(ok ? td::Accent::Success : td::Accent::Error);
            },
            "Export", "dispatch_charts.pdf");
    }
    void describe(const CaseParams& cp)
    {
        std::ostringstream os;
        os << SCENARIO_NAME[static_cast<int>(cp.scenario)] << "   \xc2\xb7   demand " << fmt(cp.demandMW(), 0)
           << " MW   \xc2\xb7   wind " << fmt(cp.windFrac * 100.0, 0) << " % of " << fmt(WIND_INSTALLED_MW, 0)
           << " MW   \xc2\xb7   " << (cp.useLosses ? "with losses" : "lossless");
        _caseLabel.setTitle(os.str().c_str());
    }
public:
    explicit ChartsView(const CaseParams& cp)
        : gui::View(10, 8, 10, 10), _title("Deliverable charts"), _caseLabel(""), _status(""), _btnExport("Export PDF..."), _canvas(cp)
    {
        _title.setBold();
        _btnExport.onClick([this]() { onExport(); });
        describe(cp);

        gui::HorizontalLayout* row = new gui::HorizontalLayout(5);
        row->append(_title,     td::HAlignment::Left);
        row->append(_caseLabel, td::HAlignment::Left);
        row->appendSpacer();
        row->append(_status,    td::HAlignment::Right);
        row->append(_btnExport, td::HAlignment::Right);
        row->setSpaceBetweenCells(12);

        gui::VerticalLayout* main = new gui::VerticalLayout(2);
        main->appendLayout(*row);
        main->append(_canvas);
        main->setSpaceBetweenCells(6);
        setLayout(main);
    }
    void setCase(const CaseParams& cp) { describe(cp); _status.setTitle(""); _canvas.setCase(cp); }
};

// =====================================================================
//  Main view: header strip + tab view; routes results between tabs
// =====================================================================
class MainView : public gui::View
{
    HeaderCanvas         _header;
    gui::StandardTabView _tabs;
    OverviewView*   _overview;
    ParametersView* _params;
    NetworkView*    _network;
    ChartsView*     _charts;
    Scenario        _scenario = Scenario::Base;

    void onSolved(CaseParams cp, const core::OPFResult& r, bool showCharts)
    {
        cp.scenario = _scenario;
        const chart::Palette pal = chart::palette(false);
        _overview->canvas().setResult(cp, r);
        _network->canvas().setResult(cp, r);
        _charts->setCase(cp);
        _header.set(SCENARIO_NAME[static_cast<int>(_scenario)],
                    r.feasible ? "Optimal \xe2\x9c\x93" : "Infeasible", r.feasible ? pal.ok : pal.err);
        if (showCharts) _tabs.setCurrentViewPos(3);
    }

    void onScenario(Scenario s)
    {
        _scenario = s;
        if (s == Scenario::Custom)
        {
            _header.set(SCENARIO_NAME[5], "edit on Parameters tab", td::ColorID::Gray);
            _tabs.setCurrentViewPos(1);
            return;
        }
        _params->applyPreset(SCENARIO_PRESET[static_cast<int>(s)]);   // solves and routes via onSolved
    }

    void onUserEdit()
    {
        if (_scenario == Scenario::Custom) return;
        _scenario = Scenario::Custom;
        _overview->showScenario(Scenario::Custom);
        _header.set(SCENARIO_NAME[5], "not solved", td::ColorID::Gray);
    }

public:
    MainView() : gui::View()
    {
        _overview = new OverviewView();
        _params   = new ParametersView();
        _network  = new NetworkView();

        CaseParams initial = _params->currentCase();
        _charts = new ChartsView(initial);
        _overview->canvas().setCase(initial);
        _network->canvas().setCase(initial);

        _params->onSolved([this](const CaseParams& cp, const core::OPFResult& r, bool sc) { onSolved(cp, r, sc); });
        _params->onUserEdit([this]() { onUserEdit(); });
        _overview->onScenario([this](Scenario s) { onScenario(s); });

        _tabs.addView(_overview, "Overview");
        _tabs.addView(_params,   "Parameters");
        _tabs.addView(_network,  "Network");
        _tabs.addView(_charts,   "Charts");
        _tabs.setCurrentViewPos(0);

        gui::VerticalLayout* main = new gui::VerticalLayout(2);
        main->append(_header);
        main->append(_tabs);
        main->setSpaceBetweenCells(0);
        setLayout(main);
    }
};

class MainWindow : public gui::Window
{
    MainView _view;
public:
    MainWindow() : gui::Window(gui::Size(1280, 800))
    {
        setTitle("Economic Dispatch \xe2\x80\x94 DC-OPF with Wind Integration");
        setCentralView(&_view);
    }
};

class DispatchApp : public gui::Application
{
protected:
    gui::Window* createInitialWindow() override { return new MainWindow(); }
public:
    DispatchApp(int argc, const char** argv) : gui::Application(argc, argv) {}
};

int main(int argc, const char** argv)
{
    DispatchApp app(argc, argv);
    app.init("EN");   // Regionals must exist before any NumericEdit formats a decimal
    return app.run();
}
