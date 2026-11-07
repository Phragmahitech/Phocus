#pragma once

#include <cmath>
#include <algorithm>

/**
    Courbe de sensibilité de la détection (« EQ inverse ») : 6 bandes.

      LOW CUT / HIGH CUT : coupent la détection sous / au-dessus d'une fréquence (6 à 48 dB/oct).
      4 bandes : PEAK, LOW SHELF, HIGH SHELF, BAND SHELF, REJECT, TILT.

    Une bande « montée » rend la détection plus sensible (plus de réduction) dans sa zone,
    une bande « baissée » moins sensible. REJECT exclut sa zone du traitement.
    Chaque bande a sa balance stéréo (dans le mode du plugin : L/R ou M/S).

    Sans dépendance : utilisée par le moteur et par l'interface (même calcul pour le dessin).
*/
namespace phocus
{

enum BandType { peak = 0, lowShelf, highShelf, bandShelf, reject, tilt, numBandTypes };

struct BandSpec
{
    bool on = true;
    int type = peak;
    float freq = 1000.0f;     // Hz
    float sens = 0.0f;        // dB, -24 .. +24
    float q = 1.0f;           // 0.1 .. 10
    float balance = 0.0f;     // -1 (A seul) .. +1 (B seul)
};

struct CutSpec
{
    bool on = false;
    float freq = 30.0f;
    int slope = 1;            // 0 = 6, 1 = 12, 2 = 24, 3 = 48 dB/oct
};

struct CurveSpec
{
    static constexpr int numBands = 4;
    CutSpec lowCut, highCut;
    BandSpec bands[numBands];

    bool operator== (const CurveSpec& o) const noexcept
    {
        // Égalité exacte voulue : on ne recalcule la courbe que si un réglage a bougé.
        auto eq = [] (float a, float b) { return ! (a < b) && ! (b < a); };
        auto same = [eq] (const BandSpec& a, const BandSpec& b)
        {
            return a.on == b.on && a.type == b.type && eq (a.freq, b.freq) && eq (a.sens, b.sens) && eq (a.q, b.q) && eq (a.balance, b.balance);
        };
        auto sameCut = [eq] (const CutSpec& a, const CutSpec& b) { return a.on == b.on && eq (a.freq, b.freq) && a.slope == b.slope; };

        if (! sameCut (lowCut, o.lowCut) || ! sameCut (highCut, o.highCut))
            return false;
        for (int i = 0; i < numBands; ++i)
            if (! same (bands[i], o.bands[i]))
                return false;
        return true;
    }
    bool operator!= (const CurveSpec& o) const noexcept     { return ! (*this == o); }
};

/** Valeur de la courbe à une fréquence. */
struct CurvePoint
{
    float sensA = 0.0f, sensB = 0.0f;     // dB ajoutés à la détection, par canal
    float weightA = 1.0f, weightB = 1.0f; // 0..1 : part du traitement gardée (coupures, REJECT)
};

/** Largeur en octaves d'une bande de facteur Q. */
inline float bandwidthOctaves (float q) noexcept
{
    q = std::max (0.05f, q);
    return (float) (2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q)));
}

/** Part de la balance pour chaque canal : à gauche (−) le canal A garde tout, B s'efface ; et inversement. */
inline void balanceWeights (float balance, float& a, float& b) noexcept
{
    balance = std::clamp (balance, -1.0f, 1.0f);
    a = balance <= 0.0f ? 1.0f : 1.0f - balance;
    b = balance >= 0.0f ? 1.0f : 1.0f + balance;
}

/** Forme d'une bande (sans son gain) à x octaves de sa fréquence, et part rejetée pour REJECT. */
inline float bandShape (const BandSpec& band, float x) noexcept
{
    const float bw = std::max (0.03f, bandwidthOctaves (band.q));

    switch (band.type)
    {
        case lowShelf:  return 1.0f / (1.0f + std::exp (x * 4.4f / bw));
        case highShelf: return 1.0f / (1.0f + std::exp (-x * 4.4f / bw));
        case bandShelf:
        case reject:
        {
            const float k = 10.0f / std::max (0.25f, bw);
            const float lo = 1.0f / (1.0f + std::exp (-(x + 0.5f * bw) * k));
            const float hi = 1.0f / (1.0f + std::exp (-(x - 0.5f * bw) * k));
            return std::clamp (lo - hi, 0.0f, 1.0f);
        }
        case tilt:      return std::clamp (x / 3.0f, -2.5f, 2.5f);
        case peak:
        default:
        {
            const float sigma = bw / 2.3548f;
            return std::exp (-0.5f * (x / sigma) * (x / sigma));
        }
    }
}

/** Atténuation d'une coupure (Butterworth d'ordre n, en amplitude) : 0..1. */
inline float cutWeight (const CutSpec& cut, float f, bool high) noexcept
{
    if (! cut.on)
        return 1.0f;
    if (f <= 0.0f)
        return high ? 1.0f : 0.0f;

    static constexpr int orders[] = { 1, 2, 4, 8 };
    const int n = orders[std::clamp (cut.slope, 0, 3)];
    const double ratio = high ? (double) f / (double) cut.freq : (double) cut.freq / (double) f;
    return (float) (1.0 / std::sqrt (1.0 + std::pow (ratio, 2.0 * n)));
}

inline CurvePoint evaluateCurve (const CurveSpec& spec, float f) noexcept
{
    CurvePoint p;
    const float w = cutWeight (spec.lowCut, f, false) * cutWeight (spec.highCut, f, true);
    p.weightA = p.weightB = w;

    if (f <= 0.0f)
        return p;

    for (const auto& band : spec.bands)
    {
        if (! band.on)
            continue;

        float a, b;
        balanceWeights (band.balance, a, b);
        const float x = std::log2 (f / std::max (1.0f, band.freq));
        const float shape = bandShape (band, x);

        if (band.type == reject)
        {
            p.weightA *= 1.0f - shape * a;
            p.weightB *= 1.0f - shape * b;
        }
        else
        {
            p.sensA += band.sens * shape * a;
            p.sensB += band.sens * shape * b;
        }
    }

    return p;
}

} // namespace phocus
