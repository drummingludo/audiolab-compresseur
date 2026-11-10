// Lab Comp 2.0.0 · banc qualité du moteur (sans JUCE)
//   clang++ -O2 -std=c++17 -I Source tests/quality_test.cpp -o qt && ./qt
// Mesures chiffrées avec seuils : identité avec la 1.2.1, plafond du limiteur ∞ (échantillons et crête vraie),
// latence et test de nul, repliement selon le suréchantillonnage, même réponse à 44,1 / 48 / 96 / 192 kHz,
// détecteur (link, Mid-Side, Hold, Auto release, fenêtre RMS), side-chain externe, transitions sans clic,
// latence constante (AAX) identique à la latence variable décalée. Renvoie 1 en cas d'échec.
#include "CompressorCore.h"
#include "../tests/ref_core_v1.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace audiolab;
static int fails = 0;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); if (! c) ++fails; }
static constexpr double PI = 3.14159265358979323846;
static double dB (double x) { return 20 * std::log10 (std::max (1e-15, x)); }

// ── Outils ──────────────────────────────────────────────────────────────
struct Out { std::vector<float> L, R, dL, dR; int lat = 0; };
using Change = std::function<void (int pos, CompParams&)>;

static double lastRunMs = 0;   // durée du seul traitement (hors préparation) du dernier run()
static Out run (double fs, const std::vector<float>& L, const std::vector<float>& R, CompParams p, bool constLat = false,
                const std::vector<float>* sL = nullptr, const std::vector<float>* sR = nullptr, int block = 256, Change ch = {})
{
    CompressorEngine c; c.prepare (fs, constLat, p);
    const int n = (int) L.size();
    Out o; o.L.resize ((size_t) n); o.R.resize ((size_t) n); o.dL.resize ((size_t) n); o.dR.resize ((size_t) n);
    const auto t0 = std::chrono::steady_clock::now();
    for (int off = 0; off < n; off += block)
    {
        const int m = std::min (block, n - off);
        if (ch) { ch (off, p); c.setParams (p); }
        c.process (L.data() + off, R.data() + off, sL ? sL->data() + off : nullptr, sR ? sR->data() + off : nullptr,
                   o.L.data() + off, o.R.data() + off, o.dL.data() + off, o.dR.data() + off, m);
        o.lat = c.latency();
    }
    lastRunMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return o;
}

static std::vector<float> sine (double fs, double f, double dbfs, double sec, double ph = 0)
{
    std::vector<float> x ((size_t) (fs * sec)); const double a = std::pow (10, dbfs / 20);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (a * std::sin (2 * PI * f * i / fs + ph));
    return x;
}
static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}
static double peakDb (const std::vector<float>& x, size_t from = 0)
{
    double m = 0; for (size_t i = from; i < x.size(); ++i) m = std::max (m, (double) std::abs (x[i]));
    return dB (m);
}
// Crête vraie mesurée à ×8 (référence indépendante du détecteur du limiteur : Oversampler.h ×8)
static double truePeakDb (const std::vector<float>& x, size_t from = 0)
{
    os::Oversampler o; o.prepare (3, 1, 512); double m = 0;
    for (size_t off = 0; off + 512 <= x.size(); off += 512)
    {
        const float* p = x.data() + off; auto* hi = o.upsample (&p, 512);
        if (off >= from + 1024) for (int i = 0; i < 4096; ++i) m = std::max (m, (double) std::abs (hi[0][i]));
    }
    return dB (m);
}
// Passe-bas FIR très raide (sinc fenêtré Kaiser β 10, 1023 coefficients)
static std::vector<float> lowpass (const std::vector<float>& x, double fs, double fc)
{
    const int N = 1023, c = N / 2;
    auto I0 = [] (double v) { double s = 1, t = 1; for (int k = 1; k < 60; ++k) { t *= (v / 2) / k; s += t * t; } return s; };
    std::vector<double> h (N); double sum = 0;
    for (int i = 0; i < N; ++i)
    {
        const double d = i - c, wc = 2 * fc / fs, s = d == 0 ? wc : std::sin (PI * wc * d) / (PI * d), r = d / c;
        h[(size_t) i] = s * I0 (10 * std::sqrt (std::max (0.0, 1 - r * r))) / I0 (10); sum += h[(size_t) i];
    }
    std::vector<float> y (x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double acc = 0; const int kmax = (int) std::min<size_t> ((size_t) N - 1, n);
        for (int k = 0; k <= kmax; ++k) acc += h[(size_t) k] * x[n - (size_t) k];
        y[n] = (float) (acc / sum);
    }
    return y;
}
// Signal d'épreuve du plafond : batterie (bruit + 200 Hz), sinus 11 / 12 kHz (crêtes entre les échantillons),
// impulsions isolées, musique dense ; crêtes proches de 0 dBFS
static void stress (double fs, double sec, std::vector<float>& L, std::vector<float>& R, unsigned seed = 3)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> U (-1, 1);
    const double w = std::tan (PI * std::min (20000.0, 0.45 * fs) / fs);
    double z[2][4] = {};
    auto bq = [&] (double x, double q, double* st) { const double k = 1 / q, a1 = 1 / (1 + w * (w + k)), a2 = w * a1, a3 = w * a2; const double v3 = x - st[1], v1 = a1 * st[0] + a2 * v3, v2 = st[1] + a2 * st[0] + a3 * v3; st[0] = 2 * v1 - st[0]; st[1] = 2 * v2 - st[1]; return v2; };
    auto nz = [&] (int c) { return 1.3 * bq (bq (U (rng), 0.5412, z[c]), 1.3066, z[c] + 2); };
    const int N = (int) (fs * sec);
    L.assign ((size_t) N, 0.0f); R.assign ((size_t) N, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const double ts = i / fs; const int per = (int) (0.25 * fs), t = i % per;
        const double env = std::exp (-t / (0.03 * fs));
        double a = 0.9 * env * (0.6 * nz (0) + 0.4 * std::sin (2 * PI * 200 * t / fs));
        double b = 0.9 * env * (0.6 * nz (1) + 0.4 * std::sin (2 * PI * 200 * t / fs + 0.3));
        if (ts > 0.375 * sec && ts <= 0.5625 * sec) { a = 0.95 * std::sin (2 * PI * 12000 * i / fs + PI / 4); b = 0.95 * std::sin (2 * PI * 11000 * i / fs + PI / 3); }
        else if (ts > 0.5625 * sec && ts <= 0.75 * sec) { a = (U (rng) > 0.999 ? 1.0 : 0.0) * (i % 2 ? 1 : -1); b = -a; }
        else if (ts > 0.75 * sec)
        {
            const double tb = std::fmod (ts, 0.5);
            const double kick = std::sin (2 * PI * (50 + 120 * std::exp (-tb * 30)) * tb) * std::exp (-tb * 9) * 0.8;
            a = kick + 0.25 * std::sin (2 * PI * 55 * ts) + 0.2 * std::sin (2 * PI * 880 * ts + 3 * std::sin (2 * PI * 5 * ts)) + 0.15 * nz (0);
            b = kick + 0.25 * std::sin (2 * PI * 55 * ts) + 0.12 * std::sin (2 * PI * 660 * ts) + 0.15 * nz (1);
        }
        L[(size_t) i] = (float) a; R[(size_t) i] = (float) b;
    }
}
// « Clic » : crête de ce qui dépasse 3 kHz (passe-haut 48 dB/oct) sur un sinus de 220 Hz, en dB sous son amplitude.
// Un fondu ou une variation de gain douce reste vers −100 dB ; un saut de signal (clic) remonte vers −20 dB.
static double clickDb (const std::vector<float>& y, double fs, double amp, size_t from = 4096)
{
    Biquad h[4]; for (auto& b : h) b.set (Biquad::HPF, 3000, 0.7071, fs);
    double m = 0;
    for (size_t i = 0; i < y.size(); ++i) { double v = y[i]; for (auto& b : h) v = b.run (v); if (i >= from) m = std::max (m, std::abs (v)); }
    return dB (m / amp);
}
// Repliement : énergie hors harmoniques de f0 rapportée au fondamental (dB)
static double aliasDb (const std::vector<float>& y, double fs, double f0, double fmax = 1e9)
{
    const size_t N = 1 << 15; std::vector<std::complex<double>> a (N);
    const size_t off = y.size() - N;
    // fenêtre de Kaiser β 30 (lobes secondaires sous −200 dB : la mesure descend bien sous −100 dB)
    auto I0 = [] (double v) { double s = 1, q = 1; for (int k = 1; k < 200; ++k) { q *= (v / 2) / k; s += q * q; } return s; };
    const double i0b = I0 (30.0);
    for (size_t i = 0; i < N; ++i) { const double r = 2.0 * i / (N - 1) - 1; a[i] = y[off + i] * I0 (30.0 * std::sqrt (std::max (0.0, 1 - r * r))) / i0b; }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= N; len <<= 1)
    {
        const std::complex<double> wl (std::cos (-2 * PI / len), std::sin (-2 * PI / len));
        for (size_t i = 0; i < N; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
    const double bin = fs / N; double fund = 0, rest = 0;
    for (size_t k = 0; k < N / 2; ++k)
    {
        const double f = k * bin, P = std::norm (a[k]);
        bool harm = f < 24 * bin;
        for (int h = 1; h * f0 < fs / 2; ++h) if (std::abs (f - h * f0) < 24 * bin) harm = true;
        if (std::abs (f - f0) < 24 * bin) fund += P; else if (! harm && f <= fmax) rest += P;
    }
    return 10 * std::log10 (rest / fund + 1e-30);
}
static CompParams brickwall (double look = 0.005)
{
    CompParams p; p.thr = -10; p.ratio = 20; p.knee = 0; p.atk = 0.0001; p.rel = 0.06; p.mk = 6; p.look = look;
    return p;
}

int main()
{
    char m[400];
    const double rates[] = { 44100, 48000, 96000, 192000 };

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n1) Réglages par défaut : identique au bit près à la 1.2.1 (donc au code web d'Audio Lab)\n");
    {
        struct C { const char* name; double thr, ratio, knee, atk, rel, mk, look; bool rms; int sc; double scF, scQ; bool listen; };
        const C cases[] = { { "réglages par défaut", 0, 1, 0, 0.0001, 0.01, 0, 0, false, 0, 1000, 0.7, false },
                            { "voix transparente", -22, 3, 8, 0.010, 0.080, 4, 0, false, 0, 1000, 0.7, false },
                            { "pompage RMS", -40, 8, 0, 0.001, 0.4, 10, 0, true, 0, 1000, 0.7, false },
                            { "SC HPF 150 Hz, lookahead 5 ms", -24, 4, 6, 0.005, 0.12, 3, 0.005, false, 1, 150, 0.7, false },
                            { "SC cloche, lookahead 10 ms, knee 10", -28, 5, 10, 0.0005, 0.15, 0, 0.010, false, 3, 3000, 1.5, false },
                            { "écoute du side-chain", -28, 5, 0, 0.0001, 0.01, 0, 0, false, 1, 2000, 0.7, true },
                            { "ratio 19.9 (juste sous ∞)", -20, 19.9, 0, 0.0001, 0.05, 0, 0.002, false, 0, 1000, 0.7, false } };
        std::mt19937 rng (5); std::normal_distribution<double> G (0, 1);
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const int n = (int) fs * 2;
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i) { const double e = (i / 4800) % 3 == 0 ? 0.9 : 0.1; L[(size_t) i] = (float) (e * std::sin (2 * PI * 220 * i / fs) + 0.05 * G (rng)); R[(size_t) i] = (float) (0.7 * L[(size_t) i] + 0.02 * G (rng)); }
            int bad = 0;
            for (const auto& c : cases)
            {
                CompParams p; p.thr = c.thr; p.ratio = c.ratio; p.knee = c.knee; p.atk = c.atk; p.rel = c.rel; p.mk = c.mk; p.look = c.look; p.rms = c.rms; p.scType = c.sc; p.scF = c.scF; p.scQ = c.scQ; p.scListen = c.listen;
                audiolab_v1::CompParams q; q.thr = c.thr; q.ratio = c.ratio; q.knee = c.knee; q.atk = c.atk; q.rel = c.rel; q.mk = c.mk; q.look = c.look; q.rms = c.rms; q.scType = c.sc; q.scF = c.scF; q.scQ = c.scQ; q.scListen = c.listen;
                auto o = run (fs, L, R, p, false, nullptr, nullptr, 333);
                audiolab_v1::CompressorCore v; v.prepare (fs); v.setParams (q);
                std::vector<float> a ((size_t) n), b ((size_t) n);
                for (int off = 0; off < n; off += 333) { const int k = std::min (333, n - off); v.process (L.data() + off, R.data() + off, a.data() + off, b.data() + off, k); }
                for (int i = 0; i < n; ++i) if (a[(size_t) i] != o.L[(size_t) i] || b[(size_t) i] != o.R[(size_t) i]) { ++bad; std::printf ("    écart : %s à %.0f Hz\n", c.name, fs); break; }
            }
            std::snprintf (m, sizeof m, "%.1f kHz : 7 réglages (dont side-chain, lookahead, écoute), blocs de 333 éch. : sortie identique", fs / 1000);
            check (bad == 0, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n2) Ratio ∞ : plafond = seuil + make-up (preset \"Brickwall limiter\" : −10 + 6 = −4 dBFS)\n");
    for (double fs : rates)
    {
        std::vector<float> L, R; stress (fs, 4, L, R);
        // avant : moteur 1.2.1 (compresseur 100:1)
        {
            audiolab_v1::CompParams q; q.thr = -10; q.ratio = 100; q.atk = 0.0001; q.rel = 0.06; q.mk = 6; q.look = 0.005;
            audiolab_v1::CompressorCore v; v.prepare (fs); v.setParams (q);
            std::vector<float> a (L.size()), b (L.size());
            for (size_t off = 0; off < L.size(); off += 512) { const int k = (int) std::min<size_t> (512, L.size() - off); v.process (L.data() + off, R.data() + off, a.data() + off, b.data() + off, k); }
            std::printf ("    avant (1.2.1) %.1f kHz : crête échantillon %+.2f dBFS, crête vraie %+.2f dBTP\n", fs / 1000,
                         std::max (peakDb (a), peakDb (b)), std::max (truePeakDb (a), truePeakDb (b)));
        }
        for (bool tp : { false, true })
        {
            auto p = brickwall(); p.truePeak = tp;
            auto o = run (fs, L, R, p);
            const double sp = std::max (peakDb (o.L), peakDb (o.R)), t = std::max (truePeakDb (o.L), truePeakDb (o.R));
            std::snprintf (m, sizeof m, "%.1f kHz, True peak %s : crête échantillon %.4f dBFS, crête vraie %.3f dBTP", fs / 1000, tp ? "On " : "Off", sp, t);
            check (sp <= -4 + 1e-4 && (! tp || t <= -4 + 0.1), m);
        }
    }
    for (double fs : { 44100.0, 48000.0 })
    {
        std::vector<float> L, R; stress (fs, 4, L, R);
        auto bl = lowpass (L, fs, 20000), br = lowpass (R, fs, 20000);
        for (double look : { 0.001, 0.005 })
        {
            auto o = run (fs, bl, br, brickwall (look));
            const double t = std::max (truePeakDb (o.L, 2048), truePeakDb (o.R, 2048));
            std::snprintf (m, sizeof m, "%.1f kHz, contenu limité à 20 kHz, lookahead %.0f ms : crête vraie %.3f dBTP (sous le plafond)", fs / 1000, look * 1000, t);
            check (t <= -4.0, m);
        }
    }
    {
        const double fs = 48000; std::vector<float> L, R; stress (fs, 4, L, R);
        for (int os : { 1, 2 })
        {
            auto p = brickwall(); p.os = os;
            auto o = run (fs, L, R, p);
            const double sp = std::max (peakDb (o.L), peakDb (o.R)), t = std::max (truePeakDb (o.L), truePeakDb (o.R));
            std::snprintf (m, sizeof m, "suréchantillonnage %d× : crête échantillon %.4f dBFS, crête vraie %.3f dBTP, latence %d éch.", 1 << os, sp, t, o.lat);
            check (sp <= -4 + 1e-4 && t <= -4 + 0.1, m);
        }
        for (double look : { 0.0, 0.0005, 0.001, 0.0015, 0.010 })
        {
            auto p = brickwall (look);
            auto o = run (fs, L, R, p);
            const double sp = std::max (peakDb (o.L), peakDb (o.R)), t = std::max (truePeakDb (o.L), truePeakDb (o.R));
            const bool tpOn = look >= 0.001;
            const double tol = 0.1;   // plein spectre (contenu jusqu'à Nyquist) : ≤ 0.1 dB ; ≤ 20 kHz : sous le plafond (voir plus haut)
            std::snprintf (m, sizeof m, "lookahead %.1f ms : crête échantillon %.4f dBFS, crête vraie %.3f dBTP%s", look * 1000, sp, t, tpOn ? "" : " (true peak inactif sous 32 échantillons)");
            check (sp <= -4 + 1e-4 && (! tpOn || t <= -4 + tol), m);
        }
        {
            auto p = brickwall(); p.thr = -20; p.mk = 19; // plafond −1 dBFS
            auto o = run (fs, L, R, p, true);
            const double sp = std::max (peakDb (o.L), peakDb (o.R)), t = std::max (truePeakDb (o.L), truePeakDb (o.R));
            std::snprintf (m, sizeof m, "seuil −20, make-up +19, latence constante (AAX) : crête %.4f dBFS, crête vraie %.3f dBTP (plafond −1)", sp, t);
            check (sp <= -1 + 1e-4 && t <= -1 + 0.1, m);
        }
        {
            // stereo link 0 % : chaque voie tient le plafond séparément
            auto p = brickwall(); p.link = 0;
            auto o = run (fs, L, R, p);
            const double sp = std::max (peakDb (o.L), peakDb (o.R)), t = std::max (truePeakDb (o.L), truePeakDb (o.R));
            std::snprintf (m, sizeof m, "stereo link 0 %% : crête %.4f dBFS, crête vraie %.3f dBTP", sp, t);
            check (sp <= -4 + 1e-4 && t <= -4 + 0.1, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n3) Latence annoncée exacte, signal sec recalé, test de nul\n");
    {
        const double fs = 48000;
        std::mt19937 rng (9); std::normal_distribution<double> G (0, 0.2);
        std::vector<float> nz (48000); for (auto& v : nz) v = (float) G (rng);
        auto bl = lowpass (nz, fs, 18000);
        for (bool aax : { false, true })
            for (int os : { 0, 1, 2, 3 })
                for (double look : { 0.0, 0.005, 0.010 })
                {
                    CompParams p; p.look = look; p.os = os;
                    std::vector<float> imp (8192, 0.0f); imp[1000] = 0.5f;
                    auto o = run (fs, imp, imp, p, aax);
                    int pw = 0, pd = 0;
                    for (int i = 0; i < 8192; ++i) { if (std::abs (o.L[(size_t) i]) > std::abs (o.L[(size_t) pw])) pw = i; if (std::abs (o.dL[(size_t) i]) > std::abs (o.dL[(size_t) pd])) pd = i; }
                    const int expect = (aax ? 480 + 64 : (int) std::lround (look * fs)) + (os == 0 ? 0 : os == 1 ? 85 : os == 2 ? 93 : 96);
                    auto n = run (fs, bl, bl, p, aax);
                    double num = 0, den = 0;
                    for (size_t i = 4096; i < bl.size(); ++i) { const double d = (double) n.L[i] - n.dL[i]; num += d * d; den += (double) n.dL[i] * n.dL[i]; }
                    const double nul = 10 * std::log10 (num / den + 1e-30);
                    std::snprintf (m, sizeof m, "%s, %d×, lookahead %4.1f ms : latence %d (attendu %d), traité / sec au même endroit (%d / %d), nul traité − sec %.1f dB",
                                   aax ? "AAX " : "VST3", 1 << os, look * 1000, o.lat, expect, pw - 1000, pd - 1000, nul);
                    check (o.lat == expect && pw - 1000 == expect && pd - 1000 == expect && num == 0, m);   // 3.0.0 : nul parfait aussi en suréchantillonnant
                }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n4) Suréchantillonnage : repliement d'un sinus aigu compressé vite (att 0.1 ms, rel 10 ms, 4:1)\n");
    {
        // 3.0.0 : cible −100 dB en qualité maximale (8×), filtres à phase linéaire comme à phase minimale
        struct T { double fs, f0, lvl, thr, ratio; };
        for (T t : { T { 44100, 9500, -6, -30, 4 }, T { 44100, 9500, -12, -24, 8 }, T { 48000, 15000, -6, -30, 4 }, T { 48000, 5000, -6, -30, 4 }, T { 96000, 15000, -6, -30, 4 } })
            for (bool mn : { false, true })
            {
                double r[4], full = 0;
                for (int os = 0; os < 4; ++os)
                {
                    CompParams p; p.thr = t.thr; p.ratio = t.ratio; p.atk = 0.0001; p.rel = 0.010; p.os = os; p.osMin = mn;
                    auto x = sine (t.fs, t.f0, t.lvl, 1.2);
                    auto o = run (t.fs, x, x, p);
                    r[os] = aliasDb (o.L, t.fs, t.f0, 20000);       // bande audible, jusqu'à 20 kHz
                    if (os == 3) full = aliasDb (o.L, t.fs, t.f0);   // tout le spectre (zone de transition du filtre comprise)
                }
                std::snprintf (m, sizeof m, "%.1f kHz, sinus %.1f kHz à %.0f dBFS, phase %s : Off %.1f dB, 2× %.1f, 4× %.1f, 8× %.1f dB (8×, au-delà de 20 kHz compris : %.1f)",
                               t.fs / 1000, t.f0 / 1000, t.lvl, mn ? "minimale" : "linéaire", r[0], r[1], r[2], r[3], full);
                check (r[3] <= -100 && full <= -90 && r[2] <= -85 && r[1] <= r[0] - 5, m);
            }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n5) Même réponse à 44,1 / 48 / 96 / 192 kHz (et suréchantillonné)\n");
    {
        auto theory = [] (double di) { return CompressorCore::curve (di, -20, 4, 6, 3); };
        for (double fs : rates)
            for (int os : { 0, 2 })
            {
                if (fs > 96000 && os > 0) continue;
                double worst = 0;
                for (double lvl = -40; lvl <= 0; lvl += 4)
                {
                    CompParams p; p.thr = -20; p.ratio = 4; p.knee = 6; p.mk = 3; p.atk = 0.001; p.rel = 0.05; p.os = os;
                    std::vector<float> dc ((size_t) (fs * 0.25), (float) std::pow (10, lvl / 20));
                    auto o = run (fs, dc, dc, p);
                    worst = std::max (worst, std::abs (dB (std::abs (o.L.back())) - theory (lvl)));
                }
                std::snprintf (m, sizeof m, "%.1f kHz%s : courbe statique (seuil −20, 4:1, knee 6, make-up +3), écart max %.4f dB", fs / 1000, os ? " ×4" : "", worst);
                check (worst < 0.01, m);
            }
        // constantes de temps d'attaque et de release (63 %), échelon −40 → 0 → −40 dBFS
        for (double fs : rates)
        {
            const double a = 0.010, r = 0.100;
            const int n = (int) (fs * 1.2), on = (int) (0.02 * fs), off = on + (int) (0.3 * fs);
            std::vector<float> x ((size_t) n, 0.01f); for (int i = on; i < off; ++i) x[(size_t) i] = 1.0f;
            CompParams p; p.thr = -40; p.ratio = 10; p.atk = a; p.rel = r;
            auto o = run (fs, x, x, p);
            std::vector<double> gr ((size_t) n); for (int i = 0; i < n; ++i) gr[(size_t) i] = -dB (std::abs (o.L[(size_t) i]) / x[(size_t) i]);
            const double top = gr[(size_t) off - 1];
            int ia = on; while (gr[(size_t) ia] < 0.632 * top) ++ia;
            int ir = off; while (gr[(size_t) ir] > 0.368 * top) ++ir;
            const double ta = (ia - on) / fs, tr = (ir - off) / fs;
            std::snprintf (m, sizeof m, "%.1f kHz : attaque 10 ms → %.2f ms, release 100 ms → %.1f ms", fs / 1000, ta * 1000, tr * 1000);
            check (std::abs (ta - a) < 0.02 * a + 1.5 / fs && std::abs (tr - r) < 0.02 * r, m);
        }
        // filtre du side-chain (écoute) : HPF 150 Hz Q 0.707 → −3.01 dB à 150 Hz
        for (double fs : rates)
        {
            CompParams p; p.scType = Biquad::HPF; p.scF = 150; p.scQ = 0.7071; p.scListen = true;
            auto x = sine (fs, 150, -20, 0.5);
            auto o = run (fs, x, x, p);
            const double g = dB (rms (o.L, x.size() / 2, x.size()) / rms (x, x.size() / 2, x.size()));
            std::snprintf (m, sizeof m, "%.1f kHz : filtre SC HPF 150 Hz à 150 Hz : %+.3f dB", fs / 1000, g);
            check (std::abs (g + 3.01) < 0.05, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n6) Détecteur : fenêtre RMS, Hold, Auto release\n");
    {
        // fenêtre RMS : ondulation de la réduction de gain sur un sinus 50 Hz
        for (double fs : { 44100.0, 192000.0 })
        {
            double rip[2];
            int k = 0;
            for (double win : { 0.003, 0.100 })
            {
                CompParams p; p.thr = -30; p.ratio = 4; p.rms = true; p.rmsWin = win; p.atk = 0.0001; p.rel = 0.01;
                auto x = sine (fs, 50, -6, 1.5);
                auto o = run (fs, x, x, p);
                double lo = 1e9, hi = -1e9;
                for (size_t i = (size_t) (fs * 1.2); i < x.size(); ++i)
                    if (std::abs (x[i]) > 0.1) { const double g = -dB (std::abs (o.L[i] / x[i])); lo = std::min (lo, g); hi = std::max (hi, g); }
                rip[k++] = hi - lo;
            }
            std::snprintf (m, sizeof m, "%.1f kHz : ondulation de GR sur 50 Hz, fenêtre RMS 3 ms %.2f dB, 100 ms %.3f dB", fs / 1000, rip[0], rip[1]);
            check (rip[0] > 1.0 && rip[1] < 0.15, m);
        }
        // Hold : la réduction reste en place pendant le maintien, puis le release commence
        for (double fs : { 44100.0, 96000.0 })
        {
            const double hold = 0.100;
            const int n = (int) (fs * 0.8), on = (int) (0.05 * fs), off = (int) (0.25 * fs);
            std::vector<float> x ((size_t) n, 0.001f); for (int i = on; i < off; ++i) x[(size_t) i] = 1.0f;
            CompParams p; p.thr = -30; p.ratio = 10; p.atk = 0.0005; p.rel = 0.05; p.hold = hold;
            auto o = run (fs, x, x, p);
            const double top = -dB (std::abs (o.L[(size_t) off - 1]) / x[(size_t) off - 1]);
            int i = off; while (i < n && std::abs (-dB (std::abs (o.L[(size_t) i]) / x[(size_t) i]) - top) < 0.05) ++i;
            const double held = (i - off) / fs;
            std::snprintf (m, sizeof m, "%.1f kHz : Hold 100 ms → réduction tenue %.2f ms après la fin de la salve", fs / 1000, held * 1000);
            check (std::abs (held - hold) < 0.002, m);
        }
        // Auto release : retour rapide après une crête brève, lent après une compression longue
        {
            const double fs = 48000;
            // réduction restante 150 ms après la fin d'une salve à 0 dBFS (seuil −30, 10:1 : 27 dB pendant la salve)
            auto left = [&] (double burst, bool autoRel)
            {
                const int n = (int) (fs * (burst + 1)), on = (int) (0.05 * fs), off = on + (int) (burst * fs);
                std::vector<float> x ((size_t) n, 0.001f); for (int i = on; i < off; ++i) x[(size_t) i] = 1.0f;
                CompParams p; p.thr = -30; p.ratio = 10; p.atk = 0.001; p.rel = 0.1; p.autoRel = autoRel;
                auto o = run (fs, x, x, p);
                const size_t i = (size_t) (off + 0.150 * fs);
                return -dB (std::abs (o.L[i]) / x[i]);
            };
            const double s0 = left (0.03, false), l0 = left (1.5, false), s1 = left (0.03, true), l1 = left (1.5, true);
            std::snprintf (m, sizeof m, "GR restante 150 ms après la salve (release 100 ms) : Off %.1f dB (salve 30 ms) / %.1f dB (1.5 s) ; Auto %.1f dB / %.1f dB", s0, l0, s1, l1);
            check (std::abs (s0 - l0) < 0.2 && s1 < 0.5 * s0 && l1 > 2 * l0, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n7) Stereo link et Mid-Side\n");
    {
        const double fs = 48000;
        auto L = sine (fs, 1000, -6, 1.0), R = sine (fs, 1500, -50, 1.0);   // R sous le seuil
        auto gainDb = [&] (const std::vector<float>& y, const std::vector<float>& x) { return dB (rms (y, 30000, 48000) / rms (x, 30000, 48000)); };
        for (bool lim : { false, true })
        {
            double gl[3], gr[3]; int k = 0;
            for (double lk : { 1.0, 0.5, 0.0 })
            {
                CompParams p; p.thr = -26; p.ratio = lim ? 20 : 4; p.atk = 0.001; p.rel = 0.1; p.link = lk;
                auto o = run (fs, L, R, p);
                gl[k] = gainDb (o.L, L); gr[k] = gainDb (o.R, R); ++k;
            }
            std::snprintf (m, sizeof m, "%s, L fort / R faible : GR de R = %.2f / %.2f / %.2f dB pour link 100 / 50 / 0 %% (GR de L %.2f dB)",
                           lim ? "limiteur ∞" : "compresseur 4:1", -gr[0], -gr[1], -gr[2], -gl[0]);
            check (std::abs (gr[0] - gl[0]) < 0.05 && std::abs (gr[1] - 0.5 * gl[1]) < 0.15 && std::abs (gr[2]) < 0.01, m);
        }
        // Mid-Side, link 0 : M fort compressé, S faible intact
        {
            auto a = sine (fs, 1000, -6, 1.0), b = sine (fs, 3000, -40, 1.0);
            std::vector<float> Lx (a.size()), Rx (a.size());
            for (size_t i = 0; i < a.size(); ++i) { Lx[i] = a[i] + b[i]; Rx[i] = a[i] - b[i]; }
            CompParams p; p.thr = -20; p.ratio = 4; p.atk = 0.001; p.rel = 0.1; p.midSide = true; p.link = 0;
            auto o = run (fs, Lx, Rx, p);
            std::vector<float> M (a.size()), S (a.size());
            for (size_t i = 0; i < a.size(); ++i) { M[i] = 0.5f * (o.L[i] + o.R[i]); S[i] = 0.5f * (o.L[i] - o.R[i]); }
            const double gM = dB (rms (M, 30000, 48000) / rms (a, 30000, 48000)), gS = dB (rms (S, 30000, 48000) / rms (b, 30000, 48000));
            std::snprintf (m, sizeof m, "Mid-Side, link 0 %% : Mid compressé de %.2f dB, Side inchangé (%+.3f dB)", -gM, gS);
            check (gM < -9 && std::abs (gS) < 0.01, m);
            // mono en Mid-Side : les deux voies restent identiques
            CompParams q = p; q.link = 1;
            auto mo = run (fs, a, a, q);
            bool same = true; for (size_t i = 0; i < a.size(); ++i) same = same && mo.L[i] == mo.R[i];
            check (same, "Mid-Side sur un signal mono : gauche et droite restent identiques");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n8) Side-chain externe\n");
    {
        const double fs = 48000;
        auto prog = sine (fs, 1000, -20, 2.0);
        std::vector<float> sc ((size_t) (fs * 2)), lowSc ((size_t) (fs * 2));
        for (size_t i = 0; i < sc.size(); ++i)
        {
            const bool on = (i / 24000) % 2 == 1;
            sc[i] = on ? (float) std::sin (2 * PI * 200 * i / fs) : 0.0f;
            lowSc[i] = (float) std::sin (2 * PI * 100 * i / fs);
        }
        CompParams p; p.thr = -30; p.ratio = 4; p.atk = 0.001; p.rel = 0.05; p.scExternal = true;
        auto o = run (fs, prog, prog, p, false, &sc, &sc);
        const double gOff = dB (rms (o.L, 18000, 24000) / rms (prog, 18000, 24000)), gOn = dB (rms (o.L, 42000, 48000) / rms (prog, 42000, 48000));
        std::snprintf (m, sizeof m, "External : SC muet → GR %.2f dB ; SC à 0 dBFS → GR %.2f dB (attendu 22.5)", -gOff, -gOn);
        check (std::abs (gOff) < 0.05 && std::abs (-gOn - 22.5) < 0.6, m);
        auto o2 = run (fs, prog, prog, p, false, nullptr, nullptr);
        const double gInt = dB (rms (o2.L, 42000, 48000) / rms (prog, 42000, 48000));
        std::snprintf (m, sizeof m, "External sans bus side-chain branché → détection interne : GR %.2f dB (attendu 7.5)", -gInt);
        check (std::abs (-gInt - 7.5) < 0.5, m);
        CompParams q = p; q.scType = Biquad::HPF; q.scF = 1000;
        auto o3 = run (fs, prog, prog, q, false, &lowSc, &lowSc);
        const double gF = dB (rms (o3.L, 42000, 48000) / rms (prog, 42000, 48000));
        std::snprintf (m, sizeof m, "filtre SC sur le side-chain externe : SC 100 Hz, HPF 1 kHz → GR %.2f dB (presque rien)", -gF);
        check (-gF < 0.3, m);
        CompParams l = p; l.scListen = true; l.scType = Biquad::HPF; l.scF = 1000;
        auto o4 = run (fs, prog, prog, l, false, &lowSc, &lowSc);
        const double lev = dB (rms (o4.L, 42000, 48000) / rms (lowSc, 42000, 48000));
        std::snprintf (m, sizeof m, "écoute du side-chain externe filtré : 100 Hz à %.1f dB (HPF 1 kHz, 12 dB/oct)", lev);
        check (lev < -35 && lev > -45, m);
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n9) Changements en cours de lecture, sans clic (énergie au-dessus de 3 kHz sur un sinus 220 Hz)\n");
    {
        const double fs = 48000, amp = 0.5;
        auto x = sine (fs, 220, -6, 1.5);
        auto at = [&] (std::vector<std::pair<int, std::function<void (CompParams&)>>> ev) {
            return [ev] (int pos, CompParams& p) { for (auto& e : ev) if (pos == e.first) e.second (p); };
        };
        // avant : moteur 1.2.1 avec les mêmes changements (saut de lecture, seuil non lissé)
        auto v1 = [&] (audiolab_v1::CompParams q, std::function<void (int, audiolab_v1::CompParams&)> ch) {
            audiolab_v1::CompressorCore c; c.prepare (fs); c.setParams (q);
            std::vector<float> a (x.size()), b (x.size());
            for (size_t off = 0; off < x.size(); off += 256) { ch ((int) off, q); c.setParams (q); c.process (x.data() + off, x.data() + off, a.data() + off, b.data() + off, (int) std::min<size_t> (256, x.size() - off)); }
            return a;
        };
        {
            CompParams p;
            auto o = run (fs, x, x, p, false, nullptr, nullptr, 256, at ({ { 256 * 40, [] (CompParams& q) { q.look = 0.005; } },
                                                                          { 256 * 100, [] (CompParams& q) { q.look = 0.010; } },
                                                                          { 256 * 160, [] (CompParams& q) { q.look = 0.002; } } }));
            audiolab_v1::CompParams q;
            auto before = v1 (q, [] (int pos, audiolab_v1::CompParams& r) { if (pos == 256 * 40) r.look = 0.005; if (pos == 256 * 100) r.look = 0.010; if (pos == 256 * 160) r.look = 0.002; });
            const double c1 = clickDb (o.L, fs, amp), c2 = clickDb (o.dL, fs, amp), c0 = clickDb (before, fs, amp);
            std::snprintf (m, sizeof m, "lookahead 0 → 5 → 10 → 2 ms (VST3, la latence change) : traité %.1f dB, sec %.1f dB (avant : %.1f dB)", c1, c2, c0);
            check (c1 < -70 && c2 < -70, m);
            CompParams pc; pc.thr = -20; pc.ratio = 4; pc.atk = 0.002; pc.rel = 0.08;
            auto a = run (fs, x, x, pc, true, nullptr, nullptr, 256, at ({ { 256 * 40, [] (CompParams& r) { r.look = 0.005; } },
                                                                          { 256 * 100, [] (CompParams& r) { r.look = 0.010; } } }));
            const double c3 = clickDb (a.L, fs, amp);
            std::snprintf (m, sizeof m, "lookahead 0 → 5 → 10 ms en compressant (AAX, latence fixe, le gain est recalé) : %.1f dB", c3);
            check (c3 < -70, m);
        }
        {
            CompParams p; p.thr = -20; p.ratio = 10; p.atk = 0.0001; p.rel = 0.05; p.look = 0.005;
            auto o = run (fs, x, x, p, false, nullptr, nullptr, 256, at ({ { 256 * 50, [] (CompParams& q) { q.ratio = 20; } },
                                                                          { 256 * 150, [] (CompParams& q) { q.ratio = 8; } } }));
            const double c = clickDb (o.L, fs, amp);
            const double pk = peakDb (std::vector<float> (o.L.begin() + 256 * 110, o.L.begin() + 256 * 145));
            std::snprintf (m, sizeof m, "ratio 10 → ∞ → 8 : %.1f dB ; plafond tenu en ∞ (%.4f dBFS, plafond −20)", c, pk);
            check (c < -70 && pk <= -20 + 1e-4, m);
        }
        {
            CompParams p; p.thr = 0;
            auto o = run (fs, x, x, p, false, nullptr, nullptr, 64, at ({ { 64 * 200, [] (CompParams& q) { q.mk = 12; } } }));
            double worst = 0;
            for (size_t i = 64 * 190; i < 64 * 400; ++i)
                if (std::abs (x[i]) > 0.2 && std::abs (x[i - 1]) > 0.2) worst = std::max (worst, std::abs (dB (std::abs (o.L[i] / x[i])) - dB (std::abs (o.L[i - 1] / x[i - 1]))));
            audiolab_v1::CompParams q;
            auto before = v1 (q, [] (int pos, audiolab_v1::CompParams& r) { if (pos == 256 * 50) r.mk = 12; });
            std::snprintf (m, sizeof m, "make-up 0 → +12 dB : pas maximal %.4f dB par échantillon (rampe de 20 ms), %.1f dB au-dessus de 3 kHz (avant : %.1f dB)",
                           worst, clickDb (o.L, fs, amp), clickDb (before, fs, amp));
            check (worst < 1.05 * 12 / (0.02 * fs) && clickDb (o.L, fs, amp) < -70, m);
            // seuil 0 → −30 dB d'un coup : réglage courant (att 1 ms) puis extrême (att 0.1 ms, rel 10 ms ; l'attaque
            // de 0.1 ms suit alors la réduction qui monte de 27 dB en 20 ms, comme si la musique montait d'autant)
            for (double atk : { 0.001, 0.0001 })
            {
                CompParams t; t.thr = 0; t.ratio = 10; t.atk = atk; t.rel = atk < 0.0005 ? 0.01 : 0.05;
                auto o2 = run (fs, x, x, t, false, nullptr, nullptr, 64, at ({ { 64 * 200, [] (CompParams& r) { r.thr = -30; } } }));
                audiolab_v1::CompParams q2; q2.thr = 0; q2.ratio = 10; q2.atk = t.atk; q2.rel = t.rel;
                auto b2 = v1 (q2, [] (int pos, audiolab_v1::CompParams& r) { if (pos == 256 * 50) r.thr = -30; });
                const double c = clickDb (o2.L, fs, amp), c0 = clickDb (b2, fs, amp);
                std::snprintf (m, sizeof m, "seuil 0 → −30 dB (10:1, att %.1f / rel %.0f ms) : %.1f dB (avant : %.1f dB)", t.atk * 1000, t.rel * 1000, c, c0);
                check (atk >= 0.0005 ? c < -70 : (c < -50 && c < c0 - 30), m);
            }
        }
        {
            auto y = sine (fs, 330, -10, 1.5, 0.7);
            std::vector<float> Rr (x.size()); for (size_t i = 0; i < x.size(); ++i) Rr[i] = 0.5f * x[i] + y[i];
            CompParams p; p.thr = -24; p.ratio = 6; p.atk = 0.001; p.rel = 0.08; p.link = 0.3;
            auto o = run (fs, x, Rr, p, false, nullptr, nullptr, 256, at ({ { 256 * 60, [] (CompParams& q) { q.midSide = true; } },
                                                                           { 256 * 150, [] (CompParams& q) { q.midSide = false; } } }));
            const double c = std::max (clickDb (o.L, fs, amp), clickDb (o.R, fs, amp));
            std::snprintf (m, sizeof m, "Stereo → Mid-Side → Stereo en compressant : %.1f dB", c);
            check (c < -70, m);
        }
        {
            CompParams p; p.thr = -20; p.ratio = 4; p.atk = 0.001; p.rel = 0.08;
            auto o = run (fs, x, x, p, false, nullptr, nullptr, 256, at ({ { 256 * 60, [] (CompParams& q) { q.os = 2; } }, { 256 * 150, [] (CompParams& q) { q.os = 1; } } }));
            const double c = std::max (clickDb (o.L, fs, amp), clickDb (o.dL, fs, amp));
            std::snprintf (m, sizeof m, "suréchantillonnage Off → 4× → 2× (fondu enchaîné, sans coupure) : %.1f dB, latence finale %d", c, o.lat);
            check (c < -90 && o.lat == 85, m);
        }
        {
            auto sc = sine (fs, 100, -3, 1.5);
            CompParams p; p.thr = -30; p.ratio = 4; p.atk = 0.0001; p.rel = 0.05;
            auto o = run (fs, x, x, p, false, &sc, &sc, 256, at ({ { 256 * 60, [] (CompParams& q) { q.scExternal = true; } }, { 256 * 150, [] (CompParams& q) { q.link = 0.2; q.autoRel = true; q.hold = 0.05; } } }));
            const double c = clickDb (o.L, fs, amp);
            std::snprintf (m, sizeof m, "SC source Internal → External, puis link, Auto, Hold : %.1f dB", c);
            check (c < -70, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n10) Latence constante (AAX) = latence variable décalée, au bit près\n");
    {
        const double fs = 48000; std::vector<float> L, R; stress (fs, 2, L, R, 11);
        struct K { const char* name; CompParams p; };
        CompParams a = brickwall(); a.scType = Biquad::HPF; a.scF = 150;
        CompParams b; b.thr = -24; b.ratio = 4; b.knee = 6; b.look = 0.003; b.midSide = true; b.link = 0.4; b.hold = 0.02; b.autoRel = true; b.rms = true; b.rmsWin = 0.02;
        CompParams c = b; c.os = 1; c.midSide = false;
        for (const K& k : { K { "limiteur ∞ + SC HPF", a }, K { "Mid-Side, link 40 %, Hold, Auto, RMS 20 ms", b }, K { "suréchantillonnage 2×", c } })
        {
            auto v = run (fs, L, R, k.p, false), x = run (fs, L, R, k.p, true);
            const int sh = x.lat - v.lat;
            double d = 0;
            for (size_t i = 0; i + (size_t) sh < L.size(); ++i) d = std::max ({ d, (double) std::abs (x.L[i + (size_t) sh] - v.L[i]), (double) std::abs (x.R[i + (size_t) sh] - v.R[i]) });
            std::snprintf (m, sizeof m, "%s : décalage %d éch., écart max %.3g", k.name, sh, d);
            // 3.0.0 : la latence constante vaut 10 ms + 64 éch. (second étage crête vraie), que le limiteur ∞ compte déjà en VST3
            check (d == 0 && sh == 480 + 64 - (int) std::lround (k.p.look * fs) - (k.p.ratio >= 20 ? 64 : 0), m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n12) Automation de CHAQUE réglage sans clic (3.0.0) : résidu au-dessus de 3 kHz sous −90 dB\n"
                 "    (sinus 220 Hz, détecteur propre : RMS 50 ms, attaque 10 ms, release 100 ms ; aller à 0,5 s, retour à 1 s)\n");
    {
        const double fs = 48000, amp = 0.5;
        auto x = sine (fs, 220, -6, 1.6);
        auto sc = sine (fs, 220, -12, 1.6, 1.0);
        struct E { const char* name; std::function<void (CompParams&)> go, back; bool limiter; };
        auto S = [] (std::function<void (CompParams&)> a, std::function<void (CompParams&)> b) { return std::make_pair (a, b); };
        std::vector<E> ev = {
            { "Threshold −20 → −40 dB", [] (CompParams& q) { q.thr = -40; }, [] (CompParams& q) { q.thr = -20; }, false },
            { "Ratio 4 → 12", [] (CompParams& q) { q.ratio = 12; }, [] (CompParams& q) { q.ratio = 4; }, false },
            { "Ratio 4 → ∞ (limiteur)", [] (CompParams& q) { q.ratio = 20; }, [] (CompParams& q) { q.ratio = 4; }, false },
            { "Knee 6 → 20 dB", [] (CompParams& q) { q.knee = 20; }, [] (CompParams& q) { q.knee = 6; }, false },
            { "Attack 10 → 0.5 ms", [] (CompParams& q) { q.atk = 0.0005; }, [] (CompParams& q) { q.atk = 0.01; }, false },
            { "Release 100 → 20 ms", [] (CompParams& q) { q.rel = 0.02; }, [] (CompParams& q) { q.rel = 0.1; }, false },
            { "Makeup 0 → +12 dB", [] (CompParams& q) { q.mk = 12; }, [] (CompParams& q) { q.mk = 0; }, false },
            { "Lookahead 0 → 5 ms", [] (CompParams& q) { q.look = 0.005; }, [] (CompParams& q) { q.look = 0; }, false },
            { "Detection RMS → Peak", [] (CompParams& q) { q.rms = false; }, [] (CompParams& q) { q.rms = true; }, false },
            { "RMS time 50 → 5 ms", [] (CompParams& q) { q.rmsWin = 0.005; }, [] (CompParams& q) { q.rmsWin = 0.05; }, false },
            { "Hold 0 → 100 ms", [] (CompParams& q) { q.hold = 0.1; }, [] (CompParams& q) { q.hold = 0; }, false },
            { "Release Fixed → Auto", [] (CompParams& q) { q.autoRel = true; }, [] (CompParams& q) { q.autoRel = false; }, false },
            { "Range 60 → 3 dB", [] (CompParams& q) { q.range = 3; }, [] (CompParams& q) { q.range = 60; }, false },
            { "Auto gain Off → On", [] (CompParams& q) { q.autoGain = true; }, [] (CompParams& q) { q.autoGain = false; }, false },
            { "Side-chain Off → HPF 500 Hz", [] (CompParams& q) { q.scType = Biquad::HPF; q.scF = 500; }, [] (CompParams& q) { q.scType = Biquad::Off; }, false },
            { "SC freq 500 → 2000 Hz (HPF)", [] (CompParams& q) { q.scF = 2000; }, [] (CompParams& q) { q.scF = 500; }, false },
            { "SC source Internal → External", [] (CompParams& q) { q.scExternal = true; }, [] (CompParams& q) { q.scExternal = false; }, false },
            { "Stereo link 100 → 0 %", [] (CompParams& q) { q.link = 0; }, [] (CompParams& q) { q.link = 1; }, false },
            { "Mode Stereo → Mid-Side", [] (CompParams& q) { q.midSide = true; }, [] (CompParams& q) { q.midSide = false; }, false },
            { "Oversampling Off → 8×", [] (CompParams& q) { q.os = 3; }, [] (CompParams& q) { q.os = 0; }, false },
            { "Oversampling 2× → 4×", [] (CompParams& q) { q.os = 2; }, [] (CompParams& q) { q.os = 1; }, false },
            { "OS filter Linear → Minimum (4×)", [] (CompParams& q) { q.osMin = true; }, [] (CompParams& q) { q.osMin = false; }, false },
            { "∞ : True peak On → Off", [] (CompParams& q) { q.truePeak = false; }, [] (CompParams& q) { q.truePeak = true; }, true },
            { "∞ : Threshold −20 → −30 dB", [] (CompParams& q) { q.thr = -30; }, [] (CompParams& q) { q.thr = -20; }, true },
            { "∞ : Lookahead 5 → 1 ms", [] (CompParams& q) { q.look = 0.001; }, [] (CompParams& q) { q.look = 0.005; }, true },
            { "∞ : Release 100 → 20 ms", [] (CompParams& q) { q.rel = 0.02; }, [] (CompParams& q) { q.rel = 0.1; }, true },
        };
        (void) S;
        for (auto& e : ev)
        {
            CompParams p; p.thr = -20; p.ratio = 4; p.knee = 6; p.atk = 0.01; p.rel = 0.1; p.rms = true; p.rmsWin = 0.05;
            if (e.limiter) { p.ratio = 20; p.look = 0.005; p.knee = 0; }
            const std::string nm = e.name;
            if (nm.find ("Ratio 4 → ∞") != std::string::npos) p.look = 0.005;   // sans anticipation un limiteur écrête : ce ne serait pas un clic
            if (nm.find ("SC freq") != std::string::npos) { p.scType = Biquad::HPF; p.scF = 500; }
            if (nm.find ("2× → 4×") != std::string::npos) p.os = 1;
            if (nm.find ("OS filter") != std::string::npos) p.os = 2;
            auto go = e.go; auto back = e.back;
            auto o = run (fs, x, x, p, false, &sc, &sc, 256, [go, back] (int pos, CompParams& q) { if (pos == 256 * 94) go (q); if (pos == 256 * 188) back (q); });
            const double c = std::max (clickDb (o.L, fs, amp, 8192), clickDb (o.dL, fs, amp, 8192));
            std::snprintf (m, sizeof m, "%-34s %.1f dB", e.name, c);
            check (c < -90, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n13) Limiteur ∞ 3.0.0 : crête vraie sous le plafond même avec du contenu jusqu'à Nyquist (bruit blanc plein spectre)\n");
    {
        for (double fs : rates)
            for (double look : { 0.0008, 0.001, 0.005 })
            {
                if ((int) std::lround (look * fs) < 32) continue;      // crête vraie active dès 32 échantillons d'anticipation
                std::mt19937 rng (21); std::uniform_real_distribution<double> U (-1, 1);
                std::vector<float> a ((size_t) (fs * 1.5)), b (a.size());
                for (size_t i = 0; i < a.size(); ++i) { a[i] = (float) U (rng); b[i] = (float) U (rng); }
                std::vector<float> sl, sr; stress (fs, 1.5, sl, sr, 5);
                CompParams p = brickwall (look);
                auto o = run (fs, a, b, p); auto o2 = run (fs, sl, sr, p);
                const size_t from = (size_t) (0.1 * fs);
                const double tpn = std::max (truePeakDb (o.L, from), truePeakDb (o.R, from)), tps = std::max (truePeakDb (o2.L, from), truePeakDb (o2.R, from));
                const double spk = std::max (peakDb (o.L, from), peakDb (o2.L, from));
                std::snprintf (m, sizeof m, "%.1f kHz, lookahead %.1f ms : bruit blanc %.3f dBTP, signal d'épreuve %.3f dBTP, crête échantillon %.3f dBFS (plafond −4)", fs / 1000, look * 1000, tpn, tps, spk);
                check (tpn <= -4.0 && tps <= -4.0 && spk <= -4.0, m);
            }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n14) Range, auto-gain, phase minimale, grave en release rapide (3.0.0)\n");
    {
        const double fs = 48000;
        {   // Range : la réduction ne dépasse jamais la valeur réglée
            auto x = sine (fs, 1000, 0, 1.0);
            CompParams p; p.thr = -40; p.ratio = 10; p.atk = 0.001; p.rel = 0.1; p.rms = true; p.rmsWin = 0.02;
            auto full = run (fs, x, x, p); p.range = 6; auto lim6 = run (fs, x, x, p);
            const double gFull = dB (rms (full.L, 40000, 48000) / rms (x, 40000, 48000)), g6 = dB (rms (lim6.L, 40000, 48000) / rms (x, 40000, 48000));
            std::snprintf (m, sizeof m, "Range 6 dB : réduction %.2f dB (sans Range : %.2f dB)", -g6, -gFull);
            check (std::abs (g6 + 6) < 0.02 && gFull < -30, m);
            CompParams d; d.thr = -20; d.ratio = 4; auto r60 = run (fs, x, x, d); d.range = 60; auto r60b = run (fs, x, x, d);
            check (r60.L == r60b.L, "Range 60 dB (défaut) : sortie identique au bit près");
        }
        {   // Auto gain : même volume (pondération K) en sortie qu'en entrée
            // musique dense stationnaire : le dernier quart du signal d'épreuve (0,5 s), répété 16 fois
            std::vector<float> sL, sR, L, R; stress (fs, 2, sL, sR, 4);
            for (int k = 0; k < 16; ++k) { L.insert (L.end(), sL.begin() + 72000, sL.end()); R.insert (R.end(), sR.begin() + 72000, sR.end()); }
            for (double thr : { -30.0, -18.0 })
            {
                CompParams p; p.thr = thr; p.ratio = 4; p.atk = 0.005; p.rel = 0.1; p.autoGain = true;
                auto o = run (fs, L, R, p);
                auto kw = [&] (const std::vector<float>& v) {
                    Biquad h; h.set (Biquad::HPF, 60, 0.5, fs); double s = 0; size_t n = 0;
                    for (size_t i = 0; i < v.size(); ++i) { const double y = h.run (v[i]); if (i >= (size_t) (6.0 * fs) && i < (size_t) (8.0 * fs)) { s += y * y; ++n; } }
                    return 10 * std::log10 (s / n + 1e-30); };
                p.autoGain = false; auto raw = run (fs, L, R, p);
                const double dIn = kw (o.dL), dOut = kw (o.L), dRaw = kw (raw.L);
                std::snprintf (m, sizeof m, "Auto gain, seuil %.0f dB : sortie %+.2f dB par rapport à l'entrée (sans auto-gain : %+.2f dB)", thr, dOut - dIn, dRaw - dIn);
                check (std::abs (dOut - dIn) < 0.5 && dRaw - dIn < -1.5, m);
            }
        }
        {   // phase minimale : latence annoncée nulle, retard réel et écart Dry/Wet
            for (int os = 1; os <= 3; ++os)
            {
                CompParams p; p.os = os; p.osMin = true;
                std::mt19937 rng (9); std::normal_distribution<double> G (0, 0.2);
                std::vector<float> nz (48000); for (auto& v : nz) v = (float) G (rng);
                auto o = run (fs, nz, nz, p);
                double num = 0, den = 0;
                for (size_t i = 4096; i < nz.size(); ++i) { const double d = (double) o.L[i] - o.dL[i]; num += d * d; den += (double) o.dL[i] * o.dL[i]; }
                audiolab::os::Oversampler ov; ov.prepare (os, 1, 256, true);
                std::snprintf (m, sizeof m, "phase minimale %d× : latence annoncée %d, retard réel %.1f éch. (%.2f ms), nul traité − sec %.1f dB", 1 << os, o.lat, ov.minPhaseDelay(), ov.minPhaseDelay() / fs * 1000, 10 * std::log10 (num / den + 1e-30));
                check (o.lat == 0 && num / den < 1e-10 && ov.minPhaseDelay() < 8, m);
            }
        }
        {   // grave : distorsion d'un sinus 50 Hz compressé de 12 dB, release rapide (mesure d'information et garde-fou)
            auto x = sine (fs, 50, -6, 2.0);
            auto thd = [&] (CompParams p) {
                auto o = run (fs, x, x, p);
                // fondamental par corrélation, le reste = distorsion
                const size_t a0 = 48000, n = 48000; double c = 0, s = 0, e = 0;
                for (size_t i = 0; i < n; ++i) { const double w = 2 * PI * 50 * (double) i / fs; c += o.L[a0 + i] * std::cos (w); s += o.L[a0 + i] * std::sin (w); e += (double) o.L[a0 + i] * o.L[a0 + i]; }
                const double f2 = 2 * (c * c + s * s) / n;
                return 100 * std::sqrt (std::max (0.0, e - f2) / f2); };
            CompParams p; p.thr = -24; p.ratio = 3; p.atk = 0.005; p.rel = 0.03;
            const double tFix = thd (p);
            CompParams q = p; q.hold = 0.02; const double tHold = thd (q);
            CompParams r = p; r.rms = true; r.rmsWin = 0.05; const double tRms = thd (r);
            std::snprintf (m, sizeof m, "sinus 50 Hz, release 30 ms : distorsion %.2f %% (Peak), %.2f %% avec Hold 20 ms, %.3f %% en RMS 50 ms", tFix, tHold, tRms);
            check (tHold < tFix && tRms < 0.5, m);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    std::printf ("\n11) Charge de calcul (indicative, 48 kHz stéréo)\n");
    {
        const double fs = 48000; std::vector<float> L, R; stress (fs, 4, L, R);
        struct K { const char* name; CompParams p; };
        CompParams base; base.thr = -20; base.ratio = 4;
        CompParams o2 = base; o2.os = 1; CompParams o4 = base; o4.os = 2; CompParams o8 = base; o8.os = 3;
        CompParams m8 = o8; m8.osMin = true; CompParams ag = base; ag.autoGain = true;
        for (const K& k : { K { "compresseur", base }, K { "compresseur 2×", o2 }, K { "compresseur 4×", o4 }, K { "compresseur 8×", o8 },
                            K { "compresseur 8× phase min.", m8 }, K { "compresseur + auto-gain", ag }, K { "limiteur ∞ true peak", brickwall() } })
        {
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep) { auto o = run (fs, L, R, k.p); best = std::min (best, lastRunMs); }
            std::printf ("    %-28s %.2f %% d'un cœur\n", k.name, best / 4000 * 100);
        }
    }

    std::printf ("\n%s\n", fails == 0 ? "RÉSULTAT : tout est conforme" : "RÉSULTAT : des écarts à corriger");
    return fails == 0 ? 0 : 1;
}
