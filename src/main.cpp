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
        return "0.1.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(
            getApplicationName(),
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
            centreWithSize(1100, 700);
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