"""Bir deney dosyasini (P1_..., T3P3_..., LOG_...) okuyup ozet tablo ve grafik uretir.

Kullanim:
    python plot_run.py P1_20261118_1656.txt [P1_20261118_1656_2.txt ...]

Ayni deneyin parcalari (_2, _3 ...) birlikte verilirse birlestirilir.
Cikti: <ilk dosya>.png ve konsolda faz (basamak) bazinda ozet.
"""
import io
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def load(paths):
    frames, notes = [], []
    for p in paths:
        lines = Path(p).read_text(encoding="latin-1").splitlines()
        hdr = next(l for l in lines if l.startswith("date,")).split(",")
        rows = [l for l in lines if l and not l.startswith("#") and not l.startswith("date,")]
        notes += [l for l in lines if l.startswith("#")]
        frames.append(pd.read_csv(io.StringIO("\n".join(rows)), names=hdr))
    df = pd.concat(frames, ignore_index=True)
    df["ts"] = pd.to_datetime(df.date + " " + df.time, format="%d/%m/%Y %H:%M:%S")
    return df.sort_values("ts").reset_index(drop=True), notes


def summary(df):
    def tail_mean(x):
        return x.iloc[-300:].mean()          # basamagin son 5 dakikasi
    g = df.groupby("phase").agg(
        basla=("ts", "min"), u=("u_cmd", "median"),
        hava=("T_air_dht", tail_mean), yuzey=("T_heatsink", tail_mean),
        duvar=("T_wall", tail_mean), ortam=("T_ambient", "mean"),
        nem=("RH_dht", tail_mean), yuzey_max=("T_heatsink", "max"),
        koruma_s=("safety", lambda s: int((s != 0).sum())))
    g["hava_eksi_ortam"] = g.hava - g.ortam
    return g


def main(paths):
    df, notes = load(paths)
    gaps = df.ts.diff().dt.total_seconds()
    print(f"{len(df)} satir, {df.ts.min()} - {df.ts.max()}")
    print(f"atlanan saniye: {int((gaps[gaps > 1] - 1).sum())}, guvenlik satiri: {int((df.safety != 0).sum())}")
    pd.set_option("display.width", 200)
    s = summary(df)
    num = s.select_dtypes("number").columns
    s[num] = s[num].round(2)
    print(s.to_string())

    fig, ax = plt.subplots(3, 1, figsize=(14, 9), sharex=True,
                           gridspec_kw={"height_ratios": [3, 1.3, 1.3]})
    ax[0].plot(df.ts, df.T_heatsink, color="#F5A35C", lw=1, label="Peltier ic yuzeyi")
    ax[0].plot(df.ts, df.T_air_dht, color="k", lw=1.6, label="Kabin havasi (DHT22)")
    ax[0].plot(df.ts, df.T_wall, color="#9B59B6", lw=1, label="Karsi duvar")
    ax[0].plot(df.ts, df.T_ambient, color="#3498DB", lw=1, label="Dis ortam")
    ax[0].axhline(80, color="r", ls=":", lw=1, label="Koruma 80 C")
    ax[0].set_ylabel("Sicaklik (C)")
    ax[0].legend(loc="upper right", ncol=5, fontsize=9)
    u = df.u_cmd / 10.0
    ax[1].fill_between(df.ts, 0, u.clip(lower=0), color="#F5A35C", step="post", label="isitma")
    ax[1].fill_between(df.ts, 0, u.clip(upper=0), color="#3FA9F5", step="post", label="sogutma")
    ax[1].set_ylabel("Peltier (%)")
    ax[1].set_ylim(-105, 105)
    ax[2].plot(df.ts, df.RH_dht, color="#2E86C1", lw=1)
    if "door" in df.columns:                      # kapak acik araliklari (D7 anahtari)
        op = df.door.fillna(0).astype(int) == 1
        for a in ax:
            a.fill_between(df.ts, 0, 1, where=op, transform=a.get_xaxis_transform(),
                           color="orange", alpha=0.15, linewidth=0)
    ev = df[df.event != 0]
    for t in ev.ts:
        for a in ax:
            a.axvline(t, color="g", lw=0.8, alpha=0.6)
    ax[2].set_ylabel("Bagil nem (%)")
    for a in ax:
        a.grid(alpha=0.3)
    fig.suptitle(Path(paths[0]).name)
    fig.tight_layout()
    out = Path(paths[0]).with_suffix(".png")
    fig.savefig(out, dpi=130)
    print("grafik:", out)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
