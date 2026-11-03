// Lab Comp · moteur audio 2.0.0 (aucune dépendance à JUCE, testable seul)
//
// Réglages par défaut (Off / Internal / 100 % / Stereo / Hold 0 / Auto Off / RMS 3 ms, ratio < ∞) :
// calcul identique au processeur AudioWorklet « al-comp » d'Audio_Lab.html (mêmes formules, même ordre),
// sortie identique au bit près à la version 1.2.1.
//
// Ce qui s'ajoute en 2.0.0 :
//  - ratio ∞ (potentiomètre ≥ 20) = vrai limiteur à plafond garanti (seuil + make-up) : détection crête ou
//    crête vraie (lim::TruePeak de Limiter.h), minimum glissant sur la fenêtre d'anticipation, retour en dB,
//    deux moyennes glissantes (rampe en S) ; le gain s'applique au son retardé de l'anticipation ;
//  - side-chain externe (fondu entre détection interne et externe), filtre du side-chain sur les deux ;
//  - détecteur : stereo link 0 à 100 %, Stereo / Mid-Side, maintien (Hold), release automatique à deux
//    constantes de temps, fenêtre RMS réglable ;
//  - suréchantillonnage ×2 / ×4 du compresseur (Oversampler.h : détecteur et gain au rythme haut) ;
//  - seuil, pente (ratio), knee, make-up, link lissés ; anticipation, passage ↔ ∞, Stereo ↔ Mid-Side
//    par fondu enchaîné ; suréchantillonnage par fondu de sortie (latence modifiée).
//
// Forme générale de la sortie : y[n] = x[n − A] · g[n − B], avec A − B = anticipation.
//  - latence variable (VST3, AU) : A = anticipation, B = 0 ;
//  - latence constante (AAX) : A = anticipation maximale (10 ms), B = A − anticipation (gain retardé).
// Latence totale = A + latence du suréchantillonnage. Le signal sec (Dry/Wet, bypass) est lu dans le même
// anneau, à la même latence, avec les mêmes fondus : aucun filtre en peigne.
// Temps réel : toute la mémoire est réservée dans prepare(), rien n'est alloué ni verrouillé ensuite.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include "Limiter.h"
#include "Oversampler.h"

namespace audiolab
{
// π en double (= Math.PI en JavaScript) ; M_PI n'existe pas sous Windows
inline constexpr double kPi = 3.14159265358979323846;

// Biquad du side-chain (RBJ, transformée bilinéaire pré-déformée à f0), forme transposée directe II — identique au Biquad JS.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    enum Type { Off = 0, HPF = 1, LPF = 2, Bell = 3 };

    void set (int type, double f, double Q, double fs)
    {
        const double w  = 2.0 * kPi * std::max (10.0, std::min (fs * 0.45, f)) / fs;
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
    void clear() { z1 = z2 = 0; }
};

struct CompParams
{
    double thr   = 0;       // dB
    double ratio = 1;       // valeur du potentiomètre : ≥ 20 = ∞:1 (vrai limiteur)
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
    // 2.0.0
    bool   truePeak   = true;    // limiteur ∞ : plafond en crête vraie
    bool   scExternal = false;   // side-chain externe (si le bus est branché)
    double link       = 1.0;     // stereo link 0 … 1
    bool   midSide    = false;   // détection et gain sur Mid / Side
    double hold       = 0;       // s
    bool   autoRel    = false;   // release automatique (deux constantes de temps)
    double rmsWin     = 0.003;   // s
    int    os         = 0;       // suréchantillonnage : 0 = Off, 1 = ×2, 2 = ×4
};

// Égalité exacte voulue (identité au bit près quand un réglage ne bouge pas), sans avertissement du compilateur
inline bool same (double a, double b) { return ! (a < b) && ! (b < a); }

// Lissage d'un réglage : rampe linéaire (20 ms) suivie d'un passe-bas de 2 ms qui arrondit les coudes
// (pas de petit « tic » au début ni à la fin) ; la valeur rejoint EXACTEMENT la cible à la fin,
// de sorte que le calcul est identique au bit près tant que le réglage ne bouge pas.
struct Ramp
{
    double lin = 0, cur = 0, target = 0, step = 0, a = 0, eps = 1e-9;
    int left = 0, len = 1;
    void reset (double v) { lin = cur = target = v; left = 0; step = 0; }
    void setLength (int n, double fsRate) { len = std::max (1, n); a = std::exp (-1.0 / (0.002 * fsRate)); }
    void set (double t) { if (! same (t, target)) { target = t; left = len; step = (t - lin) / len; } }
    bool active() const { return left > 0 || ! same (cur, target); }
    inline double next()
    {
        if (left > 0) { if (--left == 0) lin = target; else lin += step; }
        else if (same (cur, target)) return cur;
        cur = lin + a * (cur - lin);
        if (left == 0 && std::abs (cur - target) <= eps) cur = target;
        return cur;
    }
};

// Lissage de la réduction de gain (dB, positive) : attaque / release exponentiels, maintien (Hold),
// étage lent pour le release automatique (sortie = max (rapide, lent), dosé par wAuto).
struct GainStage
{
    double g = 0, gs = 0;
    int hc = 0;

    // Release automatique = deux détecteurs en parallèle (double RC) : le rapide (release ÷ 3) et un lent
    // (constante release × 3, en charge comme en décharge) qui ne se remplit que si la compression dure ;
    // on garde le plus fort des deux.
    // compresseur : attaque exponentielle (formule d'Audio Lab)
    inline double comp (double gr, double aA, double aR, double aS, int hold, double wAuto)
    {
        if (gr > g) { g = aA * g + (1 - aA) * gr; hc = hold; }
        else if (hc > 0) --hc;
        else g = aR * g + (1 - aR) * gr;
        gs = aS * gs + (1 - aS) * gr;
        return wAuto > 0 ? g + wAuto * std::max (0.0, gs - g) : g;
    }
    // limiteur : attaque instantanée (l'anticipation fait la rampe) ; ne descend jamais sous la cible
    inline double lim (double t, double aR, double aS, int hold, double wAuto)
    {
        if (t >= g) { g = t; hc = hold; }
        else if (hc > 0) --hc;
        else g = aR * g + (1 - aR) * t;
        gs = aS * gs + (1 - aS) * t;
        return wAuto > 0 ? g + wAuto * std::max (0.0, gs - g) : g;
    }
};

class CompressorCore
{
public:
    static constexpr int chunk = 256;            // traitement interne par paquets de 256 échantillons
    static constexpr double xfadeSec = 0.020;    // fondu enchaîné (anticipation, ∞, Mid-Side)
    static constexpr double osFadeSec = 0.005;   // fondu de sortie au changement de suréchantillonnage
    static constexpr double rampSec = 0.020;     // lissage des réglages
    static constexpr double lookMaxSec = 0.010;  // anticipation maximale (paramètre Lookahead)

    // ── Préparation (hors temps réel) ─────────────────────────────────────
    void prepare (double sampleRate, bool constantLatency, const CompParams& initial)
    {
        fs = sampleRate; constLat = constantLatency;
        maxLook = std::max (1, jsRound (lookMaxSec * fs));
        p = initial;
        osCur = osTarget = std::clamp (initial.os, 0, 2);

        // anneaux du chemin compresseur, dimensionnés pour ×4
        int sz = 1; while (sz < maxLook * 4 + 8) sz <<= 1;
        hMask = sz - 1;
        for (int c = 0; c < 2; ++c) { xb[c].assign ((size_t) sz, 0.0f); db[c].assign ((size_t) sz, 0.0f); }
        for (auto& b : bank) for (int c = 0; c < 2; ++c) b.gh[c].assign ((size_t) sz, 1.0);
        for (int s = 0; s < 2; ++s)
        {
            upM[s].prepare (s + 1, 2, chunk);
            upS[s].prepare (s + 1, 2, chunk);
        }
        for (int c = 0; c < 2; ++c) { wet1[c].assign ((size_t) chunk, 0.0f); comp[c].assign ((size_t) chunk, 0.0f); }
        osLatMax = std::max (upM[0].latency(), upM[1].latency());

        // anneaux du chemin de base (sec, limiteur)
        int bs = 1; while (bs < maxLook + osLatMax + 8) bs <<= 1;
        bMask = bs - 1;
        for (int c = 0; c < 2; ++c) { mainR[c].assign ((size_t) bs, 0.0f); scR[c].assign ((size_t) bs, 0.0f); }
        for (auto& u : unit) u.allocate (maxLook);

        xfLen = std::max (1, jsRound (xfadeSec * fs));
        osFadeLen = std::max (1, jsRound (osFadeSec * fs));

        // état initial = réglages demandés, sans transition
        cur = desired (p);
        nxt = cur;
        phase = Phase::idle; osPhase = OsPhase::none;
        ba = bn = 0; ua = un = 0; needUnit = false;
        for (auto& b : bank)
        {
            for (int c = 0; c < 2; ++c) { b.st[c] = GainStage(); b.acc[c] = 0; std::fill (b.gh[c].begin(), b.gh[c].end(), 1.0); }
            b.accLink = 0; b.ms = cur.ms;
        }
        hw = 0; bw = 0;
        for (int c = 0; c < 2; ++c) { std::fill (xb[c].begin(), xb[c].end(), 0.0f); std::fill (db[c].begin(), db[c].end(), 0.0f);
                                      std::fill (mainR[c].begin(), mainR[c].end(), 0.0f); std::fill (scR[c].begin(), scR[c].end(), 0.0f); }
        scFCur = p.scF; scQCur = p.scQ; scTypeCur = p.scType;
        applyFactor (osCur, true);
        setParams (p);
        resetRamps();
        if (cur.lim) unit[ua].reset (cur.W, cur.useTp, nullptr);
        latNow = osLat + cur.A;
        grNow.store (0); grMax.store (0);
    }

    // ── Réglages (appelé à chaque bloc) ───────────────────────────────────
    void setParams (const CompParams& np)
    {
        p = np;
        osTarget = std::clamp (p.os, 0, 2);
        updateCoefs();
        // cibles des rampes (rythme haut et rythme de base)
        const double s = 1.0 / compRatio (p.ratio) - 1.0;
        rH.thr.set (p.thr); rH.slope.set (s); rH.knee.set (p.knee); rH.mk.set (p.mk); rH.link.set (clamp01 (p.link));
        rH.autoW.set (p.autoRel ? 1.0 : 0.0);
        rB.thr.set (p.thr); rB.mk.set (p.mk); rB.link.set (clamp01 (p.link)); rB.autoW.set (p.autoRel ? 1.0 : 0.0);
    }

    // ── Traitement ────────────────────────────────────────────────────────
    // L, R : entrée principale (R = L en mono) ; sL, sR : side-chain externe (nullptr si le bus n'est pas branché).
    // oL, oR : signal traité ; dL, dR : signal sec recalé sur la même latence (pour le Dry/Wet et le bypass).
    void process (const float* L, const float* R, const float* sL, const float* sR,
                  float* oL, float* oR, float* dL, float* dR, int n)
    {
        for (int off = 0; off < n; off += chunk)
        {
            const int m = std::min (chunk, n - off);
            processChunk (L + off, R + off, sL != nullptr ? sL + off : nullptr, sR != nullptr ? sR + off : nullptr,
                          oL + off, oR + off, dL + off, dR + off, m);
        }
    }

    int latency() const          { return latNow; }   // latence à annoncer à l'hôte (échantillons)
    int lookaheadSamples() const { return std::min (maxLook, jsRound (p.look * fs)); }
    int maxLookahead() const     { return maxLook; }
    int oversamplingLatency (int stages) const { return stages <= 0 ? 0 : upM[std::min (stages, 2) - 1].latency(); }
    bool limiterActive() const   { return cur.lim; }
    bool truePeakActive() const  { return cur.lim && cur.useTp; }
    bool inTransition() const    { return phase != Phase::idle || osPhase != OsPhase::none; }
    int tpMinLook() const        { return jsRound (0.001 * fs); }   // anticipation minimale pour la crête vraie (1 ms)

    // Réduction de gain courante (dB, positive) et crête depuis la dernière lecture — pour l'affichage.
    float getGR() const          { return grNow.load(); }
    float takeGRMax()            { return grMax.exchange (0); }

    // Courbe statique (vue Transfert) — identique à compCurve() de l'outil web ; ratioKnob ≥ 20 : limiteur (plafond).
    static double curve (double di, double thr, double ratioKnob, double knee, double mk)
    {
        if (ratioKnob >= 20) return std::min (di, thr) + mk;
        const double r = ratioKnob, hk = knee / 2, ex = di - thr;
        double o;
        if (ex < -hk) o = di;
        else if (ex < hk) { const double t = (ex + hk) / knee; o = di + t * t * (1 / r - 1) * knee / 2; }
        else o = thr + (di - thr) / r;
        return o + mk;
    }
    // Ratio du chemin compresseur : ∞ = 100:1 (comme compParams() d'Audio Lab), il reste prêt pour le retour sous ∞
    static double compRatio (double knob) { return knob >= 20 ? 100 : knob; }

private:
    // ─────────────────────────────────────────────────────────────────────
    struct Config
    {
        int L = 0, A = 0, B = 0, W = 0;
        bool lim = false, useTp = false, ms = false;
        bool operator== (const Config& o) const { return L == o.L && A == o.A && B == o.B && lim == o.lim && useTp == o.useTp && ms == o.ms; }
        bool operator!= (const Config& o) const { return ! (*this == o); }
    };

    Config desired (const CompParams& q) const
    {
        Config c;
        c.L = std::min (maxLook, std::max (0, jsRound (q.look * fs)));
        c.A = constLat ? maxLook : c.L;
        c.B = c.A - c.L;
        c.lim = q.ratio >= 20;
        // crête vraie : le détecteur (×8) consomme 24 échantillons de l'anticipation ; il faut au moins 1 ms
        // d'anticipation (rampe de gain assez douce pour ne pas créer de crêtes entre les échantillons)
        c.useTp = c.lim && q.truePeak && c.L >= std::max (lim::TruePeak::delay + 1, tpMinLook());
        c.W = c.useTp ? c.L - lim::TruePeak::delay : c.L;
        c.ms = q.midSide;
        return c;
    }

    // ── Banque de détection du compresseur (Stereo ou Mid-Side) ──────────
    struct Bank
    {
        GainStage st[2];
        double accLink = 0, acc[2] { 0, 0 };
        bool ms = false;
        std::vector<double> gh[2];   // historique des gains (make-up compris) pour le gain retardé
    };

    // ── Unité limiteur (rythme de base) ──────────────────────────────────
    struct LimUnit
    {
        lim::TruePeak tp;
        bool useTp = false;
        int W = 0, L1 = 1, L2 = 1, mask = 0;
        std::vector<double> req[2], rel1[2], box1[2], gh[2], dqVal[2];
        std::vector<long> dqIdx[2];
        long dqH[2] { 0, 0 }, dqT[2] { 0, 0 };
        double s1[2] { 1, 1 }, s2[2] { 1, 1 };
        GainStage st[2];
        long n = 0;
        double minGain = 1;

        void allocate (int maxW)
        {
            int sz = 1; while (sz < maxW + 8) sz <<= 1;
            mask = sz - 1;
            for (int c = 0; c < 2; ++c)
            {
                req[c].assign ((size_t) sz, 1.0); rel1[c].assign ((size_t) sz, 1.0); box1[c].assign ((size_t) sz, 1.0);
                gh[c].assign ((size_t) sz, 1.0); dqVal[c].assign ((size_t) sz, 1.0); dqIdx[c].assign ((size_t) sz, 0);
            }
            tp.prepare (2);
        }
        // from : unité dont on reprend l'état de release (continuité), ou nullptr
        void reset (int w, bool tpOn, const LimUnit* from)
        {
            W = std::max (0, w); useTp = tpOn;
            L1 = std::max (1, (W + 1) / 2); L2 = std::max (1, W - L1 + 1);   // L1 + L2 − 2 ≤ W : plafond garanti
            for (int c = 0; c < 2; ++c)
            {
                std::fill (req[c].begin(), req[c].end(), 1.0); std::fill (rel1[c].begin(), rel1[c].end(), 1.0);
                std::fill (box1[c].begin(), box1[c].end(), 1.0); std::fill (gh[c].begin(), gh[c].end(), 1.0);
                dqH[c] = dqT[c] = 0; s1[c] = L1; s2[c] = L2;
                st[c] = from != nullptr ? from->st[c] : GainStage();
            }
            tp.reset(); n = 0; minGain = 1;
        }
        int warmLength (int B) const { return W + (useTp ? lim::TruePeak::delay : 0) + L1 + L2 + lim::TruePeak::taps + B + 16; }

        // det : signal de détection (L, R) ; thrLin : seuil linéaire ; mk : make-up linéaire
        inline void step (const double* det, double thrLin, double lk, double mk, double aR, double aS, int hold, double wA)
        {
            double own[2];
            const double cl = useTp ? thrLin * 0.99426694 : thrLin;   // −0,05 dB en crête vraie (marge d'interpolation)
            for (int c = 0; c < 2; ++c)
            {
                double pk;
                if (useTp) { double sp; pk = tp.push (c, det[c], sp); }
                else pk = std::abs (det[c]);
                own[c] = pk > cl ? cl / pk : 1.0;
            }
            const double lnk = std::min (own[0], own[1]);
            for (int c = 0; c < 2; ++c)
            {
                // stereo link : mélange des gains demandés en dB (jamais au-dessus du gain propre de la voie)
                double q = lnk;
                if (lk < 1 && lnk < 1) q = lk <= 0 ? own[c] : std::exp (lk * std::log (lnk) + (1 - lk) * std::log (own[c]));
                req[c][(size_t) (n & mask)] = q;
                // minimum glissant sur [n − W, n]
                auto& dv = dqVal[c]; auto& di = dqIdx[c];
                while (dqH[c] != dqT[c] && dv[(size_t) ((dqT[c] - 1) & mask)] >= q) --dqT[c];
                di[(size_t) (dqT[c] & mask)] = n; dv[(size_t) (dqT[c] & mask)] = q; ++dqT[c];
                while (dqH[c] != dqT[c] && di[(size_t) (dqH[c] & mask)] < n - W) ++dqH[c];
                const double mn = dv[(size_t) (dqH[c] & mask)];
                // retour (release) en dB, maintien, release automatique ; r ≤ mn toujours
                const double t = mn < 1 ? -20 * std::log10 (mn) : 0.0;
                const double G = st[c].lim (t, aR, aS, hold, wA);
                const double r = G > 0 ? std::min (mn, std::pow (10.0, -G / 20)) : mn;
                // deux moyennes glissantes (rampe en S de longueur W)
                s1[c] += r - rel1[c][(size_t) ((n - L1) & mask)]; rel1[c][(size_t) (n & mask)] = r;
                const double b = s1[c] / L1;
                s2[c] += b - box1[c][(size_t) ((n - L2) & mask)]; box1[c][(size_t) (n & mask)] = b;
                const double gain = std::min (1.0, s2[c] / L2);
                if (gain < minGain) minGain = gain;
                gh[c][(size_t) (n & mask)] = gain * mk;
            }
            ++n;
            if ((n & 0xFFFF) == 0) resync();   // resynchronise les sommes (dérive des additions)
        }
        inline double gainAt (int c, int back) const { return gh[c][(size_t) ((n - 1 - back) & mask)]; }
        void resync()
        {
            for (int c = 0; c < 2; ++c)
            {
                s1[c] = 0; for (int i = 0; i < L1; ++i) s1[c] += rel1[c][(size_t) ((n - 1 - i) & mask)];
                s2[c] = 0; for (int i = 0; i < L2; ++i) s2[c] += box1[c][(size_t) ((n - 1 - i) & mask)];
            }
        }
    };

    // ── Rampes de lissage ────────────────────────────────────────────────
    struct Ramps { Ramp thr, slope, knee, mk, link, autoW, scW; };

    void resetRamps()
    {
        const double s = 1.0 / compRatio (p.ratio) - 1.0;
        for (Ramps* r : { &rH, &rB })
        {
            r->thr.reset (p.thr); r->slope.reset (s); r->knee.reset (p.knee); r->mk.reset (p.mk);
            r->link.reset (clamp01 (p.link)); r->autoW.reset (p.autoRel ? 1.0 : 0.0); r->scW.reset (0.0);
        }
        mkLinH = mkLinB = std::pow (10.0, p.mk / 20.0);
        thrLinB = std::pow (10.0, p.thr / 20.0);
    }

    void updateCoefs()
    {
        const double fh = fs * factor;
        aA = std::exp (-1.0 / (std::max (0.00002, p.atk) * fh));
        aR = std::exp (-1.0 / (std::max (0.001, p.rel) * fh));
        aRfast = std::exp (-1.0 / (std::max (0.001, p.rel / 3) * fh));   // release auto : ÷ 3 après une crête brève
        aS = std::exp (-1.0 / (std::max (0.001, p.rel * 3) * fh));       // × 3 après une compression prolongée
        rmsCoef = std::exp (-1.0 / (std::max (0.0005, p.rmsWin) * fh));
        holdH = std::max (0, jsRound (p.hold * fh));
        // rythme de base (limiteur)
        aRB = std::exp (-1.0 / (std::max (0.001, p.rel) * fs));
        aRfastB = std::exp (-1.0 / (std::max (0.001, p.rel / 3) * fs));
        aSB = std::exp (-1.0 / (std::max (0.001, p.rel * 3) * fs));
        holdB = std::max (0, jsRound (p.hold * fs));
    }

    // Choisit le facteur de suréchantillonnage (hors lecture ou pendant le silence du fondu)
    void applyFactor (int stages, bool initial)
    {
        const int oldF = factor;
        osCur = stages;
        factor = 1 << stages;
        osLat = stages == 0 ? 0 : upM[stages - 1].latency();
        if (stages > 0) { upM[stages - 1].reset(); upS[stages - 1].reset(); }
        for (int c = 0; c < 2; ++c) { std::fill (xb[c].begin(), xb[c].end(), 0.0f); std::fill (db[c].begin(), db[c].end(), 0.0f); }
        for (auto& b : bank) for (int c = 0; c < 2; ++c)
        {
            std::fill (b.gh[c].begin(), b.gh[c].end(), 1.0);
            if (! initial && oldF > 0) b.st[c].hc = (int) ((long) b.st[c].hc * factor / oldF);
        }
        const int rl = std::max (1, jsRound (rampSec * fs * factor));
        for (Ramp* r : { &rH.thr, &rH.slope, &rH.knee, &rH.mk, &rH.link, &rH.autoW, &rH.scW }) r->setLength (rl, fs * factor);
        const int rb = std::max (1, jsRound (rampSec * fs));
        for (Ramp* r : { &rB.thr, &rB.slope, &rB.knee, &rB.mk, &rB.link, &rB.autoW, &rB.scW }) r->setLength (rb, fs);
        for (int c = 0; c < 2; ++c) { scH[c].clear(); scB[c].clear(); }
        setScFilters();
        updateCoefs();
    }

    void setScFilters()
    {
        for (int c = 0; c < 2; ++c)
        {
            scH[c].set (scTypeCur, scFCur, scQCur, fs * factor);
            scB[c].set (scTypeCur, scFCur, scQCur, fs);
        }
    }

    // Filtre du side-chain : la fréquence et le Q rejoignent leur cible en douceur (≈ 15 ms), une mise à jour par paquet
    void smoothScFilter (int n)
    {
        if (p.scType != scTypeCur)
        {
            scTypeCur = p.scType; scFCur = p.scF; scQCur = p.scQ;
            setScFilters();
            return;
        }
        if (same (scFCur, p.scF) && same (scQCur, p.scQ)) return;
        const double k = 1 - std::exp (-(double) n / (0.015 * fs));
        scFCur *= std::pow (p.scF / scFCur, k);
        scQCur += (p.scQ - scQCur) * k;
        if (std::abs (std::log (scFCur / p.scF)) < 1e-3) scFCur = p.scF;
        if (std::abs (scQCur - p.scQ) < 1e-3) scQCur = p.scQ;
        setScFilters();
    }

    // Copie l'état d'une banque dans l'autre (passage Stereo ↔ Mid-Side), avec conversion approchée des énergies
    void initBankFrom (Bank& dst, const Bank& src, bool ms)
    {
        dst.ms = ms;
        const double g = 0.5 * (src.st[0].g + src.st[1].g), gs = 0.5 * (src.st[0].gs + src.st[1].gs);
        const int hc = std::max (src.st[0].hc, src.st[1].hc);
        for (int c = 0; c < 2; ++c) { dst.st[c].g = g; dst.st[c].gs = gs; dst.st[c].hc = hc; }
        // (L² + R²) / 2 = M² + S² : l'énergie moyenne par voie est divisée par 2 en Mid-Side
        dst.accLink = src.ms == ms ? src.accLink : (ms ? src.accLink * 0.5 : src.accLink * 2);
        dst.acc[0] = dst.acc[1] = dst.accLink;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < dst.gh[c].size(); ++i) dst.gh[c][i] = 0.5 * (src.gh[0][i] + src.gh[1][i]);
    }

    // ── Gain du compresseur (formules d'Audio Lab) ───────────────────────
    static inline double gainComputer (double dbv, double thr, double s, double kn)
    {
        const double ex = dbv - thr, hk = kn / 2;
        double gr;
        if (ex <= -hk) gr = 0;
        else if (ex < hk && kn > 0) { const double t = ex + hk; gr = s * t * t / (2 * kn); }
        else gr = s * ex;
        return -gr;   // réduction positive en dB
    }
    static inline double toDb (double lvl) { return lvl > 1e-6 ? 20 * std::log10 (lvl) : -120; }

    // Une banque traite un échantillon (rythme haut) : écrit les gains dans son historique, renvoie la GR max
    inline double runBank (Bank& b, double dl, double dr, double thr, double s, double kn, double lk, double mk, double wA, int w)
    {
        double d0 = dl, d1 = dr;
        if (b.ms) { d0 = (dl + dr) * 0.5; d1 = (dl - dr) * 0.5; }
        double lvl;
        if (p.rms)
        {
            const double sq = (d0 * d0 + d1 * d1) / 2;
            b.accLink = rmsCoef * b.accLink + (1 - rmsCoef) * sq;
            b.acc[0] = rmsCoef * b.acc[0] + (1 - rmsCoef) * d0 * d0;
            b.acc[1] = rmsCoef * b.acc[1] + (1 - rmsCoef) * d1 * d1;
            lvl = std::sqrt (b.accLink);
        }
        else lvl = std::max (std::abs (d0), std::abs (d1));
        const double dbLink = toDb (lvl);
        const double aRel = p.autoRel ? aRfast : aR;
        if (lk >= 1)
        {
            const double G = b.st[0].comp (gainComputer (dbLink, thr, s, kn), aA, aRel, aS, holdH, wA);
            b.st[1] = b.st[0];
            const double lin = std::pow (10.0, -G / 20) * mk;
            b.gh[0][(size_t) w] = lin; b.gh[1][(size_t) w] = lin;
            return G;
        }
        // stereo link < 100 % : mélange des réductions visées (dB) liée et propre à chaque voie
        // (à 50 %, une voie silencieuse reçoit la moitié de la réduction de l'autre)
        const double grLink = gainComputer (dbLink, thr, s, kn);
        double gmax = 0;
        for (int c = 0; c < 2; ++c)
        {
            const double own = p.rms ? std::sqrt (b.acc[c]) : std::abs (c == 0 ? d0 : d1);
            const double grv = lk * grLink + (1 - lk) * gainComputer (toDb (own), thr, s, kn);
            const double G = b.st[c].comp (grv, aA, aRel, aS, holdH, wA);
            b.gh[c][(size_t) w] = std::pow (10.0, -G / 20) * mk;
            gmax = std::max (gmax, G);
        }
        return gmax;
    }

    inline void renderComp (const Bank& b, int A, int B, int w, double& yl, double& yr) const
    {
        const size_t ri = (size_t) ((w - A) & hMask), gi = (size_t) ((w - B) & hMask);
        const double xl = xb[0][ri], xr = xb[1][ri];
        if (b.ms)
        {
            const double m = (xl + xr) * 0.5 * b.gh[0][gi], sd = (xl - xr) * 0.5 * b.gh[1][gi];
            yl = m + sd; yr = m - sd;
        }
        else { yl = xl * b.gh[0][gi]; yr = xr * b.gh[1][gi]; }
    }

    // ── Paquet de m ≤ 256 échantillons ───────────────────────────────────
    void processChunk (const float* L, const float* R, const float* sL, const float* sR,
                       float* oL, float* oR, float* dL, float* dR, int m)
    {
        const bool scOn = sL != nullptr && sR != nullptr;
        const double scTarget = (p.scExternal && scOn) ? 1.0 : 0.0;
        rH.scW.set (scTarget); rB.scW.set (scTarget);
        smoothScFilter (m);
        advanceStateMachine();

        const bool xf = phase == Phase::xfade;
        const bool dualBank = phase != Phase::idle && bn != ba;   // la nouvelle banque tourne dès le début de la transition
        const bool newBank = xf && bn != ba;
        const Config& C = cur;
        const Config& N = xf ? nxt : cur;
        // unités du limiteur : courante (si ∞) et suivante (nouvelle, ou la même si seule la lecture change)
        LimUnit* uC = C.lim ? &unit[ua] : nullptr;
        LimUnit* uN = nullptr;
        if (phase != Phase::idle && nxt.lim) uN = needUnit ? &unit[un] : uC;

        // ── 1. chemin compresseur (rythme haut) ──
        const int F = factor, mh = m * F;
        float* hi[2]; const float* hs[2] = { nullptr, nullptr };
        if (osCur > 0)
        {
            const float* in[2] = { L, R };
            auto* up = upM[osCur - 1].upsample (in, m);
            hi[0] = up[0]; hi[1] = up[1];
            if (scOn) { const float* si[2] = { sL, sR }; auto* us = upS[osCur - 1].upsample (si, m); hs[0] = us[0]; hs[1] = us[1]; }
        }
        else
        {
            std::copy (L, L + m, wet1[0].data()); std::copy (R, R + m, wet1[1].data());
            hi[0] = wet1[0].data(); hi[1] = wet1[1].data();
            if (scOn) { hs[0] = sL; hs[1] = sR; }
        }
        const double xfDen = (double) xfLen * F;
        double grComp = 0;
        for (int j = 0; j < mh; ++j)
        {
            const double xl = hi[0][j], xr = hi[1][j];
            // détection : interne, externe ou fondu des deux
            const double ws = rH.scW.next();
            double dl = xl, dr = xr;
            if (ws > 0 && hs[0] != nullptr)
            {
                if (ws >= 1) { dl = hs[0][j]; dr = hs[1][j]; }
                else { dl = xl + ws * (hs[0][j] - xl); dr = xr + ws * (hs[1][j] - xr); }
            }
            if (scTypeCur != Biquad::Off) { dl = scH[0].run (dl); dr = scH[1].run (dr); }
            const int w = hw;
            xb[0][(size_t) w] = hi[0][j]; xb[1][(size_t) w] = hi[1][j];
            db[0][(size_t) w] = (float) dl; db[1][(size_t) w] = (float) dr;

            const double thr = rH.thr.next(), s = rH.slope.next(), kn = rH.knee.next(), lk = rH.link.next(), wA = rH.autoW.next();
            if (rH.mk.active()) mkLinH = std::pow (10.0, rH.mk.next() / 20.0);   // fin de rampe : exactement la cible
            const double mk = mkLinH;

            const double G = runBank (bank[ba], dl, dr, thr, s, kn, lk, mk, wA, w);
            if (dualBank) runBank (bank[bn], dl, dr, thr, s, kn, lk, mk, wA, w);
            if (G > grComp) grComp = G;

            double yl, yr;
            if (p.scListen)
            {
                yl = db[0][(size_t) ((w - C.A * F) & hMask)]; yr = db[1][(size_t) ((w - C.A * F) & hMask)];
                if (xf && N.A != C.A)
                {
                    const double t = smooth ((xfPos * F + j + 1) / xfDen);
                    yl += t * (db[0][(size_t) ((w - N.A * F) & hMask)] - yl); yr += t * (db[1][(size_t) ((w - N.A * F) & hMask)] - yr);
                }
            }
            else
            {
                renderComp (bank[ba], C.A * F, C.B * F, w, yl, yr);
                if (xf && (newBank || N.A != C.A || N.B != C.B))
                {
                    double zl, zr;
                    renderComp (bank[newBank ? bn : ba], N.A * F, N.B * F, w, zl, zr);
                    const double t = smooth ((xfPos * F + j + 1) / xfDen);
                    yl = (1 - t) * yl + t * zl; yr = (1 - t) * yr + t * zr;
                }
            }
            hi[0][j] = (float) yl; hi[1][j] = (float) yr;
            hw = (w + 1) & hMask;
        }
        if (osCur > 0) { float* out[2] = { comp[0].data(), comp[1].data() }; upM[osCur - 1].downsample (out, m); }
        else { std::copy (hi[0], hi[0] + m, comp[0].data()); std::copy (hi[1], hi[1] + m, comp[1].data()); }

        // ── 2. chemin de base : sec recalé, limiteur, mélange ──
        if (uC != nullptr) uC->minGain = 1;
        if (uN != nullptr) uN->minGain = 1;
        double gO = 1;
        for (int i = 0; i < m; ++i)
        {
            const int w = bw;
            mainR[0][(size_t) w] = L[i]; mainR[1][(size_t) w] = R[i];
            scR[0][(size_t) w] = scOn ? sL[i] : L[i]; scR[1][(size_t) w] = scOn ? sR[i] : R[i];
            const double lk = rB.link.next(), wA = rB.autoW.next(), ws = rB.scW.next();
            if (rB.mk.active()) mkLinB = std::pow (10.0, rB.mk.next() / 20.0);
            if (rB.thr.active()) thrLinB = std::pow (10.0, rB.thr.next() / 20.0);
            const double mk = mkLinB;

            // détection du limiteur (à la même latence de suréchantillonnage que le compresseur)
            const size_t di = (size_t) ((w - osLat) & bMask);
            double det[2] = { mainR[0][di], mainR[1][di] };
            if (ws > 0) for (int c = 0; c < 2; ++c) det[c] += ws * (scR[c][di] - det[c]);
            if (scTypeCur != Biquad::Off) for (int c = 0; c < 2; ++c) det[c] = scB[c].run (det[c]);

            const double aRl = p.autoRel ? aRfastB : aRB;
            if (uC != nullptr) uC->step (det, thrLinB, lk, mk, aRl, aSB, holdB, wA);
            if (uN != nullptr && uN != uC) uN->step (det, thrLinB, lk, mk, aRl, aSB, holdB, wA);

            // sortie de fondu du suréchantillonnage
            if (osPhase == OsPhase::fadeOut) gO = 1.0 - smooth ((double) (osPos + i + 1) / osFadeLen);
            else if (osPhase == OsPhase::mute) gO = 0;
            else if (osPhase == OsPhase::fadeIn) gO = smooth ((double) (osPos + i + 1) / osFadeLen);

            const double t = xf ? smooth ((double) (xfPos + i + 1) / xfLen) : 0.0;
            // sec
            const size_t rc = (size_t) ((w - osLat - C.A) & bMask);
            double sl = mainR[0][rc], sr = mainR[1][rc];
            if (xf && N.A != C.A)
            {
                const size_t rn = (size_t) ((w - osLat - N.A) & bMask);
                sl += t * (mainR[0][rn] - sl); sr += t * (mainR[1][rn] - sr);
            }
            // traité
            double yl, yr;
            if (p.scListen) { yl = comp[0][(size_t) i]; yr = comp[1][(size_t) i]; }
            else
            {
                auto wetOf = [&] (const Config& K, const LimUnit* u, double& a, double& b)
                {
                    if (! K.lim) { a = comp[0][(size_t) i]; b = comp[1][(size_t) i]; return; }
                    const size_t r = (size_t) ((w - osLat - K.A) & bMask);
                    a = mainR[0][r] * u->gainAt (0, K.B); b = mainR[1][r] * u->gainAt (1, K.B);
                };
                wetOf (C, uC, yl, yr);
                if (xf && (C.lim || N.lim))
                {
                    double zl, zr;
                    wetOf (N, uN, zl, zr);
                    yl = (1 - t) * yl + t * zl; yr = (1 - t) * yr + t * zr;
                }
            }
            oL[i] = (float) (yl * gO); oR[i] = (float) (yr * gO);
            dL[i] = (float) (sl * gO); dR[i] = (float) (sr * gO);
            bw = (w + 1) & bMask;
        }

        // ── 3. compteurs des transitions ──
        if (phase == Phase::warm) warmLeft -= m;
        else if (phase == Phase::xfade) xfPos += m;
        if (osPhase == OsPhase::fadeOut || osPhase == OsPhase::fadeIn) osPos += m;
        else if (osPhase == OsPhase::mute) osMute -= m;

        // ── 4. mesure ──
        double gr = grComp;
        if (uC != nullptr && ! p.scListen) gr = -20 * std::log10 (std::max (1e-9, (xf && uN != nullptr) ? uN->minGain : uC->minGain));
        grNow.store ((float) gr);
        float prev = grMax.load();
        while ((float) gr > prev && ! grMax.compare_exchange_weak (prev, (float) gr)) {}
    }

    // ── Transitions (au début de chaque paquet) ──────────────────────────
    void advanceStateMachine()
    {
        // suréchantillonnage : fondu de sortie, changement pendant le silence, remplissage, fondu d'entrée
        if (osPhase == OsPhase::fadeOut && osPos >= osFadeLen)
        {
            applyFactor (osTarget, false);
            if (cur.lim) unit[ua].reset (cur.W, cur.useTp, &unit[ua]);
            latNow = osLat + cur.A;
            osPhase = OsPhase::mute;
            osMute = cur.A + osLat + (cur.lim ? unit[ua].warmLength (cur.B) : 0) + 64;
        }
        if (osPhase == OsPhase::mute && osMute <= 0) { osPhase = OsPhase::fadeIn; osPos = 0; }
        if (osPhase == OsPhase::fadeIn && osPos >= osFadeLen) osPhase = OsPhase::none;
        if (osPhase != OsPhase::none) return;

        if (phase == Phase::warm && warmLeft <= 0) { phase = Phase::xfade; xfPos = 0; latNow = osLat + nxt.A; }
        if (phase == Phase::xfade && xfPos >= xfLen)
        {
            cur = nxt;
            if (needUnit) ua = un;
            if (bn != ba) ba = bn;
            phase = Phase::idle;
        }
        if (phase != Phase::idle) return;

        if (osTarget != osCur) { osPhase = OsPhase::fadeOut; osPos = 0; return; }
        const Config d = desired (p);
        if (d == cur) return;
        nxt = d;
        bn = ba;
        if (nxt.ms != cur.ms) { bn = 1 - ba; initBankFrom (bank[bn], bank[ba], nxt.ms); }
        needUnit = nxt.lim && (! cur.lim || nxt.L != cur.L || nxt.useTp != cur.useTp);
        if (needUnit)
        {
            un = cur.lim ? 1 - ua : ua;
            if (! cur.lim) unit[un].reset (nxt.W, nxt.useTp, nullptr);
            else unit[un].reset (nxt.W, nxt.useTp, &unit[ua]);
            phase = Phase::warm; warmLeft = unit[un].warmLength (nxt.B);
        }
        else { phase = Phase::xfade; xfPos = 0; latNow = osLat + nxt.A; }
    }

    static double clamp01 (double v) { return std::min (1.0, std::max (0.0, v)); }
    // courbe de fondu douce (pente nulle aux deux bouts : pas de coude audible) ; somme des poids = 1
    static double smooth (double u) { u = clamp01 (u); return u * u * (3 - 2 * u); }
    static int jsRound (double x) { return (int) std::floor (x + 0.5); } // Math.round()

    enum class Phase { idle, warm, xfade };
    enum class OsPhase { none, fadeOut, mute, fadeIn };

    double fs = 48000;
    bool constLat = false;
    CompParams p;
    int maxLook = 480, osLatMax = 76;
    int osCur = 0, osTarget = 0, factor = 1, osLat = 0;
    int latNow = 0;

    // coefficients
    double aA = 0, aR = 0, aRfast = 0, aS = 0, rmsCoef = 0;
    double aRB = 0, aRfastB = 0, aSB = 0;
    int holdH = 0, holdB = 0;
    Ramps rH, rB;
    double mkLinH = 1, mkLinB = 1, thrLinB = 1;

    // filtre du side-chain (rythme haut / rythme de base)
    Biquad scH[2], scB[2];
    int scTypeCur = Biquad::Off;
    double scFCur = 1000, scQCur = 0.7;

    // chemin compresseur
    os::Oversampler upM[2], upS[2];
    std::vector<float> xb[2], db[2], wet1[2], comp[2];
    int hMask = 0, hw = 0;
    Bank bank[2];
    int ba = 0, bn = 0;

    // chemin de base
    std::vector<float> mainR[2], scR[2];
    int bMask = 0, bw = 0;
    LimUnit unit[2];
    int ua = 0, un = 0;
    bool needUnit = false;

    // transitions
    Config cur, nxt;
    Phase phase = Phase::idle;
    int warmLeft = 0, xfPos = 0, xfLen = 960;
    OsPhase osPhase = OsPhase::none;
    int osPos = 0, osFadeLen = 240, osMute = 0;

    std::atomic<float> grNow { 0 }, grMax { 0 };
};
} // namespace audiolab
