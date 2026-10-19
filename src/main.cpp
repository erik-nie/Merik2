#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "WebServer.h"
#include "ProcessMemory.h"
#include "RenderJob.h"


juce::String merikBuildDateTime()
{
    const juce::String date(__DATE__); // "Oct  6 2026"
    const juce::String time(__TIME__); // "19:10:32"

    return date.substring(4, 6).trimStart() + " " + date.substring(0, 3) + " " + date.substring(7) + " " + time;
}

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
        return "0.6.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    static juce::String getBuildDateTime()
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

    // Zonder venster renderen: Merik --render-internal <soundfont.sf2> <uitvoermap> [--demo] <bestand.mid> ...
    // Voor ontwikkeling en voor wie veel bestanden in één keer wil omzetten.
    bool renderFromCommandLine(const juce::String& commandLine)
    {
        auto tokens = juce::StringArray::fromTokens(commandLine, true);
        const int flag = tokens.indexOf("--render-internal");

        if (flag < 0 || tokens.size() < flag + 4)
            return false;

        RenderOptions options;
        options.internalSynth = true;
        options.soundFontPath = tokens[flag + 1].unquoted().toStdString();
        options.outputFolder = juce::File(tokens[flag + 2].unquoted());
        options.outputFolder.createDirectory();

        for (int i = flag + 3; i < tokens.size(); ++i)
        {
            if (tokens[i] == "--demo")
                options.demo = true;
            else
                options.midiFiles.push_back(juce::File(tokens[i].unquoted()));
        }

        RenderJob job(nullptr, options);

        while (!job.status().finished)
            juce::Thread::sleep(100);

        for (const auto& line : job.status().log)
            std::cout << line << std::endl;

        if (job.status().error.isNotEmpty())
            std::cout << job.status().error << std::endl;

        return true;
    }

    void initialise(const juce::String& commandLine) override
    {
        if (renderFromCommandLine(commandLine))
        {
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *this);

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
            const auto title = baseTitle + "    web:merik.local" + juce::String(WebServer::portSuffix());

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