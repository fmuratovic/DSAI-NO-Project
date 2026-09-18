#include <gui/WinMain.h>
#include <gui/Application.h>
#include <gui/Window.h>
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/VerticalLayout.h>

#include <iostream>

class HelloView : public gui::View
{
    gui::Label _label;
public:
    HelloView()
        : gui::View()
        , _label("Economic Dispatch / DC-OPF -- stage 1 ok")
    {
        gui::VerticalLayout* layout = new gui::VerticalLayout(1);
        *layout << _label;
        setLayout(layout);
    }
};

class MainWindow : public gui::Window
{
    HelloView _view;
public:
    MainWindow()
        : gui::Window(gui::Size(640, 240))
    {
        setTitle("Economic Dispatch (stage 1)");
        setCentralView(&_view);
    }
};

class DispatchApp : public gui::Application
{
protected:
    gui::Window* createInitialWindow() override
    {
        return new MainWindow();
    }
public:
    DispatchApp(int argc, const char** argv)
        : gui::Application(argc, argv)
    {
    }
};

int main(int argc, const char** argv)
{
    DispatchApp app(argc, argv);
    return app.run();
}