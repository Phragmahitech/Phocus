#pragma once

#include "RealFFT.h"
#include "SensitivityCurve.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

/**
    Moteur de Phocus : suppresseur dynamique de résonances, par analyse spectrale (STFT).

    Principe
      - Le son passe par trames de N échantillons (N = 2048 jusqu'à 64 kHz, 4096 jusqu'à 128 kHz,
        8192 au-delà), fenêtre racine de Hann à l'analyse et à la synthèse, complétées de zéros
        jusqu'à M = N × OVERSAMPLE pour une FFT plus fine.
      - Pour chaque case de fréquence, on compare le niveau à l'enveloppe lissée du spectre autour
        d'elle (lissage sur une largeur en octaves). Ce qui dépasse l'enveloppe de plus que le seuil
        (SELECTIVITY), corrigé par la courbe de sensibilité, est une résonance.
      - SOFT : la réduction ne dépend que de ce dépassement (indépendante du niveau).
        HARD  : elle est aussi pondérée par le niveau au-dessus d'un seuil absolu (plus forte sur
                ce qui est fort, rien sur ce qui est faible).
      - La réduction est étalée en fréquence (SHARPNESS règle la largeur), puis lissée dans le
        temps par case (ATTACK / RELEASE, plus rapides dans l'aigu), et appliquée comme un gain
        par case. Reconstruction par addition des trames, normalisée par la somme des fenêtres :
        exacte quelle que soit la cadence des trames, qui peut donc changer en cours de route.
      - Latence fixe : N échantillons, quels que soient les réglages de qualité.

    Le moteur traite le signal mouillé seulement (mix, delta, trim : dans le processeur).
    Sans dépendance (testable seul). Toute la mémoire est réservée par prepare().
*/
namespace phocus
{

struct EngineSettings
{
    bool hard = false;
    float depth = 5.0f;           // 0 .. 20
    float sharpness = 4.0f;       // 0 .. 10
    float selectivity = 3.5f;     // 0 .. 10
    float attackMs = 5.0f;        // référence à 1 kHz
    float releaseMs = 50.0f;      // référence à 1 kHz
    int oversample = 1;           // 1, 2, 4
    int resolution = 1;           // 0 ECO, 1 NORMAL, 2 HIGH, 3 ULTRA
    bool midSide = true;
    float link = 1.0f;            // 0 .. 1
    float balance = 0.0f;         // -1 (A) .. +1 (B)
    float inputTrimDb = 0.0f;     // décale le niveau vu par la détection
};

/** Retard simple (signal sec aligné sur la latence). */
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        buffer.assign ((size_t) maxDelay + 1, 0.0f);
        pos = 0;
    }

    void reset()                            { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

    /** Écrit x et rend l'échantillon d'il y a `delay` (delay <= maxDelay). */
    float process (float x, int delay) noexcept
    {
        const int size = (int) buffer.size();
        buffer[(size_t) pos] = x;
        int r = pos - delay;
        if (r < 0)
            r += size;
        const float y = buffer[(size_t) r];
        if (++pos == size)
            pos = 0;
        return y;
    }

private:
    std::vector<float> buffer;
    int pos = 0;
};

class ResonanceEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int displayPoints = 256;
    static constexpr float maxReductionDb = 60.0f;
    static constexpr float displayMinHz = 20.0f, displayMaxHz = 20000.0f;

    static int frameSizeFor (double sampleRate) noexcept
    {
        return sampleRate <= 64000.0 ? 2048 : sampleRate <= 128000.0 ? 4096 : 8192;
    }

    static int hopDivisor (int resolution) noexcept
    {
        static constexpr int divisors[] = { 4, 8, 16, 32 };
        return divisors[std::clamp (resolution, 0, 3)];
    }

    static float displayFrequency (int i) noexcept
    {
        return displayMinHz * std::pow (displayMaxHz / displayMinHz, (float) i / (float) (displayPoints - 1));
    }

    ResonanceEngine()
    {
        for (auto& a : displayReduction) a.store (0.0f);
        for (auto& a : displayLevel)     a.store (-120.0f);
    }

    //==========================================================================
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = std::clamp (numChannels, 1, maxChannels);
        N = frameSizeFor (sampleRate);

        const int maxM = 4 * N, maxBins = maxM / 2 + 1;

        for (int i = 0; i < 3; ++i)
            ffts[i].setSize (N << i);

        // Fenêtre racine de Hann périodique
        window.resize ((size_t) N);
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) N));
            sum += window[(size_t) i];
        }
        levelScale = (float) (2.0 / sum);   // un sinus d'amplitude A au centre d'une case lit 20 log10 (A)

        ringSize = 2 * N;
        for (int c = 0; c < maxChannels; ++c)
        {
            inRing[c].assign ((size_t) N, 0.0f);
            sideRing[c].assign ((size_t) N, 0.0f);
            acc[c].assign ((size_t) ringSize, 0.0f);
            re[c].assign ((size_t) maxBins, 0.0f);
            im[c].assign ((size_t) maxBins, 0.0f);
            level[c].assign ((size_t) maxBins, -200.0f);
            power[c].assign ((size_t) maxBins, 0.0f);
            reduction[c].assign ((size_t) maxBins, 0.0f);
            state[c].assign ((size_t) maxBins, 0.0f);
            sens[c].assign ((size_t) maxBins, 0.0f);
            weight[c].assign ((size_t) maxBins, 1.0f);
            own[c].assign ((size_t) maxBins, 0.0f);
        }
        wsum.assign ((size_t) ringSize, 0.0f);

        frame.assign ((size_t) maxM, 0.0f);
        sideRe.assign ((size_t) maxBins, 0.0f);
        sideIm.assign ((size_t) maxBins, 0.0f);
        linkedLevel.assign ((size_t) maxBins, -200.0f);
        sensMean.assign ((size_t) maxBins, 0.0f);
        weightMean.assign ((size_t) maxBins, 1.0f);
        envelope.assign ((size_t) maxBins, 0.0f);
        temp.assign ((size_t) maxBins, 0.0f);
        prefix.assign ((size_t) maxBins + 1, 0.0);
        listen.assign ((size_t) maxBins, 0.0f);
        invTimeScale.assign ((size_t) maxBins, 1.0f);
        attackCoef.assign ((size_t) maxBins, 0.0f);
        releaseCoef.assign ((size_t) maxBins, 0.0f);
        displayBin.assign ((size_t) displayPoints, 0.0f);

        M = 0;   // forcé au premier calcul
        reset();
    }

    void reset()
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (sideRing[c].begin(), sideRing[c].end(), 0.0f);
            std::fill (acc[c].begin(), acc[c].end(), 0.0f);
            std::fill (state[c].begin(), state[c].end(), 0.0f);
            std::fill (power[c].begin(), power[c].end(), 0.0f);
        }
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        time = 0;
        sinceFrame = 0;
        for (auto& a : displayReduction) a.store (0.0f, std::memory_order_relaxed);
        for (auto& a : displayLevel)     a.store (-120.0f, std::memory_order_relaxed);
    }

    int getLatency() const noexcept               { return N; }
    int getFrameSize() const noexcept             { return N; }
    int getFftSize() const noexcept               { return M; }
    int getHop() const noexcept                   { return N / hopDivisor (settings.resolution); }
    double getSampleRate() const noexcept         { return fs; }

    void setSettings (const EngineSettings& s) noexcept     { settings = s; }
    const EngineSettings& getSettings() const noexcept      { return settings; }

    void setCurve (const CurveSpec& c) noexcept
    {
        if (c != curve)
        {
            curve = c;
            curveDirty = true;
        }
    }

    /** Écoute d'une bande : -1 = aucune ; 0..3 = bandes ; 4 = LOW CUT ; 5 = HIGH CUT. Sortie = ce que la bande retire. */
    void setListen (int band) noexcept
    {
        if (band != listenBand)
        {
            listenBand = band;
            curveDirty = true;
        }
    }
    int getListen() const noexcept               { return listenBand; }

    /**
        Traite n échantillons. in / out : `channels` canaux (L, R) ; side : détection externe
        (null = le signal lui-même ; sideChannels = 1 ou 2). in et out peuvent être les mêmes tableaux.
    */
    void process (const float* const* in, const float* const* side, int sideChannels, float* const* out, int n) noexcept
    {
        const bool ms = settings.midSide && channels == 2;
        const bool useSide = side != nullptr && sideChannels > 0;
        const int hop = N / hopDivisor (settings.resolution);

        for (int i = 0; i < n; ++i)
        {
            float a[maxChannels] = {}, s[maxChannels] = {};
            toInternal (in, i, channels, ms, a);
            if (useSide)
            {
                const float* sideIn[maxChannels] = { side[0], side[sideChannels > 1 ? 1 : 0] };
                toInternal (sideIn, i, channels, ms, s);
            }

            const int w = (int) (time % (unsigned long long) N);
            for (int c = 0; c < channels; ++c)
            {
                inRing[c][(size_t) w] = a[c];
                sideRing[c][(size_t) w] = s[c];
            }

            if (++sinceFrame >= hop && time + 1 >= (unsigned long long) N)
            {
                sinceFrame = 0;
                runFrame (useSide);
            }

            // Sortie : l'échantillon d'il y a N, maintenant complet
            const int r = (int) ((time + (unsigned long long) ringSize - (unsigned long long) N) % (unsigned long long) ringSize);
            const float norm = wsum[(size_t) r];
            float y[maxChannels] = {};
            for (int c = 0; c < channels; ++c)
            {
                // En régime établi norm >= 1 (au moins 4 trames par échantillon). Il n'est plus petit
                // qu'au démarrage : on ne divise pas par presque rien (fondu d'entrée au lieu d'un pic).
                y[c] = acc[c][(size_t) r] / std::max (norm, 0.5f);
                acc[c][(size_t) r] = 0.0f;
            }
            wsum[(size_t) r] = 0.0f;

            fromInternal (y, out, i, channels, ms);
            ++time;
        }
    }

    //==========================================================================
    // Pour l'interface (lecture seule, depuis n'importe quel fil)
    float getDisplayReduction (int i) const noexcept   { return displayReduction[(size_t) i].load (std::memory_order_relaxed); }
    float getDisplayLevel (int i) const noexcept       { return displayLevel[(size_t) i].load (std::memory_order_relaxed); }

    /** Réduction actuelle (dB) d'une case, pour les tests. */
    float getReductionAtFrequency (int channel, float hz) const noexcept
    {
        if (M == 0)
            return 0.0f;
        const int k = std::clamp ((int) std::lround (hz * (float) M / (float) fs), 0, M / 2);
        return state[channel][(size_t) k];
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    static void toInternal (const float* const* in, int i, int numChannels, bool ms, float* a) noexcept
    {
        if (numChannels == 1)
        {
            a[0] = in[0][i];
            return;
        }
        const float l = in[0][i], r = in[1][i];
        if (ms) { a[0] = 0.5f * (l + r); a[1] = 0.5f * (l - r); }
        else    { a[0] = l; a[1] = r; }
    }

    static void fromInternal (const float* y, float* const* out, int i, int numChannels, bool ms) noexcept
    {
        if (numChannels == 1)
        {
            out[0][i] = y[0];
            return;
        }
        if (ms) { out[0][i] = y[0] + y[1]; out[1][i] = y[0] - y[1]; }
        else    { out[0][i] = y[0]; out[1][i] = y[1]; }
    }

    //==========================================================================
    void configure()
    {
        const int os = settings.oversample >= 4 ? 4 : settings.oversample >= 2 ? 2 : 1;
        const int newM = N * os;
        if (newM == M)
            return;

        M = newM;
        fft = &ffts[os == 4 ? 2 : os == 2 ? 1 : 0];
        bins = M / 2 + 1;
        binHz = (float) (fs / (double) M);

        for (int c = 0; c < maxChannels; ++c)
        {
            std::fill (state[c].begin(), state[c].end(), 0.0f);
            std::fill (power[c].begin(), power[c].end(), 0.0f);
        }

        for (int k = 0; k < bins; ++k)
        {
            const float f = (float) k * binHz;
            const float scale = f > 1.0f ? std::clamp (std::sqrt (1000.0f / f), 0.35f, 2.5f) : 2.5f;
            invTimeScale[(size_t) k] = 1.0f / scale;
        }

        for (int i = 0; i < displayPoints; ++i)
            displayBin[(size_t) i] = displayFrequency (i) / binHz;

        curveDirty = true;
        coefKey = -1.0f;
    }

    void updateCurve()
    {
        curveSymmetric = true;
        for (const auto& b : curve.bands)
            if (b.on && std::abs (b.balance) > 1.0e-6f)
                curveSymmetric = false;

        for (int k = 0; k < bins; ++k)
        {
            const float f = (float) k * binHz;
            const auto p = evaluateCurve (curve, f);
            sens[0][(size_t) k] = p.sensA;
            sens[1][(size_t) k] = p.sensB;
            weight[0][(size_t) k] = p.weightA;
            weight[1][(size_t) k] = p.weightB;
            sensMean[(size_t) k] = 0.5f * (p.sensA + p.sensB);
            weightMean[(size_t) k] = 0.5f * (p.weightA + p.weightB);

            float shape = 1.0f;
            if (listenBand >= 0 && listenBand < CurveSpec::numBands)
            {
                const auto& band = curve.bands[listenBand];
                if (band.type == tilt)
                    shape = 1.0f;
                else if (f <= 0.0f)
                    shape = band.type == lowShelf ? 1.0f : 0.0f;
                else
                    shape = std::clamp (bandShape (band, std::log2 (f / std::max (1.0f, band.freq))), 0.0f, 1.0f);
            }
            else if (listenBand == CurveSpec::numBands)
            {
                CutSpec cut = curve.lowCut;
                cut.on = true;
                shape = 1.0f - cutWeight (cut, f, false);
            }
            else if (listenBand == CurveSpec::numBands + 1)
            {
                CutSpec cut = curve.highCut;
                cut.on = true;
                shape = 1.0f - cutWeight (cut, f, true);
            }
            listen[(size_t) k] = shape;
        }
        curveDirty = false;
    }

    void updateCoefficients (int hop)
    {
        const float key = settings.attackMs * 1.0e3f + settings.releaseMs + (float) hop * 1.0e7f + (float) M * 1.0e-3f;
        if (! (key < coefKey) && ! (coefKey < key))
            return;
        coefKey = key;

        const float hopSec = (float) hop / (float) fs;
        const float aT = std::max (0.05f, settings.attackMs) * 1.0e-3f;
        const float rT = std::max (0.5f, settings.releaseMs) * 1.0e-3f;

        for (int k = 0; k < bins; ++k)
        {
            const float inv = invTimeScale[(size_t) k];
            attackCoef[(size_t) k]  = std::exp (-hopSec * inv / aT);
            releaseCoef[(size_t) k] = std::exp (-hopSec * inv / rT);
        }
    }

    /** Spectre de la trame d'un anneau (les N derniers échantillons), complété de zéros jusqu'à M. */
    void analyse (const std::vector<float>& ring, float* outRe, float* outIm) noexcept
    {
        const int start = (int) ((time + 1) % (unsigned long long) N);   // le plus ancien échantillon
        for (int j = 0; j < N; ++j)
        {
            int idx = start + j;
            if (idx >= N)
                idx -= N;
            frame[(size_t) j] = ring[(size_t) idx] * window[(size_t) j];
        }
        std::fill (frame.begin() + N, frame.begin() + M, 0.0f);
        fft->forward (frame.data(), outRe, outIm);
    }

    /**
        Niveaux (dB) vus par la détection. La puissance de chaque case est moyennée sur ~10 ms :
        le bruit fluctue d'une trame à l'autre (des pics de plusieurs dB qui ne sont pas des
        résonances), une résonance dure.
    */
    void levels (const float* r, const float* i, float* smoothed, float* out, float coef) const noexcept
    {
        const float scale2 = levelScale * levelScale;
        const float trim = settings.inputTrimDb;
        for (int k = 0; k < bins; ++k)
        {
            const float p = (r[k] * r[k] + i[k] * i[k]) * scale2;
            smoothed[k] = p + coef * (smoothed[k] - p);
            out[k] = 10.0f * std::log10 (smoothed[k] + 1.0e-24f) + trim;
        }
    }

    /** Moyenne glissante sur une largeur proportionnelle à la fréquence (en octaves), via des sommes cumulées. */
    void smooth (const float* in, float* out, float octaves, float minHalfWidth, float exponent) noexcept
    {
        const double p = (double) exponent;
        const bool linear = exponent < 1.0001f;
        const bool cubic = std::abs (exponent - 3.0f) < 1.0e-4f;
        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const double x = (double) std::max (0.0f, in[k]);
            const double v = linear ? (double) in[k] : (cubic ? x * x * x : std::pow (x, p));
            prefix[(size_t) k + 1] = prefix[(size_t) k] + v;
        }

        // demi-largeur en cases à la case k : 0.5 × k × (2^(oct/2) − 2^(−oct/2))
        const float spread = 0.5f * (std::pow (2.0f, 0.5f * octaves) - std::pow (2.0f, -0.5f * octaves));
        const double total = (double) bins;

        auto integral = [this, total] (double x) noexcept
        {
            x = std::clamp (x, 0.0, total);
            const int i0 = (int) x;
            if (i0 >= bins)
                return prefix[(size_t) bins];
            return prefix[(size_t) i0] + (x - (double) i0) * (prefix[(size_t) i0 + 1] - prefix[(size_t) i0]);
        };

        for (int k = 0; k < bins; ++k)
        {
            const double h = (double) std::max (minHalfWidth, spread * (float) k);
            const double centre = (double) k + 0.5;
            const double lo = std::max (0.0, centre - h), hi = std::min (total, centre + h);
            const double mean = (integral (hi) - integral (lo)) / std::max (1.0e-9, hi - lo);
            out[k] = linear ? (float) mean : (float) (cubic ? std::cbrt (std::max (0.0, mean)) : std::pow (std::max (0.0, mean), 1.0 / p));
        }
    }

    /** Réduction (dB) voulue pour un spectre de niveaux. */
    void detect (const float* lev, const float* sensDb, const float* w, float* out) noexcept
    {
        const auto& s = settings;
        const float sharp = std::clamp (s.sharpness, 0.0f, 10.0f);
        const float sel = std::clamp (s.selectivity, 0.0f, 10.0f);
        const float depth = std::clamp (s.depth, 0.0f, 20.0f);

        const float threshold = 1.5f + 1.1f * sel;
        const float sharpFactor = 0.55f + 0.09f * sharp;
        const float envelopeOct = 1.1f - 0.085f * sharp;
        const float spreadOct = 0.36f - 0.033f * sharp;
        const float knee = 6.0f;

        // Enveloppe robuste : un premier lissage, on écrête les pics au-dessus, on relisse
        // (en cases : plus fines avec l'oversample, d'où les largeurs minimales proportionnelles)
        const float os = (float) M / (float) N;
        smooth (lev, envelope.data(), envelopeOct, 3.0f * os, 1.0f);
        for (int k = 0; k < bins; ++k)
            temp[(size_t) k] = std::min (lev[k], envelope[(size_t) k] + 2.0f);
        smooth (temp.data(), envelope.data(), envelopeOct, 3.0f * os, 1.0f);

        float top = -200.0f;
        for (int k = 1; k < bins; ++k)
            top = std::max (top, lev[k]);
        const float floorDb = std::max (top - 80.0f, -110.0f);

        const float softGain = depth * 0.16f * sharpFactor;
        const float hardGain = depth * 0.22f * sharpFactor;
        const float absThreshold = -20.0f - 3.5f * depth;

        for (int k = 0; k < bins; ++k)
        {
            const float x = lev[k] - envelope[(size_t) k] + sensDb[k] - threshold;
            float r;
            if (x <= -0.5f * knee)      r = 0.0f;
            else if (x >= 0.5f * knee)  r = x;
            else                        r = (x + 0.5f * knee) * (x + 0.5f * knee) / (2.0f * knee);

            if (s.hard)
            {
                const float above = lev[k] + sensDb[k] - absThreshold;
                r *= hardGain * std::clamp (above / 12.0f, 0.0f, 2.0f);
            }
            else
            {
                r *= softGain;
            }

            const float gate = std::clamp ((lev[k] - floorDb) / 10.0f, 0.0f, 1.0f);
            temp[(size_t) k] = std::min (maxReductionDb, r) * gate * w[k];
        }

        // Étalement en fréquence (moyenne puissance 3 : garde la profondeur des encoches étroites)
        smooth (temp.data(), out, spreadOct, 0.75f * os, 3.0f);
    }

    void runFrame (bool useSide) noexcept
    {
        configure();
        if (curveDirty)
            updateCurve();

        const int hop = N / hopDivisor (settings.resolution);
        updateCoefficients (hop);

        // Analyse
        const float powerCoef = std::exp (-(float) hop / (float) fs / 0.010f);
        for (int c = 0; c < channels; ++c)
        {
            analyse (inRing[c], re[c].data(), im[c].data());
            if (useSide)
            {
                analyse (sideRing[c], sideRe.data(), sideIm.data());
                levels (sideRe.data(), sideIm.data(), power[c].data(), level[c].data(), powerCoef);
            }
            else
            {
                levels (re[c].data(), im[c].data(), power[c].data(), level[c].data(), powerCoef);
            }
        }

        // Détection
        float wA = 1.0f, wB = 1.0f;
        balanceWeights (channels == 2 ? settings.balance : 0.0f, wA, wB);
        const float chanWeight[maxChannels] = { wA, wB };

        if (channels == 1)
        {
            detect (level[0].data(), sens[0].data(), weight[0].data(), reduction[0].data());
        }
        else
        {
            const float link = std::clamp (settings.link, 0.0f, 1.0f);

            if (link > 0.0f)
            {
                // Puissances déjà moyennées (power[]) : pas de retour du dB vers la puissance
                for (int k = 0; k < bins; ++k)
                    linkedLevel[(size_t) k] = 10.0f * std::log10 (0.5f * (power[0][(size_t) k] + power[1][(size_t) k]) + 1.0e-24f)
                                            + settings.inputTrimDb;
            }

            for (int c = 0; c < 2; ++c)
            {
                if (link > 0.0f && c == 1 && curveSymmetric)
                    std::copy (reduction[0].begin(), reduction[0].begin() + bins, reduction[1].begin());   // même courbe : même calcul
                else if (link > 0.0f)
                    detect (linkedLevel.data(), sens[c].data(), weight[c].data(), reduction[c].data());
                else
                    std::fill (reduction[c].begin(), reduction[c].begin() + bins, 0.0f);

            }

            for (int c = 0; c < 2; ++c)
            {
                if (link < 1.0f)
                {
                    detect (level[c].data(), sens[c].data(), weight[c].data(), own[c].data());
                    for (int k = 0; k < bins; ++k)
                        reduction[c][(size_t) k] = link * reduction[c][(size_t) k] + (1.0f - link) * own[c][(size_t) k];
                }
            }
        }

        // Lissage temporel, gain, synthèse
        const int start = (int) ((time + 1 + (unsigned long long) ringSize - (unsigned long long) N) % (unsigned long long) ringSize);
        const bool listening = listenBand >= 0;

        for (int c = 0; c < channels; ++c)
        {
            auto& st = state[c];
            auto& red = reduction[c];
            float* xr = re[c].data();
            float* xi = im[c].data();
            const float cw = chanWeight[c];

            for (int k = 0; k < bins; ++k)
            {
                const float target = red[(size_t) k] * cw;
                const float coef = target > st[(size_t) k] ? attackCoef[(size_t) k] : releaseCoef[(size_t) k];
                st[(size_t) k] = target + coef * (st[(size_t) k] - target);

                const float g = std::exp (-0.11512925f * st[(size_t) k]);   // 10^(-dB/20)
                const float m = listening ? (1.0f - g) * listen[(size_t) k] : g;
                xr[k] *= m;
                xi[k] *= m;
            }

            fft->inverse (xr, xi, frame.data());

            auto& ac = acc[c];
            for (int j = 0; j < N; ++j)
            {
                int idx = start + j;
                if (idx >= ringSize)
                    idx -= ringSize;
                ac[(size_t) idx] += frame[(size_t) j] * window[(size_t) j];
            }
        }

        for (int j = 0; j < N; ++j)
        {
            int idx = start + j;
            if (idx >= ringSize)
                idx -= ringSize;
            wsum[(size_t) idx] += window[(size_t) j] * window[(size_t) j];
        }

        publishDisplay();
    }

    void publishDisplay() noexcept
    {
        for (int i = 0; i < displayPoints; ++i)
        {
            const float b = std::clamp (displayBin[(size_t) i], 0.0f, (float) (bins - 1));
            const int k0 = std::min ((int) b, bins - 2);
            const float t = b - (float) k0;

            float red = 0.0f, lev = -200.0f;
            for (int c = 0; c < channels; ++c)
            {
                const float r = state[c][(size_t) k0] + t * (state[c][(size_t) k0 + 1] - state[c][(size_t) k0]);
                const float l = level[c][(size_t) k0] + t * (level[c][(size_t) k0 + 1] - level[c][(size_t) k0]);
                red = std::max (red, r);
                lev = std::max (lev, l - settings.inputTrimDb);
            }
            displayReduction[(size_t) i].store (red, std::memory_order_relaxed);
            displayLevel[(size_t) i].store (lev, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    EngineSettings settings;
    CurveSpec curve;
    bool curveDirty = true, curveSymmetric = true;
    int listenBand = -1;

    double fs = 44100.0;
    int channels = 2, N = 2048, M = 0, bins = 0, ringSize = 4096;
    float binHz = 1.0f, levelScale = 1.0f, coefKey = -1.0f;
    unsigned long long time = 0;
    int sinceFrame = 0;

    RealFFT ffts[3];
    RealFFT* fft = &ffts[0];

    std::vector<float> window, frame, wsum;
    std::vector<float> inRing[maxChannels], sideRing[maxChannels], acc[maxChannels];
    std::vector<float> re[maxChannels], im[maxChannels], level[maxChannels], power[maxChannels], reduction[maxChannels], state[maxChannels];
    std::vector<float> sens[maxChannels], weight[maxChannels], own[maxChannels];
    std::vector<float> sideRe, sideIm, linkedLevel, sensMean, weightMean, envelope, temp, listen;
    std::vector<float> invTimeScale, attackCoef, releaseCoef, displayBin;
    std::vector<double> prefix;

    std::atomic<float> displayReduction[displayPoints], displayLevel[displayPoints];
};

} // namespace phocus
