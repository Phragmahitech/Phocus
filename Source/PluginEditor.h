#pragma once

#include "PluginProcessor.h"
#include "PresetBar.h"

/**
    Look de Phocus : celui du skin Serum 2 « Phragma electricity » — panneaux bleu nuit,
    texte clair, accents bleu clair (#3CD2F8) et mauve (#9850E8), lilas (#C898F8),
    écrans en dégradé bleu clair -> mauve. Police Chakra Petch embarquée.
*/
class PhocusLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PhocusLookAndFeel();

    static juce::Colour ground()      { return juce::Colour (0xff0e181c); }
    static juce::Colour panelTop()    { return juce::Colour (0xff1e2f36); }
    static juce::Colour panelBottom() { return juce::Colour (0xff17252b); }
    static juce::Colour edge()        { return juce::Colour (0xff2b4250); }
    static juce::Colour screen()      { return juce::Colour (0xff0b1418); }
    static juce::Colour text()        { return juce::Colour (0xffdde8ee); }
    static juce::Colour dim()         { return juce::Colour (0xff8aa0ac); }
    static juce::Colour accent()      { return juce::Colour (0xff3cd2f8); }   // bleu clair
    static juce::Colour mauve()       { return juce::Colour (0xff9850e8); }
    static juce::Colour lilac()       { return juce::Colour (0xffc898f8); }
    static juce::Colour steel()       { return juce::Colour (0xff5a8cf0); }

    /** Couleur d'une bande : 0..3 bandes, 4 LOW CUT, 5 HIGH CUT. */
    static juce::Colour bandColour (int band);

    static juce::Path chamfered (juce::Rectangle<float> r, float cut);
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title, const juce::Font& font);

    juce::Font font (float height, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    juce::Font getPopupMenuFont() override                  { return font (12.0f, true, 0.06f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& w, int& h) override;

private:
    juce::Typeface::Ptr semiBoldFace, boldFace;
};

class PhocusFacade;

//==============================================================================
/**
    Écran principal : spectre d'entrée (léger), réduction en temps réel (dégradé orange -> mauve,
    qui descend du haut), courbe de sensibilité et ses six nœuds.

      glisser un nœud            fréquence (et sensibilité pour les bandes)
      Cmd / Ctrl + glisser       Q
      Maj                        réglage fin
      Alt + clic                 écoute de la bande (tant qu'on tient)
      double-clic                bande allumée / éteinte
      molette                    Q (Cmd / Ctrl : balance)
      clic droit                 menu de la bande
*/
class GraphView final : public juce::Component,
                        private juce::Timer
{
public:
    GraphView (PhocusAudioProcessor&, PhocusLookAndFeel&);
    ~GraphView() override;

    std::function<void (int)> onBandSelected;
    void setSelectedBand (int band)                     { selected = band; repaint(); }
    int getSelectedBand() const noexcept                { return selected; }

    /** Plage de la réduction affichée (dB). */
    void setRange (float db)                            { rangeDb = db; repaint(); }
    float getRange() const noexcept                     { return rangeDb; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Pour les tests
    juce::Point<float> nodePosition (int band) const;
    float xForFreq (float hz) const;
    float freqForX (float x) const;

private:
    void timerCallback() override;

    juce::Rectangle<float> plotArea() const             { return getLocalBounds().toFloat().reduced (1.0f); }
    float yForSens (float db) const;
    float sensForY (float y) const;
    int hitNode (juce::Point<float>) const;
    juce::RangedAudioParameter* param (int band, const char* what) const;
    void showBandMenu (int band);

    PhocusAudioProcessor& processor;
    PhocusLookAndFeel& lf;

    float reduction[phocus::ResonanceEngine::displayPoints] {};
    float spectrum[phocus::ResonanceEngine::displayPoints] {};
    float rangeDb = 30.0f;
    int selected = 2, dragging = -1, hover = -1;
    float hoverX = -1.0f;
    bool listeningFromDrag = false;

    struct DragStart { float freq = 0, sens = 0, q = 1; juce::Point<float> mouse; };
    DragStart dragStart;
    std::vector<juce::RangedAudioParameter*> gestureParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphView)
};

//==============================================================================
/** La façade à taille fixe ; l'éditeur la met à l'échelle. */
class PhocusFacade final : public juce::Component,
                           private juce::Timer
{
public:
    static constexpr int width = 1180, height = 740;

    explicit PhocusFacade (PhocusAudioProcessor&);
    ~PhocusFacade() override;

    phragma::PresetBar& getPresetBar() noexcept         { return presetBar; }
    GraphView& getGraph() noexcept                      { return graph; }

    void selectBand (int band);
    int getSelectedBand() const noexcept                { return graph.getSelectedBand(); }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Pour les tests
    juce::Component* findControl (const juce::String& componentId);

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    /** Interrupteur à plusieurs positions relié à un paramètre à choix. */
    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    /** Bouton qui affiche un choix et ouvre un menu (qualité). */
    struct ChoiceMenu
    {
        juce::TextButton button;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void knob (juce::Slider&, const char* id, juce::Rectangle<int> area, bool bipolar = false, bool big = false);
    void toggle (juce::TextButton&, const char* id, juce::Rectangle<int> area, const juce::String& componentId);
    void choiceSwitch (ChoiceSwitch&, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area, bool vertical);
    void choiceMenu (ChoiceMenu&, const char* id, juce::Rectangle<int> area);
    void bindBandControls();

    void timerCallback() override;

    PhocusAudioProcessor& audioProcessor;
    PhocusLookAndFeel lookAndFeel;
    GraphView graph;

    // Principal, vitesse, stéréo, sortie
    juce::Slider depthKnob, sharpnessKnob, selectivityKnob, attackKnob, releaseKnob, linkKnob, balanceKnob, mixKnob, trimKnob;
    ChoiceSwitch modeSwitch, stereoSwitch;
    ChoiceMenu oversampleMenu, resolutionMenu, offlineOversampleMenu, offlineResolutionMenu;
    juce::TextButton sidechainButton { "SIDECHAIN" }, sidechainListenButton { "LISTEN" };
    juce::TextButton deltaButton { "DELTA" }, bypassButton { "BYPASS" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, abButton { "A" }, copyButton { "A > B" }, rangeButton { "30 dB" };

    // Bande choisie
    juce::TextButton bandChips[6];
    juce::TextButton bandOnButton { "ON" }, bandListenButton { "LISTEN" };
    juce::Slider bandFreqKnob, bandSensKnob, bandQKnob, bandBalanceKnob;
    ChoiceSwitch bandTypeSwitch, slopeSwitch;
    std::unique_ptr<APVTS::SliderAttachment> bandFreqAttachment, bandSensAttachment, bandQAttachment, bandBalanceAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> bandOnAttachment;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    float shownReduction = 0.0f, inLevel = -60.0f, outLevel = -60.0f;
    bool stereo = true;

    // Barre des presets et de la taille : déclarée en dernier, détruite en premier.
    phragma::PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhocusFacade)
};

//==============================================================================
class PhocusAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PhocusAudioProcessorEditor (PhocusAudioProcessor&);
    ~PhocusAudioProcessorEditor() override;

    void resized() override;

    PhocusFacade& getFacade() noexcept                  { return facade; }
    float getUiScale() const                            { return (float) getWidth() / (float) PhocusFacade::width; }
    void setUiScale (float scale);

private:
    PhocusAudioProcessor& audioProcessor;
    PhocusFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhocusAudioProcessorEditor)
};
