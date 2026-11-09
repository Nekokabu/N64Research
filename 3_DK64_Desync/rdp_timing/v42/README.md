# rdp_timing v42 — CPU / SP DMA の負荷を横で掛けながらゲーム・デモのリストを再生（パート 4 の −3.3% の切り分け）

MGC のパート 4（書き込みだけのテクスチャ三角形、640 幅）は、リストの再生（VI オフ、CPU 待機）では実機/ares 1.005、RSP の出力を追いかける
経路（v41 fifo）も ares の見積もりが 0.7% 以内なのに、デモの実フレーム（実機タスクログ）では ares が 3.3% 速い。残る違いは
**CPU がフレームの間ずっと動いている**（パート 4 だけ cpu_gap がフレームいっぱい）ことなので、v10〜v12 でゲームのリストにやったのと同じ
CPU / SP DMA の負荷を横で掛けながら再生します。VI オフと各タイトルの VI モードの両方。8 MB、実機で約 4 分。

- リスト: MGC パート 4（3 本）、パート 5 / 3 / 2（各 1 本）、BAR 2 本、DK64 2 本
- variant: `full`（RDRAM から）/ `fifo1k`（v41 と同じ、RSP が 1 KiB ずつ書いて DP_END を進める。DK64 は空きが無いので無し）
- 負荷（`X` 行の 11 列目）: `none` / `cpu_chase`（依存するキャッシュミス）/ `cpu_icache`（命令フェッチ）/ `cpu_random_write`（フィル + ライトバック）/
  `cpu_mixed`（ロード・ストア・ALU・FPU）/ `cpu_uncached_write32` / `sp_dma_64B` / `sp_dma_512B`（RSP の DMA。fifo1k では RSP を使うので無し）
- `X` 行: v22〜v41 の 9 列 + 負荷の名前、負荷側の ticks、負荷の操作数、1 操作あたりの ticks。`ares_reference.txt` = ares series v12。
  `python3 compare.py 実機ファイル` で負荷ごとの倍率（none 比）を実機と ares で並べます。

見方: パート 4 のリストで CPU 負荷の倍率が ares より実機で大きければ（v10〜v12 では Z ありのリストで合わせた係数なので、書き込みだけの
スパンには別の係数が要る可能性）、それがデモの −3.3% の中身です。

作: Nekokabu
