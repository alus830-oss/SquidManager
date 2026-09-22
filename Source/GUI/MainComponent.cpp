#include "MainComponent.h"
#include "Theme/SquidColourIds.h"
#include "../SystemServices.h"
#include "../SquidSalmple/SquidBankProperties.h"
#include "../SquidSalmple/SquidChannelProperties.h"
#include "../SquidSalmple/Bank/BankManagerProperties.h"
#include "oolib/Properties/PersistentRootProperties.h"

static const juce::String kUnsavedText { "UNSAVED EDITS" };

MainComponent::MainComponent (juce::ValueTree rootPropertiesVT)
{
    setSize (1117, 640);
    setWantsKeyboardFocus (true);
    rootProperties = rootPropertiesVT;

    PersistentRootProperties persistentRootProperties (rootPropertiesVT, PersistentRootProperties::WrapperType::client, PersistentRootProperties::EnableCallbacks::no);
    guiProperties.wrap (persistentRootProperties.getValueTree (), GuiProperties::WrapperType::client, GuiProperties::EnableCallbacks::yes);
    guiProperties.onShowSettingsDialog = [this] () { showSettingsDialog (); };
    appProperties.wrap (persistentRootProperties.getValueTree (), AppProperties::WrapperType::client, AppProperties::EnableCallbacks::no);
    runtimeRootProperties.wrap (rootPropertiesVT, RuntimeRootProperties::WrapperType::client, RuntimeRootProperties::EnableCallbacks::yes);
    audioPlayerProperties.wrap (runtimeRootProperties.getValueTree (), AudioPlayerProperties::WrapperType::client, AudioPlayerProperties::EnableCallbacks::no);
    SystemServices systemServices (runtimeRootProperties.getValueTree (), SystemServices::WrapperType::client, SystemServices::EnableCallbacks::no);
    editManager = systemServices.getEditManager ();

    // The editing code expects to work inside a 'bank' folder, so it is given a private
    // scratch folder. Opened files are copied in there, and the user's files are only
    // written to when they save.
    workDirectory = juce::File (runtimeRootProperties.getAppDataPath ()).getChildFile ("SingleFileWork");
    clearWorkDirectory ();
    workDirectory.createDirectory ();
    appProperties.addRecentlyUsedFile (workDirectory.getFullPathName ());

    lastFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    // HEADER
    titleLabel.setText ("SQUID SINGLE", juce::dontSendNotification);
    titleLabel.setFont (SquidType::sectionHeader ());
    titleLabel.setBorderSize ({ 0, 0, 0, 0 });
    addAndMakeVisible (titleLabel);
    fileNameLabel.setBorderSize ({ 0, 0, 0, 0 });
    fileNameLabel.setFont (SquidType::nameField ());
    addAndMakeVisible (fileNameLabel);

    openButton.setTooltip ("Open an audio file (Cmd/Ctrl + O)");
    openButton.onClick = [this] () { openFile (); };
    addAndMakeVisible (openButton);
    saveButton.setTooltip ("Save the wav, with all slices inside it (Cmd/Ctrl + S)");
    saveButton.onClick = [this] () { save (); };
    addAndMakeVisible (saveButton);
    saveAsButton.setTooltip ("Save to a new wav file (Cmd/Ctrl + Shift + S)");
    saveAsButton.onClick = [this] () { saveAs (); };
    addAndMakeVisible (saveAsButton);
    settingsButton.setTooltip ("Audio output and appearance settings");
    settingsButton.onClick = [this] () { showSettingsDialog (); };
    addAndMakeVisible (settingsButton);

    // EDITOR
    SquidBankProperties squidBankProperties;
    BankManagerProperties bankManagerProperties (runtimeRootProperties.getValueTree (), BankManagerProperties::WrapperType::owner, BankManagerProperties::EnableCallbacks::no);
    squidBankProperties.wrap (bankManagerProperties.getBank ("edit"), SquidBankProperties::WrapperType::client, SquidBankProperties::EnableCallbacks::no);
    channelEditor.init (squidBankProperties.getChannelVT (kChannelIndex), rootPropertiesVT);
    addAndMakeVisible (channelEditor);

    // QUIT
    runtimeRootProperties.onSystemRequestedQuit = [this] ()
    {
        audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
        runtimeRootProperties.setPreferredQuitState (RuntimeRootProperties::QuitState::idle, false);
        loseEditsWarning ("Exiting", [this] ()
        {
            juce::MessageManager::callAsync ([this] () { runtimeRootProperties.setQuitState (RuntimeRootProperties::QuitState::now, false); });
        });
    };

    updateFileNameLabel ();
    sendLookAndFeelChange ();
    startTimer (250);
}

MainComponent::~MainComponent ()
{
    stopTimer ();
    clearWorkDirectory ();
}

void MainComponent::clearWorkDirectory ()
{
    // plain delete rather than moving to the trash, so temp copies do not pile up in the user's Trash
    if (workDirectory.isDirectory ())
        workDirectory.deleteRecursively ();
}

bool MainComponent::hasSample ()
{
    SquidChannelProperties channelProperties (editManager->getChannelPropertiesVT (kChannelIndex), SquidChannelProperties::WrapperType::client, SquidChannelProperties::EnableCallbacks::no);
    return channelProperties.getSampleFileName ().isNotEmpty ();
}

void MainComponent::loseEditsWarning (juce::String title, std::function<void ()> continueFunction)
{
    if (! editManager->channelHasUnsavedEdits (kChannelIndex) || ! hasSample ())
    {
        continueFunction ();
        return;
    }
    juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon, title,
        "You have unsaved edits.\n  Select Continue to lose your changes.\n  Select Cancel to go back and save.", "Continue (lose changes)", "Cancel", nullptr,
        juce::ModalCallbackFunction::create ([continueFunction] (int option)
        {
            if (option == 1)
                juce::MessageManager::callAsync (continueFunction);
        }));
}

void MainComponent::openFile ()
{
    loseEditsWarning ("Open File", [this] ()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open an audio file", lastFolder, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResults ().isEmpty ())
                return;
            loadFile (fc.getResult ());
        });
    });
}

void MainComponent::loadFile (juce::File file)
{
    if (! file.existsAsFile ())
        return;
    if (! editManager->isSquidManagerSupportedAudioFile (file))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Unsupported File", "This file type is not supported:\n" + file.getFileName ());
        return;
    }
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    lastFolder = file.getParentDirectory ();

    // the editor copies the file into the work folder, and reads any Squid metadata (slices) it already holds
    workDirectory.createDirectory ();
    if (! channelEditor.loadFile (file.getFullPathName ()))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Open Failed", "Could not open:\n" + file.getFileName ());
        return;
    }
    editManager->markChannelSaved (kChannelIndex);

    openedFile = file;
    // only a wav can be saved back in place; anything else has to go through SAVE AS
    saveFile = file.hasFileExtension ("wav") ? file : juce::File ();
    lastSeenSampleFileName = SquidChannelProperties (editManager->getChannelPropertiesVT (kChannelIndex), SquidChannelProperties::WrapperType::client, SquidChannelProperties::EnableCallbacks::no).getSampleFileName ();
    updateFileNameLabel ();
}

void MainComponent::save ()
{
    if (! hasSample ())
        return;
    if (saveFile == juce::File ())
    {
        saveAs ();
        return;
    }
    writeTo (saveFile);
}

void MainComponent::saveAs ()
{
    if (! hasSample ())
        return;
    auto initialFile { saveFile != juce::File () ? saveFile
                                                 : (openedFile != juce::File () ? openedFile.withFileExtension ("wav")
                                                                                : lastFolder.getChildFile ("untitled.wav")) };
    fileChooser = std::make_unique<juce::FileChooser> ("Save wav file", initialFile, "*.wav");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
    {
        if (fc.getResults ().isEmpty ())
            return;
        auto file { fc.getResult () };
        if (! file.hasFileExtension ("wav"))
            file = file.withFileExtension ("wav");
        if (writeTo (file))
        {
            saveFile = file;
            lastFolder = file.getParentDirectory ();
            updateFileNameLabel ();
        }
    });
}

bool MainComponent::writeTo (juce::File file)
{
    audioPlayerProperties.setPlayState (AudioPlayerProperties::PlayState::stop, false);
    if (! editManager->saveChannelToFile (kChannelIndex, file))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Save Failed", "Could not write:\n" + file.getFullPathName ());
        return false;
    }
    return true;
}

void MainComponent::updateFileNameLabel ()
{
    juce::String text;
    if (! hasSample ())
        text = "No file. Press OPEN, or drag an audio file onto the editor.";
    else if (saveFile != juce::File ())
        text = saveFile.getFullPathName ();
    else if (openedFile != juce::File ())
        text = openedFile.getFullPathName () + "   (not saved yet - use SAVE AS)";
    else
        text = "New sample   (not saved yet - use SAVE AS)";
    fileNameLabel.setText (text, juce::dontSendNotification);
    fileNameLabel.setTooltip (text);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command { juce::ModifierKeys::commandModifier };
    if (key == juce::KeyPress ('o', command, 0)) { openFile (); return true; }
    if (key == juce::KeyPress ('s', command, 0)) { save (); return true; }
    if (key == juce::KeyPress ('s', command | juce::ModifierKeys::shiftModifier, 0)) { saveAs (); return true; }
    return false;
}

void MainComponent::timerCallback ()
{
    // a file dropped straight onto the editor replaces the sample without going through OPEN
    const auto sampleFileName { SquidChannelProperties (editManager->getChannelPropertiesVT (kChannelIndex), SquidChannelProperties::WrapperType::client, SquidChannelProperties::EnableCallbacks::no).getSampleFileName () };
    if (sampleFileName != lastSeenSampleFileName)
    {
        lastSeenSampleFileName = sampleFileName;
        openedFile = juce::File ();
        saveFile = juce::File ();
        updateFileNameLabel ();
    }

    const auto sampleLoaded { hasSample () };
    saveButton.setEnabled (sampleLoaded);
    saveAsButton.setEnabled (sampleLoaded);

    const auto unsaved { sampleLoaded && editManager->channelHasUnsavedEdits (kChannelIndex) };
    if (unsaved != hasUnsavedEdits)
    {
        hasUnsavedEdits = unsaved;
        saveButton.setPrimary (hasUnsavedEdits);
        repaint (unsavedBounds);
    }
}

void MainComponent::showSettingsDialog ()
{
    juce::DialogWindow::LaunchOptions options;
    options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = findColour (SquidColours::dialogBackground);
    options.dialogTitle = "SETTINGS";
    options.resizable = false;
    auto* settingsComponent { new SettingsDialogComponent () };
    settingsComponent->init (rootProperties);
    settingsComponent->setBounds (settingsComponent->getPreferredBounds ());
    options.content.setOwned (settingsComponent);
    options.launchAsync ();
}

void MainComponent::lookAndFeelChanged ()
{
    juce::Component::lookAndFeelChanged ();
    titleLabel.setColour (juce::Label::textColourId, findColour (SquidColours::accentText));
    fileNameLabel.setColour (juce::Label::textColourId, findColour (SquidColours::text));
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (findColour (SquidColours::windowBackground));
    g.setColour (findColour (SquidColours::listBackground));
    g.fillRect (headerBounds);
    g.setColour (findColour (SquidColours::outline));
    g.fillRect (headerBounds.withTop (headerBounds.getBottom () - 1));

    if (hasUnsavedEdits)
    {
        auto tagBounds { unsavedBounds };
        const auto ledBounds { tagBounds.removeFromLeft (static_cast<int> (StatusLed::kDiameter)).toFloat ()
                                        .withSizeKeepingCentre (StatusLed::kDiameter, StatusLed::kDiameter) };
        StatusLed::draw (g, ledBounds, true, *this, SquidColours::unsavedEdits);
        tagBounds.removeFromLeft (6);
        g.setFont (SquidType::statusTag ());
        g.setColour (findColour (SquidColours::unsavedEdits));
        g.drawText (kUnsavedText, tagBounds, juce::Justification::centredLeft, false);
    }
}

void MainComponent::resized ()
{
    auto localBounds { getLocalBounds () };
    headerBounds = localBounds.removeFromTop (kHeaderHeight);

    static constexpr auto kGap { 9 };
    auto headerRow { headerBounds.withTrimmedBottom (1).reduced (kGap, 0) };
    auto placeLeft = [&headerRow] (juce::Component& component, int width, int height)
    {
        component.setBounds (headerRow.removeFromLeft (width).withSizeKeepingCentre (width, height));
        headerRow.removeFromLeft (kGap);
    };
    auto placeRight = [&headerRow] (ActionButton& button)
    {
        const auto width { button.getIdealWidth () };
        button.setBounds (headerRow.removeFromRight (width).withSizeKeepingCentre (width, ActionButton::kNormalHeight));
        headerRow.removeFromRight (kGap);
    };
    placeLeft (titleLabel, SquidPaint::textWidth (SquidType::sectionHeader (), titleLabel.getText ()) + 4, headerRow.getHeight ());
    placeLeft (openButton, openButton.getIdealWidth (), ActionButton::kNormalHeight);
    placeLeft (saveButton, saveButton.getIdealWidth (), ActionButton::kNormalHeight);
    placeLeft (saveAsButton, saveAsButton.getIdealWidth (), ActionButton::kNormalHeight);
    placeRight (settingsButton);
    unsavedBounds = headerRow.removeFromRight (static_cast<int> (StatusLed::kDiameter) + 6 + SquidPaint::textWidth (SquidType::statusTag (), kUnsavedText) + 2);
    headerRow.removeFromRight (kGap);
    fileNameLabel.setBounds (headerRow);

    channelEditor.setBounds (localBounds.withWidth (std::max (localBounds.getWidth (), kMinimumEditorWidth)));
}
