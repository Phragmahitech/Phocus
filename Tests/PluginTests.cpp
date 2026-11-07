// Tests du plugin complet (processeur JUCE + interface), sans DAW.
// Usage : PhocusPluginTests [dossier où écrire les captures de l'interface]

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

static int failures = 0;

static void check (bool condition, const juce::String& message)
{
    if (! condition)
    {
        ++failures;
        std::cout << "  ECHEC : " << message << std::endl;
    }
}

using Ids = PhocusAudioProcessor;

static PhocusAudioProcessor& asPhocus (juce::AudioProcessor& p)    { return dynamic_cast<PhocusAudioProcessor&> (p); }

static void setParam (juce::AudioProcessor& p, const juce::String& id, float realValue)
{
    auto* param = asPhocus (p).apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

static float getParam (juce::AudioProcessor& p, const juce::String& id)
{
    return asPhocus (p).apvts.getRawParameterValue (id)->load();
}

static std::unique_ptr<juce::AudioProcessor> makePlugin (double fs = 48000.0, int block = 512, bool withSidechain = false)
{
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto layout = p->getBusesLayout();
    if (withSidechain && layout.inputBuses.size() > 1)
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    p->setBusesLayout (layout);
    p->setRateAndBufferSizeDetails (fs, block);
    p->prepareToPlay (fs, block);
    return p;
}

/** Traite in (2 canaux, plus 2 de sidechain si présents) bloc par bloc. */
static juce::AudioBuffer<float> run (juce::AudioProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                     const juce::AudioBuffer<float>* side = nullptr)
{
    const int total = p.getTotalNumInputChannels();
    juce::AudioBuffer<float> out (2, in.getNumSamples());
    juce::AudioBuffer<float> io (juce::jmax (2, total), block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        io.setSize (io.getNumChannels(), n, false, false, true);
        io.clear();
        for (int c = 0; c < 2; ++c)
            io.copyFrom (c, 0, in, c, pos, n);
        if (side != nullptr && total >= 4)
            for (int c = 0; c < 2; ++c)
                io.copyFrom (2 + c, 0, *side, c, pos, n);
        p.processBlock (io, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

static juce::AudioBuffer<float> noiseWithTone (int n, double fs, float toneHz, float toneAmp, unsigned seed = 1)
{
    juce::AudioBuffer<float> b (2, n);
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 0.08f);
    float lp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        lp = 0.7f * lp + 0.3f * nd (rng);
        const float t = toneAmp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * toneHz * i / fs);
        b.setSample (0, i, lp + t);
        b.setSample (1, i, lp * 0.9f + t);
    }
    return b;
}

static double toneLevel (const juce::AudioBuffer<float>& b, int ch, int from, int count, double hz, double fs)
{
    double c = 0.0, s = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (from + i) / fs;
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / count);
        c += b.getSample (ch, from + i) * std::cos (ph) * w;
        s += b.getSample (ch, from + i) * std::sin (ph) * w;
    }
    return juce::Decibels::gainToDecibels (2.0 * std::sqrt (c * c + s * s) / (0.5 * count), -200.0);
}

//==============================================================================
static void testParameters()
{
    std::cout << "- parametres" << std::endl;
    auto p = makePlugin();
    const auto& params = p->getParameters();
    check (params.size() == 50, "50 parametres attendus, " + juce::String (params.size()));

    juce::StringArray ids;
    for (auto* prm : params)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            check (! ids.contains (r->getParameterID()), "identifiant en double " + r->getParameterID());
            ids.add (r->getParameterID());
            const auto text = r->getText (r->getValue(), 32);
            check (text.isNotEmpty(), "texte vide " + r->getParameterID());
        }

    for (auto id : { "mode", "depth", "sharpness", "selectivity", "attack", "release", "stereoMode", "link", "balance", "oversample",
                     "resolution", "offlineOversample", "offlineResolution", "sidechain", "sidechainListen", "mix", "trim", "delta",
                     "bypass", "inputTrim", "lowCut", "lowCutFreq", "lowCutSlope", "highCut", "highCutFreq", "highCutSlope",
                     "b1On", "b1Type", "b1Freq", "b1Sens", "b1Q", "b1Balance", "b4Balance" })
        check (ids.contains (id), juce::String ("parametre absent : ") + id);

    // Fréquences : écriture façon 6k3
    auto* f = asPhocus (*p).apvts.getParameter ("b1Freq");
    check (std::abs (f->convertFrom0to1 (f->getValueForText ("6k3")) - 6300.0f) < 5.0f, "6k3 = 6300 Hz");
    check (std::abs (f->convertFrom0to1 (f->getValueForText ("6,3k")) - 6300.0f) < 5.0f, "6,3k = 6300 Hz");
    check (f->getText (f->convertTo0to1 (6300.0f), 10) == "6k3", "6300 Hz affiche 6k3 : " + f->getText (f->convertTo0to1 (6300.0f), 10));

    check (p->getLatencySamples() == 2048, "latence 2048 a 48 kHz");
    auto q = makePlugin (96000.0);
    check (q->getLatencySamples() == 4096, "latence 4096 a 96 kHz");
}

static void testProcessing()
{
    std::cout << "- traitement : transparence, reduction, mix, delta, bypass, trim" << std::endl;
    const double fs = 48000.0;
    const auto in = noiseWithTone (96000, fs, 3000.0f, 0.3f);
    const int lat = 2048;

    auto delayedIn = [&] (int ch, int i) { return i >= lat ? in.getSample (ch, i - lat) : 0.0f; };

    // Depth 0 : transparent (retard près)
    {
        auto p = makePlugin();
        setParam (*p, Ids::depthId, 0.0f);
        const auto out = run (*p, in, 512);
        double err = 0.0;
        for (int i = 6000; i < in.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                err = juce::jmax (err, (double) std::abs (out.getSample (c, i) - delayedIn (c, i)));
        check (err < 5.0e-4, "depth 0 transparent : " + juce::String (err));
    }

    // Défaut : le sinus baisse
    double processed;
    {
        auto p = makePlugin();
        const auto out = run (*p, in, 256);
        processed = toneLevel (out, 0, 48000, 40000, 3000.0, fs);
        juce::AudioBuffer<float> d (2, in.getNumSamples());
        for (int i = 0; i < in.getNumSamples(); ++i) d.setSample (0, i, delayedIn (0, i));
        const double original = toneLevel (d, 0, 48000, 40000, 3000.0, fs);
        std::cout << "    sinus : " << original << " dB -> " << processed << " dB" << std::endl;
        check (processed < original - 3.0, "le reglage par defaut reduit la resonance");
        check (asPhocus (*p).getPeakReduction() > 1.0f, "reduction max affichee");
    }

    // Mix 0 : sec
    {
        auto p = makePlugin();
        setParam (*p, Ids::mixId, 0.0f);
        const auto out = run (*p, in, 512);
        double err = 0.0;
        for (int i = 24000; i < in.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - delayedIn (0, i)));
        check (err < 1.0e-5, "mix 0 = signal sec retarde");
    }

    // Delta : sec - mouillé ; delta + sortie = sec
    {
        auto p = makePlugin();
        const auto wet = run (*p, in, 512);
        auto q = makePlugin();
        setParam (*q, Ids::deltaId, 1.0f);
        const auto delta = run (*q, in, 512);
        double err = 0.0;
        for (int i = 6000; i < in.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (wet.getSample (0, i) + delta.getSample (0, i) - delayedIn (0, i)));
        check (err < 1.0e-4, "delta + sortie = sec : " + juce::String (err));
    }

    // Bypass doux : sec retardé
    {
        auto p = makePlugin();
        setParam (*p, Ids::bypassId, 1.0f);
        p->prepareToPlay (fs, 512);
        const auto out = run (*p, in, 512);
        double err = 0.0;
        for (int i = 6000; i < in.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - delayedIn (0, i)));
        check (err < 1.0e-5, "bypass = sec retarde");
    }

    // Bypass de l'hôte : même retard
    {
        auto p = makePlugin();
        juce::AudioBuffer<float> io (2, 512);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> out (2, 8192);
        for (int pos = 0; pos < 8192; pos += 512)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, 512);
            p->processBlockBypassed (io, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, 512);
        }
        double err = 0.0;
        for (int i = 3000; i < 8192; ++i)
            err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - delayedIn (0, i)));
        check (err < 1.0e-6, "bypass de l'hote : retard garde");
    }

    // Trim : seulement le mouillé (à depth 0, mouillé = sec, donc -6 dB = moitié)
    {
        auto p = makePlugin();
        setParam (*p, Ids::depthId, 0.0f);
        setParam (*p, Ids::trimId, -6.0206f);
        const auto out = run (*p, in, 512);
        double err = 0.0;
        for (int i = 8000; i < in.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - 0.5f * delayedIn (0, i)));
        check (err < 1.0e-3, "trim -6 dB sur le mouille");
    }

    // Mono
    {
        std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->setBusesLayout (layout), "mono accepte");
        p->prepareToPlay (fs, 512);
        juce::AudioBuffer<float> io (1, 512);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
        {
            io.copyFrom (0, 0, in, 0, pos, 512);
            p->processBlock (io, midi);
            for (int i = 0; i < 512; ++i) finite = finite && std::isfinite (io.getSample (0, i));
        }
        check (finite, "mono : sortie finie");
    }

    // Qualité hors ligne
    {
        auto p = makePlugin();
        setParam (*p, Ids::offlineOversampleId, 3.0f);   // 4X
        setParam (*p, Ids::offlineResolutionId, 4.0f);   // ULTRA
        p->setNonRealtime (true);
        run (*p, in, 512);
        check (asPhocus (*p).getEngine().getFftSize() == 8192, "hors ligne : oversample 4X");
        check (asPhocus (*p).getEngine().getHop() == 64, "hors ligne : resolution ULTRA");
        p->setNonRealtime (false);
        run (*p, in, 512);
        check (asPhocus (*p).getEngine().getFftSize() == 2048, "temps reel : oversample 1X");
    }
}

static void testSidechain()
{
    std::cout << "- sidechain" << std::endl;
    const double fs = 48000.0;
    auto p = makePlugin (fs, 512, true);
    check (p->getTotalNumInputChannels() == 4, "entree sidechain stereo");

    const auto main = noiseWithTone (96000, fs, 2000.0f, 0.03f, 3);
    const auto key = noiseWithTone (96000, fs, 2000.0f, 0.5f, 4);

    setParam (*p, Ids::depthId, 10.0f);
    const auto without = run (*p, main, 512, &key);
    check (asPhocus (*p).isSidechainConnected(), "sidechain vu comme branche");

    auto q = makePlugin (fs, 512, true);
    setParam (*q, Ids::depthId, 10.0f);
    setParam (*q, Ids::sidechainId, 1.0f);
    const auto with = run (*q, main, 512, &key);

    const double a = toneLevel (without, 0, 48000, 40000, 2000.0, fs), b = toneLevel (with, 0, 48000, 40000, 2000.0, fs);
    std::cout << "    2 kHz : cle coupee " << a << " dB, cle active " << b << " dB" << std::endl;
    check (b < a - 3.0, "la cle commande la reduction");

    setParam (*q, Ids::sidechainListenId, 1.0f);
    const auto heard = run (*q, main, 512, &key);
    double err = 0.0;
    for (int i = 6000; i < 96000; ++i)
        err = juce::jmax (err, (double) std::abs (heard.getSample (0, i) - key.getSample (0, i - 2048)));
    check (err < 1.0e-5, "ecoute du sidechain");
}

static void testHistory()
{
    std::cout << "- annuler / retablir, A / B, etat" << std::endl;
    auto p = makePlugin();
    auto& ph = asPhocus (*p);
    auto* depth = ph.apvts.getParameter (Ids::depthId);

    auto turn = [] (juce::RangedAudioParameter* prm, float real)
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
        prm->endChangeGesture();
        juce::Thread::sleep (200);
    };

    check (! ph.canUndo(), "historique vide au depart");
    turn (depth, 8.0f);
    turn (depth, 12.0f);
    check (ph.canUndo(), "historique apres un reglage");
    ph.undo();
    check (std::abs (getParam (*p, Ids::depthId) - 8.0f) < 0.02f, "annuler : depth 8");
    ph.undo();
    check (std::abs (getParam (*p, Ids::depthId) - 5.0f) < 0.02f, "annuler : depth 5");
    ph.redo();
    check (std::abs (getParam (*p, Ids::depthId) - 8.0f) < 0.02f, "retablir : depth 8");

    // A / B
    ph.switchAb();                                    // B = copie de A
    check (ph.getAbSlot() == 1, "slot B");
    turn (depth, 2.0f);
    ph.switchAb();
    check (std::abs (getParam (*p, Ids::depthId) - 8.0f) < 0.02f, "retour en A");
    ph.switchAb();
    check (std::abs (getParam (*p, Ids::depthId) - 2.0f) < 0.02f, "B garde son reglage");

    // L'état garde A et B
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makePlugin();
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (asPhocus (*q).getAbSlot() == 1, "etat : slot B");
    check (std::abs (getParam (*q, Ids::depthId) - 2.0f) < 0.02f, "etat : depth");
    asPhocus (*q).switchAb();
    check (std::abs (getParam (*q, Ids::depthId) - 8.0f) < 0.02f, "etat : A recharge");

    // Le delta et le bypass ne sont pas dans l'historique
    turn (ph.apvts.getParameter (Ids::deltaId), 1.0f);
    ph.undo();
    check (getParam (*p, Ids::deltaId) > 0.5f, "delta hors historique");

    // États abîmés
    q->setStateInformation ("nimporte", 8);
    check (std::abs (getParam (*q, Ids::depthId) - 8.0f) < 0.02f, "etat invalide ignore");

    // Points de départ d'usine
    check (Ids::getNumFactoryPresets() == 8, "8 points de depart");
    ph.presets.loadFactory (1);
    check (ph.presets.getDisplayName() == "DE-ESS", "DE-ESS charge");
    check (std::abs (getParam (*p, Ids::lowCutFreqId) - 3500.0f) < 1.0f, "DE-ESS : low cut");
    check (! ph.presets.isModified(), "DE-ESS non modifie");
    check (getParam (*p, Ids::deltaId) > 0.5f, "un preset ne touche pas au delta");
}

static void testEditor (const juce::File& outputDir)
{
    std::cout << "- interface" << std::endl;
    auto p = makePlugin();
    auto& ph = asPhocus (*p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorAndMakeActive());
    auto* editor = dynamic_cast<PhocusAudioProcessorEditor*> (ed.get());
    check (editor != nullptr, "editeur");
    if (editor == nullptr)
        return;

    auto& facade = editor->getFacade();
    check (editor->getWidth() == PhocusFacade::width && editor->getHeight() == PhocusFacade::height, "taille");

    // Tout est dans la façade et rien ne se chevauche (hors panneau SAVE et écran)
    juce::Array<juce::Component*> visible;
    for (auto* c : facade.getChildren())
        if (c->isVisible())
        {
            check (facade.getLocalBounds().contains (c->getBounds()), "hors de la facade : " + c->getComponentID());
            visible.add (c);
        }
    for (int i = 0; i < visible.size(); ++i)
        for (int j = i + 1; j < visible.size(); ++j)
        {
            auto* a = visible[i]; auto* b = visible[j];
            if (a->getComponentID() == "graph" || b->getComponentID() == "graph")
            {
                auto* other = a->getComponentID() == "graph" ? b : a;
                if (other->getComponentID() == "range")
                    continue;
            }
            check (! a->getBounds().intersects (b->getBounds()), "chevauchement : " + a->getComponentID() + " / " + b->getComponentID());
        }

    // Bande choisie : les contrôles suivent
    facade.selectBand (0);
    auto& graph = facade.getGraph();
    const auto node = graph.nodePosition (0);
    check (std::abs (node.x - graph.xForFreq (200.0f)) < 1.0f, "noeud 1 a 200 Hz");

    // Glisser le nœud 1 vers la droite d'une octave
    {
        const auto start = graph.nodePosition (0);
        const float target = graph.xForFreq (400.0f);
        juce::MouseInputSource source = juce::Desktop::getInstance().getMainMouseSource();
        auto make = [&] (juce::Point<float> pos, juce::ModifierKeys mods)
        {
            return juce::MouseEvent (source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &graph, &graph, juce::Time::getCurrentTime(),
                                     start, juce::Time::getCurrentTime(), 1, false);
        };
        graph.mouseDown (make (start, juce::ModifierKeys::leftButtonModifier));
        graph.mouseDrag (make ({ target, start.y - 20.0f }, juce::ModifierKeys::leftButtonModifier));
        graph.mouseUp (make ({ target, start.y - 20.0f }, {}));
        check (std::abs (getParam (*p, Ids::bandId (0, "Freq")) - 400.0f) < 8.0f, "glisser : 400 Hz, " + juce::String (getParam (*p, Ids::bandId (0, "Freq"))));
        check (getParam (*p, Ids::bandId (0, "Sens")) > 1.0f, "glisser vers le haut : plus de sensibilite");

        // Alt : écoute pendant le glisser
        graph.mouseDown (make (graph.nodePosition (0), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)));
        check (ph.getBandListen() == 0, "alt : ecoute de la bande");
        graph.mouseUp (make (graph.nodePosition (0), {}));
        check (ph.getBandListen() == -1, "fin de l'ecoute");

        // Double-clic : éteint la bande
        graph.mouseDoubleClick (make (graph.nodePosition (0), juce::ModifierKeys::leftButtonModifier));
        check (getParam (*p, Ids::bandId (0, "On")) < 0.5f, "double-clic eteint la bande");
    }

    // Les coupures montrent la pente, pas le type
    facade.selectBand (4);
    check (facade.findControl ("lowCutSlope_0") != nullptr, "pentes de la coupure basse");
    check (facade.findControl ("b1Type_0") == nullptr, "types caches pour une coupure");
    facade.selectBand (2);
    check (facade.findControl ("b3Type_0") != nullptr, "types de la bande 3");

    // Choix SOFT / HARD par l'interface
    if (auto* hard = dynamic_cast<juce::Button*> (facade.findControl ("mode_1")))
    {
        hard->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (getParam (*p, Ids::modeId) > 0.5f, "bouton HARD");
    }
    else
        check (false, "bouton HARD absent");

    // Écoute de la bande par le bouton
    if (auto* listen = dynamic_cast<juce::Button*> (facade.findControl ("bandListen")))
    {
        listen->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (ph.getBandListen() == 2, "LISTEN de la bande 3");
        listen->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (ph.getBandListen() == -1, "LISTEN coupe");
    }

    // Captures
    const auto in = noiseWithTone (48000, 48000.0, 3000.0f, 0.3f);
    run (*p, in, 512);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    auto snapshot = [&] (const juce::String& name)
    {
        if (outputDir == juce::File())
            return;
        outputDir.createDirectory();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (outputDir.getChildFile (name + ".png"));
        if (os.openedOk())
        {
            os.setPosition (0);
            os.truncate();
            png.writeImageToStream (img, os);
        }
    };
    snapshot ("phocus");
    facade.selectBand (4);
    snapshot ("phocus-lowcut");

    editor->setUiScale (0.75f);
    snapshot ("phocus-75");

    p->editorBeingDeleted (editor);
    ed.reset();
}

static void testFuzz()
{
    std::cout << "- reglages au hasard, blocs de taille variable" << std::endl;
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (double fs : { 44100.0, 96000.0 })
    {
        auto p = makePlugin (fs, 1024, true);
        juce::AudioBuffer<float> io (4, 1024);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int iter = 0; iter < 300; ++iter)
        {
            for (auto* prm : p->getParameters())
                if (u (rng) < 0.1f)
                    prm->setValueNotifyingHost (u (rng));
            asPhocus (*p).setBandListen ((int) (u (rng) * 8.0f) - 2);
            const int n = 1 + (int) (u (rng) * 1023.0f);
            io.setSize (4, n, false, false, true);
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i < n; ++i)
                    io.setSample (c, i, (u (rng) - 0.5f) * (c < 2 ? 1.0f : 0.3f));
            p->processBlock (io, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    finite = finite && std::isfinite (io.getSample (c, i)) && std::abs (io.getSample (c, i)) < 20.0f;
        }
        check (finite, "sortie finie et bornee a " + juce::String ((int) fs));
    }
}

#include "PresetTests.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto outputDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])) : juce::File();

    testParameters();
    testProcessing();
    testSidechain();
    testHistory();
    testEditor (outputDir);
    testFuzz();
    testPresetsAndWindowSize<PhocusAudioProcessor, PhocusAudioProcessorEditor, PhocusFacade> (Ids::transientParameters(), outputDir,
                                                                                             Ids::getNumFactoryPresets());

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
