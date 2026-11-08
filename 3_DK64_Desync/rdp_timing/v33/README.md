# rdp_timing v33 — ゲームのモード（Z・ブレンドあり）での三角形の固定費と細いスパン

v32 で、書き込みだけ（IMAGE_READ 無し・Z 無し）の三角形は **コマンドの語の分の固定費**（1 スパンの三角形: エッジだけ 26 clk、
シェード 35、シェード+テクスチャ 65。テーブルは 20 / 23 / 23）と幅によらない **+1.2〜2.8 clk/スパン** がテーブルに無いと分かり、
0013 で入れました。ゲーム本体（BAR の 2 サイクル texture+blend+Z、DK64 の 1 サイクル）のモードは読み書きがあり、テーブルに
1 スパン +15 の固定費があるので同じ構造が隠れている可能性があります（v22 W は幅 4 / 12 / 32、8 スパン/三角形しか測っていません）。
BAR の intro は ares 2.0 VI/フレーム、実機 2.5 のまま残っていて、そのリストは 1 スパン約 10 px、小さい三角形が多数です。

Y 系列（合成、VI オフ、幅 1 / 2 / 3 / 4 / 6 / 8 / 13 / 16 / 32 px、320 px 幅のカラーバッファ、Z は最遠クリア = 全部通る）:
- `bar_rect` / `bar_rect_h1` / `_h2` / `_h4` / `_h32`: BAR の Set_Other_Modes（0x182c0f / 0x0c182078）、0F 三角形、1 三角形 8 / 1 / 2 / 4 / 32 スパン
  → clk/三角形 = T + H × s(w) で固定費 T とスパン単価 s(w) を分離
- `bar_apex` / `bar_apex_h32`: 本物の三角形（頂点が上、底辺 2w）
- `bar_edgez_h1`（09、6 語）/ `bar_shadez_h1`（0D、14 語）/ `bar_texz_h1`（0B、14 語）: コマンドの語数ごとの固定費
- `bar_rect_zfail` / `bar_rect_h1_zfail`: Z が全部落ちる（最近クリア）ときの細いスパンと固定費（DK64 の細い三角形 −17% の内訳）
- `dk_rect` / `dk_rect_h1` / `dk_rect_h32` / `dk_apex` / `dk_rect_zfail` / `dk_edgez_h1`: DK64 の 1 サイクル（0x882c0f / 0x00552078）で同じ
- `mgc_blend640` / `mgc_blend640_h1`: MGC パート 5 の 1CYC16_BLEND（読み書き、Z 無し）、640 幅、残っている −4.5% のため
- X 系列は `mgc_p4_4316` の full / tri_only（継続性）。

`Y` 行: case, w, H, triangles, spans, DPC_CLOCK（3 回の最小）, ticks, clk/スパン, clk/三角形, VI プローブ。8 MB 必須、実機で約 3 分。
解析は v32 の `compare.py` と同じ（幅ごとの実機/ares と、H = 1/2/4/8/32 からの T・s(w) のフィット）。`ares_reference.txt` は series v9。

作: Nekokabu
