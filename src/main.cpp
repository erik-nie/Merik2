#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "WebServer.h"
#include "ProcessMemory.h"


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
        return "0.5.0";
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

    // Eerste niet-loopback IPv4-adres, zodat de webinterface vanaf een
    // ander apparaat te vinden is (bijv. 192.168.2.212:8888).
    static juce::String getLocalAddress()
    {
        for (const auto& address : juce::IPAddress::getAllAddresses(false))
        {
            if (!address.isIPv6 && address != juce::IPAddress::local())
                return address.toString();
        }

        return {};
    }

    // Bestanden die via het besturingssysteem of de opdrachtregel worden geopend.
    void openFromCommandLine(const juce::String& commandLine)
    {
        if (mainWindow == nullptr)
            return;

        for (const auto& token : juce::StringArray::fromTokens(commandLine, true))
        {
            const juce::File file(token.unquoted());

            if (file.existsAsFile() && file.hasFileExtension("mid;midi;kar;merik"))
            {
                // macOS meldt een geopend bestand soms twee keer.
                const auto now = juce::Time::getMillisecondCounter();

                if (file == lastOpened && now - lastOpenedAt < 3000)
                    return;

                lastOpened = file;
                lastOpenedAt = now;
                mainWindow->loadFile(file);
                return;
            }
        }
    }

    void initialise(const juce::String& commandLine) override
    {
        const auto ip = getLocalAddress();
        const juce::String web = ip.isNotEmpty()
            ? "    web:" + ip + ":" + juce::String(WebServer::defaultPort)
            : juce::String();

        mainWindow = std::make_unique<MainWindow>(
            getApplicationName() + "    v:" + getApplicationVersion() + "    build:" + getBuildDateTime() + web,
            *this
        );

        // Even wachten tot het audioapparaat klaar is (de stems worden naar
        // de samplefrequentie van het apparaat omgezet).
        juce::Timer::callAfterDelay(800, [this, commandLine] { openFromCommandLine(commandLine); });
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        openFromCommandLine(commandLine);
    }

private:
    class MainWindow : public juce::DocumentWindow,
                       private juce::Timer
    {
    public:
        MainWindow(const juce::String& name,
                   MerikApplication& application)
            : DocumentWindow(
                  name,
                  juce::Colours::black,
                  juce::DocumentWindow::allButtons),
              baseTitle(name),
              app(application)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(900, 810);
            setResizeLimits( 500,600,4000,4000);
            setVisible(true);

            updateTitle();
            startTimer(2000);
        }

        void closeButtonPressed() override
        {
            app.systemRequestedQuit();
        }

        void loadFile(const juce::File& file)
        {
            if (auto* component = dynamic_cast<MainComponent*>(getContentComponent()))
                component->openFile(file);
        }

    private:
        // Titelbalk: naam, versie, adres en het geheugen dat Merik nu gebruikt.
        void updateTitle()
        {
            const auto memory = formatMemory(getProcessMemoryBytes());
            const auto title = memory.isNotEmpty() ? baseTitle + "    mem:" + memory : baseTitle;

            if (title != getName())
                setName(title);
        }

        void timerCallback() override
        {
            updateTitle();
        }

        const juce::String baseTitle;
        MerikApplication& app;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
    juce::File lastOpened;
    juce::uint32 lastOpenedAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MerikApplication)
};

START_JUCE_APPLICATION(MerikApplication)