# rdp_timing v40 — VI（画面出力）による RDP の遅れを、スパンの種類・VI モード・表示バッファごとに測る

実機再生（v29、v37/v38）で MGC 2011 の VI モード（0x3242、640x480 インターレース、VI_WIDTH 1280）はパート 4〜6 のリスト（書き込みだけ・ブレンド）に
**まったく効かず**（game/vi_off 1.000、PAL 機は全部 1.000）、NTSC 機のパート 1〜3 のリスト（Z あり）だけ +3% でした。ares の VI 係数は
モードごとの一律の係数（v12/v15 で Z ありのリストから求めたもの）を全スパンに掛けていて、MGC では +2.5%（パート 4・5 の残り −1.7 / −2.0% と逆向き）。
VI の読み出しが RDP のどのメモリアクセスと競合するのかを直接測ります。VI オフ、8 MB、実機で約 2 分（画面は乱れます: 正常）。

- スパンの種類（1 プリミティブ 8 スパン、ランダム配置、Z は全部通る）: `bar_z`（BAR の 2 サイクル tex+blend+Z、幅 13）/ `bar_z_w32`（幅 32）/
  `dk_z`（DK64 の 1 サイクル）/ `blend320` `blend640`（IMAGE_READ + ブレンド、Z 無し: MGC パート 5 の 1CYC16_BLEND）/
  `wo_tex320` `wo_tex640`（書き込みだけの 1 サイクルテクスチャ: MGC パート 4。640 はデモと同じ 0x600000 の 640 幅バッファ）/
  `fill320` `fill640`（Fill 矩形 32x8）/ `load`（Load_Block 1024 texel RGBA16 × 512: TMEM ロードだけ）
- VI モード: オフ / `aa3_0x3302`（この ROM のコンソール表示）/ `bar_0x13012`（320 幅、aa0 + divot + dedither）/ `dk64_0x13016` /
  `hires_0x13052`（640x480i、VI_WIDTH 640）/ `mgc_0x3242_w1280`
- 表示バッファ（VI_ORIGIN）: `console`（この ROM の表示バッファ、先頭の 1 MB）/ `colour_buffer`（描いているカラーバッファそのもの = 同じ 1 MB）/
  `depth_buffer`（Z バッファ）。VI が RDP の書き込み先と同じ 1 MB バンクを読むときの競合（v35 の「同じ 1 MB」と同種）を見ます。
- `V` 行: case, VI モード, 表示バッファ, 幅, スパン/プリミティブ, プリミティブ数, スパン数, clock（3 回の最小）, clk/スパン, **VI オフ比**, VI プローブ。
  `ares_reference.txt` = ares series v11（ares の係数はモードだけで決まり、種類・表示バッファに依らない）。`python3 compare.py 実機ファイル`。

見方: 種類ごとの VI オフ比が ares の係数と違えば、VI 係数をスパン種別（Z 読み書き / IMAGE_READ / 書き込みだけ / ロード）で分ける修正になります。
`colour_buffer` / `depth_buffer` が `console` より重ければ、表示バッファと描画先が同じ 1 MB にある場合の係数（4 MB のゲームの多くが該当）です。

作: Nekokabu
