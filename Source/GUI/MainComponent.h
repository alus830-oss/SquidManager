#pragma once

#include <JuceHeader.h>
#include "GuiProperties.h"
#include "SettingsDialogComponent.h"
#include "Theme/UiComponents.h"
#include "SquidSalmple/ChannelEditorComponent.h"
#include "../AppProperties.h"
#include "../SquidSalmple/Audio/AudioPlayerProperties.h"
#include "../SquidSalmple/EditManager/EditManager.h"
#include "oolib/Properties/RuntimeRootProperties.h"

// Single file editor: one window, one sample, one channel editor.
// Open any audio file, edit its slices (cue sets) and parameters, then save it as a
// standalone wav that carries all of the Squid Salmple metadata inside it.
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent (juce::ValueTree rootPropertiesVT);
    ~MainComponent () override;

private:
    static constexpr int kChannelIndex { 0 };   // everything is edited as channel 1
    static constexpr int kHeaderHeight { 44 };
    static constexpr int kMinimumEditorWidth { 1000 };

    juce::ValueTree rootProperties;
    GuiProperties guiProperties;
    AppProperties appProperties;
    RuntimeRootProperties runtimeRootProperties;
    AudioPlayerProperties audioPlayerProperties;
    EditManager* editManager { nullptr };

    juce::Label titleLabel;
    juce::Label fileNameLabel;
    ActionButton openButton { "OPEN" };
    ActionButton saveButton { "SAVE" };
    ActionButton saveAsButton { "SAVE AS" };
    ActionButton settingsButton { "SETTINGS" };
    ChannelEditorComponent channelEditor;
    juce::TooltipWindow tooltipWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::File workDirectory;       // private scratch folder the editor works in
    juce::File openedFile;          // the file the user opened
    juce::File saveFile;            // where SAVE writes to (empty until the first SAVE AS)
    juce::String lastSeenSampleFileName;
    juce::File lastFolder;
    bool hasUnsavedEdits { false };
    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> unsavedBounds;

    void openFile ();
    void loadFile (juce::File file);
    void save ();
    void saveAs ();
    bool writeTo (juce::File file);
    void loseEditsWarning (juce::String title, std::function<void ()> continueFunction);
    void clearWorkDirectory ();
    bool hasSample ();
    void showSettingsDialog ();
    void updateFileNameLabel ();

    bool keyPressed (const juce::KeyPress& key) override;
    void timerCallback () override;
    void resized () override;
    void paint (juce::Graphics& g) override;
    void lookAndFeelChanged () override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
