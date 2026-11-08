# rdp_timing v34d — ロード系列（L）の再測定

v34 は R（矩形）を保存した後、L（ロード）の途中で止まり L の結果が残りませんでした（L は系列の最後にしか保存していなかったため。
v23 でも RDP を止めた Load_TLUT が原因とみています）。その後に走らせた v35 が最初のコマンドでタイムアウトしたのは、止まった RDP が
リセットでは戻らないためです（**電源を入れ直してから** v35 を再実行してください）。
v34d は残りのロードを測ります。CI4 は 2 回 RDP を止めたので最後に回し、テクスチャ画像の宣言も libultra の _4b マクロと同じ（Load_Tile: 8 bit・半分の幅、Load_Block: 16 bit・幅 1）にしました。電源を入れ直してから実行してください。
タイル fmt 0 / size 0 を TMEM 0x100、Load_TLUT、Sync_Pipe）にして最後に回しました。4 KB ちょうどのロード（RGBA32 32x32、I8 64x64）は
2 KB に縮めています。`L` 行の形式は v34 の README のとおり。`ares_reference_v34d.txt` = ares series v9。

作: Nekokabu
