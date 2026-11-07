// Tests du moteur de Phocus (sans JUCE).
#include "ResonanceEngine.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace phocus;

static int failures = 0, checks = 0;

static void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  ECHEC : %s\n", what.c_str());
    }
}

static constexpr double pi = 3.14159265358979323846;

/** Amplitude d'une sinusoïde dans un signal (corrélation sur un nombre entier de périodes ~). */
static double toneAmplitude (const std::vector<float>& x, size_t from, size_t count, double hz, double fs)
{
    double c = 0.0, s = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double ph = 2.0 * pi * hz * (double) (from + i) / fs;
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) count);
        c += x[from + i] * std::cos (ph) * w;
        s += x[from + i] * std::sin (ph) * w;
    }
    return 2.0 * std::sqrt (c * c + s * s) / (0.5 * (double) count);
}

static double rms (const std::vector<float>& x, size_t from, size_t count)
{
    double e = 0.0;
    for (size_t i = 0; i < count; ++i)
        e += (double) x[from + i] * x[from + i];
    return std::sqrt (e / (double) count);
}

static double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-12)); }

/** Bruit rose approché (filtre de Paul Kellet). */
struct Pink
{
    std::mt19937 rng { 1234 };
    std::normal_distribution<float> n { 0.0f, 1.0f };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    explicit Pink (unsigned seed) : rng (seed) {}
    float next()
    {
        const float w = n (rng) * 0.05f;
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out;
    }
};

struct Run
{
    std::vector<float> outL, outR;
};

/** Traite un signal stéréo par blocs de taille variable. */
static Run runEngine (ResonanceEngine& e, const std::vector<float>& l, const std::vector<float>& r,
                      const std::vector<float>* sideL = nullptr, int block = 333)
{
    Run res;
    res.outL.assign (l.size(), 0.0f);
    res.outR.assign (l.size(), 0.0f);
    size_t pos = 0;
    int b = block;
    while (pos < l.size())
    {
        const int n = (int) std::min<size_t> ((size_t) b, l.size() - pos);
        const float* in[2] = { l.data() + pos, r.data() + pos };
        float* out[2] = { res.outL.data() + pos, res.outR.data() + pos };
        const float* side[1] = { sideL != nullptr ? sideL->data() + pos : nullptr };
        e.process (in, sideL != nullptr ? side : nullptr, sideL != nullptr ? 1 : 0, out, n);
        pos += (size_t) n;
        b = b == 333 ? 64 : b == 64 ? 1 : b == 1 ? 1024 : 333;
    }
    return res;
}

//==============================================================================
static void testFFT()
{
    std::printf ("- FFT reelle\n");
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    for (int m : { 8, 64, 512, 2048 })
    {
        RealFFT fft;
        fft.setSize (m);
        std::vector<float> x ((size_t) m), re ((size_t) m / 2 + 1), im ((size_t) m / 2 + 1), back ((size_t) m);
        for (auto& v : x) v = u (rng);
        fft.forward (x.data(), re.data(), im.data());

        double err = 0.0;
        for (int k = 0; k <= m / 2; ++k)
        {
            double cr = 0.0, ci = 0.0;
            for (int n = 0; n < m; ++n)
            {
                cr += x[(size_t) n] * std::cos (2.0 * pi * k * n / m);
                ci -= x[(size_t) n] * std::sin (2.0 * pi * k * n / m);
            }
            err = std::max (err, std::abs (cr - re[(size_t) k]) + std::abs (ci - im[(size_t) k]));
        }
        check (err < 1.0e-3 * m / 64.0 + 1.0e-4, "DFT taille " + std::to_string (m) + " erreur " + std::to_string (err));

        fft.inverse (re.data(), im.data(), back.data());
        double e2 = 0.0;
        for (int n = 0; n < m; ++n)
            e2 = std::max (e2, (double) std::abs (back[(size_t) n] - x[(size_t) n]));
        check (e2 < 1.0e-5, "aller-retour taille " + std::to_string (m));
    }
}

static void testTransparency()
{
    std::printf ("- transparence (depth 0) et latence\n");
    const double fs = 48000.0;
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.8f, 0.8f);
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = u (rng); r[i] = u (rng) * 0.5f + 0.3f * l[i]; }

    for (int os : { 1, 2, 4 })
        for (int res = 0; res < 4; ++res)
            for (bool ms : { false, true })
            {
                ResonanceEngine e;
                e.prepare (fs, 2);
                EngineSettings s;
                s.depth = 0.0f; s.oversample = os; s.resolution = res; s.midSide = ms;
                e.setSettings (s);
                const auto out = runEngine (e, l, r);
                const int lat = e.getLatency();
                check (lat == 2048, "latence a 48 kHz");

                double err = 0.0;
                for (size_t i = (size_t) lat * 2; i < l.size(); ++i)
                    err = std::max (err, (double) std::abs (out.outL[i] - l[i - (size_t) lat]) + std::abs (out.outR[i] - r[i - (size_t) lat]));
                check (err < 2.0e-4, "transparence os " + std::to_string (os) + " res " + std::to_string (res) + (ms ? " M/S" : " L/R")
                                        + " erreur " + std::to_string (err));
            }

    // Changer la qualité en cours de route reste transparent
    ResonanceEngine e;
    e.prepare (fs, 2);
    EngineSettings s;
    s.depth = 0.0f;
    e.setSettings (s);
    std::vector<float> oL (l.size()), oR (l.size());
    for (size_t pos = 0; pos < l.size(); pos += 500)
    {
        s.resolution = (int) (pos / 500) % 4;
        s.oversample = 1 << ((pos / 1500) % 3);
        e.setSettings (s);
        const int n = (int) std::min<size_t> (500, l.size() - pos);
        const float* in[2] = { l.data() + pos, r.data() + pos };
        float* out[2] = { oL.data() + pos, oR.data() + pos };
        e.process (in, nullptr, 0, out, n);
    }
    double err = 0.0;
    for (size_t i = 4096; i < l.size(); ++i)
        err = std::max (err, (double) std::abs (oL[i] - l[i - 2048]));
    check (err < 2.0e-4, "transparence pendant les changements de qualite : " + std::to_string (err));

    // Latence selon la fréquence d'échantillonnage
    for (auto [rate, expected] : { std::pair<double, int> { 44100.0, 2048 }, { 96000.0, 4096 }, { 192000.0, 8192 } })
    {
        ResonanceEngine x;
        x.prepare (rate, 1);
        check (x.getLatency() == expected, "latence a " + std::to_string ((int) rate));
    }
}

/** Bruit rose + sinusoïde (la résonance). Rend (réduction du sinus en dB, variation du bruit en dB). */
static void measureResonance (ResonanceEngine& e, double fs, float toneHz, float toneAmp, float gain,
                              double& toneDrop, double& noiseChange, bool stereoSame = true)
{
    const size_t n = (size_t) (fs * 3.0);
    Pink pink (11);
    std::vector<float> l (n), r (n), noiseOnly (n);
    for (size_t i = 0; i < n; ++i)
    {
        const float nz = pink.next() * gain;
        noiseOnly[i] = nz;
        l[i] = nz + toneAmp * gain * (float) std::sin (2.0 * pi * toneHz * (double) i / fs);
        r[i] = stereoSame ? l[i] : nz;
    }

    const auto out = runEngine (e, l, r);
    const int lat = e.getLatency();
    const size_t from = (size_t) (fs * 1.5), count = (size_t) (fs * 1.0);

    std::vector<float> inDelayed (n, 0.0f);
    for (size_t i = (size_t) lat; i < n; ++i)
        inDelayed[i] = l[i - (size_t) lat];

    toneDrop = db (toneAmplitude (inDelayed, from, count, toneHz, fs)) - db (toneAmplitude (out.outL, from, count, toneHz, fs));

    // Bruit loin du sinus : comparer la sortie et l'entrée à 300 Hz (bande étroite)
    noiseChange = db (toneAmplitude (out.outL, from, count, 300.0, fs) + 1e-9) - db (toneAmplitude (inDelayed, from, count, 300.0, fs) + 1e-9);
}

static void testResonance()
{
    std::printf ("- detection d'une resonance\n");
    const double fs = 48000.0;

    double drop, noise;
    {
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;          // réglages par défaut (soft)
        e.setSettings (s);
        measureResonance (e, fs, 3000.0f, 0.3f, 1.0f, drop, noise);
        std::printf ("    soft defaut : sinus -%.1f dB, bruit %+.2f dB\n", drop, noise);
        check (drop > 3.0, "le sinus devrait etre reduit (soft, defaut)");
        check (std::abs (noise) < 1.0, "le bruit loin du sinus ne devrait pas bouger");
    }

    double dropHigh;
    {
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        s.depth = 15.0f;
        e.setSettings (s);
        measureResonance (e, fs, 3000.0f, 0.3f, 1.0f, dropHigh, noise);
        std::printf ("    soft depth 15 : sinus -%.1f dB, bruit %+.2f dB\n", dropHigh, noise);
        check (dropHigh > drop + 3.0, "plus de depth = plus de reduction");
    }

    // SOFT : peu dépendant du niveau
    {
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        e.setSettings (s);
        double dropQuiet;
        measureResonance (e, fs, 3000.0f, 0.3f, 0.03f, dropQuiet, noise);
        std::printf ("    soft -30 dB : sinus -%.1f dB\n", dropQuiet);
        check (std::abs (dropQuiet - drop) < 1.5, "soft : reduction independante du niveau");
    }

    // HARD : dépend du niveau
    {
        double dropLoud, dropQuiet;
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        s.hard = true;
        e.setSettings (s);
        measureResonance (e, fs, 3000.0f, 0.3f, 1.0f, dropLoud, noise);
        ResonanceEngine q;
        q.prepare (fs, 2);
        q.setSettings (s);
        measureResonance (q, fs, 3000.0f, 0.3f, 0.01f, dropQuiet, noise);
        std::printf ("    hard : fort -%.1f dB, faible -%.1f dB\n", dropLoud, dropQuiet);
        check (dropLoud > dropQuiet + 3.0, "hard : la reduction depend du niveau");
        check (dropLoud > 3.0, "hard : reduction sur un son fort");
    }

    // Sélectivité : haute = moins de coupes
    {
        double dropSel;
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        s.selectivity = 10.0f;
        e.setSettings (s);
        measureResonance (e, fs, 3000.0f, 0.06f, 1.0f, dropSel, noise);
        ResonanceEngine f;
        f.prepare (fs, 2);
        s.selectivity = 0.0f;
        f.setSettings (s);
        double dropLow;
        measureResonance (f, fs, 3000.0f, 0.06f, 1.0f, dropLow, noise);
        std::printf ("    petite resonance : selectivity 10 -%.1f dB, selectivity 0 -%.1f dB\n", dropSel, dropLow);
        check (dropLow > dropSel + 1.0, "selectivity basse = plus de coupes");
    }

    // Grave avec oversample
    {
        double d1, d4;
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        s.depth = 10.0f;
        e.setSettings (s);
        measureResonance (e, fs, 90.0f, 0.5f, 1.0f, d1, noise);
        ResonanceEngine f;
        f.prepare (fs, 2);
        s.oversample = 4;
        f.setSettings (s);
        measureResonance (f, fs, 90.0f, 0.5f, 1.0f, d4, noise);
        std::printf ("    90 Hz : oversample 1x -%.1f dB, 4x -%.1f dB\n", d1, d4);
        check (d4 > 2.0, "une resonance grave est traitee en oversample 4x");
    }
}

static void testCurve()
{
    std::printf ("- courbe de sensibilite\n");
    const double fs = 48000.0;
    double base, boosted, rejected, noise;

    EngineSettings s;
    ResonanceEngine a;
    a.prepare (fs, 2);
    a.setSettings (s);
    CurveSpec flat;
    for (auto& b : flat.bands) b.sens = 0.0f;
    a.setCurve (flat);
    measureResonance (a, fs, 3000.0f, 0.3f, 1.0f, base, noise);

    CurveSpec boost = flat;
    boost.bands[0] = { true, peak, 3000.0f, 12.0f, 1.0f, 0.0f };
    ResonanceEngine b;
    b.prepare (fs, 2);
    b.setSettings (s);
    b.setCurve (boost);
    measureResonance (b, fs, 3000.0f, 0.3f, 1.0f, boosted, noise);

    CurveSpec rej = flat;
    rej.bands[0] = { true, reject, 3000.0f, 0.0f, 1.0f, 0.0f };
    ResonanceEngine c;
    c.prepare (fs, 2);
    c.setSettings (s);
    c.setCurve (rej);
    measureResonance (c, fs, 3000.0f, 0.3f, 1.0f, rejected, noise);

    std::printf ("    plat -%.1f, bande +12 dB -%.1f, reject -%.1f\n", base, boosted, rejected);
    check (boosted > base + 2.0, "une bande montee augmente la reduction");
    check (rejected < 0.5, "REJECT supprime le traitement dans sa zone");

    // Coupure basse au-dessus du sinus
    CurveSpec cut = flat;
    cut.lowCut = { true, 8000.0f, 3 };
    ResonanceEngine d;
    d.prepare (fs, 2);
    d.setSettings (s);
    d.setCurve (cut);
    double dropCut;
    measureResonance (d, fs, 3000.0f, 0.3f, 1.0f, dropCut, noise);
    check (dropCut < 0.5, "LOW CUT au-dessus du sinus : pas de traitement");

    // Fonctions de forme
    check (std::abs (bandShape ({ true, peak, 1000.0f, 0.0f, 1.0f, 0.0f }, 0.0f) - 1.0f) < 1e-5f, "pic : 1 au centre");
    check (bandShape ({ true, highShelf, 1000.0f, 0.0f, 1.0f, 0.0f }, 3.0f) > 0.99f, "high shelf : 1 en haut");
    check (bandShape ({ true, lowShelf, 1000.0f, 0.0f, 1.0f, 0.0f }, 3.0f) < 0.01f, "low shelf : 0 en haut");
    float wa, wb;
    balanceWeights (-1.0f, wa, wb);
    check (wa == 1.0f && wb == 0.0f, "balance a gauche");
}

static void testStereo()
{
    std::printf ("- stereo : link, balance, mono\n");
    const double fs = 48000.0;
    const size_t n = 96000;
    Pink pl (1), pr (2);
    std::vector<float> l (n), r (n);
    for (size_t i = 0; i < n; ++i)
    {
        l[i] = pl.next() + 0.3f * (float) std::sin (2.0 * pi * 2500.0 * (double) i / fs);   // résonance à gauche seulement
        r[i] = pr.next();
    }

    auto rightChange = [&] (float link, bool ms, float balance)
    {
        ResonanceEngine e;
        e.prepare (fs, 2);
        EngineSettings s;
        s.depth = 10.0f; s.link = link; s.midSide = ms; s.balance = balance;
        e.setSettings (s);
        const auto out = runEngine (e, l, r);
        std::vector<float> rd (n, 0.0f), ld (n, 0.0f);
        for (size_t i = 2048; i < n; ++i) { rd[i] = r[i - 2048]; ld[i] = l[i - 2048]; }
        const double rChange = db (toneAmplitude (out.outR, 48000, 40000, 2500.0, fs) + 1e-9) - db (toneAmplitude (rd, 48000, 40000, 2500.0, fs) + 1e-9);
        const double lDrop = db (toneAmplitude (ld, 48000, 40000, 2500.0, fs)) - db (toneAmplitude (out.outL, 48000, 40000, 2500.0, fs));
        return std::pair<double, double> { lDrop, rChange };
    };

    const auto unlinked = rightChange (0.0f, false, 0.0f);
    std::printf ("    L/R link 0 : gauche -%.1f dB, droite %+.2f dB a 2,5 kHz\n", unlinked.first, unlinked.second);
    check (unlinked.first > 3.0, "link 0 : le canal gauche est traite");
    check (std::abs (unlinked.second) < 1.5, "link 0 : le canal droit n'est pas touche (a peine le bruit)");

    const auto linked = rightChange (1.0f, false, 0.0f);
    check (linked.second < unlinked.second - 1.0, "link 100 : le canal droit suit le gauche");

    const auto balanced = rightChange (0.0f, false, 1.0f);   // tout sur B (droite)
    check (balanced.first < 0.5, "balance a droite : le gauche n'est plus traite");

    // Mono
    ResonanceEngine m;
    m.prepare (fs, 1);
    EngineSettings s;
    s.depth = 10.0f;
    m.setSettings (s);
    std::vector<float> outMono (n);
    for (size_t pos = 0; pos < n; pos += 512)
    {
        const int k = (int) std::min<size_t> (512, n - pos);
        const float* in[1] = { l.data() + pos };
        float* out[1] = { outMono.data() + pos };
        m.process (in, nullptr, 0, out, k);
    }
    std::vector<float> ld (n, 0.0f);
    for (size_t i = 2048; i < n; ++i) ld[i] = l[i - 2048];
    check (db (toneAmplitude (ld, 48000, 40000, 2500.0, fs)) - db (toneAmplitude (outMono, 48000, 40000, 2500.0, fs)) > 3.0, "mono : reduction");
}

static void testSidechain()
{
    std::printf ("- sidechain\n");
    const double fs = 48000.0;
    const size_t n = 120000;
    Pink p (5);
    std::vector<float> l (n), side (n);
    for (size_t i = 0; i < n; ++i)
    {
        const float tone = 0.3f * (float) std::sin (2.0 * pi * 2000.0 * (double) i / fs);
        l[i] = p.next() + tone * 0.2f;        // le son traité contient un peu de 2 kHz
        side[i] = p.next() * 0.2f + tone;     // la clé en a beaucoup
    }

    ResonanceEngine e;
    e.prepare (fs, 2);
    EngineSettings s;
    s.depth = 10.0f;
    e.setSettings (s);
    const auto withSide = runEngine (e, l, l, &side);

    ResonanceEngine f;
    f.prepare (fs, 2);
    f.setSettings (s);
    const auto without = runEngine (f, l, l);

    const double a = db (toneAmplitude (withSide.outL, 60000, 48000, 2000.0, fs));
    const double b = db (toneAmplitude (without.outL, 60000, 48000, 2000.0, fs));
    std::printf ("    2 kHz : avec cle %.1f dB, sans cle %.1f dB\n", a, b);
    check (a < b - 3.0, "la cle sidechain commande la reduction");
}

static void testListen()
{
    std::printf ("- ecoute d'une bande\n");
    const double fs = 48000.0;
    const size_t n = 96000;
    Pink p (9);
    std::vector<float> l (n);
    for (size_t i = 0; i < n; ++i)
        l[i] = p.next() + 0.3f * (float) std::sin (2.0 * pi * 3000.0 * (double) i / fs);

    // Depth 0 : rien n'est retiré, l'écoute est silencieuse
    ResonanceEngine e;
    e.prepare (fs, 2);
    EngineSettings s;
    s.depth = 0.0f;
    e.setSettings (s);
    e.setListen (2);
    const auto silent = runEngine (e, l, l);
    check (rms (silent.outL, 10000, 80000) < 1.0e-5, "ecoute a depth 0 : silence");

    // Avec réduction : on entend surtout le sinus retiré, autour de la bande
    ResonanceEngine f;
    f.prepare (fs, 2);
    s.depth = 10.0f;
    f.setSettings (s);
    CurveSpec c;
    c.bands[2] = { true, peak, 3000.0f, 3.0f, 2.0f, 0.0f };
    f.setCurve (c);
    f.setListen (2);
    const auto heard = runEngine (f, l, l);
    const double tone = db (toneAmplitude (heard.outL, 48000, 40000, 3000.0, fs));
    const double far = db (toneAmplitude (heard.outL, 48000, 40000, 300.0, fs) + 1e-9);
    std::printf ("    ecoute : 3 kHz %.1f dB, 300 Hz %.1f dB\n", tone, far);
    check (tone > far + 20.0, "l'ecoute montre ce que la bande retire");
}

static void testSpeed()
{
    std::printf ("- attack / release\n");
    const double fs = 48000.0;
    const size_t n = 60000;
    Pink p (4);
    std::vector<float> l (n);
    const size_t onset = 30000;
    for (size_t i = 0; i < n; ++i)
        l[i] = p.next() + (i >= onset ? 0.4f * (float) std::sin (2.0 * pi * 2000.0 * (double) i / fs) : 0.0f);

    auto reductionAfter = [&] (float attackMs, double afterMs)
    {
        ResonanceEngine e;
        e.prepare (fs, 1);
        EngineSettings s;
        s.depth = 10.0f;
        s.attackMs = attackMs;
        e.setSettings (s);
        std::vector<float> out (n);
        const size_t stop = onset + 2048 + (size_t) (afterMs * 48.0);
        for (size_t pos = 0; pos < stop; pos += 64)
        {
            const int k = (int) std::min<size_t> (64, stop - pos);
            const float* in[1] = { l.data() + pos };
            float* o[1] = { out.data() + pos };
            e.process (in, nullptr, 0, o, k);
        }
        return e.getReductionAtFrequency (0, 2000.0f);
    };

    const float fast = reductionAfter (1.0f, 15.0);
    const float slow = reductionAfter (40.0f, 15.0);
    std::printf ("    15 ms apres l'attaque : attack 1 ms %.1f dB, 40 ms %.1f dB\n", fast, slow);
    check (fast > slow + 2.0, "attack lente = reduction plus lente");
}

static void testFuzz()
{
    std::printf ("- reglages au hasard, 44,1 a 192 kHz\n");
    std::mt19937 rng (99);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    bool finite = true;
    float peak = 0.0f;

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        ResonanceEngine e;
        e.prepare (fs, 2);
        const size_t n = (size_t) (fs * 1.2);
        std::vector<float> l (n), r (n), side (n);
        Pink p (17);
        for (size_t i = 0; i < n; ++i)
        {
            l[i] = p.next() * 3.0f * (i % 20000 < 10000 ? 1.0f : 0.0f) + (u (rng) - 0.5f) * 0.01f;
            r[i] = (float) std::sin (2.0 * pi * 50.0 * (double) i / fs) * 2.0f;
            side[i] = u (rng) - 0.5f;
        }
        std::vector<float> oL (n), oR (n);

        for (size_t pos = 0; pos < n; )
        {
            EngineSettings s;
            s.hard = u (rng) < 0.5f;
            s.depth = u (rng) * 20.0f;
            s.sharpness = u (rng) * 10.0f;
            s.selectivity = u (rng) * 10.0f;
            s.attackMs = 0.5f + u (rng) * 50.0f;
            s.releaseMs = 5.0f + u (rng) * 500.0f;
            s.oversample = 1 << (int) (u (rng) * 2.99f);
            s.resolution = (int) (u (rng) * 3.99f);
            s.midSide = u (rng) < 0.5f;
            s.link = u (rng);
            s.balance = u (rng) * 2.0f - 1.0f;
            s.inputTrimDb = u (rng) * 48.0f - 24.0f;
            e.setSettings (s);

            CurveSpec c;
            c.lowCut = { u (rng) < 0.5f, 10.0f + u (rng) * 990.0f, (int) (u (rng) * 3.99f) };
            c.highCut = { u (rng) < 0.5f, 1000.0f + u (rng) * 21000.0f, (int) (u (rng) * 3.99f) };
            for (auto& b : c.bands)
                b = { u (rng) < 0.8f, (int) (u (rng) * 5.99f), 20.0f + u (rng) * 19980.0f, u (rng) * 48.0f - 24.0f, 0.1f + u (rng) * 9.9f, u (rng) * 2.0f - 1.0f };
            e.setCurve (c);
            e.setListen ((int) (u (rng) * 8.0f) - 2);

            const int k = (int) std::min<size_t> ((size_t) (1 + u (rng) * 3000.0f), n - pos);
            const float* in[2] = { l.data() + pos, r.data() + pos };
            float* out[2] = { oL.data() + pos, oR.data() + pos };
            const float* sd[1] = { side.data() + pos };
            e.process (in, u (rng) < 0.3f ? sd : nullptr, 1, out, k);
            pos += (size_t) k;
        }

        for (size_t i = 0; i < n; ++i)
        {
            finite = finite && std::isfinite (oL[i]) && std::isfinite (oR[i]);
            peak = std::max (peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
        }
        for (int i = 0; i < ResonanceEngine::displayPoints; ++i)
            finite = finite && std::isfinite (e.getDisplayReduction (i)) && std::isfinite (e.getDisplayLevel (i));
    }

    check (finite, "sortie finie");
    std::printf ("    crete de sortie %.2f\n", peak);
    check (peak < 30.0f, "pas d'emballement");
}

int main()
{
    testFFT();
    testTransparency();
    testResonance();
    testCurve();
    testStereo();
    testSidechain();
    testListen();
    testSpeed();
    testFuzz();

    std::printf ("%d verifications, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
