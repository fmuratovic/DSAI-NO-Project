#include <gui/WinMain.h>
#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/VerticalLayout.h>

#include <sstream>
#include <iomanip>

#include "Network.h"
#include "OPFSolver.h"
#include "TestNetwork.h"

class ResultsView : public gui::View
{
    gui::Label  _title;
    gui::Label  _lineGen;
    gui::Label  _lineLambda;
    gui::Label  _lineFlow;
    gui::Label  _lineCost;
    gui::Button _btnSolve;
    gui::Button _btnSolveWind;

    core::Network _net;
    std::vector<core::Generator> _gens;

    static std::string fmt(double v, int prec = 2)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(prec) << v;
        return os.str();
    }

    void showResult(const core::OPFResult& r, const char* what)
    {
        if (!r.feasible)
        {
            _lineGen.setTitle("no feasible solution");
            _lineLambda.setTitle("");
            _lineFlow.setTitle("");
            _lineCost.setTitle("");
            return;
        }

        std::ostringstream g;
        g << what << " -- generation: ";
        for (size_t i = 0; i < r.P.size(); ++i)
            g << "G" << (i + 1) << " = " << fmt(r.P[i]) << " MW   ";
        if (r.windUsed > 0.0) g << "wind = " << fmt(r.windUsed) << " MW";
        _lineGen.setTitle(g.str().c_str());

        std::ostringstream l;
        l << "prices: ";
        for (size_t i = 0; i < r.lambda.size(); ++i)
            l << "bus" << i << " = " << fmt(r.lambda[i], 3) << "   ";
        _lineLambda.setTitle(l.str().c_str());

        std::ostringstream f;
        f << "flows: ";
        for (size_t i = 0; i < r.flow.size(); ++i)
        {
            f << "L" << i << " = " << fmt(r.flow[i]);
            if (r.mu[i] != 0.0) f << "*";
            f << "   ";
        }
        f << "   (* = at limit)";
        _lineFlow.setTitle(f.str().c_str());

        std::ostringstream c;
        c << "total cost = " << fmt(r.totalCost);
        if (r.totalLoss > 0.0) c << "     losses = " << fmt(r.totalLoss, 3) << " MW";
        c << "     residual = " << r.residual;
        _lineCost.setTitle(c.str().c_str());
    }

    void onSolve()
    {
        core::OPFSolver opf;
        showResult(opf.solve(_net, _gens, 0.0), "no wind");
    }

    void onSolveWind()
    {
        core::OPFSolver opf;
        showResult(opf.solve(_net, _gens, 1.0), "full wind");
    }

public:
    ResultsView()
        : gui::View()
        , _title("Economic dispatch / DC-OPF -- 5-bus test network")
        , _lineGen("press Solve")
        , _lineLambda("")
        , _lineFlow("")
        , _lineCost("")
        , _btnSolve("Solve (no wind)")
        , _btnSolveWind("Solve (full wind)")
        , _net(core::buildTestNetwork(0.0, 150.0, 1e7))
        , _gens(core::buildTestGenerators())
    {
        _btnSolve.onClick([this]() { onSolve(); });
        _btnSolveWind.onClick([this]() { onSolveWind(); });

        gui::VerticalLayout* layout = new gui::VerticalLayout(7);
        layout->append(_title);
        layout->append(_lineGen);
        layout->append(_lineLambda);
        layout->append(_lineFlow);
        layout->append(_lineCost);
        layout->append(_btnSolve);
        layout->append(_btnSolveWind);
        setLayout(layout);
    }
};

class MainWindow : public gui::Window
{
    ResultsView _view;
public:
    MainWindow()
        : gui::Window(gui::Size(900, 400))
    {
        setTitle("Economic Dispatch (stage 2)");
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