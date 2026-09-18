// =====================================================================
//  GUI STAGE 4 -- the four deliverable charts on a gui::Canvas.
//
//    1. incremental cost curves, operating lambda marked
//    2. dispatch bar chart (with Pmax ghost bars)
//    3. lambda vs total demand
//    4. total cost vs wind penetration (lossless + with losses)
//
//  Standalone: builds its own network / generators from core::TestNetwork
//  and runs the sweeps once at construction. Export button writes the
//  canvas to PDF through Canvas::exportToPDF.
//
//  Every natID drawing/text call lives in namespace prim at the top of the
//  file. Everything below prim is arithmetic on gui::Point / gui::Rect.
//
//  natID facts this file relies on (all read from Common/Include):
//    - Canvas draws in DrawableView::onDraw(const gui::Rect&); repaint via
//      Frame::reDraw(). Size via Canvas::getSize(gui::Size&).
//    - gui::Rect is td::RectNormalized {left, top, right, bottom}, y down.
//    - Lines: static Shape::drawLine / Shape::drawRect. Polylines and
//      circles: Shape object -> createPolyLine/createCircle -> drawWire /
//      drawFill / drawFillAndWire.
//    - Text: static DrawableString::draw(td::String, Rect, Font::ID,
//      ColorID, TextAlignment, VAlignment). Rect+alignment is used
//      everywhere so the Point-anchor convention never matters.
//    - Colors are td::ColorID; SysText tracks light/dark mode.
//    - SaveFileDialog::show(Frame*, title, ext, wndID, callback).
// =====================================================================

#include <gui/WinMain.h>       // /entry:mainCRTStartup on Windows so main() links under /SUBSYSTEM:WINDOWS
#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Canvas.h>
#include <gui/Shape.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/VerticalLayout.h>
#include <gui/HorizontalLayout.h>
#include <gui/FileDialog.h>

#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#include "Network.h"
#include "OPFSolver.h"
#include "Sweep.h"
#include "TestNetwork.h"

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

inline void fillRect(const gui::Rect& r, td::ColorID fill)
{
    gui::Shape::drawRect(r, fill);
}

inline void frameRect(const gui::Rect& r, td::ColorID lineColor, float w = 1.0f)
{
    gui::Shape::drawRect(r, lineColor, w);
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
//  Chart helpers -- pure arithmetic
// =====================================================================
namespace chart
{

struct Series
{
    std::vector<double> x, y;
    td::ColorID color = td::ColorID::SteelBlue;
    float width = 2.0f;
    td::LinePattern pattern = td::LinePattern::Solid;
    std::string legend;
};

struct Palette
{
    td::ColorID text, grid, axis, bg;
};

inline Palette palette(bool forExport)
{
    Palette p;
    if (forExport)
    {
        p.text = td::ColorID::Black;
        p.grid = td::ColorID::LightGray;
        p.axis = td::ColorID::DimGray;
        p.bg   = td::ColorID::White;
    }
    else
    {
        const bool dark = gui::Application::isDarkMode();
        p.text = td::ColorID::SysText;
        p.grid = dark ? td::ColorID::DarkGray : td::ColorID::LightGray;
        p.axis = dark ? td::ColorID::Gray     : td::ColorID::DimGray;
        p.bg   = td::ColorID::SysCtrlBack;
    }
    return p;
}

inline std::string num(double v, int prec)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

// "nice" tick step for a data span, roughly nTarget intervals
inline double niceStep(double span, int nTarget)
{
    if (span <= 0.0) return 1.0;
    const double raw = span / nTarget;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / mag;
    double nf = 1.0;
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

// Plot area + data range -> pixel mapping.
struct Axes
{
    gui::Rect plot;                    // pixel rect of the plotting area
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;

    double px(double x) const
    { return plot.left + (x - xMin) / (xMax - xMin) * plot.width(); }
    double py(double y) const
    { return plot.bottom - (y - yMin) / (yMax - yMin) * plot.height(); }
    gui::Point p(double x, double y) const { return gui::Point(px(x), py(y)); }
};

// Expand [lo,hi] to nice tick multiples. Set zeroBased to force lo = 0.
inline void niceRange(double& lo, double& hi, double& step, bool zeroBased, int nTicks = 5)
{
    if (zeroBased) lo = std::min(lo, 0.0);
    if (hi <= lo) hi = lo + 1.0;
    step = niceStep(hi - lo, nTicks);
    lo = std::floor(lo / step) * step;
    hi = std::ceil (hi / step) * step;
    if (hi - lo < step) hi = lo + step;
}

struct Margins { double left = 58, right = 14, top = 30, bottom = 40; };

// Draws title, frame, gridlines, ticks and axis labels. Fills in ax.plot.
inline void frame(const gui::Rect& panel, const std::string& title,
                  const std::string& xLabel, const std::string& yLabel,
                  Axes& ax, double xStep, double yStep, const Palette& pal,
                  const Margins& m = Margins())
{
    using namespace prim;

    ax.plot = gui::Rect(panel.left + m.left, panel.top + m.top,
                        panel.right - m.right, panel.bottom - m.bottom);
    if (ax.plot.width() < 20 || ax.plot.height() < 20) return;

    // title
    text(gui::Rect(panel.left + m.left, panel.top + 4, panel.right - m.right, panel.top + m.top - 4),
         title, gui::Font::ID::SystemBold, pal.text,
         td::TextAlignment::Left, td::VAlignment::Center);

    // horizontal grid + y ticks
    const int yDec = decimalsFor(yStep);
    for (double v = ax.yMin; v <= ax.yMax + yStep * 1e-6; v += yStep)
    {
        const double y = ax.py(v);
        line(gui::Point(ax.plot.left, y), gui::Point(ax.plot.right, y),
             pal.grid, 1.0f, td::LinePattern::Dot);
        text(gui::Rect(panel.left + 2, y - 9, ax.plot.left - 5, y + 9),
             num(v, yDec), gui::Font::ID::SystemSmaller, pal.text,
             td::TextAlignment::Right, td::VAlignment::Center);
    }

    // x ticks (xStep <= 0 -> none, used by the bar chart)
    const int xDec = decimalsFor(xStep);
    for (double v = ax.xMin; xStep > 0.0 && v <= ax.xMax + xStep * 1e-6; v += xStep)
    {
        const double x = ax.px(v);
        line(gui::Point(x, ax.plot.bottom), gui::Point(x, ax.plot.bottom + 4), pal.axis, 1.0f);
        text(gui::Rect(x - 30, ax.plot.bottom + 4, x + 30, ax.plot.bottom + 18),
             num(v, xDec), gui::Font::ID::SystemSmaller, pal.text,
             td::TextAlignment::Center, td::VAlignment::Top);
    }

    // frame
    frameRect(ax.plot, pal.axis, 1.0f);

    // axis labels
    text(gui::Rect(ax.plot.left, ax.plot.bottom + 20, ax.plot.right, panel.bottom - 2),
         xLabel, gui::Font::ID::SystemSmaller, pal.text,
         td::TextAlignment::Center, td::VAlignment::Center);
    text(gui::Rect(panel.left + 2, panel.top + 4, ax.plot.left - 5, panel.top + m.top - 4),
         yLabel, gui::Font::ID::SystemSmaller, pal.text,
         td::TextAlignment::Right, td::VAlignment::Center);
}

inline void drawSeries(const Axes& ax, const Series& s)
{
    std::vector<gui::Point> pts;
    pts.reserve(s.x.size());
    for (size_t i = 0; i < s.x.size() && i < s.y.size(); ++i)
        pts.push_back(ax.p(s.x[i], s.y[i]));
    prim::polyline(pts, s.color, s.width, s.pattern);
}

// Small legend in the top-left corner of the plot area.
inline void legend(const Axes& ax, const std::vector<Series>& series, const Palette& pal)
{
    double y = ax.plot.top + 8;
    for (const auto& s : series)
    {
        if (s.legend.empty()) continue;
        const double x0 = ax.plot.left + 10;
        prim::line(gui::Point(x0, y + 7), gui::Point(x0 + 22, y + 7), s.color, s.width, s.pattern);
        prim::text(gui::Rect(x0 + 28, y, x0 + 220, y + 14), s.legend,
                   gui::Font::ID::SystemSmaller, pal.text);
        y += 16;
    }
}

} // namespace chart

// =====================================================================
//  The four deliverable charts
// =====================================================================
namespace figs
{

using namespace chart;

static const td::ColorID GEN_COLOR[3] = {
    td::ColorID::SteelBlue, td::ColorID::DarkOrange, td::ColorID::ForestGreen
};
inline td::ColorID genColor(size_t g) { return GEN_COLOR[g % 3]; }

// Fig 1: incremental cost 2aP+b for each generator over [Pmin,Pmax],
// dashed line at the operating lambda, dot at each generator's dispatch.
inline void costCurves(const gui::Rect& panel,
                       const std::vector<core::Generator>& gens,
                       const core::OPFResult& op, const Palette& pal)
{
    Axes ax;
    double xLo = 0.0, xHi = 0.0, yLo = 1e300, yHi = -1e300;
    for (const auto& g : gens)
    {
        xHi = std::max(xHi, g.Pmax);
        yLo = std::min(yLo, std::min(2.0 * g.a * g.Pmin + g.b, 2.0 * g.a * g.Pmax + g.b));
        yHi = std::max(yHi, std::max(2.0 * g.a * g.Pmin + g.b, 2.0 * g.a * g.Pmax + g.b));
    }
    const double lam = (op.feasible && !op.lambda.empty()) ? op.lambda[0] : 0.0;
    if (op.feasible) { yLo = std::min(yLo, lam); yHi = std::max(yHi, lam); }

    double xStep, yStep;
    niceRange(xLo, xHi, xStep, true);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Incremental cost curves, lambda marked",
          "P [MW]", "$/MWh", ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;

    std::vector<Series> all;
    for (size_t g = 0; g < gens.size(); ++g)
    {
        auto curve = core::Sweep::costCurve(gens[g], 41);
        Series s;
        s.color = genColor(g);
        s.legend = "G" + std::to_string(g + 1);
        for (const auto& pt : curve) { s.x.push_back(pt.P); s.y.push_back(pt.incrementalCost); }
        drawSeries(ax, s);
        all.push_back(std::move(s));
    }

    if (op.feasible)
    {
        prim::line(ax.p(ax.xMin, lam), ax.p(ax.xMax, lam),
                   td::ColorID::Crimson, 1.5f, td::LinePattern::Dash);
        prim::text(gui::Rect(ax.plot.right - 150, ax.py(lam) - 18, ax.plot.right - 4, ax.py(lam) - 2),
                   "lambda = " + num(lam, 3), gui::Font::ID::SystemSmallerBold,
                   td::ColorID::Crimson, td::TextAlignment::Right);
        for (size_t g = 0; g < gens.size() && g < op.P.size(); ++g)
            prim::dot(ax.p(op.P[g], 2.0 * gens[g].a * op.P[g] + gens[g].b),
                      4.0, genColor(g), pal.text);
    }
    legend(ax, all, pal);
}

// Fig 2: dispatch bars with Pmax ghost bars and value labels.
inline void dispatchBars(const gui::Rect& panel,
                         const std::vector<core::Generator>& gens,
                         const core::OPFResult& op, const Palette& pal)
{
    Axes ax;
    double yLo = 0.0, yHi = 0.0;
    for (const auto& g : gens) yHi = std::max(yHi, g.Pmax);
    double yStep;
    niceRange(yLo, yHi, yStep, true);
    ax.xMin = 0; ax.xMax = static_cast<double>(gens.size());
    ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Optimal dispatch (bars) vs capacity (outline)",
          "", "MW", ax, 0.0, yStep, pal);
    if (ax.plot.width() < 20) return;

    const double slot = ax.plot.width() / static_cast<double>(gens.size());
    const double bw = slot * 0.45;

    for (size_t g = 0; g < gens.size(); ++g)
    {
        const double cx = ax.plot.left + slot * (static_cast<double>(g) + 0.5);
        const gui::Rect cap(cx - bw / 2, ax.py(gens[g].Pmax), cx + bw / 2, ax.py(0.0));
        prim::frameRect(cap, pal.axis, 1.0f);

        const double P = (op.feasible && g < op.P.size()) ? std::max(0.0, op.P[g]) : 0.0;
        const gui::Rect bar(cx - bw / 2, ax.py(P), cx + bw / 2, ax.py(0.0));
        prim::fillRect(bar, genColor(g));

        std::string tag = "G" + std::to_string(g + 1);
        if (op.feasible && g < op.genAtMax.size() && op.genAtMax[g]) tag += " (at max)";
        if (op.feasible && g < op.genAtMin.size() && op.genAtMin[g]) tag += " (at min)";
        prim::text(gui::Rect(cx - slot / 2, ax.plot.bottom + 4, cx + slot / 2, ax.plot.bottom + 18),
                   tag, gui::Font::ID::SystemSmaller, pal.text, td::TextAlignment::Center,
                   td::VAlignment::Top);
        prim::text(gui::Rect(cx - slot / 2, ax.py(P) - 18, cx + slot / 2, ax.py(P) - 2),
                   num(P, 1) + " MW", gui::Font::ID::SystemSmallerBold, pal.text,
                   td::TextAlignment::Center, td::VAlignment::Bottom);
    }

    if (op.feasible)
    {
        double sum = 0.0;
        for (double v : op.P) sum += v;
        prim::text(gui::Rect(ax.plot.left + 8, ax.plot.top + 6, ax.plot.right - 8, ax.plot.top + 22),
                   "thermal " + num(sum, 1) + " MW, wind " + num(op.windUsed, 1) +
                   " MW, cost " + num(op.totalCost, 1),
                   gui::Font::ID::SystemSmaller, pal.text, td::TextAlignment::Right);
    }
}

// Fig 3: system lambda vs total demand, nominal demand marked.
inline void lambdaVsDemand(const gui::Rect& panel,
                           const std::vector<core::DemandPoint>& pts,
                           double nominalDemand, const Palette& pal)
{
    Series s;
    s.color = td::ColorID::Crimson;
    s.legend = "system lambda";
    for (const auto& p : pts)
    {
        if (!p.feasible) continue;
        s.x.push_back(p.totalDemand);
        s.y.push_back(p.systemLambda);
    }
    if (s.x.size() < 2) return;

    Axes ax;
    double xLo = *std::min_element(s.x.begin(), s.x.end());
    double xHi = *std::max_element(s.x.begin(), s.x.end());
    double yLo = *std::min_element(s.y.begin(), s.y.end());
    double yHi = *std::max_element(s.y.begin(), s.y.end());
    double xStep, yStep;
    niceRange(xLo, xHi, xStep, false);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Marginal price vs total demand", "demand [MW]", "$/MWh",
          ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;

    if (nominalDemand >= ax.xMin && nominalDemand <= ax.xMax)
        prim::line(ax.p(nominalDemand, ax.yMin), ax.p(nominalDemand, ax.yMax),
                   pal.axis, 1.0f, td::LinePattern::Dash);
    drawSeries(ax, s);
    legend(ax, { s }, pal);
}

// Fig 4: total cost vs wind penetration, lossless and with losses.
inline void costVsWind(const gui::Rect& panel,
                       const std::vector<core::WindPoint>& lossless,
                       const std::vector<core::WindPoint>& lossy,
                       const Palette& pal)
{
    Series a, b;
    a.color = td::ColorID::ForestGreen; a.legend = "lossless";
    b.color = td::ColorID::SteelBlue;   b.legend = "with losses (r = 10% x)";
    b.pattern = td::LinePattern::Dash;
    for (const auto& p : lossless) if (p.feasible) { a.x.push_back(p.penetrationPct); a.y.push_back(p.totalCost); }
    for (const auto& p : lossy)    if (p.feasible) { b.x.push_back(p.penetrationPct); b.y.push_back(p.totalCost); }
    if (a.x.size() < 2) return;

    double xLo = 0.0, xHi = 0.0, yLo = 1e300, yHi = -1e300;
    for (const Series* s : { &a, &b })
    {
        for (double v : s->x) xHi = std::max(xHi, v);
        for (double v : s->y) { yLo = std::min(yLo, v); yHi = std::max(yHi, v); }
    }
    Axes ax;
    double xStep, yStep;
    niceRange(xLo, xHi, xStep, true);
    niceRange(yLo, yHi, yStep, false);
    ax.xMin = xLo; ax.xMax = xHi; ax.yMin = yLo; ax.yMax = yHi;

    frame(panel, "Total fuel cost vs wind penetration", "wind [% of demand]", "cost",
          ax, xStep, yStep, pal);
    if (ax.plot.width() < 20) return;

    drawSeries(ax, a);
    if (b.x.size() >= 2) drawSeries(ax, b);
    legend(ax, { a, b }, pal);
}

} // namespace figs

// =====================================================================
//  Canvas owning the data and painting the 2x2 grid
// =====================================================================
class ChartCanvas : public gui::Canvas
{
    std::vector<core::Generator>   _gens;
    core::OPFResult                _op;
    std::vector<core::DemandPoint> _demand;
    std::vector<core::WindPoint>   _windLossless;
    std::vector<core::WindPoint>   _windLossy;
    double _nominalDemand = 0.0;
    bool   _forExport = false;

    void compute()
    {
        _gens = core::buildTestGenerators();

        // Same cases as cli Part B: limits relaxed, wind 150 MW at bus 4.
        core::Network net = core::buildTestNetwork(0.0, 150.0, 1e7);
        for (const auto& b : net.getBuses()) _nominalDemand += b.demand;

        core::OPFSolver opf;
        _op = opf.solve(net, _gens, 0.0);

        core::Sweep sweep;
        _demand       = sweep.demandSweep(net, _gens, 0.4, 1.4, 21, false);
        _windLossless = sweep.windSweep(net, _gens, 21, false);

        core::Network lossy = core::buildTestNetwork(0.10, 150.0, 1e7);
        _windLossy = sweep.windSweep(lossy, _gens, 11, true);
    }

protected:
    void onDraw(const gui::Rect& /*rect*/) override
    {
        gui::Size sz;
        getSize(sz);
        if (sz.width < 120 || sz.height < 120) return;

        const chart::Palette pal = chart::palette(_forExport);
        if (_forExport)
            prim::fillRect(gui::Rect(0, 0, sz.width, sz.height), pal.bg);

        const double gap = 8;
        const double w = (sz.width  - 3 * gap) / 2;
        const double h = (sz.height - 3 * gap) / 2;
        auto cell = [&](int col, int row)
        {
            const double x = gap + col * (w + gap);
            const double y = gap + row * (h + gap);
            return gui::Rect(x, y, x + w, y + h);
        };

        figs::costCurves    (cell(0, 0), _gens, _op, pal);
        figs::dispatchBars  (cell(1, 0), _gens, _op, pal);
        figs::lambdaVsDemand(cell(0, 1), _demand, _nominalDemand, pal);
        figs::costVsWind    (cell(1, 1), _windLossless, _windLossy, pal);
    }

public:
    ChartCanvas() : gui::Canvas()
    {
        compute();
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

// =====================================================================
class ChartView : public gui::View
{
    gui::Label  _title;
    gui::Button _btnExport;
    gui::Label  _status;
    ChartCanvas _canvas;

    enum : td::UINT4 { DLG_EXPORT_PDF = 100 };

    void onExport()
    {
        gui::SaveFileDialog::show(this, "Export charts to PDF", "pdf", DLG_EXPORT_PDF,
            [this](gui::FileDialog* dlg)
            {
                if (dlg->getStatus() != gui::FileDialog::Status::OK) return;
                td::String fn = dlg->getFileName();
                const bool ok = _canvas.exportPDF(fn);
                _status.setTitle(ok ? "exported" : "export failed");
            });
    }

public:
    ChartView()
        : gui::View()
        , _title("Economic Dispatch / DC-OPF -- deliverable charts")
        , _btnExport("Export PDF")
        , _status("")
    {
        _title.setBold();
        _btnExport.onClick([this]() { onExport(); });

        // 3 cells: title, status, button. Count must match append count.
        gui::HorizontalLayout* row = new gui::HorizontalLayout(3);
        row->append(_title,     td::HAlignment::Left,  td::VAlignment::Center);
        row->append(_status,    td::HAlignment::Right, td::VAlignment::Center);
        row->append(_btnExport, td::HAlignment::Right, td::VAlignment::Center);

        // 2 cells: the row layout and the canvas.
        gui::VerticalLayout* main = new gui::VerticalLayout(2);
        main->appendLayout(*row);
        main->append(_canvas);
        setLayout(main);
    }
};

class MainWindow : public gui::Window
{
    ChartView _view;
public:
    MainWindow()
        : gui::Window(gui::Size(1200, 820))
    {
        setTitle("Economic Dispatch (stage 4 -- charts)");
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
    app.init("EN");
    return app.run();
}
