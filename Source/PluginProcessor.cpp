#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Param = juce::AudioProcessorValueTreeState;

    juce::String hzToText (float hz)
    {
        if (hz >= 1000.0f)
        {
            // 6k3 = 6300 Hz, 12k = 12000 Hz
            const int hundreds = juce::roundToInt (hz / 100.0f);
            const int k = hundreds / 10, rest = hundreds % 10;
            return juce::String (k) + "k" + (rest != 0 ? juce::String (rest) : juce::String());
        }
        return juce::String (juce::roundToInt (hz));
    }

    float textToHz (const juce::String& raw)
    {
        auto t = raw.trim().toLowerCase().replace (",", ".").removeCharacters (" hz");
        if (t.containsChar ('k'))
        {
            const auto left = t.upToFirstOccurrenceOf ("k", false, false);
            const auto right = t.fromFirstOccurrenceOf ("k", false, false);
            if (left.containsChar ('.'))
                return left.getFloatValue() * 1000.0f;
            return left.getFloatValue() * 1000.0f + (right.isNotEmpty() ? right.getFloatValue() * std::pow (10.0f, 3.0f - (float) right.length()) : 0.0f);
        }
        return t.getFloatValue();
    }

    juce::NormalisableRange<float> logRange (float lo, float hi, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                           juce::NormalisableRange<float> range, float def,
                                                           std::function<juce::String (float, int)> toText,
                                                           std::function<float (const juce::String&)> fromText = {})
    {
        auto attr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText));
        if (fromText)
            attr = attr.withValueFromStringFunction (std::move (fromText));
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attr);
    }

    auto oneDecimal = [] (float v, int) { return juce::String (v, 1); };
    auto percent    = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto decibels   = [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto millis     = [] (float v, int) { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    auto hertz      = [] (float v, int) { return hzToText (v); };
    auto balanceTxt = [] (float v, int)
    {
        const int a = v <= 0.0f ? 100 : juce::roundToInt (100.0f - v);
        const int b = v >= 0.0f ? 100 : juce::roundToInt (100.0f + v);
        return juce::String (a) + " / " + juce::String (b);
    };
}

const juce::StringArray& PhocusAudioProcessor::bandTypeNames()
{
    static const juce::StringArray names { "PEAK", "LOW SHELF", "HIGH SHELF", "BAND SHELF", "REJECT", "TILT" };
    return names;
}

const juce::StringArray& PhocusAudioProcessor::slopeNames()
{
    static const juce::StringArray names { "6 dB", "12 dB", "24 dB", "48 dB" };
    return names;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PhocusAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&layout] (auto p) { layout.add (std::move (p)); };

    // Principal
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { modeId, 1 }, "Mode", juce::StringArray { "SOFT", "HARD" }, 0));
    add (floatParam (depthId, "Depth", { 0.0f, 20.0f, 0.01f }, 5.0f, oneDecimal));
    add (floatParam (sharpnessId, "Sharpness", { 0.0f, 10.0f, 0.01f }, 4.0f, oneDecimal));
    add (floatParam (selectivityId, "Selectivity", { 0.0f, 10.0f, 0.01f }, 3.5f, oneDecimal));

    // Vitesse
    add (floatParam (attackId, "Attack", logRange (0.5f, 50.0f), 5.0f, millis));
    add (floatParam (releaseId, "Release", logRange (5.0f, 500.0f), 50.0f, millis));

    // Stéréo
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stereoModeId, 1 }, "Stereo Mode", juce::StringArray { "L/R", "M/S" }, 1));
    add (floatParam (linkId, "Stereo Link", { 0.0f, 100.0f, 0.1f }, 100.0f, percent));
    add (floatParam (balanceId, "Stereo Balance", { -100.0f, 100.0f, 0.1f }, 0.0f, balanceTxt));

    // Qualité
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { oversampleId, 1 }, "Oversample", juce::StringArray { "1X", "2X", "4X" }, 0));
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { resolutionId, 1 }, "Resolution", juce::StringArray { "ECO", "NORMAL", "HIGH", "ULTRA" }, 1));
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { offlineOversampleId, 1 }, "Offline Oversample", juce::StringArray { "SAME", "1X", "2X", "4X" }, 0));
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { offlineResolutionId, 1 }, "Offline Resolution", juce::StringArray { "SAME", "ECO", "NORMAL", "HIGH", "ULTRA" }, 0));

    // Sidechain et sortie
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { sidechainId, 1 }, "Sidechain", false));
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { sidechainListenId, 1 }, "Sidechain Listen", false));
    add (floatParam (mixId, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, percent));
    add (floatParam (trimId, "Trim", { -24.0f, 12.0f, 0.01f }, 0.0f, decibels));
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { deltaId, 1 }, "Delta", false));
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bypassId, 1 }, "Bypass", false));
    add (floatParam (inputTrimId, "Input Trim", { -24.0f, 24.0f, 0.01f }, 0.0f, decibels));

    // Coupures de la courbe de sensibilité
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lowCutId, 1 }, "Low Cut", false));
    add (floatParam (lowCutFreqId, "Low Cut Freq", logRange (minFreq, maxFreq, 0.1f), 30.0f, hertz, textToHz));
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { lowCutSlopeId, 1 }, "Low Cut Slope", slopeNames(), 1));
    add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { highCutId, 1 }, "High Cut", false));
    add (floatParam (highCutFreqId, "High Cut Freq", logRange (minFreq, maxFreq, 0.1f), 18000.0f, hertz, textToHz));
    add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { highCutSlopeId, 1 }, "High Cut Slope", slopeNames(), 1));

    // Bandes
    struct BandDefault { int type; float freq, sens, q; };
    static constexpr BandDefault defaults[numBands] = {
        { phocus::peak,      200.0f,   0.0f, 1.0f },
        { phocus::peak,      800.0f,   0.0f, 1.0f },
        { phocus::peak,      5000.0f,  3.0f, 1.0f },
        { phocus::highShelf, 12000.0f, 0.0f, 0.7f } };

    for (int b = 0; b < numBands; ++b)
    {
        const auto name = "Band " + juce::String (b + 1) + " ";
        add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (b, "On"), 1 }, name + "On", true));
        add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandId (b, "Type"), 1 }, name + "Type", bandTypeNames(), defaults[b].type));
        add (floatParam (bandId (b, "Freq"), name + "Freq", logRange (minFreq, maxFreq, 0.1f), defaults[b].freq, hertz, textToHz));
        add (floatParam (bandId (b, "Sens"), name + "Sens", { -24.0f, 24.0f, 0.01f }, defaults[b].sens, decibels));
        add (floatParam (bandId (b, "Q"), name + "Q", logRange (0.1f, 10.0f, 0.001f), defaults[b].q,
                         [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1); }));
        add (floatParam (bandId (b, "Balance"), name + "Balance", { -100.0f, 100.0f, 0.1f }, 0.0f, balanceTxt));
    }

    return layout;
}

//==============================================================================
// Points de départ d'usine (réglés à l'oreille... pas encore : réglés sur des mesures, à écouter)
namespace
{
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<juce::String, float>> values;   // le reste : valeur par défaut
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> list {
            { "DEFAULT", {} },
            { "DE-ESS", { { "depth", 6.0f }, { "sharpness", 3.0f }, { "selectivity", 2.5f }, { "attack", 1.0f }, { "release", 30.0f },
                          { "lowCut", 1.0f }, { "lowCutFreq", 3500.0f }, { "lowCutSlope", 2.0f },
                          { "b3Freq", 7000.0f }, { "b3Sens", 5.0f }, { "b3Q", 0.8f } } },
            { "VOCAL SMOOTH", { { "depth", 5.0f }, { "sharpness", 5.0f }, { "selectivity", 4.0f }, { "attack", 4.0f }, { "release", 60.0f },
                                { "lowCut", 1.0f }, { "lowCutFreq", 120.0f }, { "b3Freq", 3500.0f }, { "b3Sens", 3.0f } } },
            { "HARSH LEAD", { { "depth", 8.0f }, { "sharpness", 6.0f }, { "selectivity", 5.0f }, { "attack", 2.0f }, { "release", 40.0f },
                              { "lowCut", 1.0f }, { "lowCutFreq", 800.0f }, { "b3Freq", 3500.0f }, { "b3Sens", 4.0f }, { "b3Q", 1.2f } } },
            { "MUD CLEANER", { { "mode", 1.0f }, { "depth", 6.0f }, { "sharpness", 3.0f }, { "selectivity", 2.0f }, { "attack", 20.0f }, { "release", 150.0f },
                               { "lowCut", 1.0f }, { "lowCutFreq", 60.0f }, { "highCut", 1.0f }, { "highCutFreq", 900.0f }, { "highCutSlope", 2.0f },
                               { "b1Freq", 300.0f }, { "b1Sens", 4.0f }, { "b3Sens", 0.0f }, { "oversample", 2.0f } } },
            { "KICK BOOM", { { "depth", 7.0f }, { "sharpness", 7.0f }, { "selectivity", 6.0f }, { "attack", 15.0f }, { "release", 120.0f },
                             { "highCut", 1.0f }, { "highCutFreq", 400.0f }, { "highCutSlope", 2.0f }, { "b3Sens", 0.0f }, { "oversample", 2.0f } } },
            { "MIX BUS GENTLE", { { "depth", 3.0f }, { "sharpness", 2.0f }, { "selectivity", 3.0f }, { "attack", 10.0f }, { "release", 120.0f },
                                  { "mix", 60.0f }, { "resolution", 2.0f } } },
            { "DE-BLEED (SIDECHAIN)", { { "mode", 1.0f }, { "depth", 9.0f }, { "sharpness", 4.0f }, { "selectivity", 1.0f }, { "attack", 2.0f },
                                        { "release", 80.0f }, { "sidechain", 1.0f }, { "b3Sens", 0.0f } } },
        };
        return list;
    }
}

int PhocusAudioProcessor::getNumFactoryPresets()                  { return (int) factoryPresets().size(); }
juce::String PhocusAudioProcessor::getFactoryPresetName (int i)   { return factoryPresets()[(size_t) juce::jlimit (0, getNumFactoryPresets() - 1, i)].name; }

void PhocusAudioProcessor::applyFactory (int index)
{
    const auto& preset = factoryPresets()[(size_t) juce::jlimit (0, getNumFactoryPresets() - 1, index)];
    const auto excluded = transientParameters();

    for (auto* p : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr || excluded.contains (ranged->getParameterID()))
            continue;

        float normalised = ranged->getDefaultValue();
        for (const auto& [id, value] : preset.values)
            if (id == ranged->getParameterID())
                normalised = ranged->convertTo0to1 (value);

        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }
}

int PhocusAudioProcessor::matchingFactory() const
{
    const auto excluded = transientParameters();

    for (int i = 0; i < getNumFactoryPresets(); ++i)
    {
        const auto& preset = factoryPresets()[(size_t) i];
        bool same = true;

        for (auto* p : getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (ranged == nullptr || excluded.contains (ranged->getParameterID()))
                continue;

            float target = ranged->getDefaultValue();
            for (const auto& [id, value] : preset.values)
                if (id == ranged->getParameterID())
                    target = ranged->convertTo0to1 (value);

            if (std::abs (ranged->getValue() - target) > 1.0e-4f)
            {
                same = false;
                break;
            }
        }

        if (same)
            return i;
    }
    return -1;
}

PresetLibrary::Factory PhocusAudioProcessor::makeFactory()
{
    return { [] { return getNumFactoryPresets(); },
             [] (int i) { return getFactoryPresetName (i); },
             [this] (int i) { applyFactory (i); },
             [this] { return matchingFactory(); } };
}

//==============================================================================
PhocusAudioProcessor::PhocusAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Phocus", createParameterLayout()),
      presets (apvts, "Phocus", ".phfpreset", makeFactory(), transientParameters(), "UNTITLED")
{
    presets.restoreFromState();   // projet neuf : DEFAULT
    addListener (this);
}

PhocusAudioProcessor::~PhocusAudioProcessor()
{
    removeListener (this);
}

double PhocusAudioProcessor::getTailLengthSeconds() const
{
    return (double) latency / juce::jmax (1.0, currentSampleRate);
}

bool PhocusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.getChannelSet (true, 1);
        if (! side.isDisabled() && side != juce::AudioChannelSet::mono() && side != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void PhocusAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int channels = juce::jlimit (1, 2, getMainBusNumOutputChannels());

    engine.prepare (sampleRate, channels);
    latency = engine.getLatency();
    setLatencySamples (latency);

    for (auto& d : dryDelay)  d.prepare (latency);
    for (auto& d : sideDelay) d.prepare (latency);

    const int block = juce::jmax (1, samplesPerBlock);
    wetBuffer.setSize (2, block, false, false, true);
    dryBuffer.setSize (2, block, false, false, true);
    sideBuffer.setSize (2, block, false, false, true);

    smoothing = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));   // 20 ms
    mixNow = apvts.getRawParameterValue (mixId)->load() * 0.01f;
    trimNow = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (trimId)->load());
    bypassNow = apvts.getRawParameterValue (bypassId)->load() > 0.5f ? 1.0f : 0.0f;
}

void PhocusAudioProcessor::reset()
{
    engine.reset();
    for (auto& d : dryDelay)  d.reset();
    for (auto& d : sideDelay) d.reset();
}

phocus::CurveSpec PhocusAudioProcessor::readCurve() const
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

    phocus::CurveSpec c;
    c.lowCut  = { raw (lowCutId) > 0.5f,  raw (lowCutFreqId),  juce::roundToInt (raw (lowCutSlopeId)) };
    c.highCut = { raw (highCutId) > 0.5f, raw (highCutFreqId), juce::roundToInt (raw (highCutSlopeId)) };
    for (int b = 0; b < numBands; ++b)
        c.bands[b] = { raw (bandId (b, "On")) > 0.5f, juce::roundToInt (raw (bandId (b, "Type"))), raw (bandId (b, "Freq")),
                       raw (bandId (b, "Sens")), raw (bandId (b, "Q")), raw (bandId (b, "Balance")) * 0.01f };
    return c;
}

void PhocusAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int channels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
    if (n == 0)
        return;

    if (wetBuffer.getNumSamples() < n)
    {
        // L'hôte envoie un bloc plus grand que prévu : rare, on agrandit (pas en temps réel strict).
        wetBuffer.setSize (2, n, false, false, true);
        dryBuffer.setSize (2, n, false, false, true);
        sideBuffer.setSize (2, n, false, false, true);
    }

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // Réglages du moteur (qualité hors ligne si l'hôte rend hors ligne)
    phocus::EngineSettings s;
    s.hard = raw (modeId) > 0.5f;
    s.depth = raw (depthId);
    s.sharpness = raw (sharpnessId);
    s.selectivity = raw (selectivityId);
    s.attackMs = raw (attackId);
    s.releaseMs = raw (releaseId);
    int os = juce::roundToInt (raw (oversampleId)), res = juce::roundToInt (raw (resolutionId));
    if (isNonRealtime())
    {
        const int offOs = juce::roundToInt (raw (offlineOversampleId)), offRes = juce::roundToInt (raw (offlineResolutionId));
        if (offOs > 0) os = offOs - 1;
        if (offRes > 0) res = offRes - 1;
    }
    s.oversample = 1 << juce::jlimit (0, 2, os);
    s.resolution = juce::jlimit (0, 3, res);
    s.midSide = raw (stereoModeId) > 0.5f;
    s.link = raw (linkId) * 0.01f;
    s.balance = raw (balanceId) * 0.01f;
    s.inputTrimDb = raw (inputTrimId);
    engine.setSettings (s);
    engine.setCurve (readCurve());
    const int listenBand = bandListen.load (std::memory_order_relaxed);
    engine.setListen (listenBand);

    // Entrée et sidechain
    for (int c = 0; c < channels; ++c)
        dryBuffer.copyFrom (c, 0, buffer, c, 0, n);

    float inPeak = 0.0f;
    for (int c = 0; c < channels; ++c)
        inPeak = juce::jmax (inPeak, buffer.getMagnitude (c, 0, n));
    inputPeak.store (juce::jmax (inputPeak.load (std::memory_order_relaxed), inPeak), std::memory_order_relaxed);

    int sideChannels = 0;
    if (getBusCount (true) > 1)
    {
        auto sideBus = getBusBuffer (buffer, true, 1);
        sideChannels = juce::jmin (2, sideBus.getNumChannels());
        for (int c = 0; c < sideChannels; ++c)
            sideBuffer.copyFrom (c, 0, sideBus, c, 0, n);
    }
    sidechainConnected.store (sideChannels > 0, std::memory_order_relaxed);
    const bool useSide = sideChannels > 0 && raw (sidechainId) > 0.5f;
    const bool listenSide = useSide && raw (sidechainListenId) > 0.5f;

    // Mouillé
    const float* in[2] = { dryBuffer.getReadPointer (0), dryBuffer.getReadPointer (channels > 1 ? 1 : 0) };
    const float* side[2] = { sideBuffer.getReadPointer (0), sideBuffer.getReadPointer (sideChannels > 1 ? 1 : 0) };
    float* wet[2] = { wetBuffer.getWritePointer (0), wetBuffer.getWritePointer (1) };
    engine.process (in, useSide ? side : nullptr, sideChannels, wet, n);

    // Sortie : mix, trim (mouillé seulement), delta, écoute, contournement doux
    const float mixTarget = raw (mixId) * 0.01f;
    const float trimTarget = juce::Decibels::decibelsToGain (raw (trimId));
    const float bypassTarget = raw (bypassId) > 0.5f ? 1.0f : 0.0f;
    const bool delta = raw (deltaId) > 0.5f;
    const bool listening = listenBand >= 0;

    float outPeak = 0.0f;
    float mixS = mixNow, trimS = trimNow, bypassS = bypassNow;

    for (int i = 0; i < n; ++i)
    {
        mixS += smoothing * (mixTarget - mixS);
        trimS += smoothing * (trimTarget - trimS);
        bypassS += smoothing * (bypassTarget - bypassS);

        for (int c = 0; c < channels; ++c)
        {
            const float dry = dryDelay[c].process (dryBuffer.getSample (c, i), latency);
            const float sc = sideDelay[c].process (sideChannels > 0 ? sideBuffer.getSample (juce::jmin (c, sideChannels - 1), i) : 0.0f, latency);
            const float w = wetBuffer.getSample (c, i);

            float y;
            if (listenSide)       y = sc;
            else if (listening)   y = w;                       // le moteur rend déjà ce que la bande retire
            else if (delta)       y = mixS * (dry - w);
            else                  y = dry * (1.0f - mixS) + w * mixS * trimS;

            y = bypassS * dry + (1.0f - bypassS) * y;
            buffer.setSample (c, i, y);
            outPeak = juce::jmax (outPeak, std::abs (y));
        }
    }

    mixNow = mixS; trimNow = trimS; bypassNow = bypassS;

    for (int c = channels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    outputPeak.store (juce::jmax (outputPeak.load (std::memory_order_relaxed), outPeak), std::memory_order_relaxed);

    float peakRed = 0.0f;
    for (int i = 0; i < phocus::ResonanceEngine::displayPoints; ++i)
        peakRed = juce::jmax (peakRed, engine.getDisplayReduction (i));
    peakReduction.store (peakRed * mixTarget, std::memory_order_relaxed);
}

void PhocusAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Contournement par l'hôte : le signal sec, retardé de la latence (pas de décalage à la remise en route).
    const int n = buffer.getNumSamples();
    const int channels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
    for (int c = 0; c < channels; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = dryDelay[c].process (d[i], latency);
    }
    for (int c = channels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    engine.reset();
}

//==============================================================================
// Historique et A / B
bool PhocusAudioProcessor::isHistoryParameter (const juce::AudioProcessorParameter& p) const
{
    if (auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (&p))
        return ! transientParameters().contains (ranged->getParameterID());
    return false;
}

PhocusAudioProcessor::Snapshot PhocusAudioProcessor::takeSnapshot() const
{
    Snapshot s;
    for (auto* p : getParameters())
        s.push_back (p->getValue());
    return s;
}

void PhocusAudioProcessor::applySnapshot (const Snapshot& s)
{
    const auto& params = getParameters();
    if (s.size() != (size_t) params.size())
        return;

    const juce::ScopedValueSetter<bool> guard (applyingHistory, true);
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        if (! isHistoryParameter (*p) || std::abs (p->getValue() - s[(size_t) i]) < 1.0e-6f)
            continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (s[(size_t) i]);
        p->endChangeGesture();
    }
}

void PhocusAudioProcessor::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex)
{
    if (applyingHistory || ! juce::MessageManager::existsAndIsCurrentThread())
        return;

    const auto& params = getParameters();
    if (! juce::isPositiveAndBelow (parameterIndex, params.size()) || ! isHistoryParameter (*params[parameterIndex]))
        return;

    // Un chargement de preset touche tous les paramètres en un instant : une seule étape.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastSnapshotTime < 150 && ! undoStack.empty())
        return;
    lastSnapshotTime = now;

    undoStack.push_back (takeSnapshot());
    if (undoStack.size() > 64)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PhocusAudioProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (takeSnapshot());
    const auto s = undoStack.back();
    undoStack.pop_back();
    applySnapshot (s);
}

void PhocusAudioProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (takeSnapshot());
    const auto s = redoStack.back();
    redoStack.pop_back();
    applySnapshot (s);
}

void PhocusAudioProcessor::switchAb()
{
    slots[abSlot] = takeSnapshot();
    abSlot = 1 - abSlot;
    if (slots[abSlot].empty())
        slots[abSlot] = slots[1 - abSlot];
    applySnapshot (slots[abSlot]);
    apvts.state.setProperty ("abSlot", abSlot, nullptr);
}

void PhocusAudioProcessor::copyAbToOther()
{
    slots[1 - abSlot] = takeSnapshot();
}

//==============================================================================
juce::AudioProcessorEditor* PhocusAudioProcessor::createEditor()
{
    return new PhocusAudioProcessorEditor (*this);
}

void PhocusAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Le réglage A / B qui n'est pas affiché est gardé avec le projet
    slots[abSlot] = takeSnapshot();
    const auto& other = slots[1 - abSlot];
    juce::String text;
    for (auto v : other)
        text << juce::String (v, 6) << " ";
    state.setProperty ("abOther", text.trim(), nullptr);
    state.setProperty ("abSlot", abSlot, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhocusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        presets.restoreFromState();

        abSlot = juce::jlimit (0, 1, (int) apvts.state.getProperty ("abSlot", 0));
        slots[abSlot] = takeSnapshot();

        juce::StringArray values;
        values.addTokens (apvts.state.getProperty ("abOther").toString(), " ", {});
        Snapshot other;
        for (auto& v : values)
            other.push_back (juce::jlimit (0.0f, 1.0f, v.getFloatValue()));
        slots[1 - abSlot] = other.size() == (size_t) getParameters().size() ? other : Snapshot();

        undoStack.clear();
        redoStack.clear();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhocusAudioProcessor();
}
