"""Banc de test du moteur : compare le plugin (C++) au vrai code d'Audio Lab (JS),
puis mesure seuil, ratio, knee, attaque, release, lookahead et filtres de side-chain.

Version 2.0.0 (décision du 03/11/2026 : les plugins sont la version « pro », la page web ne change pas
pour l'instant). Ce qui reste comparé au bit près au code web : tous les réglages du compresseur
(ratio < ∞) avec les nouveaux réglages à leur valeur par défaut (Off / Internal / 100 % / Stereo / Hold 0 /
Auto Off / RMS 3 ms). Retiré parce que devenu volontairement faux :
  - la comparaison « Limiteur brickwall (∞:1) » : ∞ est maintenant un vrai limiteur à plafond garanti,
    le web garde son compresseur 100:1 ;
  - la courbe statique ∞ comparée à 100:1 : remplacée par la courbe du limiteur (plafond = seuil + make-up) ;
  - la mesure des temps d'attaque / release se fait à 10:1 (en ∞, l'attaque est faite par l'anticipation).
Le reste des nouveautés est mesuré par tests/quality_test.cpp (sans JUCE)."""
import json, os, subprocess, sys, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get("ALC_TMP", "/tmp/alc")
os.makedirs(TMP, exist_ok=True)
rng = np.random.default_rng(1234)
OK = True

def check(cond, msg):
    global OK
    print(("  OK   " if cond else "  ÉCHEC ") + msg)
    OK &= bool(cond)

SC = {"off": 0, "hpf": 1, "lpf": 2, "bell": 3}

def run_both(fs, x, prm, block=128):
    """x : (N,2) float32. prm : paramètres au format compParams() d'Audio Lab."""
    inp = os.path.join(TMP, "in.f32"); x.astype(np.float32).tofile(inp)
    pj = os.path.join(TMP, "p.json"); json.dump(prm, open(pj, "w"))
    pt = os.path.join(TMP, "p.txt")
    open(pt, "w").write("%r %r %r %r %r %r %d %r %d %r %r %d" % (
        prm["thr"], prm["ratio"], prm["knee"], prm["atk"], prm["rel"], prm["mk"], prm["det"] == "rms",
        prm["look"], SC[prm["scType"]], prm["scF"], prm["scQ"], bool(prm.get("scListen"))))
    oj, oc = os.path.join(TMP, "oj.f32"), os.path.join(TMP, "oc.f32")
    subprocess.run(["node", os.path.join(HERE, "ref_run.js"), str(fs), pj, inp, oj], check=True)
    subprocess.run([os.path.join(HERE, "core_run"), str(fs), pt, inp, oc, str(block)], check=True)
    return np.fromfile(oj, np.float32).reshape(-1, 2), np.fromfile(oc, np.float32).reshape(-1, 2)

def P(**k):
    base = dict(thr=0, ratio=1, knee=0, atk=0.0001, rel=0.01, mk=0, det="peak", look=0,
                scType="off", scF=1000, scQ=0.7, mode="comp", scListen=False)
    base.update(k); return base

def music(fs, sec=4.0):
    """Signal de test riche : grosse caisse, caisse claire, basse, voix (sinus modulés), bruit, silence."""
    n = int(fs * sec); t = np.arange(n) / fs; L = np.zeros(n); R = np.zeros(n)
    beat = 0.5
    for k in range(int(sec / beat)):
        s = int(k * beat * fs); m = min(n - s, int(0.25 * fs)); tt = np.arange(m) / fs
        kick = np.sin(2 * np.pi * (50 + 120 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 9) * 0.9
        L[s:s + m] += kick; R[s:s + m] += kick
        if k % 2:
            sn = rng.standard_normal(m) * np.exp(-tt * 18) * 0.5
            L[s:s + m] += sn; R[s:s + m] += 0.8 * sn
    bass = 0.25 * np.sin(2 * np.pi * 55 * t) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.5 * t))
    voice = 0.2 * np.sin(2 * np.pi * 220 * t + 3 * np.sin(2 * np.pi * 5 * t)) * (np.sin(2 * np.pi * 0.7 * t) > -0.3)
    L += bass + voice; R += bass + 0.6 * voice + 0.02 * rng.standard_normal(n)
    L[int(3.2 * fs):int(3.5 * fs)] = 0; R[int(3.2 * fs):int(3.5 * fs)] = 0   # silence
    return np.stack([L, R], 1).astype(np.float32)

print("\n1) Comparaison plugin (C++) ↔ Audio Lab (code web d'origine)")
cases = [
    ("Réglages par défaut", P()),
    ("Voix transparente", P(thr=-22, ratio=3, knee=8, atk=0.010, rel=0.080, mk=4)),
    ("Punch batterie", P(thr=-18, ratio=4, knee=2, atk=0.040, rel=0.050, mk=3)),
    ("Pompage volontaire (RMS)", P(thr=-40, ratio=8, knee=0, atk=0.001, rel=0.400, mk=10, det="rms")),
    ("Side-chain anti-kick (HPF 150 Hz)", P(thr=-24, ratio=4, knee=6, atk=0.005, rel=0.120, mk=3, scType="hpf", scF=150)),
    ("Side-chain LPF 300 Hz, Q 2", P(thr=-30, ratio=6, knee=4, atk=0.002, rel=0.200, scType="lpf", scF=300, scQ=2)),
    ("Side-chain cloche +6 dB 3 kHz", P(thr=-28, ratio=5, knee=10, atk=0.0005, rel=0.150, scType="bell", scF=3000, scQ=1.5)),
    ("Écoute du side-chain", P(thr=-28, ratio=5, scType="hpf", scF=2000, scListen=True)),
    ("Lookahead 10 ms + RMS + knee 20", P(thr=-35, ratio=2.5, knee=20, atk=0.2, rel=1.0, mk=24, det="rms", look=0.010)),
]
for fs in (44100, 48000, 96000):
    x = music(fs)
    for name, prm in cases:
        oj, oc = run_both(fs, x, prm)
        d = np.max(np.abs(oj - oc)); peak = np.max(np.abs(oj))
        check(d <= 1e-6 * max(1, peak), f"{fs} Hz · {name} : écart max {d:.2e}")
# blocs de taille variable (les DAW n'envoient pas forcément 128 échantillons)
x = music(48000)
for b in (1, 64, 512, 1024, 4096):
    oj, oc = run_both(48000, x, cases[1][1], block=b)
    check(np.max(np.abs(oj - oc)) <= 1e-6, f"bloc de {b} éch. : identique au web")

def core_only(fs, x, prm, block=512):
    return run_both(fs, x, prm, block)[1]

print("\n2) Courbe statique (seuil, ratio, knee) — niveau constant, mesure après stabilisation")
fs = 48000
def static_out(thr, ratio, knee, mk, lvl_db):
    n = int(fs * 0.3); a = 10 ** (lvl_db / 20); s = np.full(n, a, np.float32)
    y = core_only(fs, np.stack([s, s], 1), P(thr=thr, ratio=ratio, knee=knee, atk=0.001, rel=0.05, mk=mk))
    return 20 * np.log10(np.abs(y[-1, 0]))
def theory(di, thr, r, kn, mk):
    if r >= 20: return min(di, thr) + mk      # ∞ : vrai limiteur (plafond = seuil + make-up), depuis la 2.0.0
    hk = kn / 2; ex = di - thr
    if ex <= -hk: o = di
    elif ex < hk and kn > 0: o = di + (1 / r - 1) * (ex + hk) ** 2 / (2 * kn)
    else: o = thr + (di - thr) / r
    return o + mk
for thr, r, kn, mk in [(-20, 4, 0, 0), (-30, 2, 10, 6), (-12, 100, 0, 0), (-40, 8, 20, 3)]:
    errs = []
    for lvl in range(-60, 1, 3):
        errs.append(abs(static_out(thr, r, kn, mk, lvl) - theory(lvl, thr, r, kn, mk)))
    rlab = "∞" if r >= 100 else str(r)
    check(max(errs) < 0.01, f"seuil {thr} dB, ratio {rlab}:1, knee {kn} dB, makeup +{mk} : écart max {max(errs):.3f} dB")

print("\n3) Temps d'attaque et de release (constante de temps à 63 %, signal en échelon)")
def step_times(atk_ms, rel_ms):
    n = int(fs * (atk_ms * 8 + rel_ms * 8) / 1000) + fs // 10
    on = int(0.02 * fs); off = on + int(atk_ms * 8 / 1000 * fs) + 10
    x = np.full(n, 0.01, np.float32); x[on:off] = 1.0   # -40 dBFS → 0 dBFS → -40 dBFS
    y = core_only(fs, np.stack([x, x], 1), P(thr=-40, ratio=10, atk=atk_ms / 1000, rel=rel_ms / 1000))[:, 0]
    gr = -20 * np.log10(np.abs(y) / np.abs(x))          # réduction appliquée (dB)
    tgt = gr[off - 1]
    def cross(seg, lvl, up):
        k = int(np.argmax(seg >= lvl if up else seg <= lvl))
        a0, a1 = seg[k - 1], seg[k]
        return (k - 1 + (lvl - a0) / (a1 - a0)) / fs * 1000
    # l'échantillon d'indice 0 du segment porte déjà un pas de lissage : origine à -1
    ia = cross(np.concatenate([[0.0], gr[on:]]), 0.632 * tgt, True)
    ir = cross(np.concatenate([[gr[off - 1]], gr[off:]]), 0.368 * gr[off - 1], False)
    return ia, ir
for a, r in [(0.1, 10), (1, 50), (10, 80), (40, 400), (200, 1000)]:
    ia, ir = step_times(a, r)
    check(abs(ia - a) <= 0.02 * a + 0.005 and abs(ir - r) <= 0.02 * r,
          f"attaque {a} ms → {ia:.2f} ms · release {r} ms → {ir:.1f} ms")

print("\n4) Lookahead : retard du signal")
for look in (0.0, 0.0005, 0.005, 0.010):
    n = 4800; x = np.zeros(n, np.float32); x[1000] = 0.5
    y = core_only(fs, np.stack([x, x], 1), P(look=look))[:, 0]
    d = int(np.argmax(np.abs(y))) - 1000
    check(d == int(np.floor(look * fs + 0.5)), f"lookahead {look*1000:.1f} ms → {d} éch. ({d/fs*1000:.2f} ms)")

print("\n5) Filtres du side-chain (réponse en fréquence, écoute SC)")
from scipy.signal import freqz
def rbj_db(kind, f0, q, f):
    w = 2 * np.pi * f0 / fs; c, sn = np.cos(w), np.sin(w); al = sn / (2 * q)
    if kind == "hpf": b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == "lpf": b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; a = [1 + al, -2 * c, 1 - al]
    else:
        A = 10 ** (6 / 40); b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    _, h = freqz(b, a, worN=[2 * np.pi * f / fs]); return 20 * np.log10(abs(h[0]))
def sc_gain(kind, f0, q, f):
    n = int(fs * 0.5); t = np.arange(n) / fs; s = 0.1 * np.sin(2 * np.pi * f * t)
    y = core_only(fs, np.stack([s, s], 1), P(scType=kind, scF=f0, scQ=q, scListen=True))[:, 0]
    seg = y[n // 2:]; ref = s[n // 2:]
    return 20 * np.log10(np.sqrt(np.mean(seg ** 2)) / np.sqrt(np.mean(ref ** 2)))   # rapport des valeurs efficaces
for kind, f0, q, f, exp_db in [("hpf", 150, 0.7071, 150, -3.01), ("hpf", 150, 0.7071, 37.5, -24.1),
                               ("lpf", 1000, 0.7071, 1000, -3.01), ("lpf", 1000, 0.7071, 4000, -24.1),
                               ("bell", 3000, 1.5, 3000, 6.0), ("bell", 3000, 1.5, 300, 0.0)]:
    g = sc_gain(kind, f0, q, f); th = rbj_db(kind, f0, q, f)
    check(abs(g - th) < 0.05 and abs(th - exp_db) < 0.6,
          f"{kind.upper()} {f0} Hz Q {q} : {g:+.2f} dB à {f} Hz (théorie numérique {th:+.2f}, analogique {exp_db:+.1f})")

print("\nRÉSULTAT :", "tout est conforme" if OK else "des écarts à corriger")
sys.exit(0 if OK else 1)
