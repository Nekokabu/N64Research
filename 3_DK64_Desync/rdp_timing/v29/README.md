# rdp_timing v29 — MGC 2011 デモのフレーム（パート 4・5）を実機で再生

MGC 2011 デモは ares（series v8）でパート 4（スピーカーの部屋）が実機より約 12%、パート 5（ガラスの格子）が約 8% 速く進み、
RETRO ロゴの時点で映像が音楽の約 7 秒先に出ます（パート 1〜3・6 は ±3%）。ares ではどちらのパートも **RDP 律速**
（CPU は DP 割り込みを待って 0.005 VI 後に次のフレームを起動）なので、まず RDP のリストそのものを実機で測ります。
v15 で測った MGC のリストはパート 1・2 のもの（実機/ares 1.00〜1.03）で、パート 4・5 は未測定でした。

- リスト（ares の Sync_Full 番号。全部 640x480 16 bit、カラー 0x600000 / Z 0x46A000、そのままの配置で再生）:
  `mgc_p1_0152`（パート 1 の家、CPU 律速の対照）、`mgc_p2_1802`（崖）、`mgc_p3_2555`（玉）、
  **`mgc_p4_3794` / `4055` / `4316` / `4544` / `4772`**（パート 4 の各シーン）、**`mgc_p5_5075` / `5360` / `5549`**（パート 5 の各シーン）、
  `mgc_p6_5810`（RETRO ロゴ）。
- variant: full / no_tri / tri_only / no_load / no_rect / state_only / om_1cyc（全部 1 サイクルなので full と同じはず）/
  om_no_imrd（IMAGE_READ オフ）。full / no_tri / tri_only はデモの VI モード（0x3242: 640x480 インターレース、aa2、VI_WIDTH 1280）でも。
- `X` 行の形式は v24/v25 と同じ。`ares_reference.txt` = ares series v8。`python3 compare.py 実機ファイル v8`
  （`v29runs/v8.txt` に ares_reference.txt を置く）。8 MB 必須、約 1 分。SD に `rdp_timing/rdp_timing_v29_NNN.txt`。

パート 4 のリストは 60〜86 万画素 / 2.7〜3.6 万スパン / 1300〜2000 三角形で、サイクルの 75〜82% が **1CYC16（ブレンド無し・Z 無し、
テクスチャ付き）** のスパン（平均 20 px/スパン）。パート 5 は 1CYC16_BLEND 41〜44% + 1CYC16 25〜29% + 1CYC16_ZPASS 12%。
パート 2・3（合っている）は 1CYC16_BLEND が主です。実機/ares が 1.1 前後なら RDP モデル（1CYC16 のテクスチャスパン）の問題、
1.0 なら RSP/CPU 側（mgc_tasklog で切り分け）です。

作: Nekokabu
