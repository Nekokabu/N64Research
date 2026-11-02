# rdp_timing v30 — MGC 2011 パート 4 のリストは何が高いのか

v29 / mgc_tasklog の結果: 実機の RDP は ares（series v8）より **パート 4 のリストで 17〜27%、パート 5 で 8〜12%、パート 1・6 で 12〜20%**
掛かり、パート 2 は −2%（RSP・CPU は一致、フレームは両方とも RDP 律速）。パート 4 のリストは 0E 三角形（シェード+テクスチャ、Z 無し、
ブレンド無し、IMAGE_READ 無し）が 2200 個、テクスチャは **CI8 + TLUT**（64x32、Load_Block）、Set_Other_Modes 0x0082CAF0F0A4000
（1 サイクル、パース補正、バイリニア、BI_LERP0、EN_TLUT、KEY_EN、ノイズディザ、FORCE_BLEND）。合っているパート 2 は RGBA16 32x32 です。
v30 はリストの属性を 1 つずつ変えて再生し、実機/ares の差がどの属性に付いているかを見ます（約 2 分、8 MB）。

- リスト: `mgc_p4_3794` / `4316` / `4544`、`mgc_p1_0152`、`mgc_p5_5075`、`mgc_p6_5810`、対照 `mgc_p2_1802`。
- variant: full / tri_only（VI オフとデモの VI モード）、om_no_tlut（EN_TLUT オフ: CI8 をそのまま 8 bit で読む）、
  om_tlut_on（EN_TLUT オン: パート 2 の RGBA16 に TLUT を掛ける。色は壊れるがトラフィックは同じ）、om_no_dither、om_point、
  om_no_bilerp（BI_LERP0/1 オフ）、om_no_persp、om_no_key（KEY_EN オフ）、om_no_fblend（FORCE_BLEND オフ）、om_no_cvg、
  om_2cyc（1 → 2 サイクル）、tri_flat（傾き 0）、tri_noshade（0E → 0A テクスチャだけ）、tri_notex（0E → 0C シェードだけ、テクスチャ無し）、
  tri_edge（0E → 08 エッジだけ）。
- `X` 行の形式は v24〜v29 と同じ。`ares_reference.txt` = ares series v8。`python3 compare.py 実機ファイル v8`（`v30runs/v8.txt` に置く）。

作: Nekokabu
