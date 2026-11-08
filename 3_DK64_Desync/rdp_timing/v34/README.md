# rdp_timing v34 — 矩形（Fill / Texture / Copy）とテクスチャロードの単価

残っている種別ごとの残差: v15 のコマンド種別分解で **BAR のクラッシュシーンの矩形が実機/ares 1.01〜1.09、DK64 のロードが 1.06〜1.07**
（他は 0.98〜1.03）。矩形とロードのテーブルは v9〜v13 で数サイズしか測っていないので、サイズ・モード・前後のコマンドを振って測り直します。
VI オフ、8 MB 必須、実機で約 4 分（R が約 3 分、L が約 1 分）。X 系列は `mgc_p4_4316` の full / tri_only だけ（継続性）。

## R: 矩形（1024 個、ランダム配置、サイズ 1x1 / 2x2 / 4x4 / 8x8 / 16x16 / 32x32 / 64x8 / 8x64 / 64x64 / 320x1 / 320x8 / 128x128、大きいものは個数を減らす）
- `fill`（Fill_Rectangle）、`tex1` / `tex2`（Texture_Rectangle 1 / 2 サイクル、書き込みだけ）、`copy`（コピーモード、dsdx 4）、`texflip`（0x25）
- `tex1_blend`（IMAGE_READ + ブレンド = HUD / フォント）、`tex1_z`（+ プリミティブ Z で比較・更新）、`tex2_blendz`（BAR のモード + プリミティブ Z）
- `tex1_loadtile` / `copy_loadtile`: 矩形ごとに Sync_Load + Load_Tile（矩形のサイズ、最大 32x32）+ Sync_Tile（グリフ描画の形）
- `tex1_syncpipe` / `tex1_settile`: 矩形ごとに Sync_Pipe / Set_Tile + Set_Tile_Size
- `fill640` / `tex1_640` / `tex1_blend640`: 640 px 幅のカラーバッファ（0x600000）
`R` 行: case, w, h, 個数, 画素数, DPC_CLOCK（3 回の最小）, ticks, clk/矩形, clk/画素, VI プローブ。

## L: ロード（512 回、毎回 Sync_Load + Set_Tile 7 + Load_* + Sync_Tile）
- Load_Tile: RGBA16 8x8 / 16x16 / 32x32 / 64x32、RGBA32 32x32、I8 32x32 / 64x64、CI4 64x64、CI8 64x32
- 同じ RGBA16 32x32 / 8x8 をソース画像の幅 320 / 640（行の間隔が広い）から
- Load_Block: RGBA16 8x8 / 32x32 / 64x32、CI8 64x32、CI4 64x64、RGBA32 32x32
- Load_TLUT: 16 / 256 エントリ
- `*_draw`: ロードごとに 4x4 の書き込みだけ三角形を 1 個挟む（ロードが描画と重なるか）、`*_nosync`: Sync_Tile 無し
`L` 行: case, 種別, テクセル数, バイト数, 回数, DPC_CLOCK, ticks, clk/ロード, clk/バイト, VI プローブ。

`ares_reference.txt` = ares series v9（同じ ROM）。実機/ares をサイズ別に並べれば、矩形・ロードの固定費と 1 画素 / 1 バイトあたりの単価が
テーブルとどこで違うかが出ます。

作: Nekokabu
