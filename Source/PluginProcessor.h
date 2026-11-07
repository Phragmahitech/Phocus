#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetLibrary.h"
#include "ResonanceEngine.h"

class PhocusAudioProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorListener
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)
    static constexpr const char* modeId              = "mode";           // SOFT / HARD
    static constexpr const char* depthId             = "depth";
    static constexpr const char* sharpnessId         = "sharpness";
    static constexpr const char* selectivityId       = "selectivity";
    static constexpr const char* attackId            = "attack";
    static constexpr const char* releaseId           = "release";
    static constexpr const char* stereoModeId        = "stereoMode";     // L/R, M/S
    static constexpr const char* linkId              = "link";
    static constexpr const char* balanceId           = "balance";
    static constexpr const char* oversampleId        = "oversample";
    static constexpr const char* resolutionId        = "resolution";
    static constexpr const char* offlineOversampleId = "offlineOversample";
    static constexpr const char* offlineResolutionId = "offlineResolution";
    static constexpr const char* sidechainId         = "sidechain";
    static constexpr const char* sidechainListenId   = "sidechainListen";
    static constexpr const char* mixId               = "mix";
    static constexpr const char* trimId              = "trim";
    static constexpr const char* deltaId             = "delta";
    static constexpr const char* bypassId            = "bypass";
    static constexpr const char* inputTrimId         = "inputTrim";      // caché dans l'interface (automation seulement)

    static constexpr const char* lowCutId            = "lowCut";
    static constexpr const char* lowCutFreqId        = "lowCutFreq";
    static constexpr const char* lowCutSlopeId       = "lowCutSlope";
    static constexpr const char* highCutId           = "highCut";
    static constexpr const char* highCutFreqId       = "highCutFreq";
    static constexpr const char* highCutSlopeId      = "highCutSlope";

    /** Bandes 1 à 4 : "b1On", "b1Type", "b1Freq", "b1Sens", "b1Q", "b1Balance"... */
    static juce::String bandId (int band, const char* what)   { return "b" + juce::String (band + 1) + what; }

    static constexpr int numBands = phocus::CurveSpec::numBands;
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f;

    /** Paramètres qui ne vont pas dans les presets ni dans l'historique (écoute, contournement). */
    static juce::StringArray transientParameters()      { return { deltaId, bypassId, sidechainListenId }; }

    static const juce::StringArray& bandTypeNames();
    static const juce::StringArray& slopeNames();

    //==========================================================================
    PhocusAudioProcessor();
    ~PhocusAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Phocus"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetLibrary presets;

    //==========================================================================
    // Pour l'interface
    const phocus::ResonanceEngine& getEngine() const noexcept   { return engine; }
    double getCurrentSampleRate() const noexcept                { return currentSampleRate; }

    /** Courbe de sensibilité telle que réglée (lue depuis les paramètres). */
    phocus::CurveSpec readCurve() const;

    /** Écoute d'une bande (-1 = aucune ; 0..3 = bandes, 4 = LOW CUT, 5 = HIGH CUT). Pas un paramètre. */
    void setBandListen (int band) noexcept                  { bandListen.store (band, std::memory_order_relaxed); }
    int getBandListen() const noexcept                      { return bandListen.load (std::memory_order_relaxed); }

    /** Vrai si l'hôte envoie un signal sur l'entrée sidechain. */
    bool isSidechainConnected() const noexcept              { return sidechainConnected.load (std::memory_order_relaxed); }

    /** Réduction maximale (dB) affichée en tournant MIX. */
    float getPeakReduction() const noexcept                 { return peakReduction.load (std::memory_order_relaxed); }

    float takeInputPeak() noexcept                          { return inputPeak.exchange (0.0f, std::memory_order_relaxed); }
    float takeOutputPeak() noexcept                         { return outputPeak.exchange (0.0f, std::memory_order_relaxed); }

    //==========================================================================
    // Annuler / rétablir, comparaison A / B (fil de l'interface)
    bool canUndo() const noexcept                           { return ! undoStack.empty(); }
    bool canRedo() const noexcept                           { return ! redoStack.empty(); }
    void undo();
    void redo();

    int getAbSlot() const noexcept                          { return abSlot; }
    void switchAb();
    void copyAbToOther();

    /** Points de départ d'usine. */
    static int getNumFactoryPresets();
    static juce::String getFactoryPresetName (int index);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    PresetLibrary::Factory makeFactory();
    void applyFactory (int index);
    int matchingFactory() const;

    using Snapshot = std::vector<float>;
    Snapshot takeSnapshot() const;
    void applySnapshot (const Snapshot&);
    bool isHistoryParameter (const juce::AudioProcessorParameter&) const;

    // AudioProcessorListener (historique)
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex) override;

    phocus::ResonanceEngine engine;
    phocus::DelayLine dryDelay[2], sideDelay[2];

    juce::AudioBuffer<float> wetBuffer, dryBuffer, sideBuffer;
    double currentSampleRate = 44100.0;
    int latency = 2048;

    float mixNow = 1.0f, trimNow = 1.0f, bypassNow = 0.0f, smoothing = 1.0f;

    std::atomic<int> bandListen { -1 };
    std::atomic<bool> sidechainConnected { false };
    std::atomic<float> peakReduction { 0.0f };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    // Historique
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot slots[2];
    int abSlot = 0;
    bool applyingHistory = false;
    juce::uint32 lastSnapshotTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhocusAudioProcessor)
};
