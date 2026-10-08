#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"


class MerikApplication : public juce::JUCEApplication
{
public:
    MerikApplication() = default;

    const juce::String getApplicationName() override
    {
        return "Merik";
    }

    const juce::String getApplicationVersion() override
    {
        return "0.1.1";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    juce::String getBuildDateTime()
    {
        const juce::String date(__DATE__); // "Oct  6 2026"
        const juce::String time(__TIME__); // "19:10:32"

        const auto month = date.substring(0, 3);
        const auto day = date.substring(4, 6).trimStart();
        const auto year = date.substring(7);

        return day + " " + month + " " + year + " " + time;
    }

    void initialise(const juce::String&) override
    {

        mainWindow = std::make_unique<MainWindow>(
            getApplicationName() + "    v:" + getApplicationVersion() + "    build:" + getBuildDateTime(),
            *this
        );
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name,
                   MerikApplication& application)
            : DocumentWindow(
                  name,
                  juce::Colours::black,
                  juce::DocumentWindow::allButtons),
              app(application)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(900, 850);
            setResizeLimits( 500,600,4000,4000);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            app.systemRequestedQuit();
        }

    private:
        MerikApplication& app;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MerikApplication)
};

START_JUCE_APPLICATION(MerikApplication)