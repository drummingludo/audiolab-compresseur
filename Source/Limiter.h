// Audio Lab · limiteur à plafond garanti, détection true peak (brique commune, sans JUCE)
// Écrit pour Transversal Studio.
//
// Principe (limiteur « brickwall » à anticipation) :
//  1. Détection : crête de chaque échantillon, et en option crête vraie (true peak) estimée par
//     interpolation ×8 (polyphase 8 × 48 coefficients, sinc fenêtré Kaiser), plus fine que le ×4 de l'ITU-R BS.1770.
//  2. Gain nécessaire g(t) = min (1, plafond / crête(t)).
//  3. Minimum glissant de g sur la fenêtre d'anticipation (L + 1 échantillons), puis retour (release)
//     exponentiel : le gain ne remonte jamais plus vite que le release, et ne dépasse jamais ce minimum.
//  4. Deux moyennes glissantes en cascade (longueur totale L) : l'attaque est une rampe douce en S.
//  5. Le gain s'applique au son retardé de L : à l'instant où passe une crête, toutes les valeurs
//     moyennées sont inférieures ou égales au gain qu'elle demande. Le plafond est donc garanti
//     (exactement sur les échantillons ; crêtes vraies, mesurées à ×8, sous le plafond pour tout contenu
//     jusqu'à 20 kHz ; un bruit blanc plein spectre, avec du contenu entre 21 et 24 kHz, peut dépasser de 0,1 dB).
// Latence fixe = anticipation maximale + 24 (retard du détecteur true peak), quelle que soit
// l'anticipation réglée : changer l'anticipation ne change pas la latence annoncée à l'hôte.
// Temps réel : mémoire réservée dans prepare(), rien n'est alloué dans le traitement.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace audiolab::lim
{
// Estimation de la crête vraie : interpolation ×8, retard de 24 échantillons
// TAPS coefficients par phase (retard TAPS / 2) : 48 pour la détection courante, 96 pour la version fine (3.0.0)
template <int TAPS, int BETA10>
class TruePeakT
{
public:
    static constexpr int taps = TAPS, phases = 8, delay = TAPS / 2;

    TruePeakT()
    {
        auto I0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / 2) / k; s += t * t; } return s; };
        const double beta = BETA10 / 10.0, i0b = I0 (beta), PI = 3.14159265358979323846;
        const int N = taps * phases, c = N / 2;
        for (int p = 0; p < phases; ++p)
        {
            double sum = 0;
            for (int j = 0; j < taps; ++j)
            {
                const int k = phases * j + p;
                const double x = (double) (k - c) / phases;
                const double s = std::abs (x) < 1e-12 ? 1.0 : std::sin (PI * x) / (PI * x);
                const double r = (double) (k - c) / c;
                h[p][j] = s * I0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
                sum += h[p][j];
            }
            for (int j = 0; j < taps; ++j) h[p][j] /= sum;
        }
    }

    void prepare (int nCh) { hist.assign ((size_t) std::max (1, nCh), std::vector<double> (2 * taps, 0.0)); pos.assign (hist.size(), 0); }
    void reset() { for (auto& v : hist) std::fill (v.begin(), v.end(), 0.0); std::fill (pos.begin(), pos.end(), 0); }

    // Ajoute x ; renvoie la crête absolue (échantillon + 7 points interpolés) de l'instant n − 24,
    // et dans sp la crête de l'échantillon seul au même instant
    inline double push (int ch, double x, double& sp)
    {
        auto& hh = hist[(size_t) ch]; int& p = pos[(size_t) ch];
        p = p == 0 ? taps - 1 : p - 1;
        hh[(size_t) p] = x; hh[(size_t) (p + taps)] = x;
        const double* v = hh.data() + p;
        // 9 points de n − 24 à n − 23 (le dernier est l'échantillon suivant, déjà connu)
        double y[phases + 1];
        y[0] = std::abs (v[delay]); y[phases] = std::abs (v[delay - 1]);
        sp = y[0];
        int jm = 0;
        for (int ph = 1; ph < phases; ++ph)
        {
            double s = 0;
            for (int j = 0; j < taps; ++j) s += h[ph][j] * v[j];
            y[ph] = std::abs (s);
            if (y[ph] > y[jm]) jm = ph;
        }
        if (y[phases] > y[jm]) jm = phases;
        double m = y[jm];
        // sommet de la parabole passant par les 3 points autour du maximum (crête entre deux points)
        if (jm > 0 && jm < phases)
        {
            const double a = y[jm - 1], b = y[jm], c = y[jm + 1], d = a - 2 * b + c;
            if (d < 0) m = std::max (m, b - (a - c) * (a - c) / (8 * d));
        }
        return m;
    }

    // Variante économe : la crête vraie n'est calculée finement que si elle peut atteindre « level ».
    // 1. si les 9 échantillons autour de l'instant sont tous sous level / 2 : crête de l'échantillon (pas de calcul) ;
    // 2. sinon 3 points interpolés (×4) ; 3. s'ils approchent level à moins de 1 dB : les 7 points (×8) et la parabole.
    inline double pushGated (int ch, double x, double& sp, double level)
    {
        auto& hh = hist[(size_t) ch]; int& p = pos[(size_t) ch];
        p = p == 0 ? taps - 1 : p - 1;
        hh[(size_t) p] = x; hh[(size_t) (p + taps)] = x;
        const double* v = hh.data() + p;
        sp = std::abs (v[delay]);
        double nb = sp;
        for (int k = 1; k <= 4; ++k) nb = std::max (nb, std::max (std::abs (v[delay - k]), std::abs (v[delay + k])));
        if (nb < 0.5 * level) return sp;
        double y[phases + 1];
        y[0] = sp; y[phases] = std::abs (v[delay - 1]);
        double coarse = std::max (y[0], y[phases]);
        for (int ph = 2; ph < phases; ph += 2)
        {
            double s = 0;
            for (int j = 0; j < taps; ++j) s += h[ph][j] * v[j];
            y[ph] = std::abs (s); coarse = std::max (coarse, y[ph]);
        }
        if (coarse < 0.89 * level) return coarse;
        int jm = 0;
        for (int ph = 1; ph < phases; ph += 2)
        {
            double s = 0;
            for (int j = 0; j < taps; ++j) s += h[ph][j] * v[j];
            y[ph] = std::abs (s);
        }
        for (int ph = 1; ph <= phases; ++ph) if (y[ph] > y[jm]) jm = ph;
        double m = y[jm];
        if (jm > 0 && jm < phases)
        {
            const double a = y[jm - 1], b = y[jm], c = y[jm + 1], d = a - 2 * b + c;
            if (d < 0) m = std::max (m, b - (a - c) * (a - c) / (8 * d));
        }
        return m;
    }

private:
    double h[phases][taps] {};
    std::vector<std::vector<double>> hist;
    std::vector<int> pos;
};

using TruePeak = TruePeakT<48, 80>;      // détection (identique à la 2.0.0)
using TruePeakHQ = TruePeakT<96, 100>;   // contrôle fin de la sortie (second étage du limiteur ∞)

class Limiter
{
public:
    // maxLookahead : anticipation maximale (échantillons, ≥ 1)
    void prepare (double sampleRate, int numChannels, int maxLookahead)
    {
        fs = sampleRate; nCh = std::max (1, numChannels);
        maxL = std::max (1, maxLookahead);
        lat = maxL + TruePeak::delay;
        tp.prepare (nCh);
        // anneaux : son (latence + 1), gain demandé (maxL + 1), file du minimum, deux moyennes
        int sz = 1; while (sz < lat + 2) sz <<= 1;
        audio.assign ((size_t) nCh, std::vector<float> ((size_t) sz, 0.0f)); aMask = sz - 1;
        int gs = 1; while (gs < maxL + 2) gs <<= 1;
        req.assign ((size_t) gs, 1.0); rMask = gs - 1;
        rel1.assign ((size_t) gs, 1.0); box1.assign ((size_t) gs, 1.0);
        dqIdx.assign ((size_t) gs, 0); dqVal.assign ((size_t) gs, 1.0);
        L = std::min (L, maxL); Lreq = L;
        reset();
    }

    void reset()
    {
        for (auto& v : audio) std::fill (v.begin(), v.end(), 0.0f);
        std::fill (req.begin(), req.end(), 1.0); std::fill (rel1.begin(), rel1.end(), 1.0); std::fill (box1.begin(), box1.end(), 1.0);
        tp.reset();
        n = 0; aw = 0; r = 1; dqHead = dqTail = 0;
        setLookahead (Lreq, true);
        grMin = 1;
    }

    void setCeiling (double lin)  { ceil = std::max (1e-6, lin); }
    void setRelease (double sec)  { kRel = 1 - std::exp (-1 / (std::max (0.001, sec) * fs)); }
    // true peak : marge de 0,05 dB (incertitude de l'interpolation) et anticipation d'au moins 1 ms
    // (un gain qui change d'un coup crée lui-même des crêtes entre les échantillons)
    void setTruePeak (bool on)    { if (on != useTp) { useTp = on; setLookahead (Lreq, true); } }
    // anticipation (échantillons, 1 à maxLookahead) ; les fenêtres sont reconstruites sans changer la latence
    void setLookahead (int samples, bool force = false)
    {
        Lreq = samples;
        const int lo = useTp ? std::min (maxL, std::max (1, (int) std::lround (0.001 * fs))) : 1;
        const int nl = std::clamp (samples, lo, maxL);
        if (nl == L && ! force) return;
        L = nl;
        L1 = std::max (1, (L + 1) / 2); L2 = L - L1 + 1;        // L1 + L2 − 1 = L
        // reconstruit la file du minimum et les sommes à partir de l'historique
        // la prochaine détection lue sera k = n − (maxL − L) ; la fenêtre couvre [k − L, k]
        dqHead = dqTail = 0;
        const long kNext = n - (maxL - L);
        for (long k = kNext - L; k < kNext; ++k) dqPush (k, reqAt (k));
        s1 = 0; for (int i = 0; i < L1; ++i) s1 += rel1[(size_t) ((n - 1 - i) & rMask)];
        s2 = 0; for (int i = 0; i < L2; ++i) s2 += box1[(size_t) ((n - 1 - i) & rMask)];
    }

    int latency() const { return lat; }
    int lookahead() const { return L; }

    // Traite sur place ; det (facultatif) = signal de détection (sinon le son lui-même)
    void process (float* const* io, int len, const float* const* det = nullptr)
    {
        for (int i = 0; i < len; ++i)
        {
            // 1. détection (instant n − 24 pour les deux mesures, pour rester alignées)
            double pk = 0;
            for (int ch = 0; ch < nCh; ++ch)
            {
                const double x = det != nullptr ? det[ch][i] : io[ch][i];
                double sp = 0;
                const double t = tp.push (ch, x, sp);          // crêtes de l'instant n − 24
                pk = std::max (pk, useTp ? t : sp);
            }
            const double c = useTp ? ceil * 0.99426694 : ceil;   // −0,05 dB en true peak
            const double g = pk > c ? c / pk : 1.0;
            req[(size_t) (n & rMask)] = g;
            // 2. minimum glissant sur L + 1 valeurs, détection retardée de (maxL − L)
            const long k = n - (maxL - L);
            dqPush (k, reqAt (k));
            while (dqHead != dqTail && dqIdx[(size_t) (dqHead & rMask)] < k - L) ++dqHead;
            const double m = dqVal[(size_t) (dqHead & rMask)];
            // 3. release (attaque instantanée sur le minimum)
            r = m < r ? m : r + (m - r) * kRel;
            // 4. deux moyennes glissantes
            s1 += r - rel1[(size_t) ((n - L1) & rMask)]; rel1[(size_t) (n & rMask)] = r;
            const double b = s1 / L1;
            s2 += b - box1[(size_t) ((n - L2) & rMask)]; box1[(size_t) (n & rMask)] = b;
            const double gain = std::min (1.0, s2 / L2);
            if (gain < grMin) grMin = gain;
            // 5. son retardé de la latence
            const int ri = (aw - lat) & aMask;
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto& a = audio[(size_t) ch];
                a[(size_t) aw] = io[ch][i];
                io[ch][i] = (float) (a[(size_t) ri] * gain);
            }
            aw = (aw + 1) & aMask;
            ++n;
            // resynchronise les sommes de temps en temps (dérive des additions)
            if ((n & 0xFFFF) == 0) resync();
        }
    }

    // Réduction de gain la plus forte depuis le dernier appel (dB, ≥ 0)
    double takeGainReductionDb() { const double g = grMin; grMin = 1; return -20 * std::log10 (std::max (1e-9, g)); }

private:
    double reqAt (long k) const { return k < 0 ? 1.0 : req[(size_t) (k & rMask)]; }
    void dqPush (long k, double v)
    {
        while (dqHead != dqTail && dqVal[(size_t) ((dqTail - 1) & rMask)] >= v) --dqTail;
        dqIdx[(size_t) (dqTail & rMask)] = k; dqVal[(size_t) (dqTail & rMask)] = v; ++dqTail;
    }
    void resync()
    {
        s1 = 0; for (int i = 0; i < L1; ++i) s1 += rel1[(size_t) ((n - 1 - i) & rMask)];
        s2 = 0; for (int i = 0; i < L2; ++i) s2 += box1[(size_t) ((n - 1 - i) & rMask)];
    }

    double fs = 48000, ceil = 1, kRel = 0.001;
    bool useTp = true;
    int nCh = 2, maxL = 1, lat = 9, L = 1, L1 = 1, L2 = 1, Lreq = 1;
    TruePeak tp;
    std::vector<std::vector<float>> audio; int aMask = 0, aw = 0;
    std::vector<double> req, rel1, box1; int rMask = 0;
    std::vector<long> dqIdx; std::vector<double> dqVal; long dqHead = 0, dqTail = 0;
    long n = 0; double r = 1, s1 = 1, s2 = 1, grMin = 1;
};
} // namespace audiolab::lim
