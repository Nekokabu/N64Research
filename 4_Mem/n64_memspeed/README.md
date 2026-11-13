# n64_memspeed – N64 RDRAM テストROM (libdragon)

起動時のメニューでモードを選びます。

- **A ボタン: RDRAM アクセス速度測定** – CPU (VR4300) と RSP DMA から RDRAM の速度を計測
- **B ボタン: RDRAM 全領域 読み書きテスト** – 搭載メモリ全バイトをパターン書き込み/読み戻しで検査

結果はどちらも SD カードに連番テキストとして保存します（速度: `n64_memspeed_v1_NNN.txt`、読み書き: `n64_ramtest_v1_NNN.txt`、`sd:/n64_memspeed/` 配下、ディレクトリが作れない libdragon ビルドではルート直下）。既存ファイルは `stat()` で確認し空いている番号を使うので上書きされません。同じテキストは画面と USB デバッグログ (`debugf`) にも出力されます。
テスト後は A / B で再実行、START でメニューに戻ります。

## B: 全領域 読み書きテスト (memtest_kernel.S)

プログラム自身・ヒープ・スタック・フレームバッファが載っている領域も含めて **RDRAM の全バイト** を検査するため、
テスト本体はアセンブラで書いた位置独立ルーチン (`memtest_kernel.S`, 約 1 KiB) を実行時に RSP DMEM (0xA4000000) にコピーして実行します
(libdragon の IPL3 と同じ手法)。手順は 4 KiB チャンクごとに

1. チャンクを RSP IMEM (0xA4001000) に退避
2. `00000000` `FFFFFFFF` `AAAAAAAA` `55555555` `ADDR` `~ADDR` の 6 パターンを書き込み → 読み戻し比較
3. IMEM から復元

なので非破壊で、終了後プログラムはそのまま続行します。実行中は割り込み禁止で、フレームバッファも検査されるため画面にノイズが出ます (8 MiB で 10〜30 秒程度)。
SP メモリは 64bit アクセス不可なので 32bit 命令のみ、スタック不使用、caller-saved レジスタのみ使用。
結果 (総エラー数、最初のエラーのアドレス/期待値/読み値、MiB ごとのエラー数) は DMEM 0xA4000F00 に書き戻され、C 側で整形して保存します。
4 MiB 機では 4–8 MiB 領域にメモリが無いことを簡易プローブで確認する行も出ます。

制限: パターン検査は 4 KiB チャンク単位なので、チャンクをまたぐアドレス線故障 (別チャンクへのエイリアス) は検出できません。

## A: 速度測定

## 計測内容

対象ウィンドウ (各 1 MiB)

| 領域 | 物理アドレス | 内容 |
|---|---|---|
| Region A | 0x00200000–0x002FFFFF | 本体 RDRAM (0–4 MiB) |
| Region B | 0x00600000–0x006FFFFF | 拡張パック (4–8 MiB)。未装着なら自動スキップ |

各ウィンドウで以下を測定します (3 回実行の最速値、割り込み禁止)。

- `pattern check` – アドレス依存パターンを uncached で書いて読み戻す整合性チェック
- `cached read/write ld/lw/sd/sw seq` – KSEG0 (キャッシュ経由) の 64bit/32bit 連続アクセス
- `cached memcpy 512K` – libc memcpy (ウィンドウ前半→後半)
- `uncached read/write ld/lw/sd/sw seq` – KSEG1 (キャッシュ無し) の連続アクセス
- `random cached (miss)` – 64B 間隔のランダム巡回ポインタチェイス。毎回 D-cache ミス → ライン(16B)フィルのレイテンシ
- `random uncached lw` – 同上を uncached で (純粋な RDRAM 読み出しレイテンシ)
- `RSP DMA RDRAM->DMEM / DMEM->RDRAM` – SP DMA レジスタ直叩き、4 KiB × 256 回

最後に `[MiB sweep]` として搭載メモリの全 MiB について読み出しのみ (cached / uncached, ld) の帯域を一覧します。
書き込みを伴わないのでコード・ヒープ・スタックが載っている MiB も含めて全域を測ります。

単位: `MB/s` = 10^6 byte/s、`ns/op` と `cyc/op` は 1 命令 (ld/sd=8B, lw/sw=4B) または DMA 1 回 (4 KiB) あたり。
`cyc` は CPU クロック (93.75 MHz) 換算。タイマは COUNT レジスタ (CPU/2 = 46.875 MHz)。

## 操作

メニューで A (速度) / B (読み書き) を押すと計測し、終了後に SD へ新しい連番ファイルとして保存します。
SD が使えない場合は画面と USB ログのみに出力します。

SD 書き込みは libdragon の `debug_init_sdfs()` (libcart) 経由なので
EverDrive 64 (V3 / X7 等)、SummerCart64、64drive で動作します。

## ビルド

```sh
export N64_INST=/opt/libdragon   # libdragon (trunk) をインストールした prefix
make
```

生成物: `n64_memspeed.z64` (ビッグエンディアン Z64)。ソースは `memspeed.c` (C 側) と `memtest_kernel.S` (DMEM 上で動く検査カーネル)。

## 安全策

書き込みテストを行うウィンドウは、実行時に `sbrk(0)` (ヒープ末尾) と
スタック領域 (RAM 末尾 64 KiB) に重ならないことを確認してから使用します。
重なる場合はその領域をスキップして理由を出力します。
