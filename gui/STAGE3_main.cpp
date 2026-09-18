#include <gui/WinMain.h>
#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/Slider.h>
#include <gui/CheckBox.h>
#include <gui/LineEdit.h>
#include <gui/VerticalLayout.h>

#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdlib>

#include "Network.h"
#include "OPFSolver.h"
#include "TestNetwork.h"

class ControlView : public gui::View
{
    gui::Label _title;
    gui::Label _tableHeader;

    static const int N_GEN = 3;
    gui::LineEdit* _a[N_GEN];
    gui::LineEdit* _b[N_GEN];
    gui::LineEdit* _c[N_GEN];
    gui::LineEdit* _pmin[N_GEN];
    gui::LineEdit* _pmax[N_GEN];

    gui::Label    _windHeader;
    gui::Label    _windLabel;
    gui::Slider   _windSlider;
    gui::CheckBox _lossesBox;
    gui::Button   _btnSolve;

    gui::Label _resultsHeader;
    gui::Label _lineGen;
    gui::Label _lineLambda;
    gui::Label _lineFlow;
    gui::Label _lineCost;

    double _windFrac = 0.0;
    bool   _useLosses = false;

    static std::string fmt(double v, int prec = 2)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(prec) << v;
        return os.str();
    }

    static double readField(gui::LineEdit* field, double fallback)
    {
        td::String t = field->getText();
        const char* s = t.c_str();
        if (!s || s[0] == '\0') return fallback;
        char* end = nullptr;
        double v = std::strtod(s, &end);
        if (end == s) return fallback;
        return v;
    }

    std::vector<core::Generator> readGenerators() const
    {
        auto fallback = core::buildTestGenerators();
        std::vector<core::Generator> gens;
        for (int i = 0; i < N_GEN; ++i)
        {
            core::Generator g{};
            g.a = readField(_a[i], fallback[i].a);
            g.b = readField(_b[i], fallback[i].b);
            g.c = readField(_c[i], fallback[i].c);
            g.Pmin = readField(_pmin[i], fallback[i].Pmin);
            g.Pmax = readField(_pmax[i], fallback[i].Pmax);
            gens.push_back(g);
        }
        return gens;
    }

    void recompute()
    {
        auto gens = readGenerators();

        core::Network net = core::buildTestNetwork(
            _useLosses ? 0.10 : 0.00, 150.0, 1e7);

        core::OPFSolver opf;
        core::OPFResult r = _useLosses
            ? opf.solveWithLosses(net, gens, _windFrac)
            : opf.solve(net, gens, _windFrac);

        {
            std::ostringstream os;
            os << "wind penetration: " << fmt(_windFrac * 100.0, 1) << " %   ("
                << fmt(_windFrac * 150.0) << " MW of 150 MW installed)";
            _windLabel.setTitle(os.str().c_str());
        }

        if (!r.feasible)
        {
            _lineGen.setTitle("no feasible solution / check generator values");
            _lineLambda.setTitle("");
            _lineFlow.setTitle("");
            _lineCost.setTitle("");
            return;
        }

        {
            std::ostringstream os;
            os << "generation:  ";
            double sum = 0.0;
            for (size_t i = 0; i < r.P.size(); ++i)
            {
                os << "G" << (i + 1) << " = " << fmt(r.P[i]) << " MW   ";
                sum += r.P[i];
            }
            os << "   thermal total = " << fmt(sum) << " MW";
            _lineGen.setTitle(os.str().c_str());
        }
        {
            std::ostringstream os;
            os << "marginal price:  ";
            for (size_t i = 0; i < r.lambda.size(); ++i)
                os << "bus" << i << " = " << fmt(r.lambda[i], 3) << "   ";
            _lineLambda.setTitle(os.str().c_str());
        }
        {
            std::ostringstream os;
            os << "line flows (MW):  ";
            for (size_t i = 0; i < r.flow.size(); ++i)
            {
                os << "L" << i << " = " << fmt(r.flow[i]);
                if (r.mu[i] != 0.0) os << " [at limit]";
                os << "   ";
            }
            _lineFlow.setTitle(os.str().c_str());
        }
        {
            std::ostringstream os;
            os << "total fuel cost = " << fmt(r.totalCost);
            if (_useLosses) os << "     transmission losses = "
                << fmt(r.totalLoss, 3) << " MW";
            _lineCost.setTitle(os.str().c_str());
        }
    }

    void onWindChanged()
    {
        _windFrac = _windSlider.getValue() / 100.0;
        if (_windFrac < 0.0) _windFrac = 0.0;
        if (_windFrac > 1.0) _windFrac = 1.0;
        recompute();
    }

    void onLossesToggled()
    {
        _useLosses = _lossesBox.isChecked();
        recompute();
    }

public:
    ControlView()
        : gui::View()
        , _title("Economic Dispatch / DC-OPF")
        , _tableHeader("Generators:  a          b          c          Pmin       Pmax")
        , _windHeader("Wind generation")
        , _windLabel("wind penetration: 0.0 %")
        , _lossesBox("include transmission losses (r = 10% of x)")
        , _btnSolve("Solve")
        , _resultsHeader("Results")
        , _lineGen(""), _lineLambda(""), _lineFlow(""), _lineCost("")
    {
        auto gens = core::buildTestGenerators();

        _title.setBold();
        _tableHeader.setBold();
        _windHeader.setBold();
        _resultsHeader.setBold();
        _btnSolve.setAsDefault();

        gui::VerticalLayout* mainLayout = new gui::VerticalLayout(12 + N_GEN * 5);
        mainLayout->append(_title);
        mainLayout->append(_tableHeader);

        for (int i = 0; i < N_GEN; ++i)
        {
            _a[i] = new gui::LineEdit();
            _b[i] = new gui::LineEdit();
            _c[i] = new gui::LineEdit();
            _pmin[i] = new gui::LineEdit();
            _pmax[i] = new gui::LineEdit();

            _a[i]->setText(fmt(gens[i].a, 4).c_str());
            _b[i]->setText(fmt(gens[i].b, 4).c_str());
            _c[i]->setText(fmt(gens[i].c, 4).c_str());
            _pmin[i]->setText(fmt(gens[i].Pmin, 2).c_str());
            _pmax[i]->setText(fmt(gens[i].Pmax, 2).c_str());

            mainLayout->append(*_a[i]);
            mainLayout->append(*_b[i]);
            mainLayout->append(*_c[i]);
            mainLayout->append(*_pmin[i]);
            mainLayout->append(*_pmax[i]);
        }

        _windSlider.setRange(0, 100);
        _windSlider.setValue(0);
        _windSlider.onChangedValue([this]() { onWindChanged(); });
        _lossesBox.onClick([this]() { onLossesToggled(); });
        _btnSolve.onClick([this]() { recompute(); });

        mainLayout->append(_windHeader);
        mainLayout->append(_windLabel);
        mainLayout->append(_windSlider);
        mainLayout->append(_lossesBox);
        mainLayout->append(_btnSolve);

        mainLayout->append(_resultsHeader);
        mainLayout->append(_lineGen);
        mainLayout->append(_lineLambda);
        mainLayout->append(_lineFlow);
        mainLayout->append(_lineCost);

        setLayout(mainLayout);
        recompute();
    }
};

class MainWindow : public gui::Window
{
    ControlView _view;
public:
    MainWindow()
        : gui::Window(gui::Size(1050, 750))
    {
        setTitle("Economic Dispatch \xe2\x80\x94 Stage 3");
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