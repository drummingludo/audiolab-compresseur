// Audio Lab Compresseur — moteur audio
// Portage fidèle du processeur AudioWorklet « al-comp » d'Audio_Lab.html (mode compresseur).
// Mêmes formules, mêmes constantes, même ordre des opérations : le plugin compresse
// exactement comme l'outil web. Aucune dépendance à JUCE (testable seul).
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <atomic>

namespace audiolab
{
// Biquad du side-chain (RBJ), forme transposée directe II — identique au Biquad JS.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    enum Type { Off = 0, HPF = 1, LPF = 2, Bell = 3 };

    void set (int type, double f, double Q, double fs)
    {
        const double w  = 2.0 * M_PI * std::max (10.0, std::min (fs * 0.45, f)) / fs;
        const double c  = std::cos (w), sn = std::sin (w);
        const double al = sn / (2.0 * std::max (0.1, Q));
        double nb0, nb1, nb2, a0, na1, na2;
        if (type == HPF)      { nb0 = (1 + c) / 2; nb1 = -(1 + c); nb2 = (1 + c) / 2; a0 = 1 + al; na1 = -2 * c; na2 = 1 - al; }
        else if (type == LPF) { nb0 = (1 - c) / 2; nb1 = 1 - c;    nb2 = (1 - c) / 2; a0 = 1 + al; na1 = -2 * c; na2 = 1 - al; }
        else if (type == Bell)
        {
            const double A = std::pow (10.0, 6.0 / 40.0); // cloche fixe +6 dB
            nb0 = 1 + al * A; nb1 = -2 * c; nb2 = 1 - al * A; a0 = 1 + al / A; na1 = -2 * c; na2 = 1 - al / A;
        }
        else { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    inline double run (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct CompParams
{
    double thr   = 0;       // dB
    double ratio = 1;       // ≥ 20 sur le potentiomètre = ∞:1 (100:1), comme compParams()
    double knee  = 0;       // dB
    double atk   = 0.0001;  // s
    double rel   = 0.01;    // s
    double mk    = 0;       // dB
    bool   rms   = false;   // détection Peak / RMS
    double look  = 0;       // s
    int    scType = Biquad::Off;
    double scF   = 1000;    // Hz
    double scQ   = 0.7;
    bool   scListen = false;
};

class CompressorCore
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        g = 0; rmsAcc = 0;
        rmsCoef = std::exp (-1.0 / (0.003 * fs));
        maxLook = (int) std::ceil (0.02 * fs);
        bufL.assign ((size_t) maxLook + 128, 0.0f);
        bufR.assign ((size_t) maxLook + 128, 0.0f);
        w = 0;
        sc[0] = Biquad(); sc[1] = Biquad();
        scTypeSet = -1;
        grNow.store (0); grMax.store (0);
    }

    // Équivalent du message port.postMessage(compParams()) : les coefficients du side-chain
    // sont recalculés (sans remettre à zéro les états) quand le filtre change.
    void setParams (const CompParams& np)
    {
        p = np;
        if (p.scType != scTypeSet || std::abs (p.scF - scFSet) > 0 || std::abs (p.scQ - scQSet) > 0)
        {
            sc[0].set (p.scType, p.scF, p.scQ, fs); sc[1].set (p.scType, p.scF, p.scQ, fs);
            scTypeSet = p.scType; scFSet = p.scF; scQSet = p.scQ;
        }
    }

    int lookaheadSamples() const { return std::min (maxLook, jsRound (p.look * fs)); }
    int maxLookahead() const     { return maxLook; }

    // Traite un bloc stéréo (R peut être égal à L pour le mono). Écrit dans oL/oR.
    void process (const float* L, const float* R, float* oL, float* oR, int n)
    {
        const int look = lookaheadSamples();
        const double aA = std::exp (-1.0 / (std::max (0.00002, p.atk) * fs));
        const double aR = std::exp (-1.0 / (std::max (0.001, p.rel) * fs));
        const double thr = p.thr, ratio = p.ratio, knee = p.knee;
        const double mk = std::pow (10.0, p.mk / 20.0);
        const int len = (int) bufL.size();
        double peakGr = 0;

        for (int i = 0; i < n; ++i)
        {
            // détecteur (side-chain filtré)
            double dl = L[i], dr = R[i];
            if (p.scType != Biquad::Off) { dl = sc[0].run (dl); dr = sc[1].run (dr); }
            double lvl;
            if (p.rms)
            {
                const double sq = (dl * dl + dr * dr) / 2;
                rmsAcc = rmsCoef * rmsAcc + (1 - rmsCoef) * sq;
                lvl = std::sqrt (rmsAcc);
            }
            else lvl = std::max (std::abs (dl), std::abs (dr));

            const double db = lvl > 1e-6 ? 20 * std::log10 (lvl) : -120;

            // calculateur de gain (knee doux)
            const double ex = db - thr, hk = knee / 2;
            double gr;
            if (ex <= -hk) gr = 0;
            else if (ex < hk && knee > 0) { const double t = ex + hk; gr = (1 / ratio - 1) * t * t / (2 * knee); }
            else gr = (1 / ratio - 1) * ex;
            gr = -gr; // réduction positive en dB

            // lissage attaque / release en dB
            g = gr > g ? aA * g + (1 - aA) * gr : aR * g + (1 - aR) * gr;
            if (g > peakGr) peakGr = g;
            const double lin = std::pow (10.0, -g / 20) * mk;

            // lookahead : le gain s'applique au signal retardé
            const int wi = w;
            bufL[(size_t) wi] = L[i]; bufR[(size_t) wi] = R[i];
            const int ri = (wi - look + len) % len;
            double yl = bufL[(size_t) ri] * lin, yr = bufR[(size_t) ri] * lin;
            if (p.scListen) { yl = dl; yr = dr; } // écoute du side-chain
            oL[i] = (float) yl; oR[i] = (float) yr;
            w = (wi + 1) % len;
        }
        grNow.store ((float) g);
        float prev = grMax.load();
        while ((float) peakGr > prev && ! grMax.compare_exchange_weak (prev, (float) peakGr)) {}
    }

    // Réduction de gain courante (dB, positive) et crête depuis la dernière lecture — pour l'affichage.
    float getGR() const        { return grNow.load(); }
    float takeGRMax()          { return grMax.exchange (0); }

    // Courbe statique (vue Transfert) — identique à compCurve() de l'outil web.
    static double curve (double di, double thr, double ratioKnob, double knee, double mk)
    {
        const double r = ratioKnob, hk = knee / 2, ex = di - thr;
        double o;
        if (ex < -hk) o = di;
        else if (ex < hk) { const double t = (ex + hk) / knee; o = di + t * t * (1 / r - 1) * knee / 2; }
        else o = thr + (di - thr) / r;
        return o + mk;
    }

private:
    static int jsRound (double x) { return (int) std::floor (x + 0.5); } // Math.round()

    double fs = 48000;
    CompParams p;
    double g = 0, rmsAcc = 0, rmsCoef = 0;
    int maxLook = 0, w = 0;
    std::vector<float> bufL, bufR; // Float32Array dans l'outil web
    Biquad sc[2];
    int scTypeSet = -1;
    double scFSet = 0, scQSet = 0;
    std::atomic<float> grNow { 0 }, grMax { 0 };
};
} // namespace audiolab
