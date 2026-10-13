#include "MainComponent.h"

#include <cmath>
#include <exception>

// ============================================================================
// Load MIDI
// ============================================================================

void MainComponent::loadMidi()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Select MIDI file",
            currentMidiFile.existsAsFile()
                ? currentMidiFile.getParentDirectory()
                : juce::File(),
            "*.mid;*.midi;*.MID;*.MIDI;*.kar;*.KAR");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (!file.existsAsFile())
                return;

            loadMidiFile(file);
        });
}
void MainComponent::loadMidiFile(const juce::File& file, bool fromSetlist,
                                 const SongEntry* entry, int setlistRow, int setlistIndex)
{
    if (!file.existsAsFile())
    {
        // Geen stille mislukking meer: laat zien welk bestand ontbreekt.
        confirmDialog("File not found",
                      "This MIDI file can no longer be found:\n" + file.getFullPathName(),
                      "OK", nullptr);
        return;
    }

    try
    {
        const double sampleRate =
            audioReady
                ? audioSampleRate
                : 48000.0;

        Song song;

        {
            const std::scoped_lock lock(midiReadMutex);

            song = midiReader.read(
                file.getFullPathName().toStdString(),
                sampleRate);
        }

        currentSong =
            std::make_shared<Song>(
                std::move(song));

        currentMidiFile = file;

        // Transpose geldt per nummer: bij elk nieuw nummer (ook via Next
        // en auto-next) terug naar 0, of naar de transpose die bij dit
        // nummer in de setlist is ingesteld.
        setTranspose(entry != nullptr ? entry->transpose : 0);

        currentDisplayName = entry != nullptr ? entry->displayName : juce::String();

        // Alleen een nummer uit de songlijst mag een volgend nummer starten.
        currentSongFromSetlist = fromSetlist;
        playingRow = fromSetlist ? setlistRow : -1;
        playingSetlist = fromSetlist ? setlistIndex : -1;

        // Belangrijk: de complete Song inclusief
        // lyrics en akkoorden naar de webserver.
        webServer.setSong(currentSong);
        webServer.setSongTitle(currentDisplayName.toStdString());

        isPlaying = false;
        playButton.setButtonText("Play");
        synthEngine.stop();

        synthEngine.setSampleRate(
            sampleRate);

        synthEngine.loadSong(
            currentSong);

        updateChannelModel();
        updateSongDisplay();

        // De "nu geladen"-driehoekjes in beide lijsten moeten meeverhuizen.
        songBox.repaint();
        setlistBox.repaint();
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Could not load MIDI",
            e.what());
    }
}

// ============================================================================
// SoundFont
// ============================================================================

void MainComponent::selectSoundFont()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Select SoundFont",
            currentSoundFont.existsAsFile()
                ? currentSoundFont.getParentDirectory()
                : juce::File(),
            "*.sf2;*.SF2");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            const auto file =
                fc.getResult();

            if (!file.existsAsFile())
                return;

            std::string error;

            if (!synthEngine.loadSoundFont(
                    file.getFullPathName().toStdString(),
                    error))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Could not load SoundFont",
                    juce::String(error));

                return;
            }

            currentSoundFont = file;

            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue(
                    "soundFontPath",
                    currentSoundFont.getFullPathName());

                properties->saveIfNeeded();
            }

            auto name =
                currentSoundFont.getFileName();

            if (name.length() > 27)
            {
                name =
                    name.substring(0, 24) + "...";
            }

            soundFontButton.setButtonText(name);
        });
}

// ============================================================================
// Play
// ============================================================================

void MainComponent::play()
{
    if (!currentSong || !audioReady)
        return;

    if (synthEngine.isPlaying())
    {
        // Pauze: behoud de huidige positie en synthstatus.
        synthEngine.stop();
        isPlaying = false;

        playButton.setButtonText("Play");
    }
    else
    {
        // Play: start of hervat vanaf de huidige positie.
        synthEngine.start();
        isPlaying = synthEngine.isPlaying();

        playButton.setButtonText(
            isPlaying ? "Pause" : "Play");
    }

    playButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour::fromRGB(0, 164, 235));

    updateTransportDisplay();
}

// ============================================================================
// Stop
// ============================================================================

void MainComponent::stop()
{
    synthEngine.stop();
    synthEngine.seekSamples(0);

    isPlaying = false;

    playButton.setButtonText("Play");
    playButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour::fromRGB(0, 164, 235));

    updateTransportDisplay();
}

// ============================================================================
// Setlist: volgend nummer
// ============================================================================

// Rij van het geladen nummer in de getoonde setlist. Een nummer mag er
// meerdere keren in staan, dus eerst de onthouden rij; anders de eerste
// rij met hetzelfde bestand.
int MainComponent::currentRowInPlayingSetlist() const
{
    if (!currentSongFromSetlist ||
        playingSetlist < 0 ||
        playingSetlist >= static_cast<int>(setlists.size()))
    {
        return -1;
    }

    const auto& songs = setlists[static_cast<std::size_t>(playingSetlist)].songs;

    if (playingRow >= 0 && playingRow < static_cast<int>(songs.size())
        && songs[static_cast<std::size_t>(playingRow)].file == currentMidiFile)
    {
        return playingRow;
    }

    for (std::size_t i = 0; i < songs.size(); ++i)
        if (songs[i].file == currentMidiFile)
            return static_cast<int>(i);

    return -1;
}

const MainComponent::SongEntry* MainComponent::nextSetlistEntry() const
{
    const int row = currentRowInPlayingSetlist();

    if (row < 0)
        return nullptr;

    const auto& songs = setlists[static_cast<std::size_t>(playingSetlist)].songs;

    return row + 1 < static_cast<int>(songs.size())
        ? &songs[static_cast<std::size_t>(row + 1)]
        : nullptr;
}

void MainComponent::playNextInSetlist()
{
    const int row = currentRowInPlayingSetlist();
    const auto* found = nextSetlistEntry();

    if (found == nullptr || !found->file.existsAsFile())
        return;

    const SongEntry next = *found;
    const int listIndex = playingSetlist;

    loadMidiFile(next.file, true, &next, row + 1, listIndex);

    // De selectie volgt alleen als de spelende setlist ook getoond wordt.
    if (selectedSetlist == listIndex)
        songBox.selectRow(row + 1);

    play();
}

// Next-knop: tijdens afspelen start het volgende nummer meteen,
// bij stop of pauze wordt het volgende nummer alleen klaargezet.
void MainComponent::skipToNextSong()
{
    // Een nummer uit een setlist gaat door in die setlist, ook als er
    // intussen een andere setlist getoond wordt.
    const int listIndex = currentSongFromSetlist ? playingSetlist : selectedSetlist;

    if (listIndex < 0 ||
        listIndex >= static_cast<int>(setlists.size()))
    {
        return;
    }

    const auto& songs =
        setlists[static_cast<std::size_t>(listIndex)].songs;

    // Huidige positie in de lijst; staat het nummer er niet in
    // (bijv. via Load), dan geldt de geselecteerde rij als "huidig".
    int index = currentRowInPlayingSetlist();

    if (index < 0 && listIndex == selectedSetlist)
        index = songBox.getSelectedRow();

    const int nextIndex = index + 1;

    if (nextIndex < 0 || nextIndex >= static_cast<int>(songs.size()))
        return;

    const bool wasPlaying = synthEngine.isPlaying();

    const SongEntry nextEntry = songs[static_cast<std::size_t>(nextIndex)];
    loadMidiFile(nextEntry.file, true, &nextEntry, nextIndex, listIndex);

    if (listIndex == selectedSetlist)
        songBox.selectRow(nextIndex);

    if (wasPlaying)
        play();
}

// ============================================================================
// Song display
// ============================================================================

void MainComponent::updateSongDisplay()
{
    if (!currentSong ||
        !currentMidiFile.existsAsFile())
    {
        return;
    }

    auto name =
        currentDisplayName.isNotEmpty()
            ? currentDisplayName
            : currentMidiFile.getFileNameWithoutExtension();

    songTitle.setText(
        name,
        juce::dontSendNotification);

    const auto* next = nextSetlistEntry();

    nextSong.setText(
        next != nullptr
            ? "Next: " + next->shownName(false)
            : juce::String("Next: -"),
        juce::dontSendNotification);

    // Positie in de setlist ("6/10").
    juce::String position;

    if (currentSongFromSetlist
        && playingSetlist >= 0
        && playingSetlist < static_cast<int>(setlists.size()))
    {
        const auto& songs = setlists[static_cast<std::size_t>(playingSetlist)].songs;
        const int row = currentRowInPlayingSetlist();

        if (row >= 0)
            position = juce::String(row + 1) + "/" + juce::String(static_cast<int>(songs.size()));
    }

    setlistIndexLabel.setText(position, juce::dontSendNotification);

    updateTransportDisplay();
}

// ============================================================================
// Transport display
// ============================================================================
void MainComponent::updateTransportDisplay()
{
    if (!currentSong)
    {
        elapsedTime.setText("00:00", juce::dontSendNotification);
        totalTime.setText("00:00", juce::dontSendNotification);
        positionSlider.setValue(0.0, juce::dontSendNotification);

        isPlaying = false;
        playButton.setButtonText("Play");
        return;
    }

    const auto lengthSamples = synthEngine.lengthSamples();
    const auto positionSamples = synthEngine.positionSamples();

    const double sampleRate =
        audioSampleRate > 0.0 ? audioSampleRate : 48000.0;

    const double positionSeconds =
        static_cast<double>(positionSamples) / sampleRate;

    const double lengthSeconds =
        static_cast<double>(lengthSamples) / sampleRate;

    elapsedTime.setText(
        formatTime(positionSeconds),
        juce::dontSendNotification);

    totalTime.setText(
        formatTime(lengthSeconds),
        juce::dontSendNotification);

    const double fraction =
        lengthSamples > 0
            ? static_cast<double>(positionSamples) /
                  static_cast<double>(lengthSamples)
            : 0.0;

    // Tijdens het slepen staat de engine stil op de oude positie;
    // de timer mag de slider dan niet terugzetten.
    if (!positionSliderDragging)
    {
        positionSlider.setValue(
            juce::jlimit(0.0, 1.0, fraction),
            juce::dontSendNotification);
    }

    const bool enginePlaying = synthEngine.isPlaying();

    // Nummer is vanzelf afgelopen (niet gepauzeerd of gestopt):
    // de engine stond aan en staat nu uit op het einde.
    const bool songFinished =
        wasEnginePlaying
        && !enginePlaying
        && lengthSamples > 0
        && positionSamples >= lengthSamples;

    wasEnginePlaying = enginePlaying;

    if (isPlaying != enginePlaying)
    {
        isPlaying = enginePlaying;

        playButton.setButtonText(
            enginePlaying ? "Pause" : "Play");

        playButton.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour::fromRGB(0, 164, 235));
    }

    if (songFinished && continuousToggle.getToggleState())
        playNextInSetlist();
}

// ============================================================================
// Time formatting
// ============================================================================

juce::String MainComponent::formatTime(
    double seconds)
{
    if (!std::isfinite(seconds) ||
        seconds < 0.0)
    {
        seconds = 0.0;
    }

    const auto totalSeconds =
        static_cast<int>(
            std::floor(seconds));

    const int minutes =
        totalSeconds / 60;

    const int remainingSeconds =
        totalSeconds % 60;

    return juce::String(minutes)
        + ":"
        + juce::String(
              remainingSeconds)
              .paddedLeft('0', 2);
}