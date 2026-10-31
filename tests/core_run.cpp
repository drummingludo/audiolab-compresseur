// Fait tourner le moteur C++ du plugin sur le même signal que ref_run.js.
// Usage : core_run <fs> <params.txt> <in.f32> <out.f32> [bloc]
// params.txt : thr ratio knee atk_s rel_s mk rms look_s scType scF scQ scListen
#include "../Source/CompressorCore.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    const double fs = atof (argv[1]);
    const int block = argc > 5 ? atoi (argv[5]) : 128;
    audiolab::CompParams p;
    FILE* pf = fopen (argv[2], "r");
    int rms = 0, scl = 0;
    if (fscanf (pf, "%lf %lf %lf %lf %lf %lf %d %lf %d %lf %lf %d", &p.thr, &p.ratio, &p.knee, &p.atk, &p.rel, &p.mk,
                &rms, &p.look, &p.scType, &p.scF, &p.scQ, &scl) != 12) return 2;
    fclose (pf);
    p.rms = rms != 0; p.scListen = scl != 0;
    FILE* f = fopen (argv[3], "rb");
    fseek (f, 0, SEEK_END); long bytes = ftell (f); fseek (f, 0, SEEK_SET);
    std::vector<float> in ((size_t) bytes / 4);
    if (fread (in.data(), 4, in.size(), f) != in.size()) return 3;
    fclose (f);
    const int N = (int) in.size() / 2;
    std::vector<float> out (in.size()), L (block), R (block), oL (block), oR (block);
    audiolab::CompressorCore c;
    c.prepare (fs);
    c.setParams (p);
    for (int s = 0; s < N; s += block)
    {
        const int n = std::min (block, N - s);
        for (int i = 0; i < n; ++i) { L[i] = in[2 * (s + i)]; R[i] = in[2 * (s + i) + 1]; }
        c.process (L.data(), R.data(), oL.data(), oR.data(), n);
        for (int i = 0; i < n; ++i) { out[2 * (s + i)] = oL[i]; out[2 * (s + i) + 1] = oR[i]; }
    }
    FILE* o = fopen (argv[4], "wb");
    fwrite (out.data(), 4, out.size(), o);
    fclose (o);
    return 0;
}
