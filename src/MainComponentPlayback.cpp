#include "MainComponent.h"
#include "MerikPackage.h"
#include "LoadingOverlay.h"
#include "PackageLoader.h"

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
            "*.mid;*.midi;*.MID;*.MIDI;*.kar;*.KAR;*.merik;*.MERIK");

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
                                 const SongEntry* entry, int setlistRow, int setlistIndex, bool insert)
{
    if (!file.existsAsFile())
    {
        // Geen stille mislukking meer: laat zien welk bestand ontbreekt.
        confirmDialog("File not found",
                      "This MIDI file can no longer be found:\n" + file.getFullPathName(),
                      "OK", nullptr);
        return;
    }

    // Een eerdere (nog lopende) laadactie vervalt.
    cancelPackageLoad();

    PendingLoad pending;
    pending.file = file;
    pending.fromSetlist = fromSetlist;
    pending.hasEntry = entry != nullptr;

    if (entry != nullptr)
        pending.entry = *entry;

    pending.row = setlistRow;
    pending.index = setlistIndex;
    pending.insert = insert;

    if (!MerikPackage::isPackage(file))
    {
        completeLoad(pending, file, nullptr, true);
        return;
    }

    // .merik: eerst stoppen, dan de stems op een eigen thread laden.
    synthEngine.stop();
    isPlaying = false;
    playButton.setButtonText("Play");

    pendingLoad = pending;
    pendingPlay = false;

    packageLoader = std::make_unique<PackageLoader>(
        file,
        audioReady ? audioSampleRate : 48000.0,
        nextLoadId++,
        [safe = juce::Component::SafePointer<MainComponent>(this)](int id)
        {
            if (safe != nullptr)
                safe->packageLoadFinished(id);
        });

    loadingOverlay->start(
        entry != nullptr && entry->displayName.isNotEmpty() ? entry->displayName : file.getFileNameWithoutExtension(),
        packageLoader.get());
}

void MainComponent::cancelPackageLoad()
{
    if (packageLoader != nullptr)
    {
        packageLoader->cancel();
        packageLoader.reset();   // wacht tot de thread klaar is (kort: er wordt per blokje gecontroleerd)
    }

    if (loadingOverlay != nullptr)
        loadingOverlay->stop();

    pendingPlay = false;
}

void MainComponent::packageLoadFinished(int id)
{
    if (packageLoader == nullptr || packageLoader->id != id)
        return;   // een oudere laadactie

    auto loader = std::move(packageLoader);
    loadingOverlay->stop();

    auto result = std::move(loader->result);
    loader.reset();

    if (result.cancelled)
    {
        pendingPlay = false;
        return;
    }

    if (result.error.isNotEmpty())
    {
        pendingPlay = false;
        confirmDialog("Could not load .merik", result.error, "OK", nullptr);
        return;
    }

    const auto pending = pendingLoad;
    completeLoad(pending, result.midiFile, result.stems, result.tempoKnown);

    if (pendingPlay)
    {
        pendingPlay = false;
        play();
    }
}

void MainComponent::completeLoad(const PendingLoad& pending, const juce::File& midiPath,
                                 std::shared_ptr<const StemSet> stems, bool tempoKnown)
{
    const auto& file = pending.file;
    const SongEntry* entry = pending.hasEntry ? &pending.entry : nullptr;
    const bool fromSetlist = pending.fromSetlist;
    const int setlistRow = pending.row;
    const int setlistIndex = pending.index;

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
                midiPath.getFullPathName().toStdString(),
                sampleRate);
        }

        song.tempoKnown = tempoKnown;

        if (MerikPackage::isPackage(file))
            MerikPackage::applyScore(file, song);

        songHasStems = stems != nullptr;
        transposeDownButton.setEnabled(!songHasStems);
        transposeUpButton.setEnabled(!songHasStems);
        synthEngine.setStems(stems);
        showFamilyNames(stems != nullptr ? stems->names : std::array<std::string, 8> {});

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
        currentIsInsert = pending.insert;

        // Weer een gewoon nummer uit een setlist: het "verder met de laatste setlist" is niet meer nodig.
        if (fromSetlist)
            resumeValid = false;
        playingRow = fromSetlist ? setlistRow : -1;
        playingSetlist = fromSetlist ? setlistIndex : -1;

        // Belangrijk: de complete Song inclusief
        // lyrics en akkoorden naar de webserver.
        beatIndicator.setSong(currentSong);

        webServer.setSong(currentSong);
        // Bij een .merik is het MIDI-bestand intern "song.mid": toon dan de naam van het pakket.
        webServer.setSongTitle((currentDisplayName.isNotEmpty() ? currentDisplayName : file.getFileNameWithoutExtension()).toStdString());

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
    // Terwijl een .merik nog laadt: onthouden en starten zodra het klaar is.
    if (packageLoader != nullptr)
    {
        pendingPlay = true;
        return;
    }

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
    pendingPlay = false;

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

// Waar het vervolg van de setlist begint: het nummer uit de setlist dat nu klinkt, of (bij een
// ingevoegd nummer uit de zoekresultaten) de plek waar we waren.
MainComponent::FlowCursor MainComponent::flowCursor() const
{
    if (currentSongFromSetlist
        && playingSetlist >= 0 && playingSetlist < static_cast<int>(setlists.size()))
    {
        return { playingSetlist, currentRowInPlayingSetlist() };
    }

    if (currentIsInsert && resumeValid
        && resumeSetlist >= 0 && resumeSetlist < static_cast<int>(setlists.size()))
    {
        return { resumeSetlist, resumeRow, true };
    }

    return {};
}

const MainComponent::SongEntry* MainComponent::nextSetlistEntry() const
{
    // Een ingevoegd nummer ("play after current song") komt eerst.
    if (queuedInsert.valid)
        return &queuedInsert.entry;

    const auto cursor = flowCursor();

    // Bij een nummer uit een setlist moet de rij bekend zijn; na een ingevoegd nummer mag de
    // plek "voor het eerste nummer" zijn (er was nog niets gespeeld).
    if (cursor.setlist < 0 || (cursor.row < 0 && !cursor.fromResume))
        return nullptr;

    const auto& songs = setlists[static_cast<std::size_t>(cursor.setlist)].songs;

    return cursor.row + 1 < static_cast<int>(songs.size())
        ? &songs[static_cast<std::size_t>(cursor.row + 1)]
        : nullptr;
}

void MainComponent::playNextInSetlist(bool startPlaying)
{
    // Een ingevoegd nummer begint altijd vanzelf.
    if (queuedInsert.valid)
    {
        const SongEntry entry = queuedInsert.entry;
        queuedInsert.valid = false;
        startInsert(entry, true);
        return;
    }

    const auto cursor = flowCursor();

    if (cursor.setlist < 0 || (cursor.row < 0 && !cursor.fromResume))
        return;

    const auto& songs = setlists[static_cast<std::size_t>(cursor.setlist)].songs;

    if (cursor.row + 1 >= static_cast<int>(songs.size()))
        return;

    const SongEntry next = songs[static_cast<std::size_t>(cursor.row + 1)];

    if (!next.file.existsAsFile())
        return;

    loadMidiFile(next.file, true, &next, cursor.row + 1, cursor.setlist);

    // De selectie volgt alleen als de spelende setlist ook getoond wordt.
    if (selectedSetlist == cursor.setlist)
        songBox.selectRow(cursor.row + 1);

    if (startPlaying)
        play();
}

// Next-knop: tijdens afspelen start het volgende nummer meteen,
// bij stop of pauze wordt het volgende nummer alleen klaargezet.
void MainComponent::skipToNextSong()
{
    const bool wasPlaying = synthEngine.isPlaying();

    // Een ingevoegd nummer ("play after current song") is het volgende nummer.
    if (queuedInsert.valid)
    {
        const SongEntry entry = queuedInsert.entry;
        queuedInsert.valid = false;
        startInsert(entry, wasPlaying);
        return;
    }

    // Een nummer uit een setlist gaat door in die setlist, ook als er intussen een andere
    // setlist getoond wordt. Na een ingevoegd nummer gaat de setlist verder waar hij was.
    int listIndex = -1;
    int index = -1;

    if (currentSongFromSetlist)
    {
        listIndex = playingSetlist;
        index = currentRowInPlayingSetlist();

        if (index < 0 && listIndex == selectedSetlist)
            index = songBox.getSelectedRow();
    }
    else if (currentIsInsert && resumeValid)
    {
        listIndex = resumeSetlist;
        index = resumeRow;
    }
    else
    {
        // Een los geladen bestand: de geselecteerde rij van de getoonde setlist geldt als "huidig".
        listIndex = selectedSetlist;
        index = songBox.getSelectedRow();
    }

    if (listIndex < 0 ||
        listIndex >= static_cast<int>(setlists.size()))
    {
        return;
    }

    const auto& songs =
        setlists[static_cast<std::size_t>(listIndex)].songs;

    const int nextIndex = index + 1;

    if (nextIndex < 0 || nextIndex >= static_cast<int>(songs.size()))
        return;

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

    // Nummer uit een setlist afgelopen: bij "auto play next" start het
    // volgende nummer; anders wordt het volgende alleen klaargezet
    // (geladen en geselecteerd, niet afgespeeld).
    if (songFinished)
        playNextInSetlist(continuousToggle.getToggleState());
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

// ============================================================================
// Spatiebalk: play / pauze
// ============================================================================

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        play();
        return true;
    }

    if (key == juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0))
    {
        showSongSearch();
        return true;
    }

    return false;
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return keyPressed(key);
}

void MainComponent::openFile(const juce::File& file)
{
    loadMidiFile(file);
}
