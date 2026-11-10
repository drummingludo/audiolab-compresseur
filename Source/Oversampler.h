// Audio Lab · suréchantillonnage ×1 / ×2 / ×4 / ×8 (brique commune, sans JUCE)
// Écrit pour Transversal Studio.
//
// Version 3 (Lab Comp 3.0.0) : réjection 120 dB (au lieu de 100), et un second jeu de filtres au choix,
// à phase minimale (demi-bandes IIR polyphases, deux branches de passe-tout, méthode de Valenzuela et
// Constantinides) : latence nulle annoncée, phase non linéaire dans l'aigu.
//
// Cascade d'étages ×2, chacun un filtre demi-bande à phase linéaire (sinc fenêtré Kaiser,
// longueur 4k+3, un coefficient sur deux nul) en forme polyphase :
//  - étage 1 (le plus exigeant) : bande passante jusqu'à 0,4535 fs (20 kHz à 44,1 kHz), réjection 100 dB ;
//  - étages suivants : bande de transition beaucoup plus large, filtres courts.
// La latence totale (montée + descente) est un nombre ENTIER d'échantillons à la fréquence de base
// (un retard de complément est ajouté au rythme le plus rapide) : le signal sec d'un Dry/Wet
// peut donc être recalé exactement, sans filtre en peigne.
// Temps réel : toute la mémoire est réservée dans prepare(), rien n'est alloué dans le traitement.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace audiolab::os
{
class HalfbandStage
{
public:
    // atten : réjection voulue (dB) ; passEdge : bord de bande passante normalisé au rythme haut (longueur 4k+3 déduite)
    void design (double atten, double passEdge)
    {
        // passEdge et stopEdge normalisés au rythme haut (1 = fs haut) ; demi-bande : stop = 0,5 − pass
        const double tw = 0.5 - 2 * passEdge;                       // largeur de transition
        int n = (int) std::ceil ((atten - 7.95) / (14.36 * tw)) + 1;
        while ((n - 3) % 4 != 0) ++n;                               // 4k+3
        len = n; c = (n - 1) / 2;
        const double beta = atten > 50 ? 0.1102 * (atten - 8.7) : 0.5842 * std::pow (atten - 21, 0.4) + 0.07886 * (atten - 21);
        auto I0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / 2) / k; s += t * t; } return s; };
        const double i0b = I0 (beta);
        // filtre d'interpolation (gain 2) : h[c] = 1, h[c ± 2m] = 0, h[c ± (2m+1)] = sinc
        odd.clear();
        for (int i = 0; i < n; ++i)
        {
            const int d = i - c;
            if (d == 0 || (d % 2) == 0) continue;
            const double x = 0.5 * d, s = std::sin (kPi * x) / (kPi * x);
            const double r = (double) d / c, w = I0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0b;
            odd.push_back (s * w);                                  // 2 × 0,5 × sinc(d/2)
        }
        // normalisation : la somme des coefficients impairs vaut 1 (gain exact au continu)
        double sum = 0; for (double v : odd) sum += v;
        for (double& v : odd) v /= sum;
        taps = (int) odd.size();                                    // = (n + 1) / 2
    }

    int length() const { return len; }
    int delayHigh() const { return c; }                             // retard en échantillons du rythme haut

    void prepareChannels (int nCh)
    {
        hUp.assign ((size_t) nCh, std::vector<double> ((size_t) taps * 2, 0.0));
        hDn.assign ((size_t) nCh, std::vector<double> ((size_t) taps * 2 + 2, 0.0));
        pUp.assign ((size_t) nCh, 0); pDn.assign ((size_t) nCh, 0);
        dnEven.assign ((size_t) nCh, std::vector<double> ((size_t) c / 2 + 2, 0.0)); pEv.assign ((size_t) nCh, 0);
        reset();
    }

    void reset()
    {
        for (auto& v : hUp) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : hDn) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : dnEven) std::fill (v.begin(), v.end(), 0.0);
        std::fill (pUp.begin(), pUp.end(), 0); std::fill (pDn.begin(), pDn.end(), 0); std::fill (pEv.begin(), pEv.end(), 0);
    }

    // Montée : 1 échantillon bas → 2 hauts. Sortie y[2m] (phase « impaire ») et y[2m+1] (retard pur).
    inline void up (int ch, double x, double& y0, double& y1)
    {
        auto& h = hUp[(size_t) ch]; int& p = pUp[(size_t) ch];
        // historique doublé (lecture contiguë sans modulo)
        p = p == 0 ? taps - 1 : p - 1;
        h[(size_t) p] = x; h[(size_t) (p + taps)] = x;
        const double* hp = h.data() + p;
        double s = 0;
        for (int j = 0; j < taps; ++j) s += odd[(size_t) j] * hp[j];
        // échantillon central : x retardé de (c − 1) / 2 échantillons bas
        y0 = s;
        y1 = hp[(c - 1) / 2];
    }

    // Descente : 2 échantillons hauts → 1 bas. x0 = premier (pair), x1 = second (impair).
    inline double down (int ch, double x0, double x1)
    {
        // filtre identique (gain 1) : y = 0,5 · [ Σ impairs · x_branche0  +  x_branche1 retardé ]
        auto& h = hDn[(size_t) ch]; int& p = pDn[(size_t) ch];
        p = p == 0 ? taps - 1 : p - 1;
        h[(size_t) p] = x0; h[(size_t) (p + taps)] = x0;
        const double* hp = h.data() + p;
        double s = 0;
        for (int j = 0; j < taps; ++j) s += odd[(size_t) j] * hp[j];
        auto& e = dnEven[(size_t) ch]; int& q = pEv[(size_t) ch];
        const int de = (c + 1) / 2;                                 // retard de la branche centrale (rythme bas)
        e[(size_t) q] = x1;
        const int ri = (q - de + (int) e.size() * 4) % (int) e.size();
        const double centre = e[(size_t) ri];
        q = (q + 1) % (int) e.size();
        return 0.5 * (s + centre);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    int len = 3, c = 1, taps = 2;
    std::vector<double> odd;
    std::vector<std::vector<double>> hUp, hDn, dnEven;
    std::vector<int> pUp, pDn, pEv;
};

// Demi-bande IIR polyphase à phase minimale : H(z) = ½ [A0(z²) + z⁻¹ A1(z²)], A0 et A1 = cascades de passe-tout
// du premier ordre (c + z⁻¹) / (1 + c z⁻¹) au rythme bas. Coefficients elliptiques (Valenzuela, Constantinides).
class IirHalfband
{
public:
    static constexpr int maxCoefs = 16;

    // atten : réjection voulue (dB) ; tbw : largeur de transition normalisée au rythme haut
    void design (double atten, double tbw)
    {
        for (nc = 2; nc <= maxCoefs; ++nc)
        {
            compute (nc, tbw);
            if (stopband (tbw) <= -atten) break;
        }
        nc = std::min (nc, maxCoefs);
        compute (nc, tbw);
        n0 = (nc + 1) / 2; n1 = nc / 2;
    }
    int numCoefs() const { return nc; }
    // pire niveau (dB) dans la bande coupée [0,25 + tbw / 2 ; 0,5]
    double stopband (double tbw) const
    {
        double worst = 0;
        for (int i = 0; i <= 400; ++i)
        {
            const double f = (0.25 + tbw / 2) + (0.25 - tbw / 2) * i / 400.0;
            worst = std::max (worst, magnitude (f));
        }
        return 20 * std::log10 (std::max (1e-30, worst));
    }
    // module de la réponse à la fréquence f (normalisée au rythme haut, 0 … 0,5)
    double magnitude (double f) const
    {
        const double w = 2 * kPi * f;
        // z⁻² = e^(−2jw)
        const double cr = std::cos (2 * w), ci = -std::sin (2 * w);
        double ar[2] = { 1, std::cos (w) }, ai[2] = { 0, -std::sin (w) };
        for (int k = 0; k < nc; ++k)
        {
            const double c = coef[k];
            const double nr = c + cr, ni = ci, dr = 1 + c * cr, di = c * ci, dd = dr * dr + di * di;
            const double qr = (nr * dr + ni * di) / dd, qi = (ni * dr - nr * di) / dd;
            const int b = k & 1;
            const double tr = ar[b] * qr - ai[b] * qi, ti = ar[b] * qi + ai[b] * qr;
            ar[b] = tr; ai[b] = ti;
        }
        const double re = 0.5 * (ar[0] + ar[1]), im = 0.5 * (ai[0] + ai[1]);
        return std::sqrt (re * re + im * im);
    }
    // retard de groupe au continu, en échantillons du rythme haut (montée ou descente seule)
    double dcDelayHigh() const
    {
        double d = 0.5;                                   // moyenne des deux branches : z⁻¹ sur l'une
        for (int k = 0; k < nc; ++k) d += 0.5 * 2 * (1 - coef[k]) / (1 + coef[k]);
        return d;
    }

    void prepareChannels (int nCh) { st.assign ((size_t) nCh * 4 * maxCoefs, 0.0); chans = nCh; }
    void reset() { std::fill (st.begin(), st.end(), 0.0); }

    inline void up (int ch, double x, double& y0, double& y1)
    {
        double* s = st.data() + (size_t) ch * 4 * maxCoefs;          // montée : états [0 … 2·maxCoefs[
        y0 = chain (x, s, 0); y1 = chain (x, s, 1);
    }
    inline double down (int ch, double x0, double x1)
    {
        double* s = st.data() + (size_t) ch * 4 * maxCoefs + 2 * maxCoefs;
        return 0.5 * (chain (x1, s, 0) + chain (x0, s, 1));
    }

private:
    // branche b (0 : coefficients pairs, 1 : impairs) ; états (x précédent, y précédent) par cellule
    inline double chain (double x, double* s, int b) const
    {
        for (int k = b; k < nc; k += 2)
        {
            double* q = s + 2 * k;
            const double y = coef[k] * (x - q[1]) + q[0];
            q[0] = x; q[1] = y; x = y;
        }
        return x;
    }
    void compute (int n, double tbw)
    {
        double k = std::tan ((0.5 - tbw) * kPi / 2); k *= k;
        const double kk = std::pow (1 - k * k, 0.25), e = 0.5 * (1 - kk) / (1 + kk), e4 = e * e * e * e;
        const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
        const int order = n * 2 + 1;
        for (int idx = 0; idx < n; ++idx)
        {
            const int c = idx + 1;
            double num = 0, den = 0.5, t; int i = 0, j = 1;
            do { t = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * kPi / order) * j; num += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
            num *= std::pow (q, 0.25);
            i = 1; j = -1;
            do { t = std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * kPi / order) * j; den += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
            double ww = num / den; ww *= ww;
            const double x = std::sqrt ((1 - ww * k) * (1 - ww / k)) / (1 + ww);
            coef[idx] = (1 - x) / (1 + x);
        }
    }
    static constexpr double kPi = 3.14159265358979323846;
    double coef[maxCoefs] {};
    int nc = 2, n0 = 1, n1 = 1, chans = 1;
    std::vector<double> st;
};

class Oversampler
{
public:
    static constexpr int maxStages = 3;

    // stages : 0 (×1), 1 (×2), 2 (×4), 3 (×8)
    void prepare (int numStages, int numChannels, int maxBlock, bool minimumPhase = false, double atten = 120.0)
    {
        minPh = minimumPhase;
        nSt = std::clamp (numStages, 0, maxStages); nCh = std::max (1, numChannels);
        fac = 1 << nSt;
        for (int s = 0; s < nSt; ++s)
        {
            // bord de bande passante normalisé au rythme haut de l'étage : 0,4535 fs_base / (2^(s+1) fs_base)
            const double pass = 0.4535 / (double) (2 << s);
            st[s].design (atten, pass);
            st[s].prepareChannels (nCh);
            iir[s].design (atten, 0.5 - 2 * pass);
            iir[s].prepareChannels (nCh);
        }
        // latence au rythme le plus rapide (montée + descente), complétée à un multiple du facteur
        int high = 0;
        for (int s = 0; s < nSt; ++s) high += 2 * st[s].delayHigh() * (fac >> (s + 1));
        pad = (fac - high % fac) % fac;
        lat = (high + pad) / fac;
        if (minPh) { pad = 0; lat = 0; }                       // phase minimale : aucune latence annoncée
        padBuf.assign ((size_t) nCh, std::vector<double> ((size_t) pad + 1, 0.0)); padPos.assign ((size_t) nCh, 0);
        buf.assign ((size_t) nCh, std::vector<float> ((size_t) maxBlock * (size_t) fac, 0.0f));
        tmp.assign ((size_t) maxBlock * (size_t) fac, 0.0);
        tmp2.assign ((size_t) maxBlock * (size_t) fac, 0.0);
        ptrs.assign ((size_t) nCh, nullptr);
        for (int ch = 0; ch < nCh; ++ch) ptrs[(size_t) ch] = buf[(size_t) ch].data();
        maxN = maxBlock;
    }

    void reset()
    {
        for (int s = 0; s < nSt; ++s) { st[s].reset(); iir[s].reset(); }
        for (auto& v : padBuf) std::fill (v.begin(), v.end(), 0.0);
        std::fill (padPos.begin(), padPos.end(), 0);
    }

    int factor() const { return fac; }
    int latency() const { return lat; }        // échantillons à la fréquence de base, entier (0 en phase minimale)
    bool minimumPhase() const { return minPh; }
    // phase minimale : retard réel au continu (montée + descente), en échantillons de base (non annoncé)
    double minPhaseDelay() const { double d = 0; for (int s = 0; s < nSt; ++s) d += 2 * iir[s].dcDelayHigh() / (double) (2 << s); return d; }
    int iirCoefs (int s) const { return iir[s].numCoefs(); }
    double iirStopband (int s) const { return iir[s].stopband (0.5 - 2 * 0.4535 / (double) (2 << s)); }
    int maxBlock() const { return maxN; }

    // Monte n échantillons de chaque voie ; renvoie les tampons hauts (n × factor échantillons par voie).
    float* const* upsample (const float* const* in, int n)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            float* out = buf[(size_t) ch].data();
            if (nSt == 0) { std::copy (in[ch], in[ch] + n, out); continue; }
            // étage par étage dans tmp / tmp2 (double)
            for (int i = 0; i < n; ++i) tmp[(size_t) i] = in[ch][i];
            int len = n;
            double* a = tmp.data(); double* b = tmp2.data();
            for (int s = 0; s < nSt; ++s)
            {
                if (minPh) for (int i = 0; i < len; ++i) iir[s].up (ch, a[i], b[2 * i], b[2 * i + 1]);
                else       for (int i = 0; i < len; ++i) st[s].up (ch, a[i], b[2 * i], b[2 * i + 1]);
                len *= 2; std::swap (a, b);
            }
            for (int i = 0; i < len; ++i) out[i] = (float) a[i];
        }
        return ptrs.data();
    }

    // Redescend les tampons hauts (traités sur place) vers out (n échantillons par voie).
    void downsample (float* const* out, int n)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* hi = buf[(size_t) ch].data();
            if (nSt == 0) { std::copy (hi, hi + n, out[ch]); continue; }
            int len = n * fac;
            double* a = tmp.data(); double* b = tmp2.data();
            // retard de complément au rythme le plus rapide
            auto& pb = padBuf[(size_t) ch]; int& pp = padPos[(size_t) ch];
            for (int i = 0; i < len; ++i)
            {
                if (pad == 0) { a[i] = hi[i]; continue; }
                pb[(size_t) pp] = hi[i];
                pp = (pp + 1) % (pad + 1);
                a[i] = pb[(size_t) pp];
            }
            for (int s = nSt - 1; s >= 0; --s)
            {
                len /= 2;
                if (minPh) for (int i = 0; i < len; ++i) b[i] = iir[s].down (ch, a[2 * i], a[2 * i + 1]);
                else       for (int i = 0; i < len; ++i) b[i] = st[s].down (ch, a[2 * i], a[2 * i + 1]);
                std::swap (a, b);
            }
            for (int i = 0; i < n; ++i) out[ch][i] = (float) a[i];
        }
    }

private:
    HalfbandStage st[maxStages];
    IirHalfband iir[maxStages];
    bool minPh = false;
    int nSt = 0, nCh = 1, fac = 1, pad = 0, lat = 0, maxN = 0;
    std::vector<std::vector<double>> padBuf; std::vector<int> padPos;
    std::vector<std::vector<float>> buf;
    std::vector<double> tmp, tmp2;
    std::vector<float*> ptrs;
};
} // namespace audiolab::os
