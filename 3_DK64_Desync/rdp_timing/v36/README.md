# rdp_timing v36 — ゲームの三角形が合成より 10〜20% 高い理由の候補

v22〜v33 で、ゲームのリストの三角形は「同じスパン統計の合成三角形」より 10〜20% 高く、モードビット・コマンドの語・傾き・テクスチャ形式・
ステップ・バッファの MB 配置では説明できていません（v23/v24/v27/v28/v35）。ダンプしたリストを数えると、ゲームの三角形の行の
**15〜43% はシザーの外に丸ごと出て**（BAR の軽いフレームで 33〜43%、DK64 d6373 で 31%）、画素の 10〜28% が部分的に切られています。
ares のモデルはシザー外の行・画素をコスト 0 にしています。ほかに合成で未測定なのは、ほぼ水平なエッジ（1 行 8 px ずれる）、面積 0 の三角形、
2 KB ページに揃っていないバッファ（DK64: カラー +576 B、Z +1536 B）、テクスチャ軸の回転・T 方向の縮小（テーブルと v27 は S 方向だけ）です。
BAR の 2 サイクルモード、320 幅、1 三角形 8 スパン、幅 4 / 13 / 32 / 64、VI オフ、8 MB、実機で約 2 分。

- `bar_base`（基準）、`bar_clip_right` / `bar_clip_left`（全スパンの右半分 / 左半分がシザー外）、`bar_clip_bottom`（行の下半分が外）、
  `bar_offscreen`（全部シザーの右外）、`bar_apex_clipr`（本物の三角形の右半分が外）
- `bar_shear8`（全エッジ 8 px/行）、`bar_degenerate`（面積 0）
- `bar_align_dk`（カラー +576 B / Z +1536 B: DK64 と同じずれ）、`bar_align_c1k` / `bar_align_z1k`（+1 KB）、`bar_align_c64`（+64 B）、`bar_align_ref`（揃っている）
- `bar_tex_rot90` / `bar_tex_rot45` / `bar_tex_min4t`（テクスチャ軸 90° / 45° 回転、T 方向に 4 texel/px）
- `dk_align_dk` / `dk_align_ref` / `dk_clip_right` / `dk_offscreen`（DK64 の 1 サイクル）、`wo_clip_right` / `wo_offscreen`（書き込みだけ）
`Y` 行の形式は v31〜v35 と同じ。`ares_reference.txt` = ares series v10（シザー外 = 0、配置・回転は無関係）。X 系列は `mgc_p4_4316`。

作: Nekokabu
