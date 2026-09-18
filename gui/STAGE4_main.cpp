// stage 4: charts on a Canvas
// only the 5 functions in plotprim need fixing if the draw API differs
// set PLOT_WITH_TEXT to 0 if DrawableString/Font are unavailable

#define PLOT_WITH_TEXT 1

#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Canvas.h>
#include <gui/Label.h>
#include <gui/Slider.h>
#include <gui/CheckBox.h>
#include <gui/Shape.h>
#include <gui/Color.h>
#include <gui/VerticalLayout.h>
#include <gui/HorizontalLayout.h>
#if PLOT_WITH_TEXT
  #include <gui/DrawableString.h>
  #include <gui/Font.h>
#endif

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

namespace plotprim
{

inline gui::Color rgb(int r, int g, int b)
{
    return gui::Color(static_cast<td::BYTE>(r),
                      static_cast<td::BYTE>(g),
                      static_cast<td::BYTE>(b));
}

inline void fillRect(gui::Canvas& cv, double x, double y, double w, double h,
                     const gui::Color& col)
{
    gui::Shape s;
    s.createRect(gui::Rect(gui::Point(x, y), gui::Size(w, h)));
    cv.fillShape(s, col);
}

inline void drawLine(gui::Canvas& cv, double x1, double y1, double x2, double y2,
                     const gui::Color& col, double width = 1.0)
{
    gui::Shape s;
    s.moveTo(gui::Point(x1, y1));
    s.lineTo(gui::Point(x2, y2));
    cv.drawShape(s, col, width);
}

inline void drawPolyline(gui::Canvas& cv, const std::vector<gui::Point>& pts,
                         const gui::Color& col, double width = 1.5)
{
    if (pts.size() < 2) return;
    gui::Shape s;
    s.moveTo(pts.front());
    for (size_t i = 1; i < pts.size(); ++i) s.lineTo(pts[i]);
    cv.drawShape(s, col, width);
}

inline void drawText(gui::Canvas& cv, double x, double y, const std::string& text,
                     const gui::Color& col)
{
#if PLOT_WITH_TEXT
    gui::DrawableString ds(text.c_str());
    ds.draw(cv, gui::Point(x, y), col);
#else
    (void)cv; (void)x; (void)y; (void)text; (void)col;
#endif
}

}

namespace {

struct Series
{
    std::string name;
    std::vector<double> xs, ys;
    gui::Color colour;
};

std::string num(double v, int prec = 1)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

void drawXYChart(gui::Canvas& cv,
                 double px, double py, double pw, double ph,
                 const std::string& title,
                 const std::string& xLabel,
                 const std::string& yLabel,
                 const std::vector<Series>& series)
{
    using namespace plotprim;
    gui::Color axis = rgb(90, 90, 90);
    gui::Color grid = rgb(215, 215, 215);
    gui::Color text = rgb(40, 40, 40);

    double mL = 62, mR = 14, mT = 26, mB = 34;
    double ax = px + mL, ay = py + mT;
    double aw = pw - mL - mR, ah = ph - mT - mB;
    if (aw <= 10 || ah <= 10) return;

    drawText(cv, px + mL, py + 4, title, text);

    double xMin = 1e300, xMax = -1e300, yMin = 1e300, yMax = -1e300;
    for (const auto& s : series)
        for (size_t i = 0; i < s.xs.size(); ++i)
        {
            xMin = std::min(xMin, s.xs[i]); xMax = std::max(xMax, s.xs[i]);
            yMin = std::min(yMin, s.ys[i]); yMax = std::max(yMax, s.ys[i]);
        }
    if (xMin > xMax || yMin > yMax) return;
    if (std::fabs(xMax - xMin) < 1e-12) xMax = xMin + 1.0;
    double yPad = (std::fabs(yMax - yMin) < 1e-12)
                ? std::max(1.0, std::fabs(yMax) * 0.1)
                : (yMax - yMin) * 0.08;
    yMin -= yPad; yMax += yPad;

    auto sx = [&](double v) { return ax + (v - xMin) / (xMax - xMin) * aw; };
    auto sy = [&](double v) { return ay + ah - (v - yMin) / (yMax - yMin) * ah; };

    int nTicks = 5;
    for (int t = 0; t <= nTicks; ++t)
    {
        double v = yMin + (yMax - yMin) * t / nTicks;
        double y = sy(v);
        drawLine(cv, ax, y, ax + aw, y, grid, 1.0);
        drawText(cv, px + 6, y - 7, num(v, 2), text);
    }
    for (int t = 0; t <= 2; ++t)
    {
        double v = xMin + (xMax - xMin) * t / 2.0;
        drawText(cv, sx(v) - 12, ay + ah + 8, num(v, 1), text);
    }

    drawLine(cv, ax, ay, ax, ay + ah, axis, 1.5);
    drawLine(cv, ax, ay + ah, ax + aw, ay + ah, axis, 1.5);

    drawText(cv, px + 4, ay + ah + 8, yLabel, text);
    drawText(cv, ax + aw - 60, ay + ah + 8, xLabel, text);

    for (const auto& s : series)
    {
        std::vector<gui::Point> pts;
        pts.reserve(s.xs.size());
        for (size_t i = 0; i < s.xs.size(); ++i)
            pts.push_back(gui::Point(sx(s.xs[i]), sy(s.ys[i])));
        drawPolyline(cv, pts, s.colour, 2.0);
    }
}

void drawDispatchBars(gui::Canvas& cv,
                      double px, double py, double pw, double ph,
                      const std::vector<double>& P,
                      const std::vector<core::Generator>& gens)
{
    using namespace plotprim;
    gui::Color axis = rgb(90, 90, 90);
    gui::Color cap  = rgb(210, 220, 235);
    gui::Color bar  = rgb(70, 110, 190);
    gui::Color text = rgb(40, 40, 40);

    double mL = 52, mR = 14, mT = 26, mB = 30;
    double ax = px + mL, ay = py + mT;
    double aw = pw - mL - mR, ah = ph - mT - mB;
    if (aw <= 10 || ah <= 10 || P.empty()) return;

    drawText(cv, px + mL, py + 4, "Dispatch by generator (MW)", text);

    double maxP = 1.0;
    for (size_t i = 0; i < P.size() && i < gens.size(); ++i)
        maxP = std::max(maxP, gens[i].Pmax);

    double slot = aw / static_cast<double>(P.size());
    double bw = slot * 0.5;

    for (int t = 0; t <= 4; ++t)
    {
        double v = maxP * t / 4.0;
        double y = ay + ah - v / maxP * ah;
        drawLine(cv, ax, y, ax + aw, y, rgb(220, 220, 220), 1.0);
        drawText(cv, px + 6, y - 7, num(v, 0), text);
    }

    for (size_t i = 0; i < P.size(); ++i)
    {
        double cx = ax + slot * (static_cast<double>(i) + 0.5);
        if (i < gens.size())
        {
            double hCap = gens[i].Pmax / maxP * ah;
            fillRect(cv, cx - bw * 0.5, ay + ah - hCap, bw, hCap, cap);
        }
        double h = std::max(0.0, P[i]) / maxP * ah;
        fillRect(cv, cx - bw * 0.5, ay + ah - h, bw, h, bar);

        drawText(cv, cx - 10, ay + ah + 6, "G" + std::to_string(i + 1), text);
        drawText(cv, cx - 16, ay + ah - h - 15, num(P[i], 1), text);
    }

    drawLine(cv, ax, ay, ax, ay + ah, axis, 1.5);
    drawLine(cv, ax, ay + ah, ax + aw, ay + ah, axis, 1.5);
}

}

class PlotCanvas : public gui::Canvas
{
    std::vector<core::WindPoint> _wind;
    std::vector<double>          _dispatch;
    std::vector<core::Generator> _gens;
    bool _useLosses = false;

public:
    PlotCanvas() : _gens(core::buildTestGenerators())
    {
        recompute(0.0, false);
    }

    void recompute(double windFrac, bool useLosses)
    {
        _useLosses = useLosses;
        core::Network net = core::buildTestNetwork(useLosses ? 0.10 : 0.00, 150.0, 1e7);

        core::Sweep sweep;
        _wind = sweep.windSweep(net, _gens, 21, useLosses);

        core::OPFSolver opf;
        core::OPFResult r = useLosses
            ? opf.solveWithLosses(net, _gens, windFrac)
            : opf.solve(net, _gens, windFrac);
        _dispatch = r.feasible ? r.P : std::vector<double>(_gens.size(), 0.0);

        reDraw();
    }

    void onDraw(const gui::Rect& /*rect*/) override
    {
        using namespace plotprim;

        double W = getWidth();
        double H = getHeight();
        if (W < 80 || H < 80) return;

        fillRect(*this, 0, 0, W, H, rgb(252, 252, 252));

        Series lam;  lam.colour = rgb(190, 70, 60);
        Series cost; cost.colour = rgb(60, 120, 90);
        Series loss; loss.colour = rgb(120, 90, 170);

        for (const auto& p : _wind)
        {
            if (!p.feasible) continue;
            lam.xs.push_back(p.penetrationPct);  lam.ys.push_back(p.systemLambda);
            cost.xs.push_back(p.penetrationPct); cost.ys.push_back(p.totalCost);
            loss.xs.push_back(p.penetrationPct); loss.ys.push_back(p.totalLoss);
        }

        double halfW = W * 0.5, halfH = H * 0.5;

        drawXYChart(*this, 0, 0, halfW, halfH,
                    "Marginal price vs wind penetration", "wind %", "$/MWh", { lam });

        drawXYChart(*this, halfW, 0, halfW, halfH,
                    "Total fuel cost vs wind penetration", "wind %", "cost", { cost });

        drawDispatchBars(*this, 0, halfH, halfW, halfH, _dispatch, _gens);

        if (_useLosses)
            drawXYChart(*this, halfW, halfH, halfW, halfH,
                        "Transmission losses vs wind penetration", "wind %", "MW", { loss });
        else
            drawText(*this, halfW + 60, halfH + 40,
                     "enable losses to plot loss curve", rgb(120, 120, 120));
    }
};

class PlotView : public gui::View
{
    gui::Label    _windLabel;
    gui::Slider   _windSlider;
    gui::CheckBox _lossesBox;
    PlotCanvas    _canvas;

    double _windFrac = 0.0;

    void refresh()
    {
        std::ostringstream os;
        os << "wind penetration: " << std::fixed << std::setprecision(1)
           << (_windFrac * 100.0) << " %";
        _windLabel.setTitle(os.str().c_str());
        _canvas.recompute(_windFrac, _lossesBox.isChecked());
    }

    void onWind()
    {
        _windFrac = static_cast<double>(_windSlider.getValue()) / 100.0;
        refresh();
    }
    void onLosses() { refresh(); }

public:
    PlotView()
        : gui::View(new gui::VerticalLayout())
        , _windLabel("wind penetration: 0.0 %")
        , _lossesBox("include transmission losses")
    {
        _windSlider.setRange(0, 100);
        _windSlider.setValue(0);
        _windSlider.onChangedValue(this, &PlotView::onWind);
        _lossesBox.onClick(this, &PlotView::onLosses);

        _windLabel.setSizeLimits(0, gui::Limit::None, 22, gui::Limit::Fixed);
        _windSlider.setSizeLimits(0, gui::Limit::None, 28, gui::Limit::Fixed);
        _lossesBox.setSizeLimits(0, gui::Limit::None, 24, gui::Limit::Fixed);

        *this << _windLabel << _windSlider << _lossesBox << _canvas;
        refresh();
    }
};

class MainWindow : public gui::Window
{
    PlotView _view;
public:
    MainWindow()
        : gui::Window(gui::Size(1100, 760), "Economic Dispatch / DC-OPF")
    {
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
    return app.run();
}
