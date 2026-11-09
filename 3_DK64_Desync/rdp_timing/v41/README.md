# rdp_timing v41 — RSP がリストを流しながら RDP が読むとき（fifo / xbus マイクロコードの経路）のコスト

デモのパート 4（書き込みだけのテクスチャ三角形）は、リストを RDRAM に置いてから RDP に読ませる再生（VI オフ）では実機/ares 1.005 なのに、
デモの実フレーム（実機タスクログ）では ares が 3.3% 速い。再生と実フレームの違いは、**RSP（F3DEX fifo）が描画リストを出力しながら
RDP が追いかけて読む**ことです。これを合成せず、同じリストを 3 通りの経路で RDP に渡して測ります。VI オフ、8 MB、実機で約 3 分。結果は variant ごとに SD に保存。v41_002 は最初のリスト（mgc_p4_4316: full 1410976 / pad1k 1412932 / fifo1k 1547402 = **+9.5%** / fifo512 +9.9%）の後、xbus のタイムアウト処理のフラグを戻し忘れて以降が全部 0 でした（修正済み）。

- `full`: 従来どおり RDRAM 上のリストを RDP が読む（v22〜v38 と同じ）
- `pad1k`: 同じリストを 1 KiB 単位の塊に切り（塊の末尾は No_Op で詰める）、RDRAM から読む（塊化そのもののコストの対照）
- `fifo2k` / `fifo1k` / `fifo512` / `fifo256`: 塊を RSP が DMEM 経由で **RDRAM の出力バッファに書き、DP_END を進める**（.fifo マイクロコード =
  MGC / BAR / DK64 の経路。RDP は RSP が書いたばかりの RDRAM を 1 塊遅れで読む）。出力バッファは MGC 0x680400、BAR 0x500000
  （DK64 は空きが無いので fifo 無し）。
- リスト: MGC パート 1〜6（9 本）、BAR 3 本、DK64 2 本。`X` 行の形式は v22〜v38 と同じ（clock = DPC_CLOCK、ticks = COP0）。
  `# XD` 行は転送内容の自己チェック（0 words differ であること）。`ares_reference.txt` = ares series v12（RSP の DMA 1 回ごとに
  RDP を 9 + 1.2 clk/8 B 止めるモデル: xbus1k +2〜4%、fifo1k +4〜9%）。`python3 compare.py 実機ファイル`。

見方: 実機の `fifo1k / pad1k` がパート 4 のリストで 1.03 前後なら、デモの −3.3% は「RSP の出力を追いかけて読むコスト」で、ares の
DMA 競合モデルの大きさ（現状 +7〜9%）を実機に合わせ直す修正になります。xbus（RDP が DMEM から読む）の variant は v41_002 で実機の RDP が最初の塊で止まった（DP_STATUS 0x760、CURRENT 0x7C0、次の DP_START/DP_END が保留のまま）ので外しました。
注意: ares（この系列のハーネス、上流の a774671d も同じ）は XBUS のコマンドを DMEM からバイト順を間違えて読むバグがあり、ハーネス側では直してあります
（`render.cpp`: `Memory::Writable` 基底クラス経由の読み出し → RSP の DMEM アクセサ経由に）。MGC / BAR / DK64 は fifo なので影響ありません。

作: Nekokabu
