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
    bool   smartRel   = false;   // 3.0.0, avec autoRel : maintien automatique calé sur la période du signal (grave sans distorsion)
    double rmsWin     = 0.003;   // s
    int    os         = 0;       // suréchantillonnage : 0 = Off, 1 = ×2, 2 = ×4, 3 = ×8
    // 3.0.0
    bool   osMin      = false;   // filtres du suréchantillonnage à phase minimale (latence nulle)
    double range      = 60;      // réduction de gain maximale (dB) ; 60 = sans limite
    bool   autoGain   = false;   // compensation automatique à volume égal (hors limiteur ∞)
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
    double g = 0, gs = 0, up = 0;
    int hc = 0;
    // Release « Smart » (3.0.0) : maintien automatique. On mesure l'intervalle entre deux débuts d'attaque successifs
    // (so : échantillons depuis le dernier début, ia : attaque en cours). S'il est court (moins de adapt, soit 25 ms),
    // le son est tenu et périodique : le maintien vaut 1,25 fois cette période, la réduction ne redescend plus entre
    // deux crêtes et le gain ne module plus la forme d'onde (distorsion du grave). Sur une frappe isolée l'intervalle
    // est long : aucun maintien, le release part aussitôt. adapt = 0 : calcul strictement inchangé.
    int so = 1 << 28, per = 0;
    bool ia = false;
    inline int holdFor (int hold, int adapt)
    {
        if (adapt <= 0) return hold;
        if (! ia) { per = so < adapt ? so : 0; so = 0; ia = true; }
        return std::max (hold, std::min (adapt, per + per / 4));
    }
    inline void tick (int adapt, bool attacking) { if (adapt > 0) { if (! attacking) ia = false; if (so < (1 << 28)) ++so; } }

    // Release automatique = deux détecteurs en parallèle (double RC) : le rapide (release ÷ 3) et un lent
    // (constante release × 3, en charge comme en décharge) qui ne se remplit que si la compression dure ;
    // on garde le plus fort des deux.
    // compresseur : attaque exponentielle (formule d'Audio Lab)
    inline double comp (double gr, double aA, double aR, double aS, int hold, double wAuto, int adapt = 0)
    {
        const bool at = gr > g;
        if (at) { g = aA * g + (1 - aA) * gr; hc = holdFor (hold, adapt); }
        else if (hc > 0) --hc;
        else g = aR * g + (1 - aR) * gr;
        tick (adapt, at);
        gs = aS * gs + (1 - aS) * gr;
        return wAuto > 0 ? g + wAuto * std::max (0.0, gs - g) : g;
    }
    // Version suréchantillonnée : même calcul, mais le passage attaque ↔ release est daté ENTRE deux échantillons
    // (la cible est supposée linéaire sur l'intervalle) et le pas est réduit à la fraction restante. Sans cela
    // l'instant de bascule est arrondi à l'échantillon, ce qui module le gain (repliement de la détection crête).
    inline double compAA (double gr, double aA, double aR, double aS, int hold, double wAuto, int adapt = 0)
    {
        const bool at = gr > g;
        // intégration par trapèzes : la cible vaut en moyenne (up + gr) / 2 sur l'intervalle
        if (gr > g)
        {
            if (up < g) g += (1 - aA) * 0.5 * (gr - g) * (gr - g) / (gr - up);   // croisement en montant : fraction (gr − g) / (gr − up)
            else        g += (1 - aA) * (0.5 * (up + gr) - g);
            hc = holdFor (hold, adapt);
        }
        else if (hc > 0) --hc;
        else if (up > g)
        {   // croisement en descendant : fin d'attaque sur la fraction t, release sur le reste
            const double t = (up - g) / (up - gr);
            g += (1 - aA) * t * 0.5 * (up - g) + (1 - aR) * (1 - t) * 0.5 * (gr - g);
        }
        else g += (1 - aR) * (0.5 * (up + gr) - g);
        tick (adapt, at);
        up = gr;
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

// Réduction de gain « anti-repliement » pour la détection crête suréchantillonnée (ADAA du premier ordre).
// La courbe du compresseur appliquée à |x| est une non-linéarité à coins (redresseur, seuil) : calculée point par
// point, ses harmoniques se replient et font battre le gain. On prend à la place sa MOYENNE exacte entre deux
// échantillons successifs, grâce à sa primitive G(x) = ∫ GR(v) dv, connue en forme fermée :
//   GR moyen = [A(x[n]) − A(x[n−1])] / (x[n] − x[n−1]),  A(x) = signe(x) · G(|x|).
// Mêmes seuil, pente, knee et Range que gainComputer ; le son ne change pas, seul le repliement baisse.
struct AdaaGR
{
    double thr = 0, sg = 0, kn = 0, rng = 60;         // sg = 1 − 1/ratio ≥ 0
    double x1 = 1, x2 = 1, xR = 1e300, G2 = 0, Q2 = 0, GR_ = 0, P1 = 0;
    static constexpr double c = 8.685889638065037;   // 20 / ln 10

    void set (double t, double slope, double k, double r)
    {
        if (same (t, thr) && same (-slope, sg) && same (k, kn) && same (r, rng)) return;
        thr = t; sg = -slope; kn = k; rng = r;
        const double hk = kn / 2;
        x1 = std::pow (10.0, (thr - hk) / 20); x2 = std::pow (10.0, (thr + hk) / 20);
        P1 = 2 * c * c * x1;
        G2 = kn > 0 ? sg / (2 * kn) * (P (x2) - P1) : 0.0;
        Q2 = Q (x2);
        xR = 1e300; GR_ = 0;
        if (rng < 60 && sg > 0)
        {
            double eR;                                 // dépassement (dB) où la réduction atteint le Range
            if (kn > 0 && rng < sg * hk) eR = std::sqrt (2 * kn * rng / sg) - hk; else eR = rng / sg;
            xR = std::pow (10.0, (thr + eR) / 20);
            GR_ = raw (xR);
        }
    }
    inline double P (double x) const { const double u = c * std::log (x) + kn / 2 - thr; return x * (u * u - 2 * c * u + 2 * c * c); }
    inline double Q (double x) const { return c * (x * std::log (x) - x) - thr * x; }
    inline double raw (double x) const
    {
        if (x <= x1) return 0;
        if (x < x2) return sg / (2 * kn) * (P (x) - P1);
        return G2 + sg * (Q (x) - Q2);
    }
    inline double A (double d) const
    {
        const double x = std::abs (d);
        const double g = x <= xR ? raw (x) : GR_ + rng * (x - xR);
        return d < 0 ? -g : g;
    }
};

class CompressorCore
{
public:
    static constexpr int chunk = 256;            // traitement interne par paquets de 256 échantillons
    static constexpr double xfadeSec = 0.020;    // fondu enchaîné (anticipation, ∞, Mid-Side)
    static constexpr double rampSec = 0.020;     // lissage des réglages
    static constexpr double lookMaxSec = 0.010;  // anticipation maximale (paramètre Lookahead)

    // ── Préparation (hors temps réel) ─────────────────────────────────────
    void prepare (double sampleRate, bool constantLatency, const CompParams& initial)
    {
        fs = sampleRate; constLat = constantLatency;
        maxLook = std::max (1, jsRound (lookMaxSec * fs));

        // anneaux du chemin compresseur, dimensionnés pour ×8
        int sz = 1; while (sz < (maxLook + tp2Delay) * 8 + 8) sz <<= 1;
        hMask = sz - 1;
        for (int c = 0; c < 2; ++c) { xb[c].assign ((size_t) sz, 0.0f); db[c].assign ((size_t) sz, 0.0f); }
        for (auto& b : bank) for (int c = 0; c < 2; ++c) b.gh[c].assign ((size_t) sz, 1.0);
        osLatMax = 0;
        for (int mp = 0; mp < 2; ++mp)
            for (int s = 0; s < 3; ++s)
            {
                upM[mp][s].prepare (s + 1, 2, chunk, mp == 1);
                upS[mp][s].prepare (s + 1, 2, chunk, mp == 1);
                osLatMax = std::max (osLatMax, upM[mp][s].latency());
            }
        for (int s = 0; s < 3; ++s) upD[s].prepare (s + 1, 2, chunk, true);
        for (int c = 0; c < 2; ++c) { wet1[c].assign ((size_t) chunk, 0.0f); comp[c].assign ((size_t) chunk, 0.0f); dryF[c].assign ((size_t) chunk, 0.0f); }

        // anneaux du chemin de base (sec, limiteur)
        int bs = 1; while (bs < maxLook + tp2Delay + osLatMax + 8) bs <<= 1;
        bMask = bs - 1;
        for (int c = 0; c < 2; ++c) { mainR[c].assign ((size_t) bs, 0.0f); scR[c].assign ((size_t) bs, 0.0f); mainF[c].assign ((size_t) bs, 0.0f); }
        for (auto& u : unit) u.allocate (maxLook);

        xfLen = std::max (1, jsRound (xfadeSec * fs));
        setKWeighting();
        restart (initial);
    }

    // Remet le moteur à l'état initial pour ces réglages, sans rien allouer (utilisable en temps réel :
    // c'est ainsi qu'un second moteur est préparé pour changer de suréchantillonnage sans coupure).
    void restart (const CompParams& initial)
    {
        p = initial;
        osCur = std::clamp (initial.os, 0, 3);
        osMinCur = initial.osMin && osCur > 0;

        // état initial = réglages demandés, sans transition
        cur = desired (p);
        nxt = cur;
        phase = Phase::idle;
        ba = bn = 0; ua = un = 0; needUnit = false;
        for (auto& b : bank)
        {
            for (int c = 0; c < 2; ++c) { b.st[c] = GainStage(); b.acc[c] = 0; std::fill (b.gh[c].begin(), b.gh[c].end(), 1.0); }
            b.accLink = 0; b.ms = cur.ms;
        }
        hw = 0; bw = 0;
        for (int c = 0; c < 2; ++c) { std::fill (xb[c].begin(), xb[c].end(), 0.0f); std::fill (db[c].begin(), db[c].end(), 0.0f);
                                      std::fill (mainR[c].begin(), mainR[c].end(), 0.0f); std::fill (scR[c].begin(), scR[c].end(), 0.0f);
                                      std::fill (mainF[c].begin(), mainF[c].end(), 0.0f); }
        scFCur = p.scF; scQCur = p.scQ; scTypeCur = p.scType;
        applyFactor (osCur, true);
        setParams (p);
        resetRamps();
        for (auto& b : bank) for (int c = 0; c < 2; ++c) { b.sm[c][0] = b.sm[c][1] = mkLinH; std::fill (b.gh[c].begin(), b.gh[c].end(), mkLinH); }
        if (cur.lim) unit[ua].reset (cur.W, cur.useTp, nullptr);
        latNow = osLat + cur.A;
        grNow.store (0); grMax.store (0);
        for (auto& k : kw) k.clear();
        msDry = msWet = 0; agDb = agTarget = 0; agCount = 0;
    }

    // Reprend l'état de la dynamique d'un autre moteur (réduction en cours, énergies RMS, auto-gain) :
    // le nouveau moteur démarre là où l'ancien en est, il ne « ré-attaque » pas.
    void adoptState (const CompressorCore& o)
    {
        const Bank& sb = o.bank[o.ba];
        for (auto& b : bank)
        {
            for (int c = 0; c < 2; ++c)
            {
                b.st[c].g = sb.st[c].g; b.st[c].gs = sb.st[c].gs;
                b.st[c].hc = (int) ((long) sb.st[c].hc * factor / std::max (1, o.factor));
                b.acc[c] = sb.acc[c];
                b.sm[c][0] = b.sm[c][1] = sb.sm[c][1] > 0 && o.factor > 1 ? sb.sm[c][1] : std::pow (10.0, -sb.st[c].g / 20) * mkLinH;
            }
            b.accLink = sb.accLink;
        }
        if (cur.lim && o.cur.lim) for (int c = 0; c < 2; ++c) unit[ua].st[c] = o.unit[o.ua].st[c];
        msDry = o.msDry; msWet = o.msWet; agDb = o.agDb; agTarget = o.agTarget;
        for (int i = 0; i < 8; ++i) kw[i] = o.kw[i];
    }
    int oversamplingStages() const { return osCur; }
    bool oversamplingMinPhase() const { return osMinCur; }
    float autoGainDb() const { return (float) agDb; }

    // ── Réglages (appelé à chaque bloc) ───────────────────────────────────
    void setParams (const CompParams& np)
    {
        p = np;
        updateCoefs();
        // cibles des rampes (rythme haut et rythme de base)
        const double s = 1.0 / compRatio (p.ratio) - 1.0;
        rH.thr.set (p.thr); rH.slope.set (s); rH.knee.set (p.knee); rH.mk.set (p.mk); rH.link.set (clamp01 (p.link));
        rH.autoW.set (p.autoRel ? 1.0 : 0.0);
        rH.range.set (std::clamp (p.range, 0.0, 60.0)); rH.detW.set (p.rms ? 1.0 : 0.0);
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
    int oversamplingLatency (int stages, bool minPhase = false) const { return stages <= 0 ? 0 : upM[minPhase ? 1 : 0][std::min (stages, 3) - 1].latency(); }
    bool limiterActive() const   { return cur.lim; }
    bool truePeakActive() const  { return cur.lim && cur.useTp; }
    bool inTransition() const    { return phase != Phase::idle; }
    // anticipation minimale pour la crête vraie : 24 échantillons de détecteur + 8 de rampe (0,67 ms à 48 kHz ; 1 ms en 2.0.0)
    int tpMinLook() const        { return lim::TruePeak::delay + 8; }
    static constexpr int tp2Win = 16, tp2Delay = lim::TruePeakHQ::delay + tp2Win;   // second étage : 64 échantillons

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
        int L = 0, A = 0, B = 0, W = 0, D = 0;   // D : retard du second étage crête vraie (0 ou tp2Delay = 64)
        bool lim = false, useTp = false, ms = false;
        bool operator== (const Config& o) const { return L == o.L && A == o.A && B == o.B && D == o.D && lim == o.lim && useTp == o.useTp && ms == o.ms; }
        bool operator!= (const Config& o) const { return ! (*this == o); }
    };

    Config desired (const CompParams& q) const
    {
        Config c;
        c.L = std::min (maxLook, std::max (0, jsRound (q.look * fs)));
        c.lim = q.ratio >= 20;
        // crête vraie : le détecteur (×8) consomme 24 échantillons de l'anticipation ; il faut au moins 1 ms
        // d'anticipation (rampe de gain assez douce pour ne pas créer de crêtes entre les échantillons)
        c.useTp = c.lim && q.truePeak && c.L >= tpMinLook();
        c.W = c.useTp ? c.L - lim::TruePeak::delay : c.L;
        // 3.0.0 : second étage crête vraie (mesure la SORTIE du premier, la retouche avec 64 échantillons d'avance)
        c.D = c.useTp ? tp2Delay : 0;
        c.A = constLat ? maxLook + tp2Delay : c.L + c.D;
        c.B = c.A - c.L;
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
        double dp[2] { 0, 0 }, Ap[2] { 0, 0 };     // ADAA : échantillon de détection précédent et sa primitive
        double sm[2][2] { { 1, 1 }, { 1, 1 } };   // suréchantillonné : gain adouci (deux pôles) contre le repliement
    };

    // ── Unité limiteur (rythme de base) ──────────────────────────────────
    struct LimUnit
    {
        lim::TruePeak tp; lim::TruePeakHQ tp2;
        bool useTp = false;
        // second étage (crête vraie de la sortie) et historique de la sortie finale
        std::vector<double> y1[2], oh[2];
        double rq2[64] {}, b1[64] {}, r1[64] {}, dq2v[64] {}; long dq2i[64] {}; long dq2H = 0, dq2T = 0;   // fenêtres ≤ 17 : anneaux de 64
        double t1 = 0, t2 = 0;
        int omask = 0;
        // plafond du second étage = plus haut seuil des W + 26 derniers instants (un seuil qui baisse ne doit pas
        // faire retoucher brutalement un son déjà limité à l'ancien plafond)
        std::vector<double> tqV; std::vector<long> tqI; long tqH = 0, tqT = 0;
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
            tp.prepare (2); tp2.prepare (2);
            int os2 = 1; while (os2 < maxW + tp2Delay + 8) os2 <<= 1;
            omask = os2 - 1;
            for (int c = 0; c < 2; ++c) { y1[c].assign (128, 0.0); oh[c].assign ((size_t) os2, 0.0); }
            tqV.assign ((size_t) os2, 0.0); tqI.assign ((size_t) os2, 0);
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
            tp.reset(); tp2.reset(); n = 0; minGain = 1;
            for (int c = 0; c < 2; ++c) { std::fill (y1[c].begin(), y1[c].end(), 0.0); std::fill (oh[c].begin(), oh[c].end(), 0.0); }
            for (int i = 0; i < 64; ++i) { rq2[i] = b1[i] = r1[i] = dq2v[i] = 1.0; dq2i[i] = 0; }
            dq2H = dq2T = 0; t1 = k1; t2 = k2; tqH = tqT = 0;
        }
        int warmLength (int B) const { return W + (useTp ? lim::TruePeak::delay + tp2Delay + lim::TruePeakHQ::taps : 0) + L1 + L2 + lim::TruePeak::taps + B + 16; }
        static constexpr int k1 = tp2Win / 2, k2 = tp2Win - k1 + 1;   // deux moyennes : k1 + k2 − 2 ≤ tp2Win

        // det : signal de détection (L, R) ; thrLin : seuil linéaire ; mk : make-up linéaire
        // xa : son auquel s'applique le gain de cet instant (entrée retardée de l'anticipation)
        inline void step (const double* det, const double* xa, double thrLin, double lk, double mk, double aR, double aS, int hold, double wA)
        {
            double own[2], g1[2];
            const double cl = useTp ? thrLin * 0.98855309 : thrLin;   // −0,10 dB en crête vraie (marge d'interpolation ; −0,05 en 2.0.0)
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
                g1[c] = gain;
            }
            if (! useTp)
                for (int c = 0; c < 2; ++c) oh[c][(size_t) (n & omask)] = xa[c] * g1[c] * mk;
            else
            {
                // second étage : crête vraie de la sortie du premier (instant n − 24), gain de retouche commun aux deux voies,
                // minimum glissant sur 16 échantillons puis deux moyennes ; appliqué à la sortie retardée de 64 échantillons.
                // Le premier étage a déjà fait l'essentiel : la retouche ne dépasse pas quelques centièmes de dB.
                while (tqH != tqT && tqV[(size_t) ((tqT - 1) & omask)] <= thrLin) --tqT;
                tqI[(size_t) (tqT & omask)] = n; tqV[(size_t) (tqT & omask)] = thrLin; ++tqT;
                while (tqH != tqT && tqI[(size_t) (tqH & omask)] < n - (W + 26)) ++tqH;
                const double cl2 = tqV[(size_t) (tqH & omask)] * 0.99083194;   // −0,08 dB : marge entre deux interpolateurs sur un bruit plein spectre
                double q = 1;
                for (int c = 0; c < 2; ++c)
                {
                    const double y = xa[c] * g1[c];
                    y1[c][(size_t) (n & 127)] = y;
                    double sp; const double pk = tp2.pushGated (c, y, sp, cl2);
                    if (pk > cl2) q = std::min (q, cl2 / pk);
                }
                while (dq2H != dq2T && dq2v[(dq2T - 1) & 63] >= q) --dq2T;
                dq2i[dq2T & 63] = n; dq2v[dq2T & 63] = q; ++dq2T;
                while (dq2H != dq2T && dq2i[dq2H & 63] < n - tp2Win) ++dq2H;
                const double mn2 = dq2v[dq2H & 63];
                t1 += mn2 - r1[(n - k1) & 63]; r1[n & 63] = mn2;
                const double bb = t1 / k1;
                t2 += bb - b1[(n - k2) & 63]; b1[n & 63] = bb;
                const double gain2 = std::min (1.0, t2 / k2);
                for (int c = 0; c < 2; ++c) oh[c][(size_t) (n & omask)] = y1[c][(size_t) ((n - tp2Delay) & 127)] * gain2 * mk;
                if ((n & 0x3FF) == 0)
                {   // resynchronise les sommes
                    t1 = 0; for (int i = 0; i < k1; ++i) t1 += r1[(n - i) & 63];
                    t2 = 0; for (int i = 0; i < k2; ++i) t2 += b1[(n - i) & 63];
                }
            }
            ++n;
            if ((n & 0xFFFF) == 0) resync();   // resynchronise les sommes (dérive des additions)
        }
        inline double gainAt (int c, int back) const { return gh[c][(size_t) ((n - 1 - back) & mask)]; }
        inline double outAt (int c, int back) const { return oh[c][(size_t) ((n - 1 - back) & omask)]; }
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
    struct Ramps { Ramp thr, slope, knee, mk, link, autoW, scW, range, detW; };

    void resetRamps()
    {
        const double s = 1.0 / compRatio (p.ratio) - 1.0;
        for (Ramps* r : { &rH, &rB })
        {
            r->thr.reset (p.thr); r->slope.reset (s); r->knee.reset (p.knee); r->mk.reset (p.mk);
            r->link.reset (clamp01 (p.link)); r->autoW.reset (p.autoRel ? 1.0 : 0.0); r->scW.reset (0.0);
            r->range.reset (std::clamp (p.range, 0.0, 60.0)); r->detW.reset (p.rms ? 1.0 : 0.0);
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
        adaptH = (p.autoRel && p.smartRel) ? jsRound (0.025 * fh) : 0;
        // suréchantillonné : le gain passe par deux pôles à gSmHz (inaudible, retire l'extrême aigu du gain qui se replierait)
        gSm = (factor > 1 && gSmHz > 0) ? 1 - std::exp (-2 * kPi * gSmHz / fh) : 0.0;
        // rythme de base (limiteur)
        aRB = std::exp (-1.0 / (std::max (0.001, p.rel) * fs));
        aRfastB = std::exp (-1.0 / (std::max (0.001, p.rel / 3) * fs));
        aSB = std::exp (-1.0 / (std::max (0.001, p.rel * 3) * fs));
        holdB = std::max (0, jsRound (p.hold * fs));
    }

    // Fixe le facteur de suréchantillonnage (à la préparation du moteur uniquement)
    void applyFactor (int stages, bool)
    {
        osCur = stages;
        factor = 1 << stages;
        osLat = stages == 0 ? 0 : M().latency();
        if (stages > 0) { M().reset(); S().reset(); upD[stages - 1].reset(); }
        for (int c = 0; c < 2; ++c) { std::fill (xb[c].begin(), xb[c].end(), 0.0f); std::fill (db[c].begin(), db[c].end(), 0.0f); }
        for (auto& b : bank) for (int c = 0; c < 2; ++c) std::fill (b.gh[c].begin(), b.gh[c].end(), 1.0);
        const int rl = std::max (1, jsRound (rampSec * fs * factor));
        for (Ramp* r : { &rH.thr, &rH.slope, &rH.knee, &rH.mk, &rH.link, &rH.autoW, &rH.scW, &rH.range, &rH.detW }) r->setLength (rl, fs * factor);
        const int rb = std::max (1, jsRound (rampSec * fs));
        for (Ramp* r : { &rB.thr, &rB.slope, &rB.knee, &rB.mk, &rB.link, &rB.autoW, &rB.scW, &rB.range, &rB.detW }) r->setLength (rb, fs);
        for (int c = 0; c < 2; ++c) { scH[c].clear(); scB[c].clear(); }
        setScFilters();
        updateCoefs();
    }
    os::Oversampler& M() { return upM[osMinCur ? 1 : 0][osCur - 1]; }
    os::Oversampler& S() { return upS[osMinCur ? 1 : 0][osCur - 1]; }

    // Pondération K (ITU-R BS.1770) pour l'auto-gain : étagère aiguë puis passe-haut, recalculés pour fs
    void setKWeighting()
    {
        double b0, b1, b2, a1, a2;
        { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
          const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
          b0 = (Vh + Vb * K / Q + K * K) / a0; b1 = 2 * (K * K - Vh) / a0; b2 = (Vh - Vb * K / Q + K * K) / a0; a1 = 2 * (K * K - 1) / a0; a2 = (1 - K / Q + K * K) / a0;
          for (int i = 0; i < 4; ++i) { kw[i].b0 = b0; kw[i].b1 = b1; kw[i].b2 = b2; kw[i].a1 = a1; kw[i].a2 = a2; } }
        { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / fs), a0 = 1 + K / Q + K * K;
          for (int i = 4; i < 8; ++i) { kw[i].b0 = 1; kw[i].b1 = -2; kw[i].b2 = 1; kw[i].a1 = 2 * (K * K - 1) / a0; kw[i].a2 = (1 - K / Q + K * K) / a0; } }
        agMs = std::exp (-1.0 / (1.5 * fs));      // intégration du volume : 1,5 s
        agSm = std::exp (-1.0 / (0.25 * fs));     // lissage du gain : 250 ms
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
        for (int c = 0; c < 2; ++c) dst.sm[c][0] = dst.sm[c][1] = 0.5 * (src.sm[0][1] + src.sm[1][1]);
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
    inline double runBank (Bank& b, double dl, double dr, double thr, double s, double kn, double lk, double mk, double wA, int w, double rng, double wR)
    {
        // Range : réduction visée plafonnée (60 = sans limite, calcul inchangé)
        auto gc = [rng] (double d, double t, double sl, double k) { const double g = gainComputer (d, t, sl, k); return (rng < 60 && g > rng) ? rng : g; };
        double d0 = dl, d1 = dr;
        if (b.ms) { d0 = (dl + dr) * 0.5; d1 = (dl - dr) * 0.5; }
        // Peak ↔ RMS : wR passe de 0 à 1 en 20 ms (fondu des réductions visées), 0 ou 1 exactement en régime établi
        const bool useR = wR > 0, useP = wR < 1;
        double lvlR = 0, lvlP = 0;
        if (useR)
        {
            const double sq = (d0 * d0 + d1 * d1) / 2;
            b.accLink = rmsCoef * b.accLink + (1 - rmsCoef) * sq;
            b.acc[0] = rmsCoef * b.acc[0] + (1 - rmsCoef) * d0 * d0;
            b.acc[1] = rmsCoef * b.acc[1] + (1 - rmsCoef) * d1 * d1;
            lvlR = std::sqrt (b.accLink);
        }
        if (useP) lvlP = std::max (std::abs (d0), std::abs (d1));
        const double aRel = p.autoRel ? aRfast : aR;
        // détection crête suréchantillonnée : réduction moyenne exacte entre deux échantillons (anti-repliement)
        const bool aa = factor > 1 && useP;
        double grA[2] = { 0, 0 };
        if (aa)
        {
            adaa.set (thr, s, kn, rng);
            const bool keep = ! adaaDirty;
            const double d[2] = { d0, d1 };
            for (int c = 0; c < 2; ++c)
            {
                const double An = adaa.A (d[c]), Ao = keep ? b.Ap[c] : adaa.A (b.dp[c]), dx = d[c] - b.dp[c];
                grA[c] = std::abs (dx) > 1e-7 ? (An - Ao) / dx : gc (toDb (std::abs (0.5 * (d[c] + b.dp[c]))), thr, s, kn);
                if (grA[c] < 0) grA[c] = 0;
                b.dp[c] = d[c]; b.Ap[c] = An;
            }
        }
        const double grP = ! useP ? 0.0 : aa ? std::max (grA[0], grA[1]) : gc (toDb (lvlP), thr, s, kn);
        const double grLink = ! useR ? grP : useP ? grP + wR * (gc (toDb (lvlR), thr, s, kn) - grP) : gc (toDb (lvlR), thr, s, kn);
        if (lk >= 1)
        {
            const double G = aa ? b.st[0].compAA (grLink, aA, aRel, aS, holdH, wA, adaptH) : b.st[0].comp (grLink, aA, aRel, aS, holdH, wA, adaptH);
            b.st[1] = b.st[0];
            double lin = std::pow (10.0, -G / 20) * mk;
            if (gSm > 0) { b.sm[0][0] += gSm * (lin - b.sm[0][0]); b.sm[0][1] += gSm * (b.sm[0][0] - b.sm[0][1]); lin = b.sm[0][1]; b.sm[1][0] = b.sm[0][0]; b.sm[1][1] = lin; }
            b.gh[0][(size_t) w] = lin; b.gh[1][(size_t) w] = lin;
            return G;
        }
        // stereo link < 100 % : mélange des réductions visées (dB) liée et propre à chaque voie
        // (à 50 %, une voie silencieuse reçoit la moitié de la réduction de l'autre)
        double gmax = 0;
        for (int c = 0; c < 2; ++c)
        {
            const double oP = ! useP ? 0.0 : aa ? grA[c] : gc (toDb (std::abs (c == 0 ? d0 : d1)), thr, s, kn);
            const double oR = useR ? gc (toDb (std::sqrt (b.acc[c])), thr, s, kn) : 0.0;
            const double grv = lk * grLink + (1 - lk) * (! useR ? oP : useP ? oP + wR * (oR - oP) : oR);
            const double G = aa ? b.st[c].compAA (grv, aA, aRel, aS, holdH, wA, adaptH) : b.st[c].comp (grv, aA, aRel, aS, holdH, wA, adaptH);
            double lin = std::pow (10.0, -G / 20) * mk;
            if (gSm > 0) { b.sm[c][0] += gSm * (lin - b.sm[c][0]); b.sm[c][1] += gSm * (b.sm[c][0] - b.sm[c][1]); lin = b.sm[c][1]; }
            b.gh[c][(size_t) w] = lin;
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
            auto* up = M().upsample (in, m);
            hi[0] = up[0]; hi[1] = up[1];
            if (scOn) { const float* si[2] = { sL, sR }; auto* us = S().upsample (si, m); hs[0] = us[0]; hs[1] = us[1]; }
            if (osMinCur)
            {   // phase minimale : le sec passe par les mêmes filtres (aucun peigne au Dry/Wet)
                upD[osCur - 1].upsample (in, m);
                float* dout[2] = { dryF[0].data(), dryF[1].data() };
                upD[osCur - 1].downsample (dout, m);
            }
        }
        else
        {
            std::copy (L, L + m, wet1[0].data()); std::copy (R, R + m, wet1[1].data());
            hi[0] = wet1[0].data(); hi[1] = wet1[1].data();
            if (scOn) { hs[0] = sL; hs[1] = sR; }
        }
        const double xfDen = (double) xfLen * F;
        // Phase linéaire : on ne suréchantillonne que la DIFFÉRENCE (traité − sec) ; le sec est rajouté au rythme
        // de base. Quand rien ne compresse la différence est nulle : sortie identique à l'entrée, au bit près.
        const bool delta = osCur > 0 && ! osMinCur && ! p.scListen;
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

            const double thr = rH.thr.next(), s = rH.slope.next(), kn = rH.knee.next(), lk = rH.link.next(), wA = rH.autoW.next(), rng = rH.range.next(), wR = rH.detW.next();
            if (rH.mk.active()) mkLinH = std::pow (10.0, rH.mk.next() / 20.0);   // fin de rampe : exactement la cible
            const double mk = mkLinH;

            adaaDirty = ! (same (thr, adaa.thr) && same (-s, adaa.sg) && same (kn, adaa.kn) && same (rng, adaa.rng));
            const double G = runBank (bank[ba], dl, dr, thr, s, kn, lk, mk, wA, w, rng, wR);
            if (dualBank) runBank (bank[bn], dl, dr, thr, s, kn, lk, mk, wA, w, rng, wR);
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
                if (delta) { const size_t r = (size_t) ((w - C.A * F) & hMask); yl -= xb[0][r]; yr -= xb[1][r]; }
                if (xf && (newBank || N.A != C.A || N.B != C.B))
                {
                    double zl, zr;
                    renderComp (bank[newBank ? bn : ba], N.A * F, N.B * F, w, zl, zr);
                    if (delta) { const size_t r = (size_t) ((w - N.A * F) & hMask); zl -= xb[0][r]; zr -= xb[1][r]; }
                    const double t = smooth ((xfPos * F + j + 1) / xfDen);
                    yl = (1 - t) * yl + t * zl; yr = (1 - t) * yr + t * zr;
                }
            }
            hi[0][j] = (float) yl; hi[1][j] = (float) yr;
            hw = (w + 1) & hMask;
        }
        if (osCur > 0) { float* out[2] = { comp[0].data(), comp[1].data() }; M().downsample (out, m); }
        else { std::copy (hi[0], hi[0] + m, comp[0].data()); std::copy (hi[1], hi[1] + m, comp[1].data()); }

        // ── 2. chemin de base : sec recalé, limiteur, mélange ──
        if (uC != nullptr) uC->minGain = 1;
        if (uN != nullptr) uN->minGain = 1;
        const bool agOn = p.autoGain || agDb != 0.0;
        for (int i = 0; i < m; ++i)
        {
            const int w = bw;
            mainR[0][(size_t) w] = L[i]; mainR[1][(size_t) w] = R[i];
            if (osMinCur) { mainF[0][(size_t) w] = dryF[0][(size_t) i]; mainF[1][(size_t) w] = dryF[1][(size_t) i]; }
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
            if (uC != nullptr)
            {
                const size_t r = (size_t) ((w - osLat - C.L) & bMask); const double xa[2] = { mainR[0][r], mainR[1][r] };
                uC->step (det, xa, thrLinB, lk, mk, aRl, aSB, holdB, wA);
            }
            if (uN != nullptr && uN != uC)
            {
                const size_t r = (size_t) ((w - osLat - nxt.L) & bMask); const double xa[2] = { mainR[0][r], mainR[1][r] };
                uN->step (det, xa, thrLinB, lk, mk, aRl, aSB, holdB, wA);
            }

            const double t = xf ? smooth ((double) (xfPos + i + 1) / xfLen) : 0.0;
            // sec (en phase minimale, hors limiteur : le sec filtré comme le traité)
            auto dryOf = [&] (const Config& K, double& a, double& b)
            {
                const size_t r = (size_t) ((w - osLat - K.A) & bMask);
                if (osMinCur && ! K.lim) { a = mainF[0][r]; b = mainF[1][r]; } else { a = mainR[0][r]; b = mainR[1][r]; }
            };
            double sl, sr;
            dryOf (C, sl, sr);
            if (xf && (N.A != C.A || (osMinCur && N.lim != C.lim)))
            {
                double nl, nr; dryOf (N, nl, nr);
                sl += t * (nl - sl); sr += t * (nr - sr);
            }
            // traité
            double yl, yr;
            if (p.scListen) { yl = comp[0][(size_t) i]; yr = comp[1][(size_t) i]; }
            else
            {
                auto wetOf = [&] (const Config& K, const LimUnit* u, double& a, double& b)
                {
                    if (! K.lim) { a = comp[0][(size_t) i]; b = comp[1][(size_t) i]; if (delta) { a += sl; b += sr; } return; }
                    a = u->outAt (0, K.B - K.D); b = u->outAt (1, K.B - K.D);
                };
                wetOf (C, uC, yl, yr);
                if (xf && (C.lim || N.lim))
                {
                    double zl, zr;
                    wetOf (N, uN, zl, zr);
                    yl = (1 - t) * yl + t * zl; yr = (1 - t) * yr + t * zr;
                }
            }
            // auto-gain : compare le volume (pondération K, 1,5 s) du sec et du traité sans make-up, hors limiteur
            if (agOn && ! p.scListen)
            {
                const bool limNow = C.lim || (xf && N.lim);
                const double wl = yl / mk, wr = yr / mk;
                const double dkl = kw[4].run (kw[0].run (sl)), dkr = kw[5].run (kw[1].run (sr));
                const double wkl = kw[6].run (kw[2].run (wl)), wkr = kw[7].run (kw[3].run (wr));
                msDry = agMs * msDry + (1 - agMs) * (dkl * dkl + dkr * dkr);
                msWet = agMs * msWet + (1 - agMs) * (wkl * wkl + wkr * wkr);
                if ((agCount++ & 63) == 0)
                {
                    if (! p.autoGain || limNow) agTarget = 0;
                    else if (msDry > 1e-8 && msWet > 1e-12) agTarget = std::clamp (10 * std::log10 (msDry / msWet), -6.0, 36.0);
                }
                agDb = agTarget + agSm * (agDb - agTarget);
                if (! p.autoGain && std::abs (agDb) < 1e-4) agDb = 0;
                const double ag = std::exp (agDb * 0.11512925464970229);
                yl *= ag; yr *= ag;
            }
            oL[i] = (float) yl; oR[i] = (float) yr;
            dL[i] = (float) sl; dR[i] = (float) sr;
            bw = (w + 1) & bMask;
        }

        // ── 3. compteurs des transitions ──
        if (phase == Phase::warm) warmLeft -= m;
        else if (phase == Phase::xfade) xfPos += m;

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
        if (phase == Phase::warm && warmLeft <= 0) { phase = Phase::xfade; xfPos = 0; latNow = osLat + nxt.A; }
        if (phase == Phase::xfade && xfPos >= xfLen)
        {
            cur = nxt;
            if (needUnit) ua = un;
            if (bn != ba) ba = bn;
            phase = Phase::idle;
        }
        if (phase != Phase::idle) return;

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

    double fs = 48000;
    bool constLat = false;
    CompParams p;
    int maxLook = 480, osLatMax = 76;
    int osCur = 0, factor = 1, osLat = 0;
    bool osMinCur = false;
    int latNow = 0;

    // coefficients
    double aA = 0, aR = 0, aRfast = 0, aS = 0, rmsCoef = 0, gSm = 0;
    AdaaGR adaa;
    int adaptH = 0;
    bool adaaDirty = false;
public:
    double gSmHz = 0;   // adoucissement du gain suréchantillonné : essayé, sans effet mesurable sur le repliement, laissé à 0
private:
    double aRB = 0, aRfastB = 0, aSB = 0;
    int holdH = 0, holdB = 0;
    Ramps rH, rB;
    double mkLinH = 1, mkLinB = 1, thrLinB = 1;

    // filtre du side-chain (rythme haut / rythme de base)
    Biquad scH[2], scB[2];
    int scTypeCur = Biquad::Off;
    double scFCur = 1000, scQCur = 0.7;

    // chemin compresseur
    os::Oversampler upM[2][3], upS[2][3], upD[3];   // [phase linéaire / minimale][×2, ×4, ×8] ; upD : sec en phase minimale
    std::vector<float> xb[2], db[2], wet1[2], comp[2], dryF[2];
    int hMask = 0, hw = 0;
    Bank bank[2];
    int ba = 0, bn = 0;

    // chemin de base
    std::vector<float> mainR[2], scR[2], mainF[2];
    int bMask = 0, bw = 0;
    LimUnit unit[2];
    int ua = 0, un = 0;
    bool needUnit = false;

    // transitions
    Config cur, nxt;
    Phase phase = Phase::idle;
    int warmLeft = 0, xfPos = 0, xfLen = 960;
    // auto-gain
    Biquad kw[8];
    double msDry = 0, msWet = 0, agDb = 0, agTarget = 0, agMs = 0, agSm = 0;
    unsigned agCount = 0;

    std::atomic<float> grNow { 0 }, grMax { 0 };
};
// ─────────────────────────────────────────────────────────────────────────────
// Moteur complet = deux CompressorCore. Un seul travaille en temps normal. Quand le suréchantillonnage
// (facteur ou type de filtre) change, le second est remis à zéro avec le nouveau réglage, reprend l'état
// de la dynamique du premier, tourne en parallèle le temps de se remplir (50 ms + sa latence), puis un
// fondu enchaîné de 20 ms passe de l'un à l'autre, traité et sec ensemble : aucune coupure, aucun clic.
// La latence annoncée change au début du fondu.
class CompressorEngine
{
public:
    void prepare (double sampleRate, bool constantLatency, const CompParams& initial)
    {
        fs = sampleRate; p = initial;
        core[0].prepare (fs, constantLatency, initial);
        core[1].prepare (fs, constantLatency, initial);
        act = 0; st = State::idle;
        cfg[0] = cfg[1] = want = wanted (initial);
        xfLen = std::max (1, (int) std::floor (CompressorCore::xfadeSec * fs + 0.5));
        for (auto& v : tmp) v.assign ((size_t) CompressorCore::chunk, 0.0f);
    }
    void setParams (const CompParams& np)
    {
        p = np; want = wanted (np);
        core[act].setParams (with (np, cfg[act]));
        if (st != State::idle) core[1 - act].setParams (with (np, cfg[1 - act]));
    }
    void process (const float* L, const float* R, const float* sL, const float* sR,
                  float* oL, float* oR, float* dL, float* dR, int n)
    {
        for (int off = 0; off < n; off += CompressorCore::chunk)
        {
            const int m = std::min (CompressorCore::chunk, n - off);
            const int o = 1 - act;
            if (st == State::idle && ! (want == cfg[act]))
            {
                cfg[o] = want;
                core[o].restart (with (p, want));
                core[o].adoptState (core[act]);
                st = State::warm;
                warmLeft = core[o].maxLookahead() + core[o].latency() + (int) (0.05 * fs);
            }
            else if (st == State::warm && warmLeft <= 0) { st = State::xfade; xfPos = 0; }

            const float* a = L + off; const float* b = R + off;
            const float* sa = sL != nullptr ? sL + off : nullptr; const float* sb = sR != nullptr ? sR + off : nullptr;
            core[act].process (a, b, sa, sb, oL + off, oR + off, dL + off, dR + off, m);
            if (st != State::idle)
            {
                core[o].process (a, b, sa, sb, tmp[0].data(), tmp[1].data(), tmp[2].data(), tmp[3].data(), m);
                if (st == State::warm) warmLeft -= m;
                else
                {
                    float* dst[4] = { oL + off, oR + off, dL + off, dR + off };
                    for (int i = 0; i < m; ++i)
                    {
                        double u = std::min (1.0, (double) (xfPos + i + 1) / xfLen); u = u * u * (3 - 2 * u);
                        for (int k = 0; k < 4; ++k) dst[k][i] = (float) (dst[k][i] + u * ((double) tmp[(size_t) k][(size_t) i] - dst[k][i]));
                    }
                    xfPos += m;
                    if (xfPos >= xfLen) { act = o; st = State::idle; }
                }
            }
        }
    }

    int latency() const          { return st == State::xfade ? core[1 - act].latency() : core[act].latency(); }
    int lookaheadSamples() const { return core[act].lookaheadSamples(); }
    int maxLookahead() const     { return core[act].maxLookahead(); }
    int oversamplingLatency (int stages, bool minPhase = false) const { return core[act].oversamplingLatency (stages, minPhase); }
    bool limiterActive() const   { return core[act].limiterActive(); }
    bool truePeakActive() const  { return core[act].truePeakActive(); }
    bool inTransition() const    { return st != State::idle || core[act].inTransition(); }
    int tpMinLook() const        { return core[act].tpMinLook(); }
    float getGR() const          { return core[act].getGR(); }
    float takeGRMax()            { return std::max (core[0].takeGRMax(), core[1].takeGRMax()); }
    float autoGainDb() const     { return core[act].autoGainDb(); }
    static double curve (double di, double thr, double ratioKnob, double knee, double mk) { return CompressorCore::curve (di, thr, ratioKnob, knee, mk); }
    static double compRatio (double knob) { return CompressorCore::compRatio (knob); }

private:
    struct Cfg { int os = 0; bool mn = false; bool operator== (const Cfg& o) const { return os == o.os && mn == o.mn; } };
    static Cfg wanted (const CompParams& q) { Cfg c; c.os = std::clamp (q.os, 0, 3); c.mn = q.osMin && c.os > 0; return c; }
    static CompParams with (CompParams q, const Cfg& c) { q.os = c.os; q.osMin = c.mn; return q; }
    enum class State { idle, warm, xfade };

    double fs = 48000;
    CompParams p;
    CompressorCore core[2];
    Cfg cfg[2], want;
    int act = 0, warmLeft = 0, xfPos = 0, xfLen = 960;
    State st = State::idle;
    std::vector<float> tmp[4];
};
} // namespace audiolab
