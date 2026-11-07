#pragma once

#include <cmath>
#include <vector>
#include <cstddef>

/**
    FFT réelle de taille M (puissance de 2), calculée par une FFT complexe de taille M/2.
    Sans dépendance (testable seule). Toute la mémoire est réservée par setSize().

    forward : x[0..M) -> X[0..M/2] (re, im), sans normalisation.
    inverse : X[0..M/2] -> x[0..M), divisé par M (inverse exacte de forward).
*/
class RealFFT
{
public:
    void setSize (int m)
    {
        if (m == size)
            return;

        size = m;
        half = m / 2;

        int bits = 0;
        while ((1 << bits) < half)
            ++bits;

        bitrev.assign ((size_t) half, 0);
        for (int i = 0; i < half; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }

        // Facteurs de la FFT complexe (taille half) et du dépliage réel (taille m)
        twRe.resize ((size_t) half / 2 + 1);
        twIm.resize ((size_t) half / 2 + 1);
        for (int k = 0; k <= half / 2; ++k)
        {
            const double a = -2.0 * pi * (double) k / (double) half;
            twRe[(size_t) k] = (float) std::cos (a);
            twIm[(size_t) k] = (float) std::sin (a);
        }

        unRe.resize ((size_t) half + 1);
        unIm.resize ((size_t) half + 1);
        for (int k = 0; k <= half; ++k)
        {
            const double a = -2.0 * pi * (double) k / (double) m;
            unRe[(size_t) k] = (float) std::cos (a);
            unIm[(size_t) k] = (float) std::sin (a);
        }

        zr.assign ((size_t) half, 0.0f);
        zi.assign ((size_t) half, 0.0f);
    }

    int getSize() const noexcept        { return size; }

    void forward (const float* x, float* re, float* im) noexcept
    {
        for (int n = 0; n < half; ++n)
        {
            const int r = bitrev[(size_t) n];
            zr[(size_t) r] = x[2 * n];
            zi[(size_t) r] = x[2 * n + 1];
        }

        butterflies (false);

        // Dépliage : X[k] = Fe[k] + W^k Fo[k]
        re[0]    = zr[0] + zi[0];
        im[0]    = 0.0f;
        re[half] = zr[0] - zi[0];
        im[half] = 0.0f;

        for (int k = 1; k < half; ++k)
        {
            const float ar = zr[(size_t) k], ai = zi[(size_t) k];
            const float br = zr[(size_t) (half - k)], bi = -zi[(size_t) (half - k)];   // conj Z[H-k]

            const float feR = 0.5f * (ar + br), feI = 0.5f * (ai + bi);
            // Fo = -0.5 i (Z[k] - conj Z[H-k])
            const float dR = ar - br, dI = ai - bi;
            const float foR = 0.5f * dI, foI = -0.5f * dR;

            const float wr = unRe[(size_t) k], wi = unIm[(size_t) k];
            re[k] = feR + (wr * foR - wi * foI);
            im[k] = feI + (wr * foI + wi * foR);
        }
    }

    void inverse (const float* re, const float* im, float* x) noexcept
    {
        for (int k = 0; k < half; ++k)
        {
            const float ar = re[k], ai = im[k];
            const float br = re[half - k], bi = -im[half - k];   // conj X[H-k]

            const float feR = 0.5f * (ar + br), feI = 0.5f * (ai + bi);
            // Fo = 0.5 (X[k] - conj X[H-k]) W^-k
            const float dR = 0.5f * (ar - br), dI = 0.5f * (ai - bi);
            const float wr = unRe[(size_t) k], wi = -unIm[(size_t) k];
            const float foR = dR * wr - dI * wi, foI = dR * wi + dI * wr;

            // Z = Fe + i Fo, rangé en ordre inversé pour la FFT
            const int r = bitrev[(size_t) k];
            zr[(size_t) r] = feR - foI;
            zi[(size_t) r] = feI + foR;
        }

        butterflies (true);

        const float scale = 1.0f / (float) half;
        for (int n = 0; n < half; ++n)
        {
            x[2 * n]     = zr[(size_t) n] * scale;
            x[2 * n + 1] = zi[(size_t) n] * scale;
        }
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    void butterflies (bool inverseDirection) noexcept
    {
        const float sign = inverseDirection ? -1.0f : 1.0f;

        for (int len = 2; len <= half; len <<= 1)
        {
            const int step = half / len, h = len / 2;
            for (int start = 0; start < half; start += len)
            {
                for (int j = 0; j < h; ++j)
                {
                    // W_len^j = W_half^(j*step) ; seuls les indices <= half/2 sont stockés
                    const int t = j * step;
                    float wr, wi;
                    if (t <= half / 2) { wr = twRe[(size_t) t]; wi = twIm[(size_t) t]; }
                    else               { wr = -twRe[(size_t) (t - half / 2)]; wi = -twIm[(size_t) (t - half / 2)]; }
                    wi *= sign;

                    const size_t a = (size_t) (start + j), b = (size_t) (start + j + h);
                    const float xr = zr[b] * wr - zi[b] * wi;
                    const float xi = zr[b] * wi + zi[b] * wr;
                    zr[b] = zr[a] - xr;
                    zi[b] = zi[a] - xi;
                    zr[a] += xr;
                    zi[a] += xi;
                }
            }
        }
    }

    int size = 0, half = 0;
    std::vector<int> bitrev;
    std::vector<float> twRe, twIm, unRe, unIm, zr, zi;
};
