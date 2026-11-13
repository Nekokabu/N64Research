# rdram_unmapped – RDRAM 未実装領域 読み出しテストROM

著者: Nekokabu <nekokabu@gmail.com>

## 目的

RDRAM アドレス空間 (物理 0x00000000–0x03EFFFFF) のうち RDRAM が実装されていない部分
(4 MiB 機: 0x00400000〜、8 MiB 機: 0x00800000〜) を読んだとき、実機が何を返すかを
アクセス幅ごとに確認する。エミュレータ側の変更「未実装領域の 32bit 読み出しを 0 にし、
8/16/64bit と揃えた」の裏付けを取るためのもの。期待値はすべて 0。

0x03F00000–0x03FFFFFF (RDRAM レジスタ) には触れない。

## 試験項目

| 区分 | 内容 |
|---|---|
| 自己診断 | 実装 RAM 上の既知パターン `8192A3B4C5D6E7F8` を同じ手順で読み、符号/ゼロ拡張込みで期待値と一致するか確認。NG なら試験結果自体が無効 |
| U | KSEG1 (uncached) で `lb`(+0,+1,+2,+3,+7) `lbu` `lh`(+0,+2,+6) `lhu` `lw`(+0,+4) `lwu` `ld`。結果は 64bit GPR 値 |
| U+P | 上と同じロードの直前に、実装 RAM から `DEADBEEFCAFEBABE` を uncached `ld` で読む (バスに残った値が返る「オープンバス」挙動の検出)。パターンと一致するバイトを含めば `NG(bus?)` と表示 |
| C | KSEG0 (cached)。D キャッシュライン (16 B) を無効化してから `lw`×4 / `ld`×2 / `lb` / `lh` でラインフィル |
| D | RSP DMA RDRAM→DMEM 64 B (DMEM は事前に `CC` で埋める)。SP DMA のアドレスは 24bit なので 16 MiB 未満のみ。転送が 0x00FFFFFF を越えないよう開始アドレスを 0x00FFFFC0 以下に制限 |
| SP DMA wrap | 0x00FFFFF8 から 16 B 転送し、後半 8 B が RDRAM 先頭 (0x00000000) と一致するか (24bit 回り込み) を確認。参考情報で NG には数えない |
| sweep | 未実装領域全体を 256 B ごとに `lw`(+0) と `ld`(+8)、8 パス。**VI 動作中** と **VI 停止 (VI_CTRL type=0, VI DMA なし)** の 2 条件で実行し、0 以外の件数・ppm・値の種類を比較。各条件の間は割り込み禁止 |
| stray values | sweep で出た 0 以外の値ごとに、条件別の出現回数・最初のアドレスと、実装 RAM および SP DMEM/IMEM 内でその値が置かれている場所 (最大 3 か所、.text/.data/.bss/heap/フレームバッファ付近/低位ベクタ等の区分付き) を表示。プログラム自身の記録テーブルとスタックは検索対象外 |
| write | 未実装アドレスへ `sw`×4 → 読み戻し (期待 0)。実装 RAM 側の 2 MiB 単位のエイリアス先候補を事前退避→変化確認→復元し、ミラーの有無を報告 |
| latency | uncached `lw` 1024 回の 1 回あたり CPU サイクル (実装 RAM と未実装領域) |

試験ブロック (各 8 B): 未実装先頭 (=搭載容量)、4 MiB 機では 0x00523450 / 0x007FFFF8、
0x00800000 (8 MiB 機は 0x00C00000)、0x00FFFFF8、0x01000000、0x02ABCDE8、0x03EFFFF8 (0x03EFFFFF を含む)。

全ロードはインラインアセンブラの 1 命令で発行し、割り込み禁止中に実行する。

## 出力

画面には自己診断・sweep・stray values・write・latency・最終判定を表示し、全ロードの詳細は
SD の `sd:/n64_memspeed/rdram_unmapped_v12_NNN.txt` (連番、上書きなし) に保存する。
同じ内容を 1 行ずつ USB ログと **ISViewer (エミュレータのログ出力)** にも出すので、
SD カードを扱えないエミュレータでもログウィンドウ / コンソールで全結果を確認できる。
ログ上では `===== BEGIN rdram_unmapped v1.2.1 report =====` 〜 `===== END ... =====` で 1 回分を囲む。
レポート冒頭の `log:` 行に、起動時に検出できた出力先 (USB / ISViewer) を表示する。
最終行が `ALL UNMAPPED READS RETURNED 0` なら期待どおり。

万一ロードでハングした場合に備えて、各ブロックの試験前に画面へ進捗 (`block 0x........`) を出すので、
停止した場合はその表示を記録すること。

## 制限

- エイリアス検出は 2 MiB (RDRAM 1 デバイス) 単位の候補のみ。それより細かい周期のミラーは検出できない。
- 結果は IPL3 (libdragon 製) が設定した RDRAM/RI 構成下のもの。

## ビルド

```sh
export N64_INST=/opt/libdragon
make        # -> rdram_unmapped.z64
```

## 変更履歴

- v1.2.1: ログで画面表示行が 2 重に出る問題を修正 (libdragon コンソールの stderr ミラーを `console_set_debug(false)` で無効化)。ログの内容は SD ファイルと同一になった。
- v1.2: SD に書く内容を ISViewer にも出力 (`debug_init_emulog()`)。BEGIN / END マーカーと出力先表示行を追加。出力ファイル名を `rdram_unmapped_v12_NNN.txt` に変更。試験内容は v1.1 と同一。
- v1.1: RSP DMA 試験が 16 MiB 境界を越えて 0x00000000 に回り込んでいた不具合を修正し、回り込みを独立した項目として追加。sweep を 256 B 間隔 × 8 パス × (VI 動作 / VI 停止) に変更。0 以外の値の出どころを実装 RAM から検索する項目を追加。出力ファイル名を `rdram_unmapped_v11_NNN.txt` に変更。
- v1.0: 初版。
