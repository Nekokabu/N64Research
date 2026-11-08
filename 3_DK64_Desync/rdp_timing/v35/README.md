# rdp_timing v35 — カラーバッファと Z バッファの RDRAM 上の配置

v24 で「カラーと Z が同じ 1 MB にあると 10〜17% 遅い」（2 サイクル texture+blend+Z、幅 4 / 12 / 32）と分かっていますが、モデルには
この項が無く、4 MB のゲームの多くはカラーと Z を同じ 1 MB に置きます。同じ MB の中の距離、モード、640 幅での効き方を測り、
モデルに入れる規則（距離・モード依存）を決めます。VI オフ、8 MB 必須、実機で約 2 分。幅 4 / 13 / 32 / 64 px、1 三角形 8 スパン、ランダム配置。

- `bar_sep_1m` / `bar_sep_2m`（別の MB、基準）、`bar_adjacent`（Z がカラーの直後、150 KB 先）、`bar_sep_300k` / `_512k` / `_768k` / `bar_same_256k`
  （同じ MB の中の距離）、`bar_cross_1m`（カラーが 4 MB 境界をまたぐ）、`bar_zbelow`（Z がカラーの下）: BAR の 2 サイクルモード
- `dk_sep_1m` / `dk_adjacent` / `dk_sep_512k`: DK64 の 1 サイクルモード
- `blend_sep_1m` / `blend_adjacent`: 1 サイクルブレンド（Z 無し）— Z のアドレスは効かないはず
- `wo_sep_1m` / `wo_adjacent`: 書き込みだけ — どちらも効かないはず
- `bar_load8_sep`: 8 三角形ごとに Load_Block（テクスチャのソースがカラーと同じ MB）
- `bar640_sep_1m` / `bar640_adjacent`: 640 px 幅のカラーバッファ（0x500000）で Z が別 MB / 直後
`Y` 行の形式は v31〜v33 と同じ。`ares_reference.txt` = ares series v9（配置の項が無いので配置によらず同じ値）。

作: Nekokabu
