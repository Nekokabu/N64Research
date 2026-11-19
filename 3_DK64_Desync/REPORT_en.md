# Root-Cause Analysis of the Donkey Kong 64 Title Demo Desync and the Fix in ares

*English translation of REPORT.md (the Japanese file is the original).*

Target ROM: Donkey Kong 64 (J) ― NDOJ, CRC 053C89A7-A5064302, big-endian 32MB
Target emulator: ares (developed on master 9408cb43; also applies to master as of a774671d, 2026-10-26)
Updated: 2026-11-19 (series v26: patches 0018 to 0032, results of hardware test ROMs v44 to v74, task_timing v21 to v26 and logger v5; see "Console results of v74, task_timing v25 / v26 and logger v5; patches 0031 / 0032 (series v26): the VI uses the bus only while it reads the picture", "Console results of v72 / v73 and patch 0030 (series v25): the loss table read again from DPC_PIPEBUSY, spans blended without depth measured", "Console results of v70 / v71: the stops came from the padding the ROM put after each list's Sync_Full" (no change to the model), "Console results of v69 (the stop reproduced), patch 0029 (series v24), and a correction to how DPC_CLOCK was read", "Console results of v68, and the loss-per-activity model tried in the harness" (the series stays v23), "Console results of v67: what DK64 draws without the depth buffer is not cheap; a lead on the stops" (no change to the model), "Console results of v66: BAR's second scene loses as little with every pixel written" (no change to the model), "Console results of v65: beside spans that pass the depth test a transfer costs 10 to 12 clk at every width" (no change to the model), "Console results of v64: wide spans do not cost more; what lowered the loss was the depth test failing, not the span width" (no change to the model), "Console results of v63: what a CPU transfer costs, measured directly for each thing the RDP does" (no change to the model), "Console results of v62: a CPU transfer costs little while a texture is loaded; alpha compare and ATOMIC_PRIM are not the cause" (no change to the model), "Console results of v61: what a CPU transfer costs falls into groups by scene" (no change to the model), "Console results of v60: what a CPU transfer costs changes with the spans the RDP is drawing" (no change to the model), "Console results of v59: what a CPU cache line transfer costs, place by place; patch 0028", "Console results of v58: what a CPU bus transfer costs is decided by where in RDRAM it is" (no change to the model), "Console results of task_timing v24, and what is left of the BAR intro" (no change to the model), "Console results of task_timing v23: what the RDP loses while the RSP runs, patch 0027", "Console results of task_timing v22: a real task's transfers repeated without the task" (no change to the model), "Console results of v57: where the RDRAM side of an SP DMA is (VI on), patch 0026", "Console results of v56: the VI's multiplier on bus contention, patch 0025", "Console results of v55: where the RDRAM side of an SP DMA is" (no change to the model), "Console results of task_timing v21: real tasks of the BAR intro, patch 0024", "Console results of v54: the AI's audio DMA and the 344 B feed, patch 0023", "Console results of v53: the effect of placement in RDRAM, patch 0022", "Console results of v49, patches 0018 / 0019", "Console results of v50 / v51, patch 0020" and "Console results of v52, the DPC_CLOCK undercount, patch 0021")
Earlier update: 2026-10-31 (reflects results of hardware test ROMs v9 to v16. VI mode factors, dynamic bus contention, CPU instruction timing and SP DMA are now based on measured values)

## Conclusion (Summary)

- The cause of the desync is **not rendering timing but a difference in the series of frame intervals the game measures (number of VI retraces = frame delta)**.
- The DK64 DKTV demo replays "inputs recorded per game frame (stick X/Y, buttons, camera angle)", and the character's movement is **scaled by the delta (2 = 30fps, 3 = 20fps, ...)**.
- On the console, this demo in Jungle Japes runs with **a constant delta of 3 (one frame reliably exceeds 2 VI = 33.3ms)**.
  This is also supported by the fact that the DK64 Recompiled project fixes the delta to 3 in the same scene
  (`patches/patches_main.c`: `if (gameIsInDKTVMode() && current_map == MAP_JAPES) D_global_asm_80744478 = 3;`).
- In ares, the RDP execution time is **0**, so the frame time is determined by CPU time alone and fluctuates right around 33ms,
  which makes the **delta flip between 2 and 3**. Since the inputs are per frame, in a delta-2 frame DK's movement is only 2/3 as large,
  so he passes on the wrong side of the palm tree or falls without reaching the rope.
- As the fix, I added to the ares RDP a **model that estimates execution time from the command stream**, and made it delay the Sync_Full DP interrupt and the
  busy flag of DPC_STATUS by that amount. The model's factors are calibrated against the **frame display intervals of the console (N64Digital HDMI output
  recorded at 60fps with an EzRecorder 330)**. After the fix, a run through from power-on completes just like the console:
  climb the cliff, jump to the rope, swing across to the far side on the pendulum, and get launched from the barrel cannon.
- Updated 2026-10-27: Using the measurement results of the hardware test ROMs (v7/v8), I replaced the estimated correction factors with **measured values (placement penalty, per-mode contention, VI)**,
  and added the CPU cache transfer delay during RDP rendering (measured value) as `0003`.
- Updated 2026-10-31 (patches 0004 to 0007): With ROMs that replay the game's own display lists on the console (v9/v10/v12/v15) and instruction-timing ROMs for the
  CPU (v11/v14) and RSP (v16), I replaced all remaining degrees of freedom in the model with measured values.
  **VI mode factors** (RDP +11.7% in the game's AA+divot+dedither mode, +32% for hires, +3% in MGC's aa2 mode),
  **dynamic contention per bus master** (CPU fill 11.7 / instruction fill 8.5 / writeback 5 / SP DMA 1.22 clk/8B),
  placement penalty 15, and on the CPU side FPU (pipelined, div/sqrt 29 to 30, cvt 5), load/store hazards,
  a fix for the double charge in the recompiler, uncached read 32 / RCP register read 23, and SP DMA 38 cycles + 0.155/B.
  The DK64 demo is the best so far (score 22), the BAR crash scene matches, and the BAR intro remains on a knife edge
  at 2.0 vs 2.5 VI/frame on the console (see below).
- Added 2026-11-01 (bar_tasklog): With a patch that puts a resident logger into the game itself, I recorded every frame of the BAR intro on the console (normal run and a run with the CPU stopped),
  and decomposed the residual. CPU contention is within 8% of the model, and RSP/RDP are +2.7% (heavy) / +6.7% (light) on the console even with the CPU stopped. A per-type analysis of v15 showed that the
  −7% in the crash scene is the **Z-test pass rate** (which depends on the scene's draw order). Because a uniform factor is incompatible with DK64, this stays at series v7 (the patch is identical to v7).
- Added 2026-11-16, part 11 (series v23, patch 0028, ROM v59): what a CPU cache line transfer costs was measured place by place and put into the model (in the colour buffer's megabyte a data fill +2.4, an instruction fill +5.8 clk, and so on).
  **BAR's seg0 goes from 246 to 236 frames (console 231, -2.3%), every 60 VI block within one frame of the console.** DK64 7 / 371 / 42 (46 frames differ), MGC "@" +32 VI and BAR's seg1 316 (console 325) are left with ares slow.
  What remains is a difference between lists (beside DK64's list the loss is about 0.8 times); rdp_timing v60 measures it with the place taken out.
- Added 2026-11-16, part 12 (still series v23, ROM v60): with the place taken out, what a CPU transfer costs **changes with the spans the RDP is drawing** (beside spans with depth compare + update 12.7 clk at width 2 and 5.9 at width 64; without depth compare about 10.4 clk at every width).
  A rule built from this fits the synthetic triangles and the BAR / MGC lists, but leaves DK64's list at 10.2 clk (console 8.1) and makes BAR's seg0 worse in the game (236 to 248 frames): **not adopted**.
  Too few game lists have been measured to find the law, so rdp_timing v61 measures the loads beside 29 of them.
- Added 2026-11-16, part 13 (still series v23, ROM v61): beside 29 game lists what a CPU transfer costs **falls into groups by scene** (sparse data fills: BAR's first scene 9.9 clk, its second scene 8.8, DK64 8.7, the MGC demo's blended parts 9.0).
  The scenes where ares is still slow are the ones with 0.88 to 0.91 times the loss. A regression on what the lists hold picks the shares drawn with alpha compare, with ATOMIC_PRIM and blended without depth; tried in the harness it gives BAR seg1 316 -> 324 (console 325), MGC "@" +32 -> +17 VI and DK64's long frames 42 -> 37.
  The cause is not confirmed (and BAR's seg0 goes from 236 to 239), so **adoption is on hold**; rdp_timing v62 changes one bit at a time in the same lists to check.
- Added 2026-11-17, part 14 (still series v23, ROM v62; the ROM stopped after 24 of 37 variants): changing one thing at a time shows that **alpha compare does nothing and ATOMIC_PRIM 3 to 4% only** (the terms of v61's regression were not causes).
  Measured instead: **a transfer during a texture load costs about 4 clk** (about 9 otherwise). The regression over 53 lists puts about 22 clk on fill rectangles, which has not been measured directly.
  Loads + fills tried in the harness: BAR 237 / 323, DK64 9 / 381 / 34, MGC "@" +21 VI. rdp_timing v63 measures the loss per activity directly (**adoption after v63**).
- Added 2026-11-17, part 15 (still series v23, ROM v63 ran to its end): the loss per activity measured directly (12 clk during 320-pixel fills, so the regression's 22 was off; 4 clk during Load_Block; the same back to back and sparse for triangles).
  Weighted by where each list's time goes, these values give **DK64 and BAR's second scene within 0.1 clk on average with nothing fitted** (+1.1 to +1.7 clk with the constant). BAR's first scene comes out 1.3 clk low: 19 to 28% of its time is in depth-buffered spans wider than 64 pixels, which have not been measured.
  rdp_timing v64 measures wide spans and the other missing values; with the table complete this becomes patch 0029 (series v24).
- Added 2026-11-18, part 16 (still series v23, ROM v64 ran to its end): depth-buffered spans wider than 64 pixels do not cost more (6.3 to 6.9 clk); last time's reading was wrong.
  BAR's light frame taken apart: 12.5 clk during rectangles and 4.5 during loads (the direct values), **9.9 clk for the triangles alone** (about 7.5 from the table). Re-read, "the wider the span, the smaller the loss" in the synthetic lists was **more spans failing the depth test** (the triangles had one depth and were drawn over each other).
  What always passes costs 9 to 12 clk at every width, what fails 5.2 clk at 64 pixels. rdp_timing v65 measures passing spans directly.
- Added 2026-11-18, part 17 (still series v23, ROM v65 ran to its end): last time's reading confirmed directly. **Beside spans that pass the depth test a transfer costs 10 to 12 clk from 4 to 256 pixels**, and game lists lose 2.6 to 4.1 clk less when everything fails.
  The table rebuilt on passing / failing (nothing fitted) gives BAR's first scene (+0.2 clk) and the MGC lists (+0.5), but **BAR's second scene (+1.5) and DK64 (+1.0) come out high**: either ares' estimate of the pass rate is too high there or another property is at work.
  rdp_timing v66 compares game lists as they are with "everything written" and "nothing written".
- Added 2026-11-18, part 18 (still series v23, ROM v66; the ROM stopped after 2 of 7 lists, where v62 did): BAR's second scene loses the same with the depth compare off and every pixel written (8.9 -> 8.8, 8.8 -> 8.5 clk).
  Nearly every pixel passes there and ares' estimate of the pass rate is right: **the low loss is not the pass rate**. The two scenes the prediction misses spend 13 to 22% of their time drawing without the depth buffer, which may cost little in reality (not measured).
  rdp_timing v67 measures the lists split into what uses the depth buffer and what does not.
- Added 2026-11-18, part 19 (still series v23, ROM v67; the ROM stopped after 3 of 5 lists): what DK64 draws without the depth buffer costs 8.3 to 8.7 clk, not little (last time's reading was wrong). What is low in DK64 is the depth-buffered part: its effective pass rate is about 0.56 (ares' estimate, 0.77, is too high).
  BAR's second scene stays open (v67 did not reach it). The stops are probably RDP timeouts whose handler enabled interrupts with the VI off and crashed (the same crash was seen in the emulator; the timeouts on the console are not confirmed).
  rdp_timing v68 has the handler fixed and measures the remaining lists.
- Added 2026-11-18, part 20 (still series v23, ROM v68 ran to its end): in BAR's second scene a transfer costs 5.3 to 5.9 clk during what is drawn without the depth buffer, measured directly. With two corrections the table of **measured values alone (nothing fitted) gives an rms of 0.73 / 0.63 clk over 78 lists** (1.56 / 0.88 with the constants).
  In the harness: DK64's long frames 42 -> 35 and its VI +1.3% -> +0.8%, BAR's seg1 316 -> 321 (console 325), BAR's seg0 236 -> 238 (console 231), the MGC demo unchanged. **Next: the implementation in the core as patch 0029 (series v24).**
  rdp_timing v69 re-runs the sequences that stopped on the console to record the cause.
- Added 2026-11-18, part 21 (**series v24, patch 0029**, ROM v69; the ROM stopped at the 10th list, where v67 did): the model of part 20 (what a CPU transfer costs by what the RDP is doing) is in the core. The harness gives the values of the trial (DK64 long frames 42 -> 35, BAR seg1 316 -> 321, BAR seg0 236 -> 238, the MGC demo unchanged).
  v69 repeats the values of v67 (losses within 0.07 clk) and stops at the same place. No timeout line: the CPU itself stops, or a CPU exception. v70 (a diagnostic) is to find the command.
  **A flaw in the measuring was found**: DPC_CLOCK of a run with a CPU load contains the delay until the load loop notices the end of the list (DPC_PIPEBUSY on the console does not). Instruction fills were read 0.2 to 0.5 clk high; "sparse transfers cost more" (part 13) was mostly this delay.
- Added 2026-11-19, part 22 (still series v24, ROM v70 twice and v71): **the cause of the stops on the console is found.** It was the padding (10 to 127 No_Op words) my ROM put after the Sync_Full of every replayed list. The RDP stops reading 23 to 29 words after a Sync_Full and reads the rest from RDRAM, and executes it, when the next list is started (shown directly in v71).
  When a longer list replaces the old one, the middle of the new list is executed as commands. Without the padding all 13 variants ran; with it put back the same stop returned. Neither the games' lists nor the ares model are at fault.
  The rest of v69 is measured: DK64 loses as much with every pixel written (8.2 against 8.1 clk), so the reading of part 19 ("0.56 of it passes") is not supported. v72 measures the table again, read from DPC_PIPEBUSY.
- Added 2026-11-19, part 23 (**series v25, patch 0030**, ROMs v72 and v73 ran to the end): the loss table is read again from DPC_PIPEBUSY (data fills 0.03 to 0.1 clk lower, instruction fills 0.02 to 0.7), and the row for spans blended without depth, which held another kind of span, has measured values. rms over the 78 lists 0.62 / 0.45 clk (the table of 0029: 0.70 / 0.70).
- Added 2026-11-19, part 24 (**series v26, patches 0031 / 0032**, ROM v74, task_timing v25 / v26, logger v5): what BAR seg0 lacked was not CPU contention but about 0.03 VI per task that only the game has. The main cause is the VI's vertical blanking (the VI uses RDRAM only while it reads the picture; the replays run through a blanking, a short task in the game never meets one). The VI's load now applies inside the picture only, and the AI beside a running task and loads from the megabyte the VI shows follow the measurements. BAR seg0 239 to 230 (console 231), seg1 323 to 317 (325: worse), DK64 and MGC nearly unchanged. v74: the place of a CPU transfer is a megabyte.
  Harness: MGC "@" +34 -> +19 VI and start of part 4 +11 -> -1 VI, DK64 long frames 35 -> 34, BAR seg1 321 -> 323 (console 325), BAR seg0 238 -> 239 (console 231).
  The reading of part 22 that most of DK64's depth-compared spans pass was wrong and is corrected (the pass rate is not measured directly). Beside spans of 128 pixels and more the loss depends strongly on the spacing of the transfers (not in the model).
- Added 2026-11-16, part 10 (ROM v58, no change to the model): what a CPU cache fill costs the RDP is decided not by the drawing mode but by **where in RDRAM the fill reads** (an instruction fill in the colour buffer's megabyte costs 3 to 5 clk more).
  The model's constants are the values of where v42 measured (data fills in the list's megabyte, instruction fills in a megabyte the RDP does not use). In the games 86% of BAR's instruction fills are in the list's megabyte and 83 to 100% of the data fills of DK64 and the MGC demo in unused megabytes,
  which fits the direction of what is left (BAR fast, DK64 and MGC slow). rdp_timing v59 measures the value of each place.
- Added 2026-11-16, part 9 (task_timing v24, no change to the model): DP_END writes and where the output is written cost the RDP nothing. The console's DPC_CURRENT is a fetch position ahead of execution, so the difference in "waiting for commands" was not a real loss.
  The task by itself now agrees, so what is left of BAR's seg0 is what only the game adds, mostly the bus contention with the CPU (0.136 VI in frames 20 to 39). Beside BAR's lists a CPU instruction cache fill costs 22 to 38% more than the model (v42).
  Raising it for everything brings seg0 to 236 frames (console 231) but overshoots seg1, so rdp_timing v58 measures how the loss depends on what the RDP is drawing.
- Added 2026-11-16, part 8 (series v22, patch 0027, task_timing v23): **the RSP merely executing costs the RDP 0.82% of that time with the VI off and 1.43% in BAR's VI mode** (an empty loop and loops reading a register alike).
  A task waiting for room in its 8 KiB FIFO keeps the RSP running, and this is what BAR's real tasks were short of (task_timing's DP done: console 0.717 / 0.620 / 0.851 / 1.603 / 1.609 / 0.902 VI, now 0.711 / 0.618 / 0.848 / 1.596 / 1.595 / 0.914).
  BAR's seg0 goes from 253 to 246 frames (console 231). DK64 (5 / 376 / 40, 56 frames differ) and the MGC demo ("@" +41 VI) move away from the console: for those two another term is too dear (CPU contention is the candidate for DK64).
- Added 2026-11-15, part 7 (task_timing v22, no change to the model): a real task's SP DMA transfers repeated alone cost what ares's model says (console / ares = 0.91 to 1.01). The shortfall is not in the SP DMA terms but in
  (1) the RSP merely running, which costs the RDP about 0.9% of that time even without transfers, (2) the RDP waiting longer for commands on the console (0.0083 against 0.0059 VI), (3) a remainder of 0.008 to 0.009 VI in the heavy frames.
  Trying (1) in the harness improves BAR's seg0 (253 to 249) but worsens MGC and DK64, so it goes in once task_timing v23 has shown what exactly costs.
  v22's VI-on rows are void (the VI had no effect; a flaw of the ROM); v23 checks the VI and takes them again.
- Added 2026-11-15, part 6 (series v21, patch 0026, ROM v57): an SP DMA in the megabyte of the colour / depth buffer costs +4.7 / +4.3 clk, and in the megabyte the VI shows another +4 to 12 clk (320x240).
  21% / 38% / 21% of the BAR intro's reads are in those. DK64 8 / 381 / 33 (46 frames differ), BAR 253 / 326, MGC "@" +4 VI.
  The shortfall of real tasks (BAR's seg0) is not explained by it; task_timing v22 repeats a real task's transfers without the task.
- Added 2026-11-15, part 5 (series v20, patch 0025, ROM v56): measured again with the VI confirmed running, **how much the VI adds to a contention loss depends on the kind of transfer**
  (320x240 modes: SP DMA x1.23, CPU data fill x1.32, instruction fill x1.57; the MGC demo's mode: x1.09 / x1.13 / x1.31; the model had x1.28 / x1.07 for all).
  MGC: total +0.0%, "@" -3 VI (was -29). DK64 6 / 381 / 36, BAR 255 / 327 frames (seg1 better, seg0 not).
  What remains of seg0 is the VI multiplier of a real task's SP DMA (console 1.47, isolated transfers 1.23); v57 measures the transfers' place and back-to-back transfers with the VI on.
- Added 2026-11-15, part 4 (ROM v55, no change to the model): an SP DMA whose RDRAM side is in the megabyte of the colour or depth buffer costs +2 to 4 clk. The effect is small and does not explain the shortfall of real tasks.
  The VI-on rows of BAR / DK64 were taken with the VI not running (a flaw of the ROM) and are void; only the multiplier in the MGC demo's mode could be checked (console x1.07 to 1.10, ares x1.07).
  Re-reading the real-task breakdown, most of what remains is the VI's multiplier in the 320x240 modes (console 1.47, ares 1.27); v56 measures it with the VI confirmed running.
- Added 2026-11-15, part 3 (series v19, patch 0024, task_timing v21): six real graphics tasks of the BAR intro replayed on the console. **On the console the RSP is never more than one output buffer (8 KiB) ahead of the RDP**;
  ares let it get up to 39.6 KiB ahead and finish 2 to 9% early. The cause was START_VALID in DPC_STATUS being cleared at the next DP_END write; it now stays set until the RDP switches
  (the RSP's task ends 0.2 to 3.7% before the console's). BAR seg0 255 to 252 frames (console 231); MGC / DK64 unchanged. In the breakdown the RDP alone agrees, and what a real task's SP DMA takes
  from the RDP is only 64 to 89% of the console's in ares (the VI's multiplier too: console 1.39, ares 1.23), the main part of what remains. v55 measures why.
- Added 2026-11-15, part 2 (series v18, patch 0023, ROM v54): the AI's audio DMA takes 2.7 clk per sample from the RDP (5.4 to 5.9 with its buffer in the megabyte drawn into or of the command list),
  and the RSP feeding 344 B at a time costs what CPU-started transfers cost: a DP_END write costs nothing. That settles the SP DMA loss at the v52 figures (read 3.5 + 1.11, write 2.4 + 1.07 clk per 8 B),
  which replace the former constants (15 to 25% too high); the AI's loss and the VI factor of AA mode 2 on the whole list (console 1.026 to 1.035, formerly 1.023 to 1.026) come with them.
  **MGC total -0.3%, "@" -29 VI, every part within ±1.0% (logo -1.4%)**: a little behind v17's headline (+0.0%, -1 VI), but no fitted constant is left. DK64 / BAR hardly change.
- Added 2026-11-15 (series v17, patch 0022, ROM v53): replays with only the placement changed show that **a command list in the megabyte being drawn into costs about 9.5 clk per primitive**
  (colour and depth alike) and that the VI showing the megabyte of the command list costs 5.3 to 8.2 clk. Where the RSP's output buffer is makes no other difference, and the feed through the RSP costs what CPU-started transfers cost.
  The MGC demo keeps its command buffer (0x79xxxx) in the megabyte of one of its three colour buffers; with this modelled, **MGC total -0.3% to +0.0%, "@" -36 to -1 VI, every part within ±1.0%**.
  DK64's VI count +1.0 to +0.8%, BAR unchanged. The SP DMA constants stay at the real-task value (the pure bus loss measured by v52 / v53 would make MGC 0.8% fast); v54 (AI, 344 B feed) measures the difference.
- Added 2026-11-14, part 3 (series v16, patch 0021, ROM v52): the RSP's instruction timing matches the console in all 30 loops (within 0.006 cycles).
  **ares' DPC_CLOCK had been running 1.7 to 2.5% slow since 0002** (the remainder of every step was dropped: the console reads 1.0006 against the CPU's count, ares 0.975); fixed.
  The model's prices were fitted to that register, so its real time was 1.72% long, which was the main cause of "the lists agree but the games run 1% slow". The prices are recalibrated by 0.9828
  and the CPU bus-contention constants re-read from v42 with the fixed register. MGC total +0.9% to **-0.3%**, "@" +79 to **-36 VI**, DK64's differing frames 55 to 46, BAR seg1 +0.3 to -1.7%.
  The console's task log also shows the frame time changing by 1% with the place of the colour buffer drawn into (not so in ares). The next ROM, v53, measures the effect of placement in RDRAM.
- Added 2026-11-14, part 2 (series v15, patch 0020, ROMs v50 / v51): the console confirms that a triangle row above the scissor costs 9.0 clk and a row below
  nothing, and that a triangle without a span costs 23.5 to 76 clk. v49's depth-span prices had been measured with the depth buffer in the colour buffer's
  megabyte and are replaced by values measured in the games' layout (another megabyte). Narrow write-only spans are no longer widened by slanted edges (v32).
  The 12 replayed MGC lists: mean −0.05% / rms 0.84% / worst 1.8% (v14: −1.14 / 1.56 / 4.2%). In the game MGC is +0.9% overall and the "@" at +79 VI (v14: −4 VI):
  with the lists right, an excess of about 1% in the terms that act only in the game (the RSP side) is no longer cancelled. With the −9% of BAR's seg0, the RSP
  side is the next item.
- Added 2026-11-14 (series v14, patches 0018 / 0019, ROMs v44-v49): replaying the MGC lists one chunk of triangles at a time (v46) put the console-ares
  difference on **the large triangles that extend beyond the scissor (the screen)**. The console walks the pixels cut off on the major-edge side at 1 clk/px
  (2 in 2-cycle) and spends 11 clk on a row without a visible pixel (v47 / v48). That is now modelled (0018), the prices that were making up for it (+42 per
  write-only triangle, the surcharges on blend and depth spans) follow the synthetic frames, and the "Z compare without update + blend" span, 27 clk short, is
  fixed (0019, v49). MGC: every part within ±1.0%, the "@" at −2 VI. DK64's delta distribution moves a little toward the console; the BAR intro is +1.1% in seg1
  and still −9% in seg0 (open: its replayed lists agree to ±2.5%; the RSP finishes 7% earlier than on the console in its light frames).
  Re-reading v46 showed that triangle rows above the scissor cost about 10 clk each and rows below nothing (not adopted yet; v50 / v51 will confirm).
- Added 2026-11-09/10 (series v13, patches 0012-0017, ROMs v22-v43): the MGC 2011 demo was recorded on the console over all its parts with a resident
  logger (mgc_tasklog a/b/c); the drift of the closing "@" and its "clank" was fixed to ±5 VI by 0015 (a triangle's spans cover the union of the four
  sub-scanlines). Per-part residuals +0.2 / +0.9 / +2.4 / −1.7 / −2.0 / +1.9% (v36-v42 measured triangle properties, order, partial depth failure, RSP
  feeding and CPU contention; 0016's class-dependent VI factor rested on rows measured with the VI stopped and was reverted in 0017, re-measured by v43).
  The demo's memory bandwidth was counted: 71-112 MB/s of RDRAM demand (14-22% of the 500 MB/s peak), not saturated; the demo is bound by the RDP's
  occupancy (80-93%), of which 42-65% is the per-span fixed cost (spans average 22-29 px).
- Added 2026-10-31 (series v7, patch 0011): With a ROM (v20b) that decomposes each condition of v20 into "large FIFO (RSP does not wait)" and "RDP frozen → RSP alone / unfrozen → RDP alone",
  I confirmed that in ares **RSP alone (F3DEX2) is +2% and RDP alone is −1% relative to the console** (frame 2316). What was missing was the **time the RSP's DMA
  takes away from the RDP** (0.057 VI/frame on the console, 0.080 in VI game mode, 0.049 in ares); I corrected it by raising the base cost of one DMA from 6 to 9 and applying the VI factor. From the same ROM's
  CPU chase, I re-measured the RDP loss per CPU fill from 11.7 to 10 (13.3 with VI on). The RDRAM page effect ROM (v21) was judged to need no change on the CPU side.
  The BAR intro goes from 252 to 249 frames (console: 225), and DK64 is unchanged.
- Added 2026-10-31 (series v6, patch 0010): With a ROM that replays the game's RSP graphics tasks on the console (v20), it turned out that **on the console the RSP task waits with the FIFO full until
  0.05 to 0.3 VI before the RDP** (ares finished 3 times faster). With the fix that returns `DPC_CURRENT` according to the model's progress,
  the RSP now waits as on the console, the BAR intro goes from 271 to 252 frames (console: 225), and the shape of the frame count per 60 VI matches the console.
- Added 2026-10-31 (patches 0007 to 0009, series v4): `0007` was replaced with a PI timing fix that gets past the MGC 2011 demo's emulator detection.
  I added the results of the mixed-load ROM (v17), the third round of CPU tests (v18), and the SP DMA contention ROMs (v19/v19b) as `0008`/`0009`: for the FPU, with normal operands
  add 3 / mul.s 5 / mul.d 8 / div.d and sqrt.d 58 (v14's "2 / 30" was the fast path), a 4-entry write buffer (uncached write 16 cycles each,
  fills and uncached reads wait for it to drain), asynchronous dirty writeback, cache instruction 5, an RDP penalty of 4 for uncached writes,
  a VI ×1.12 for SP DMA and mutual delay with CPU line transfers, and the rule that during consecutive DMA the RDP can use only 36 clk per DMA (v19b). Load interactions are additive, with no superadditivity. The BAR intro is unchanged (see Remaining Issues).

## Course of the Analysis

### 1. Game-Side Mechanism (ROM Analysis)

The J version's main code (global_asm) is the gzip at ROM 0x116F0 decompressed to RAM 0x805F8B00.

| Item | J version address | Contents |
|---|---|---|
| VI interrupt handler | 0x8060CCA4 | Increments the VI retrace count (`sched+0x284` = 0x80767EB4) and executes the pending buffer swap |
| RDP completion (DP interrupt) handler | 0x8060D1A4 | Compares with `D_807460D0` (VI at which the next swap is allowed) and either swaps or holds. After the swap, `osDpSetStatus(FREEZE)` |
| Frame delta computation | 0x805FE024 | Records `t0 = retrace count − previous` into an 8-element history and updates `D_80743D38` (the delta) with hysteresis. It then busy-waits until `retrace count ≥ previous + delta` |
| DKTV input replay | 0x80608EC4 / 0x80608F4C | Advances one entry per game frame (4 bytes: 15-bit camera angle + button update flag, stick X, stick Y) and writes it to the pad structure (0x807ED180) |
| Physics delta consumption | 0x8061B7E4 and many others | Uses `delta × 0.5` as the time step (1.0 at delta 2, 1.5 at delta 3) |

Hysteresis rule: if t0 of the last 2 frames is larger than the current delta, it is raised immediately; if all of the last 4 frames are smaller, it is lowered.
Therefore **exact cycle matching is unnecessary**, and the delta series is determined solely by "whether the frame time always exceeds 33ms".

### 2. Reproduction and Verification in ares (Headless Harness)

I ran the ROM with a self-made harness stripped of GUI dependencies, and recorded RAM (current map 0x8076A298, player coordinate ptr 0x807FBFBC +0x7C/0x80/0x84, delta 0x80743D38, DKTV index 0x80745C6C) every VI frame.

| Condition | Delta series (during demo) | Result |
|---|---|---|
| ares as is (RDP time 0) | Mostly 3, but 2 intrudes frequently (2×15, 2×22, 2×27 ...) | Falls into the valley at X≈1758 (as reported) |
| Delta fixed to 2 (code patched in RAM) | Always 2 | Falls via a different path |
| **Delta fixed to 3** | Always 3 | **Jumps to the rope, swings across to the far side → launched from the barrel cannon → transitions to the next demo (normal)** |
| Fixed ares (default values) | Almost 3 (a few 4s right after the start, short 2s in the second half) | **Completes normally** |

That the run becomes normal with the delta fixed to 3 is direct proof that the cause is the frame delta series.

Independently of the RDP time, in ares the CPU is busy almost 100% during the demo (PC samples of the idle thread 0x80000A04 are about 2.5%), and
each game frame is dominated by cache misses: about 30,000 dcache misses (≈13ms) plus about 10,000 writebacks (≈4ms).
On the console, CPU cache fills also become slower while the RDP occupies RDRAM, so the CPU side should be slower than ares as well.

### 3. Consistency with Known Information

- ares Issues [#1652](https://github.com/ares-emulator/ares/issues/1652) / [#1175](https://github.com/ares-emulator/ares/issues/1175): cause not identified, only "very sensitive to timing".
- The simple64 2022.07 release note "DK64 intro now grabs the rope" is due to a general review of timing.
- [DK64 Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled)'s `patches_main.c` fixes the delta to 3 in the Japes DKTV (consistent with the conclusion of this analysis).
- ares's "succeeds 2 times out of 10" (PAL) is probably because ares randomizes the RDRAM initialization timing (ccLow/ccHigh in `rdram.cpp`), which shifts the start time, and
  frames near the boundary happened to tip to the 3 side.

### 4. Calibration Against Console Recordings

From two console recordings (1080p 59.94fps), I extracted the number of consecutive identical frames (= the number of VI in the display interval)
(`analyze.py`: the picture is judged to have "changed" when the mean difference of 480×270 grayscale > 0.6). Over the whole demo section, the two recordings agree
within ±1 VI for the totals of every 25-frame block, so **the console's behavior is deterministic**.

For the Japes demo section (about 494 game frames), the VI count total per 25 game frames (console → ares):

| Block | 0 | 25 | 50 | 75 | 100 | 125 | 150 | 175 | 200 | 225 | 250 | 275 | 300 | 325 | 350 | 375 | 400 | 425 | 450 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Console | 79 | 75 | 76 | 78 | 83 | 82 | 78 | 75 | 75 | 74 | 71 | 73 | 75 | 80 | 77 | 75 | 74 | 70 | 73 |
| ares before fix | 97 | 74 | 73 | 80 | 87 | 88 | 83 | 76 | 71 | **65** | **64** | 72 | **63** | **64** | 73 | 77 | 72 | 74 | 72 |
| ares after fix | 98 | 77 | 76 | 80 | 86 | 87 | 84 | 77 | 76 | 75 | 75 | 76 | 79 | 87 | 76 | 77 | 75 | 75 | 69 |

Histogram of display intervals (2 / 3 / 4 or more): console 28/436/30, before fix 105/319/76, after fix 19/417/64.

- Before the fix, ares matched the console almost exactly to a few percent slower in the first half (the CPU-bound section), but in the second half (the section where the console's RDP time exceeds the CPU)
  2s occurred frequently (block totals 63 to 65 = average 2.6 VI), and there the game's delta dropped to 2 and desynced.
- The initially considered idea of "imposing a penalty on CPU cache fills while the RDP is busy" turned out, by comparison with the recordings, to slow the CPU-bound first half
  far more than the console (block totals 92 to 104), so I did not adopt it at that point
  (the value then was a flat 12 to 20 cycles. Later I adopted the values measured on the console, 10/4/12, as `0003`; see the 2026-10-27 section).
- With an RDRAM contention factor of 2.5 the second half is too fast and fails, with 3.5 it is too slow and fails, and 2.6 to 3.0 completes (error is smallest at 2.75 to 2.9).
  For compatibility with Beetle Adventure Racing described below, I adopted **2.9** (the value at the time of the estimate model. After the console-measured model was incorporated the meaning of the factor changed and it became **1.8**; see below).

## Beetle Adventure Racing (J) OP Demo (Calibration at the Time of the Estimate Model)

Using the same method, I compared console recordings (two, also agreeing within a few VI = deterministic) with ares.

- The OP is based on 30fps (display interval 2); in light scenes it leans toward 60fps (1 2 1 2 ...), and in heavy scenes it exceeds 33ms (2 3 2 3 ...).
  Scene cuts are time-based (the VI count of each scene matches between ares and the console), but the number of game frames within a scene
  varies with the emulator's frame time.
- Before the fix, ares (RDP time 0) ran the whole OP at 60fps. With a factor of 3.0, heavy scenes are about 25% slower than the console
  (e.g. seg1: console 306 frames vs ares 281), and 2.75 brings the per-scene frame counts closest.
- Helicopter crash scene (VI count from the segment start; console: shatter begins at +658 to 660, "crash" text shown at +668):

| Factor | Shatter | Text | Gap |
|---|---|---|---|
| Console | +658 to 660 | +668 | 8 to 10 |
| 2.6 | +644 | +666 | 22 |
| 2.75 | +652 | +664 | 12 |
| **2.9** | **+658** | **+668** | **10** |
| 3.0 | +662 | +672 | 10 |

  The relative timing of the shatter (car progress = frame-based) and the text (time-based) moves with the factor, and matches the console at 2.9.
- The symptom "the text appears first" occurs with the first patch I sent (factor 2.5 + CPU stall 12). The CPU stall term
  makes CPU-bound light scenes slower than the console, so the car's progress is delayed and the text gets ahead. With the current version (no CPU stall) it
  does not reproduce in the headless environment, and both order and gap match the console.
- Verification scripts: `seq.py` (display interval series from ares's rendering statistics) and `cmpseg.py` (scene-by-scene comparison with the console video).

## Integrating the Hardware-Measured RDP Timing Model (updated 2026-10-26)

I **replaced** the original estimated model ("ideal value × contention factor 2.9") with `rdp_timing_model` (v3 to v6, identical for NTSC/PAL, median error 0.5%), which was measured in a separate session. The main differences from the estimated model:

| Item | Old (estimate) | New (measured table) |
|---|---|---|
| Span | Pixel rate + per-stream (2.5+6+B/8)×2.9 + 12 | Table by mode × address phase × width (includes RDRAM waits) |
| 1cyc16 320px | 589 clk | 331 clk (almost entirely pixel-rate bound) |
| 1cyc16 blend+Z 32px | 235 clk | 170 clk (280 at ×1.8) |
| fill16 320px | 331 clk | 192 clk (282 at ×1.8) |
| Primitive | Fixed 14 | Depends on mode and width (−15 to +35, overlaps during continuous drawing) |
| Texture load | 8B/clk + RDRAM read×2.9 | Measured formula: 13.5 + 0.48/word + 22.7/64B, etc. |
| Z fail | Not distinguished | Separate table (about half of pass), pass rate estimated from overdraw |

Using the table **as is** (contention factor 1.0) makes DK64 too fast in its second half and it fails (on the console the CPU/RSP/VI compete for RDRAM).
I recalibrated by applying a factor only to the RDRAM-wait portion:

| Factor | DK64 error (25-frame block total difference) | DK64 2/3/4 histogram | DK64 completes | BAR seg0/seg1 frame counts (console 225/306) | BAR crash shatter/text (console +658 to 660/+668) |
|---|---|---|---|---|---|
| 1.0 | 72 | 91/333/76 | ✗ | – | – |
| 1.5 | 53 | 52/382/66 | ✗ | – | – |
| 1.7 | 39 | 36/408/56 | ○ | 278/347 | +629/+667 |
| **1.8** | **38** | 30/410/60 | ○ | 251/327 | +639/+667 |
| 1.9 | 42 to 46 | 31/412/57 | ○ | 251 to 253/313 to 331 | +638 to 644/+666 |
| 2.0 | 66 | 17/404/79 | ○ (block 13 is 95 = delta 4 mixed in) | – | – |
| Old model 2.9 | 40 | 25/419/56 | ○ | 257/302 | +658/+668 |

- At 1.8, DK64 is equal to or better than the old model (the histogram is also closest to the console's 28/436/30). It completes for 1.6 to 1.9.
- BAR remains **3 to 7% faster** than the console with the new model (heavy scenes have many 2-VI frames, and the crash shatter occurs about 20 VI earlier than on the console.
  The text is time-based, so it matches). For BAR alone, a factor of 2.3 to 2.4 is best, but DK64 fails at 2.0, so the two cannot both be satisfied.
  The symptom "the text appears before the shatter" does not occur (shatter +639 → text +667).
- I investigated what separates the two games but have not found a decisive factor:
  - CPU/RSP RDRAM traffic (ares RBus statistics) is the same for both games at about 32MB/s → cannot be explained by a variable factor.
  - The VI settings (320×240 16bit, AA+divot) are also identical.
  - I tried a model in which the RSP waits for the RDP to consume the output buffer (`DPC_CURRENT` throttling); only DK64 became much slower
    and BAR was unchanged → not adopted (my guess is that BAR waits for DP completion, while DK64 has a game loop that reacts to SP completion).
  - Buffer layouts of both games (obtained in ares): BAR has color 0x1DA800 / 0x200000 and Z 0x3DA800 (Z and one of the color buffers are exactly 2MiB apart =
    identical low addresses); DK64 has color 0x01D240 / 0x042A40 and Z 0x5D2E00 (Expansion Pak region).
  - A real-hardware test ROM for isolation, `rdp_timing_v7` (libdragon, saved to SD, runs on 4MiB consoles), is provided in `rdp_timing_v7.zip`
    (A: color/Z buffer layout, B: Set/sync commands between primitives, C: texture loads between small primitives,
    D: direct measurement of RDRAM contention by CPU/SP DMA/VI, E: scattered primitives). Results can be aggregated with `analyze_v7.py`.
  - Remaining candidates are color/Z buffer bank contention (the measurement placed both buffers on page boundaries; with BAR's buffer layout row misses may increase),
    implicit pipeline waits when `Set_Other_Modes` is used heavily without Sync_Pipe (BAR issues 704 per frame),
    and error in BAR's CPU time. These can be isolated with additional hardware measurements (span measurements with varying buffer addresses, the cost of consecutive Set commands (state)).
- It is also possible to set `zPassAuto=false` and fix `zPassFraction` (with a fixed 0.75, DK64 is slightly faster and BAR is faster still).

## Hardware Test ROM v7/v8 Results and the Final Model (updated 2026-10-27)

These are the NTSC/PAL hardware results of the test ROMs (`rdp_timing_v7` / `rdp_timing_v8`, libdragon,
saved to SD) built to isolate "the factor that separates the two" from the previous section (originals in `hw/`; NTSC and PAL agree for all series).

### RDP side (v7 A to E, v8 G/GT)

| Series | What was examined | Result |
|---|---|---|
| A | Relative placement of color and Z buffers | If Z is **within ±256KB** of the color buffer, +25 to 33% for 64×8 rectangles and +3% for a 200-row list. Both BAR (color 0x1DA800/0x200000, Z 0x3DA800) and DK64 (color 0x1D240/0x42A40, Z 0x5D2E00) have "far" layouts, so this is **not the cause** |
| B | Set/sync commands between primitives | Set commands take 1 to 6 clk with no implicit wait. Only `set_convert`/`set_key_r` act like a sync (≈400), and a slow state remains until the next `Set_Other_Modes` (because of garbage values). Game-like sequences: sync_pipe+set_other_modes ≈50 (Z mode 15 to 40), sync_tile+set_tile ≈34, sync_load+image+tile+load_block 128B ≈100, the full F3DEX2 texture switch set ≈155 — **matches the model's values** |
| C | Texture loads between small primitives | Interference between loads and primitives ≈0 — as in the model |
| E/G/GT | Primitive placement | For any placement other than stacking (drawing on the rows continuing from the previous primitive), modes that read color/Z cost **+35 to 50 clk per span** (scattered rectangles), 30 to 41 for side-by-side h≥4, 13 to 26 for triangle strips, and 28 to 44 for scattered triangles. Write-only modes (plain 1cyc) cost 0, and fill costs +6 to 11 |
| D | RDRAM contention from other masters | The RDP delay is proportional to the number of transactions from other masters. At about 2M trans/s, Z spans are ×1.19 to 1.22, write-only spans ×1.34 to 1.39, and copy ×1.3. Running SP DMA at full speed gives ×9. VI scanout adds +6.9% only in Z modes and 0% in plain |

The difference between the measured table (stacked drawing, idle console) and real games can be explained by the **"placement penalty (per span)" and the "contention factor (per mode)"**,
so the previous free parameter "RDRAM-wait portion ×1.8" is no longer needed.

### CPU side (v8 F: CPU while the RDP is drawing)

I measured the CPU loop with the COP0 counter (VI on, with the RDP drawing a long list). CPU cycles per operation:

| CPU operation | Idle console | ares | Increase while RDP draws (blend+Z / plain / fill) |
|---|---|---|---|
| dcache fill (sequential) | 38.8 | 45 (48/40 setting) | +3.8 / +0.9 / +2.0 |
| dcache fill (dependent, random) | 45.5 to 46.6 | — | Not measurable (the list finishes first) |
| dcache writeback (separated from copy) | ≈41 | 40 | +10 / +26 / +15 |
| icache fill (including 8 nops) | 54 | ≈56 | +10 / +12 / +12 |
| Uncached 32bit read | 41 | 11 | +3.6 / +0.6 / +1.3 |
| Uncached 32bit write | 19 | 9 | +10.7 / +6.1 / +13 |
| ALU | Match | Match | 0 |

- While the RDP is drawing, CPU cache transfers are stalled by **+10 cycles for icache, +4 for dcache, and +10 to 25 for writeback**.
  The earlier decision to remove the "CPU stall" was because the values then (a flat 12 to 20) were too large; the measured values were 1/3 to 1/2 of those.
- ares's uncached RDRAM accesses are faster than the console's (read 11 vs 40), but in both games uncached RDRAM accesses during OP are
  negligible (BAR 0, DK64 about 20 per frame), and MMIO accesses are only 100 to 140 per VI, so this has no effect.

### Final model (patches 0002 + 0003)

| Item | Value | Basis |
|---|---|---|
| Placement penalty for spans that read color/Z, `spanPenalty` | 40 clk/span | G/GT scattered 35 to 50 (strips 13 to 26) |
| Contention for spans that read color/Z, `contentionZ` | ×1.13 | D (game-equivalent 1 to 1.5M trans/s) |
| Contention for write-only spans and TMEM loads, `contentionPlain` | ×1.19 | D |
| During VI scanout, `viFactor` | ×1.07 for Z modes only | D |
| CPU: icache fill / dcache fill / writeback | +10 / +4 / +12 cycles while RDP draws | F |

Results (ares default recompiler, full run from power-on):

| Version | DK64 error | DK64 2/3/4 | Completes | BAR seg0/seg1 (console 225/306) | BAR shatter/text (console +658 to 660/+668) |
|---|---|---|---|---|---|
| Previous (×1.8) | 38 | 30/409/61 | ○ | 251/327 | +639/+667 |
| Measurement correction only (no CPU stall) | 37 | 33/408/59 | ○ | 276/335 | +636/+667 |
| **Final (measurement correction + CPU stall)** | 69 | 18/391/91 | ○ | 257/334 | **+651/+667** |
| Same, interpreter | 81 | 18/380/102 | ○ | 250/334 | **+659/+667** |

- BAR's helicopter crash is at +651 (+659 in the interpreter) against +658 to 660 on the console, a large improvement over the previous +639.
  The frame count of the heavy scene (seg0) also moved closer, 251→257 (console 225).
- DK64 completes, but the CPU-bound section in the first half (blocks 4 to 6) is about 10% slower than on the console (86 to 87 → 91 to 92, console 82 to 83).
  This section has many CPU cache misses while the RDP is drawing, and **the tension remains: with the measured stalls DK64 is too slow, and
  without them BAR is too fast**. Lowering ares's base icache/dcache fill costs (48/40) to
  the console's sequential-access values (46/35) brings DK64 back to 47, but BAR goes back to +647 (the `i4` experiment).
  The console's dependent/random-access fill is 45.5, close to ares's value, so I left the base costs unchanged.
- The ares recompiler makes the CPU about 4.5% faster than the interpreter (with the RDP model disabled, 325 vs 311 lists per 500 VI of BAR),
  so the same model gives slightly different results. The interpreter side is closer to the console.
- For fine tuning: `spanPenalty` in `rdp.hpp` (25 to 40 is within the measured range; smaller favors DK64) and
  `ICacheFillStall/DCacheFillStall/DCacheWriteBackStall` in `cpu.hpp` (setting them to 0 gives the equivalent of the previous version).

## Hardware Test ROM v9 to v16 and the Final Model (updated 2026-10-31)

Up to v7/v8, the measurements used synthetic primitives. From v9 on, I **replayed on the console game display lists dumped from ares (DK64 795 to 797 /
1795 to 1797, BAR 2316 to 2318 / 2556 to 2558, late BAR intro and start of the crash, MGC 2011)** and measured RDP clocks per list under the same
conditions as the game (VI mode, CPU/RSP bus load). Results are saved in `out/hw/`.

### RDP (v9/v10/v12/v15)
- **Accuracy of the table model**: For lists replayed with vi_off, the prediction from the table plus a placement penalty of 15 is DK64 ×0.96 to 0.98, BAR intro ×1.00,
  BAR crash ×1.06 to 1.08, late BAR ×0.99, and MGC ×0.95 to 0.98. The ratios for variants in which command types are replaced with NOP (triangles only /
  no loads / no rectangles / state only / fetch only) **agree with the console within ±3% for all lists**, and the residual is a uniform
  per-list scale (the vi_off values of the two consoles agree within 0.1%).
- **VI mode factors** (NTSC unit, 320×240 16bit, ratio to vi_off): the ROM's aa3 (replicate) ×1.004, aa2 (resample) ×1.03 (the same at 1280 px width),
  **the game's AA0+divot+dedither ×1.117**, and the same at 640 px width ×1.324. The PAL unit in the same modes is ×1.020 / ×1.148 / ×1.179 / ×1.496 respectively,
  clearly heavier, so this depends on the unit (region). The model defaults to the NTSC values.
- **Contention per bus master** (clocks the RDP loses, in the game's VI mode): CPU D cache fill 14.6, instruction fill 12, writeback 6.2,
  uncached write 5.5, SP DMA 64B 13.3 / 512B 91.5. The values in the ROM's VI mode (11.7 / 8.5 / 5 / 3.7, DMA 1.22 clk/8B)
  are multiplied by ×1.25 (`viContendScale`) via the VI factor. The fixed factors 1.13/1.19/1.07 were abolished, and each transfer is now charged through `timingDelay`.
- **Command fetch**: for a 352 KB list, 171k clk (11% of the frame, 2.8 to 3.9 clk/word). Overall it overlaps with drawing.
- **Meaning of the DPC registers (v13)**: `DPC_CURRENT` is the fetch pointer, and during a long primitive it stops at START+240 (240 B prefetch). Extending `DPC_END` during execution is accepted immediately.
  Writing a new `DPC_START` during execution switches at the next END but the RDP then hangs
  (an operation the game avoids). Lists with the syncs removed also hang the RDP (the known behavior where Set_Tile/Load hits a tile in use).

### CPU (v11/v14, same for NTSC/PAL)
- Integer: load → use in the next instruction +1, store → next is a memory instruction +1, a D cache hit itself +0 (ares had +1), an untaken likely branch
  also costs 1 cycle for the slot, mfc0 Count 2, mtc0 2, and jr/beq do not interlock on a load result.
- The FPU is pipelined: add/sub/mul (single and double) issue in 2 cycles, a dependent result comes 3 cycles later, cvt/trunc 5, div.s = sqrt.s = 28 to 29, sqrt.d 30,
  compare 1. add/sub have a value dependency, taking 3 on cancellation. A div with dividend 0.0 terminates early (that is the 15.5 in v11).
- Bus: uncached RDRAM read 33 cycles (ares 1 to 2), RCP register read 23 (MI is 11, DMEM/IMEM 29, RDRAM registers 32), PIF RAM 2981,
  cartridge ROM 219. Writes go through a 4-entry write buffer: 1 cycle each for up to 4, then ≈16 each (≈9 for the RCP), and a later read waits for the drain (not implemented).
- Cache: clean fill 41.4, with a dirty victim 66 (= fill + 25 writeback: ares is 80), two consecutive fills 43 each.
  ares's stalls while the RDP draws are measured at fill +5 / instruction +6 / wb +11, and in the game's VI mode a further +6 / +6 / +2.
- Recompiler bug: 1 cycle was double-charged for instructions that go through the slow path (miss, uncached) (wScottSh's "+1 compared with the interpreter").

### RSP (v16)
- **Instruction timing matches ares exactly** (dual issue, dependent vector instruction 4 cycles, lqv → use +1, branch bubble, mfc0 2).
- SP DMA on the console is 38 RSP cycles + 0.155/B (out is 32 + 0.145/B), and ares used 1 system clock per 8B → changed to 114 system clocks + 4/8B.

### Final model (defaults after applying patches 0002 to 0007)

| Item | Value | Basis |
|---|---|---|
| Placement penalty `spanPenalty` | 15 clk/span | v9/v15 list replay |
| VI factors `viFactorAA0/AA2/AA3` | 1.117 / 1.03 / 1.004; for AA0/1, the portion beyond 640 B/line grows by ×1.77 | v15 NTSC |
| Contention (clk the RDP loses) `contendCpu/I/W/Dma` | 11.7 / 8.5 / 5.0 / 1.22 per 8B, × (1 + 0.117×1.6) | v10/v12 |
| CPU stall (while RDP draws / VI AA0 mode) | fill +5+6, instruction fill +6+6, wb +11+2 | v10/v12 |
| CPU fill base cost | I 48, D 40, writeback 25 (fill first, asynchronous) | v14 |
| FPU issue / dependent latency | add·sub·mul 2 / 3, cvt 5, div.s·sqrt.s 29, .d 30, compare 1 | v11/v14 |
| Hazards | load-use +1, store→mem +1, mfc0 Count +1, mtc0 +1, untaken likely +1 | v11/v14 |
| Uncached read / RCP register read | 32 / 23 cycles | v14 |
| SP DMA | 114 system clocks + 4/8B (≈38 RSP cycles + 0.155/B) | v16 |

Results (ares default recompiler, full run from power-on):

| Version | DK64 error | DK64 2/3/4 | Completes | BAR seg0/seg1 (console 225/306) | BAR shatter/text (console +658 to 660/+668) |
|---|---|---|---|---|---|
| 0002+0003 (10-27) | 69 | 18/391/91 | ○ | 257/334 | +651/+667 |
| **0004 to 0007 (10-31)** | **22** | 35/438/27 | ○ | 278/339 | +627/+667 |
| 0004 to 0009 (10-31, series v4) | 28 | 33/418/49 | ○ | 271/338 | +635/+667 (measured with the harness at the same settings) |
| 0004 to 0009 + v19b rule (series v5) | 29 | 32/420/48 | ○ | 258/337 | +637/+667 (measured with the harness at the same settings) |
| **0004 to 0010: DPC_CURRENT model (series v6)** | 28 | 33/418/49 | ○ | **252/335** | +639/+667 (harness) |
| **0004 to 0011: re-measured DMA/CPU contention (series v7)** | 29 | 26/422/52 | ○ | **249/331** | +641/+667 (harness) |
| **0004 to 0012: Z whole-span fail only for spans of 16 px or less, ATOMIC_PRIM, 1-cycle AA (series v8)** | 29 | 24/423/53 | ○ | **249/331** | +641/+667 (harness) |
| **0004 to 0013: write-only span / triangle costs (series v9, MGC 2011)** | 29 | 24/423/53 | ○ | **251/331** | +641/+667 (harness) |
| **0004 to 0014: colour/depth in the same megabyte, Load_Tile source stride (series v10)** | 29 | 24/423/53 | ○ | **251/331** | +641/+667 (harness) |
| **0004 to 0015: span width = union of the four sub-scanlines (series v11)** | 36 | 24/421/55 | ○ | **252/328** | +642/+666 (harness) |
| **0004 to 0016: the VI slows read-modify-write spans only, AA0 ×1.14, AA2 grows with the line (series v12)** | 36 | 24/420/56 | ○ | **252/328** | +641/+667 (harness) |
| **0004 to 0017: 0016 reverted (the VI had stopped during v40; series v13 behaves as v11)** | 36 | 24/421/55 | ○ | **252/328** | +642/+666 (harness) |

- The DK64 demo is now the series closest to the console (per-block VI counts are within 1 to 2).
- BAR's crash scene (RDP-bound) matches the console (68/74/66 vs 70/76/69 VI/30 frames).
- BAR's intro runs at 2.0 VI/frame in ares and 2.5 on the console (a 50/50 mix of 2 and 3). The RDP time of the intro lists matches the console replay (×1.00, 1.5 VI including
  the VI factor) and is ≈1.95 VI including contention. The CPU is 87% busy in ares. In other words, **both the RDP and the CPU are within a few % of the 2 VI boundary**, with the console
  above it and ares below it. Slowing the CPU uniformly by 25% brings BAR closer to the console but breaks DK64, and slowing the RSP by 25% changes nothing,
  so the remaining difference is thought to be a few % on the CPU side (value-dependent FPU costs, the write buffer, super-additivity under combined loads that were not measured).
  The frame structure is the same in both the 8MB and 4MB console recordings, so the presence of the Expansion Pak is irrelevant.
- MGC 2011 (Retroactive): against the console recording, demo start to the RETRO logo is about 9050 VI on the console and 8620 in ares v8 (−4.8%; part 4 −12%, part 5 −8%).
  Series v9 (0013) gives 8950 (−1.1%), per part −4.5% to +1.6% (the "MGC 2011" section below).
  Against the console's task log directly (the "end of the MGC 2011 demo" section below) v10 is part 4 −3.8% / part 5 −3.5% / logo +0.8%, and the "@"
  beside the RETRO logo (on the console it lands on the "clank" in the music) comes 129 VI (2.2 s) early. Series v11 (0015): −1.7% / −2.0% / +1.9%, the "@" +5 VI.
  With the console logs of parts 1-3 (mgc_tasklog v2) all six parts are, in v12 (0016), +0.2 / +0.8 / +3.5 / −3.3 / −1.5 / +0.8%, +0.2% in total, the "@" −1 VI (section below).
  0016 was reverted (0017, series v13) because v40 had been measured with the VI stopped: as v11, +0.2 / +0.9 / +2.4 / −1.7 / −2.0 / +1.9%, +0.2% in total, the "@" +5 VI.
## Hardware test ROMs v17 to v19 and the v4 model (added 2026-10-31)

Up to v16, loads were measured one at a time. v17 to v19 measured **the interactions when loads are applied simultaneously**, plus the CPU
value-dependent costs and the write buffer that v14 left unmeasured (one NTSC and one PAL console each; results in `hw/v17_*`, `v18_*`, `v19_*`).

### v17: game-list playback x combined CPU/RSP load (8MB)
- **Loads add up.** The ratio between the prediction (the "clocks the RDP loses per operation" derived from each load alone, multiplied by the count) and the measurement under the combined condition is
  0.99 to 1.03 (only ucw+dma64 gives 1.06 to 1.07). Even with CPU+RSP+VI+RDP running at the same time, no super-additive effect occurs.
- Clocks the RDP loses per operation (hardware / ares): D-cache fill 13.8 / 14.5 (VI on), 9.7 (VI off),
  mixed load+store (2 fills + write-back) 27 to 28.5 / 34 to 37.5, **uncached write 4 to 5 (VI off 2.7) / ares 0**,
  SP DMA 64B 11 to 13 / 9 to 17, 512B 86 / 78 to 81. -> The RDP penalty for a write-back, `contendCpuW`, goes 5 -> 1, and `contendCpuU` = 4 is newly added for uncached writes.
- CPU side (while the RDP is drawing): chase 1 fill 55.5 cycles (ares 56), mixed 1 operation 130 (ares 149 -> due to the write-back being charged serially),
  **uncached write 33 (ares 8.2)**.
- Note: the `icache` load had its 2 KiB of straight-line code fit entirely in the I-cache (16 KiB), so it was effectively no load (the result can be read as a control condition meaning "the CPU is just
  running inside the cache"). Also, the VI effect disappears from the 5th list onward on NTSC and from the very start on PAL (game and vi_off have the same value), which we take to be the same "VI stops" symptom as in v15. Rows that include the VI are valid only for the first 4 lists on NTSC.

### v18: FPU value dependence, write buffer, write-back drain, cache instruction
- **FPU (issue cost, normal operands)**: add/sub (single and double) 3, mul.s 5, mul.d 8, div.s 29, **div.d 58**, sqrt.s 29, **sqrt.d 58**,
  cvt 5 (cvt.d.s 1, cvt.s.d 2), compare 1. A dependent next instruction costs an additional +1. Fast paths: adding +0.0 costs 2, multiplying by 0.0 / 1.0 / 2.0 costs 2,
  dividing with a dividend of 0.0 costs 2, sqrt(0.0) costs 2. The "add/mul issue 2" and "div.d 30" seen in v11/v14 were because the operands were 1.0/0.0 and
  hit the fast path (sqrt.d 30 is the average of 2 and 58). This means the original ares values (other than add 5) were nearly correct.
- **Write buffer**: an uncached RDRAM write drains at 16.3 cycles per entry (17.4 with VI on), regardless of width (sb/sh/sw), and 18 to 19 for sd.
  Up to 4 entries the CPU does not stall; from the 5th it waits for the drain. ALU instructions proceed during the drain (even 8 stores x 14 instructions has a total time bound by the drain rate).
  A subsequent uncached read waits for the drain to complete (+13), **a line fill waits for the drain to complete + about 9 bus turnaround (+25)**,
  and a cache hit does not wait. SP/DP register writes take 10 cycles, MI register writes take 2.
- **Dirty write-back**: if a fill comes right after, +28; if there are 16 or more cycles of work in between, most of it is hidden, leaving a residual of +6. Until now ares charged 25
  serially (because the v14 test was back-to-back).
- **cache instruction**: 5 cycles per instruction (I-cache hit invalidate 6), a dirty-line write-back takes about 17 cycles per 16 B (bound by the drain rate),
  and a clean/absent line costs only 5. ares had 1.

### v19: SP DMA under RDP drawing, VI, and CPU misses (S series)
| Condition (RDP idle/drawing, VI, CPU) | dma_in 8B | 64B | 512B | 3584B |
|---|---|---|---|---|
| idle / off / idle (baseline 38 / 44 / 108 / 581 RSP cycles) | 1.00 | 1.00 | 1.00 | 1.00 |
| idle / off / **CPU chase** | 1.16 | 1.39 | 1.74 | **1.94** |
| idle / **320x240 game mode** / idle (PAL console) | 1.04 (1.12) | 1.10 (1.09) | 1.16 (1.24) | **1.18 (1.26)** |
| idle / game mode / chase | 1.28 | 1.61 | 2.09 | **2.39** |
| while drawing fill / tex / texzb / off / idle | 1.00 to 1.12 | 1.05 to 1.12 | 1.00 to 1.06 | 1.01 |
- RDP drawing barely delays SP DMA (+0 to 5%, up to 12% for small DMAs). **A CPU line fill delays DMA substantially**:
  about 18 RCP clocks per fill overlapping a 3584 B DMA, and at the same time the CPU-side fill also goes 46 -> 65 cycles (+19). VI game mode
  slows DMA by 18% (26% on the PAL console). RSP instruction execution (vertex_like 37, mfc0 DPC_CURRENT 10) is unchanged under every load.
- The D series (measuring the time of an RDP list under continuous SP DMA) produced 0 rows in v19 because of a ROM bug; it was obtained with the fixed version **v19b** (`hw/v19b_*`).

### v19b D series: the RDP during continuous SP DMA
A fixed list (fill 40 rectangles ~ 1.77M clk, tex 20 rectangles ~ 1.63M clk) was measured while the RSP kept issuing DMAs without pause (DPC_CLOCK wraps around at 24 bits,
so values were converted from CPU ticks). **The RDP can use the bus for only about 36 clocks per DMA, regardless of DMA size**:

| DMA (continuous) | Iteration (RSP cycles) | Clk available to RDP / iteration | RDP slowdown |
|---|---|---|---|
| dma_in 8 B | 43 | 37 | x1.2 |
| 64 B | 46 | 36 | x1.3 |
| 512 B | 113 | 37 | x3.1 |
| 3584 B | 585 | 36 | **x16** |
| dma_out 512 / 3584 B | 103 / 555 | 36 / 36 | x2.8 / x15 |
The same holds for fill/tex and for NTSC/PAL. In VI game mode it is 33 to 39, and when the CPU chase overlaps it drops to about 30 (the chase also stretches the DMA itself, so
its effect on the RDP goes up or down depending on the condition). In other words, what the RDP loses is "DMA duration - about 32" = 6 per transfer + 1.2 clocks per 8 B
(8 B: 6, 64 B: 10, 512 B: 76, 3584 B: 549). ares v4 had only 1.22 per 8 B with no per-transfer component, and the DMA itself was also on the long side at 0.167/B,
so at 3584 B, with 55 to 109 clk/iteration left for the RDP, the slowdown was only x6. -> 0009 is updated to the v19b rule (DMA 0.15/B,
RDP penalty 6 + 1.2/8B). Game DMA is sparse (BAR intro: 682 per VI, 160 KB total per VI, about 3% of RDP time), so BAR does not change.

### Reflection in the v4 model (patches 0008 / 0009)
| Item | v3 | v4 | Basis |
|---|---|---|---|
| FPU issue add·sub / mul.s / mul.d / div·sqrt .s / .d / cvt | 2 / 2 / 2 / 29 / 30 / 5 | **3 / 5 / 8 / 29 / 58 / 5** (dependent +1) | v18 |
| Uncached write | 1 cycle, no RDP effect | 4-entry write buffer, 16 cycles each (+15 while the RDP is drawing), RCP registers 10, MI 2; reads and fills wait for the drain, fills get +9 | v18/v17 |
| Dirty write-back | CPU 4 + bus 25 (asynchronous) | CPU 6 + bus 17, the next fill gets +9 turnaround | v18 |
| cache instruction | 1 | 5 (write-back asynchronous 17) | v18 |
| Clk the RDP loses: write-back / uncached write | 5 / 0 | **1 / 4** | v17 |
| SP DMA | 38 RSP cycles + 0.167/B | 38 + 0.15/B, x1.12 (VI AA0/1 mode, aa3 is x1.04); +18 clk per overlapping CPU line transfer, and that CPU transfer gets +19 cycles | v16/v19 |
| Clk the RDP loses: SP DMA | 1.22 / 8 B | **6 + 1.2 / 8 B** (the RDP can use only about 36 clk between DMAs) | v19b |

Result (series v4 row of the results table above: DK64 error 28, BAR 271/338): **the BAR intro does not run correctly** (1.7 VI/frame, same as v3,
vs 2.06 on the console). Refining the CPU model with measured values does not help as much as making the CPU uniformly 25% slower did.
Since v17 ruled out super-additivity, the remaining candidates are (a) a missed "CPU fill x RDP interaction with VI on", which could not be measured because the VI stopped in v17,
(b) the RSP side of the RDP list (F3DEX2's DMA becomes 1.4 to 2 times slower under CPU fills, as in v19, which interrupts the supply to the RDP:
the ares RSP is modelled so that RDP command supply does not stop even during DMA), and (c) the game's frame loop side (where it waits for RSP task completion).
For (b), the v19b D series showed that "the RDP almost stops during continuous DMA", but game DMA is sparse and amounts to only about 3% of BAR's RDP time, so
  the BAR intro does not run correctly. What remains is (a) and (c), and the RSP-to-RDP supply timing itself during F3DEX2 execution in ares (waiting on a full command FIFO via XBUS).

## Hardware test ROMs v19b/v20 and the v6 model: the RSP waits for the RDP (added 2026-10-31)

### v20: replaying the game's RSP graphics task as-is on the console
At the moment ares starts a BAR graphics task (HALT clear in SP_STATUS), it dumps 4 MB of RDRAM, DMEM, IMEM, and the VI/DPC registers. A ROM linked at
0x80600000 restores them on the console and starts the RSP from the same state as the game, **measuring the time of RSP halt (task completion) and of the RDP's DP interrupt
(Sync_Full) simultaneously** (`hw/v20_*`, NTSC/PAL, 3 runs each, min).

| Frame / condition | Console RSP done [VI] | Console RDP done | ares v5 RSP | ares v5 RDP |
|---|---|---|---|---|
| bar_2316 game idle | **1.538** | 1.587 | **0.504** | 1.484 |
| bar_2316 game chase | 2.285 | 2.378 | 0.532 | 2.421 |
| bar_2316 vi_off idle | 1.376 | 1.418 | 0.498 | 1.399 |
| bar_2579 game idle | 1.449 | 1.652 | 0.687 | 1.696 |
| bar_2520 game idle | 0.428 | 0.637 | 0.217 | 0.573 |

- **On the console, the RSP task does not finish until 0.05 to 0.3 VI before the RDP.** F3DEX2 writes RDP commands to a FIFO in RDRAM (in this OSTask,
  8 KB at 0xFDF80 to 0x100000, 5% of the frame's 175 KB of commands); if the next flush would overwrite the RDP's unread portion, it reads
  `DPC_CURRENT` and waits, and at the end of the FIFO it waits for `DPC_CURRENT == DPC_END` before wrapping around. In ares, the instant `DPC_END` is written,
  the renderer consumes all the commands and `DPC_CURRENT = DPC_END`, so the RSP never waited and finished at 0.5 VI (3 times too early).
  The game's CPU waits for the SP interrupt (task completion) before advancing to the next frame, so ares released the CPU about 1 VI too early. This is the mechanism behind
  the BAR intro's 2.0 vs 2.5 VI/frame.
- The RDP-side time agrees between ares and the console within -6% to +8% (also under the CPU chase). The RDP fed by the RSP in a real task is 6% slower than v15/v17 (1.56M clk), which
  replay the same command sequence as a continuous list (1.65M clk: waits at the moments when the FIFO empties and RSP DMA contention).
- The PAL console matches NTSC in vi_off, and is +4% in VI game mode.

### v19b/v19c: the RDP during continuous SP DMA
The RDP can use the bus for only about 36 clocks per DMA (regardless of size) -> the 0009 rule (6 + 1.2/8B per transfer) was already reflected in v5.

### Fix (patch 0010: `n64: DPC_CURRENT follows the modelled RDP progress`)
On every `DPC_END` write, it records "the range (current..end) and the amount of work it queued", and a `DPC_CURRENT` read returns **the position inside the oldest incomplete range
that the completed amount of work has reached** (linear interpolation by work amount within the range; END once everything is done). This alone makes F3DEX2's FIFO wait and
wrap-around wait occur just as on the console, and running the v20 ROM in ares makes RSP completion come 0.05 to 0.3 VI before RDP completion (same as the console).
The range ring is not included in save states (right after a load, `DPC_CURRENT = DPC_END` until the next `DPC_END` write).

Result (clean core 0001 to 0010): series v6 row of the results table below. The BAR intro seg0 frame count approaches the 250s from 271 (console 225), and the
pattern of frame counts per 60 VI block takes the same shape as the console (45 55 66 65 75 63 53 vs console 46 56 70 67 75 65 56).
We attribute the remaining ~10% to the sum of the RDP model's -6% (real task in VI game mode) and a few percent on the CPU side.

## Hardware test ROMs v20b/v21 and the v7 model: decomposing RSP, RDP, and DMA contention (added 2026-10-31)

### v20b: decomposing each v20 condition into RSP alone / RDP alone / DMA contention
The same dump as v20 (4 frames of BAR graphics tasks) was measured as normal (8 KB FIFO, the RSP waits for the RDP), and additionally

- **Large FIFO**: the OSTask's output FIFO is moved to 1 MB on the expansion memory side (from 0x500000), and both are run with the RSP never waiting for the RDP.
- **Frozen**: additionally the RDP is DPC-frozen and only the RSP is run (`t_sp` = **F3DEX2 alone**); after the RSP halts, the counter is cleared and the RDP is unfrozen (`t_dp` = **RDP alone**,
  fed from the real FIFO, no RSP DMA contention).

(`hw/v20b_*`, NTSC/PAL, 3 runs each, min. For ares, `hw/v20b_ares_v6.txt`/`v20b_ares_v7.txt`, the median of 3 runs from thread times.
The first ares run is excluded because the VI factor has not fully taken effect and it is fast.)

| Frame / VI / CPU / condition | Console NTSC RSP / RDP [VI] | Console PAL | ares v6 | ares v7 |
|---|---|---|---|---|
| bar_2316 game idle normal | **1.538 / 1.587** | 1.602 / 1.659 | 1.463 / 1.535 | **1.474 / 1.553** |
| bar_2316 game idle large FIFO | 0.498 / 1.573 | 0.498 / 1.643 | 0.504 / 1.535 | 0.504 / 1.553 |
| bar_2316 game idle RSP alone / RDP alone | **0.494 / 1.493** | 0.495 / 1.590 | 0.504 / 1.479 | 0.504 / 1.479 |
| bar_2316 game chase normal | 2.279 / 2.372 | 2.410 / 2.483 | 2.085 / 2.333 | 2.039 / 2.268 |
| bar_2316 game chase large FIFO | 0.515 / 2.392 | 0.517 / 2.508 | 0.532 / 2.360 | 0.532 / 2.291 |
| bar_2316 game chase RSP alone / RDP alone | 0.511 / 2.274 | 0.512 / 2.428 | 0.534 / 2.301 | 0.534 / 2.205 |
| bar_2316 vi_off idle normal | 1.376 / 1.418 | 1.376 / 1.418 | 1.338 / 1.399 | 1.341 / 1.403 |
| bar_2316 vi_off idle large FIFO | 0.493 / 1.398 | 0.493 / 1.397 | 0.498 / 1.399 | 0.498 / 1.403 |
| bar_2316 vi_off idle RSP alone / RDP alone | 0.490 / 1.341 | 0.490 / 1.341 | 0.498 / 1.343 | 0.498 / 1.343 |
| bar_2316 vi_off chase normal | 1.888 / 1.960 | 1.888 / 1.960 | 1.849 / 2.057 | 1.755 / 1.929 |
| bar_2316 vi_off chase RSP alone / RDP alone | 0.501 / 1.899 | 0.501 / 1.898 | 0.529 / 2.021 | 0.529 / 1.883 |
| bar_2579 game idle normal | 1.449 / 1.651 | 1.309 / 1.499 (VI stopped) | 1.452 / 1.734 | 1.468 / 1.754 |
| bar_2579 game idle RSP alone / RDP alone | 0.674 / 1.541 | 0.670 / 1.392 (VI stopped) | 0.687 / 1.626 | 0.687 / 1.626 |
| bar_2579 vi_off idle normal | 1.308 / 1.499 | 1.308 / 1.498 | 1.336 / 1.590 | 1.338 / 1.593 |
| bar_2579 vi_off chase normal | 1.734 / 1.989 | 1.733 / 1.987 | 1.818 / 2.279 | 1.730 / 2.146 |
| bar_2520 game idle normal | 0.429 / 0.636 | 0.435 / 0.662 | 0.406 / 0.575 | 0.412 / 0.583 |
| bar_2520 game idle RSP alone / RDP alone | 0.206 / 0.599 | 0.203 / 0.540 (VI stopped) | 0.217 / 0.549 | 0.217 / 0.549 |
| bar_2520 vi_off idle normal | 0.378 / 0.569 | 0.378 / 0.569 | 0.368 / 0.522 | 0.369 / 0.524 |
| bar_2520 vi_off chase normal | 0.513 / 0.774 | 0.512 / 0.774 | 0.425 / 0.768 | 0.418 / 0.721 |

(On the PAL console the VI stops partway from bar_2316 onward (the same phenomenon as v17), and the "game" columns for bar_2520/2543/2579 have the same values as vi_off.)

Findings (frame 2316, NTSC):

- **RSP alone (F3DEX2 execution)**: console 0.494 VI, ares 0.504 (+2%). 2579: 0.674 / 0.687, 2520: 0.206 / 0.217 (+5%). The ares RSP is
  2 to 5% slower, but since the direction is toward "slower", it is not the cause of the BAR intro problem.
- **RDP alone**: console 1.493 (VI off 1.341), ares 1.479 / 1.343 (-1% / 0%). The VI game-mode factor is +11.3% on the NTSC console (model +11.7%) and +18.6% on the PAL console.
  The per-frame model error is as in v15/v17: 2579 +5.5%, 2520/2543 -8%.
- **Time the RSP's DMA takes from the RDP** (large FIFO - RDP alone): console **0.080 VI** (VI off 0.057), PAL console 0.053 (VI off 0.057). ares v6 is 0.049 regardless of VI.
  F3DEX2 performs 1164 DMAs per frame (485 list fetches of 168 B, 430 output flushes of 344 B, vertices 448 B, matrices 64 B, etc.,
  262 B on average, 305 KB total), and with VI off takes **51 clk per DMA** from the RDP. The continuous-DMA rule from v19b (6 + 1.2/8B) gives 45, and an isolated DMA is
  a few clk higher. In VI game mode it splits by console: 71 on the NTSC console and 47 on the PAL console.
- **Cost of the FIFO coupling itself** (normal - large FIFO): +0.9 to 1.4% for idle, -1% for chase. This is negligible, and with the v6 (0010) DPC_CURRENT model the RSP's
  completion time already has the shape seen on the console (RSP completion console 1.538 / ares 1.474, the difference being mostly the RDP-side difference).
- **RDP loss from the CPU chase**: (RDP alone (chase) - RDP alone (idle)) / number of loads = VI off **10.4 clk/fill**, VI game mode **13.3** (v17: 9.7 / 13.8,
  v10: 11.7). ares v6 gives 11.7 x 1.19 = 13.9 (11.7 for VI off), which is 13% too high with VI off and 4% too high with VI on.
- Contention among all three (RDP + CPU chase + RSP DMA) is slightly larger than the sum (extra DMA cost during chase 0.118 VI vs 0.080 during idle).

### v21: RDRAM page/bank effects (the remaining suspect on the CPU side)
Dependent chained loads chasing randomly through 4 MB (`hw/v21_*`): one fill costs 45.5 cycles for ranges of 64K to 256K, 43.0 for 1M, **38.0 for 4M**; alternating
access between two points 1 MB or more apart is 38.0, under 1 MB is 45.5, sequential is 38.0, with dirty-line write-back included it is 71.3 regardless of range, uncached reads are 35 (4M) / 42.6 (64K), and instruction fill is
about 44 (ares 48). So the ares value of 43 lies in the middle of the console's 38 to 45.5, and **the cost of a CPU fill is not the cause of the BAR intro residual** (no change).

### Fix (patch 0011: `n64: SP DMA and CPU bus contention against the RDP refitted on a real graphics task`)
- The RDP loss per SP DMA, `contendDmaBase`, goes 6 -> **9** (48 clk per DMA with F3DEX2's DMA mix, console 51), and the loss is multiplied by the same VI factor `contendScale`
  as the CPU (`rsp/dma.cpp`).
- The RDP loss per CPU fill, `contendCpu`, goes 11.7 -> **10.0**, and the slope of the VI factor, `viContendScale`, goes 1.6 -> **2.4** (x1.28 in VI game mode: fill 12.8, DMA 61).

Result: frame 2316 (normal, VI game mode) RSP completion / RDP completion = 1.474 / 1.553 VI (console 1.538 / 1.587, v6 1.463 / 1.535), chase 2.268 (console 2.372,
v6 2.333), VI-off chase 1.929 (console 1.960, v6 2.057). The full run of clean core 0001 to 0011 is in the series v7 row of the results table: BAR intro seg0 252 -> 249
frames, seg1 335 -> 331, DK64 28 -> 29 (within the error range), crash scene +641/+667 (console +658 to 660/+668).
## Measurement with a resident logger inside the game itself (bar_tasklog) and the v15 command-class analysis (added 2026-11-01)

### bar_tasklog: recording the frame timeline of BAR (J) on the console
This is not a test ROM but a **patch to the game itself** (`bar_tasklog/`; the ROM is not included and is generated from a dump with `make_patch.py`). At entry, a 1.2 KB logger is copied from the padding at the end of the ROM
(0xFF0000) to 0x80700000 (upper 4 MB, unused by the game, Expansion Pak required), and 4 libultra functions in RAM
(`osSpTaskLoad` / `osSpTaskStartGo` / `send_mesg` in the exception handler / `osSpTaskYield`) are replaced via `j`/`jal`. With the COP0 Count attached, it records
"graphics task start, RSP stop (yield / completion), DP interrupt, VI interrupt" at 4 bytes per entry. Recording starts automatically at the first heavy frame of the intro
(a task of 0.5 VI or more); when 32 KB has accumulated (the whole intro is ≈1600 VI) it is written to the cartridge SRAM and recording stops, and the EverDrive `.srm` is then analyzed
(`decode_tasklog.py`). The same patched ROM run in ares produces a log that can be matched against the hardware log **by frame number** (the contents of seg0 correspond by frame number,
and the length of seg0 is 465 VI on both, so they are time-synchronized). The variant `hook_idle.bin` is a version in which, during recording, the CPU that started a graphics task waits until the RSP stops
in a loop that does nothing but read SP_STATUS (it does not touch the bus, and since the scheduler is stopped there is no yield either), which lets us **isolate the in-game CPU contention directly on the console**.

Hardware logs (`hw/bar_tasklog_hw_005_normal.srm`, `..._006_idle.srm`; the normal version's 2 runs agree to within ±0.005 VI on every frame) versus ares v7
(`bar_tasklog/ares_v7*.srm`), first 165 frames of seg0 (`hw/bar_tasklog_compare_seg0.txt`):

| | Console | ares v7 | Difference |
|---|---|---|---|
| Frame period | 2 VI (95) / 3 VI (19) (last 114 frames of seg0) | mix of 1 / 2 / 3 VI | — |
| Number of frames in seg0 (465 VI) | **230** | 250 | +8.7% |
| RDP completion (CPU idle), heavy frames / light frames | 1.553 / 0.782 | 1.512 / 0.735 | **+2.7% / +6.7%** |
| RSP completion (CPU idle), heavy / light | 1.460 / 0.702 | 1.392 / 0.648 | +4.9% / +8.6% |
| In-game CPU contention (normal − idle, RDP completion), heavy / light | +0.309 / +0.134 | +0.287 / +0.118 | **+8% / +14%** |
| RDP completion, normal version, heavy / light | 1.862 / 0.920 | 1.799 / 0.853 | +3.5% / +7.9% |

- BAR's frame period is decided as 2 or 3 VI by "whether RSP completion plus roughly 0.22 to 0.25 VI of CPU post-processing fits within 2 VI" (the threshold on the console is ≈ RSP completion of 1.75 VI),
  and the heavy frames sit exactly on that boundary. In ares the light frames (RSP completion 0.62 to 0.75) fit within 1 VI, whereas on the console (0.78 to 0.85) they do not fit and
  become 2 VI — this accounts for most of the 20-frame difference in seg0.
- **The CPU contention model (10 clk/fill × VI 1.28) is within 8% of the console.** Even with the CPU stopped, the console's RDP completion is later by an average of **+0.039 VI/frame (σ 0.03)**, and
  this difference does not correlate with frame content (number of pixels, spans, primitives; memory-bound ratio) (|r| ≤ 0.3). PI DMA (1 KB/frame or less during the intro),
  audio DMA, and fills (model 43.7K clk/screen = v19b measurement 44.2K) are all ruled out as the cause.
- Fitting BAR with a uniform RDP factor breaks DK64 (harness: ×1.04 → seg0 251 / DK64 29, ×1.08 → seg0 230 / DK64 37, CPU contention 11 + ×1.05 →
  235 / 39). CPU contention of 10.8 alone gives DK64 27 to 30 and BAR 249 to 254, within error. **The residual is a content-dependent ±5%, with DK64 and BAR on opposite sides.**

### Per-class comparison using v15's "replay with a command class replaced by NOPs"
For each list in v15/v15b (hardware, VI-off equivalent), I split the difference from ares (v7, with the harness aggregating clk per class) by command class
(`hw/v15_class_fit.txt`).

| Class (contribution = full − no_X) | BAR 2316 series | BAR 2556 / c2520 (crash) | BAR t2500 (light) | DK64 797/1795 to 7 | MGC |
|---|---|---|---|---|---|
| Overall, console/ares | 0.99 to 1.01 | **0.93 to 0.95** | 1.01 | **1.02 to 1.04** | 1.00 to 1.03 |
| Triangles | 0.99 to 1.01 | 0.90 to 0.93 | 1.03 | 1.02 to 1.06 | 1.00 to 1.07 |
| Rectangles | 1.03 | 1.01 to 1.09 | 1.01 | 0.98 to 1.02 | — |
| Texture load | 1.01 to 1.02 | 1.01 to 1.07 | 1.02 | **1.06 to 1.07** | 0.99 |
| Set commands (state) only | 0.95 to 0.96 | 0.96 | 0.96 | 0.95 | 0.93 to 0.99 |

- BAR's triangles have only **5 to 11 pixels per span** (2316: 7.8K spans / 76K px), and most of the cost is the fixed per-span part (the table's
  short-span value + spanPenalty 15). DK64 is similar (1CYC16_BLEND_ZPASS, 2 to 3 px/span, 88 to 93 clk/span).
- Only the crash lists (2556/c2520) have triangles that are 7 to 10% too large. This comes from a difference in how **pixels that fail the Z test** are handled: ares's estimator
  (H(n)/n from the number of Z pixels ÷ scissor area) returned a pass rate of 1.0 with n≈1, because the Z drawing covers only half of the scissor. With an estimate that
  counts the pixels actually covered (bitmask, `T_ZCOVER`), BAR's Z drawing turns out to cover **40K pixels 2.2 to 2.5 times**, and DK64 1.5 to 1.66 times, and the
  pass rates become 0.68 to 0.73 / 0.83 to 0.88. This improves the crash lists from 0.93 to 0.97 to 0.98, but **makes the 2316 series worse (0.99 → 1.04 to 1.06) and DK64 worse
  (1.02 to 1.04 → 1.05 to 1.07)**. In other words, the 2316 and DK64 scenes are drawn back to front so nearly all Z passes, and only the crash scenes have failures.
  Since this depends on draw order it cannot be settled by estimation; **the correct approach is to return the renderer's (software RDP's) actual per-primitive Z test results to the timing model**
  (this cannot be obtained from parallel-rdp, in which case we would fall back to the current estimate). This is not included in the present series.
- DK64's texture load is 6 to 7% low and Set commands (state) are 4 to 5% high in all games (both are under 1% of the total).

### Other things confirmed
- The VI game-mode factor (model: uniform 1.117) is, by v19b measurement, ×1.140 for the fill list (PAL console ×1.202) and ×1.101 for the textured-rectangle list (×1.166).
- The cost of coupling in which the RSP waits for the RDP is +1.2 to 1.4% on the console (v20b: normal − large FIFO), but it is 0 in ares because the RDP never runs dry. Trying the granularity of `DPC_CURRENT`
  (rounding to 64 / 256 B) does not reproduce it.
- On the console the intro starts 1114 to 1134 VI after power-on, whereas in ares it is 2412 VI (a difference in the length of the boot sequence; not yet investigated).

### Conclusion
The residual of the BAR intro (seg0: 250 vs 230 frames) is the sum of (1) in-game CPU contention (+8%), (2) coupling of the RSP supply (+1.2%), (3) scene-dependent Z pass rate
(−7% in the crash scenes), and (4) content-dependent error in light frames (+6.7%), each of which is a term of a few percent at most. (1) and (2) can be put in as factors, but
they are in a range that worsens DK64 by 1 to 2 VI/block, and (3) requires coupling with the renderer, so I am leaving it as series v7.

## Hardware test ROM v22: per-class breakdown of in-game lists, DPC_CURRENT granularity, and cost of thin triangles (added 2026-11-01)

`rdp_timing_v22` (NTSC `hw/v22_ntsc_002.txt`, PAL `hw/v22_pal_001.txt`, ares v7 reference `rdp_timing_v22/ares_reference.txt`).
The VI-off values agree to within 0.1% on the 2 consoles.

### V: the actual in-game lists extracted from bar_tasklog (VI off, console/ares v7)
| List | Overall | Triangles (full − no_tri) | Texture load | Rectangles | Set commands (state) only | fetch only | Triangle px/span, spans/triangle |
|---|---|---|---|---|---|---|---|
| bar_i2274 / 2279 / 2284 (light, frames 20/25/30) | **1.035 / 1.032 / 1.032** | **1.05 / 1.06 / 1.06** | 1.02 | 1.01 | 0.94 to 0.97 | 1.00 | 37 px, 8 spans (282 to 332 triangles) |
| bar_i2314 / 2338 (heavy, frames 60/84) | 0.973 / 0.976 | 0.97 / 0.97 | 1.01 | 1.02 | 0.96 / 0.94 | 1.00 | 14 to 16 px, 13 spans (875/788 triangles) |
| bar_i2404 / 2410 (heavy, frames 150/156) | 0.963 / 0.956 | 0.94 / 0.93 | 1.15 | — | 0.95 | 1.01 | 16 px, 8 spans (1156/1071 triangles) |
| bar_2316 (v15 list) | 1.011 | 1.02 | — | — | 0.98 | 1.00 | 15 px, 13 spans (899 triangles) |
| dk64_797 | 1.058 | 1.08 | — | — | 0.95 | 0.99 | 15 px, 4.9 spans (1891 triangles, 1 cycle) |

- The +6.7% for light frames (bar_tasklog) comes from **+5 to 6% in triangles**, not from texture load, rectangles, Set commands (state), or fetch. Heavy frames are conversely −3 to 4%.
- The "texture load" and "rectangles" columns for bar_i2404/2410 and bar_2316/dk64 are omitted, because ares's reference values were order-dependent due to the automatic
  estimate of the Z pass rate (overdraw from the previous frame) (the v23 reference is taken with `T_ZPASS=1` fixed).
- Game VI-mode factors on the PAL console: overall ×1.16 to 1.17, triangles ×1.13 to 1.17, Set commands (state) only ×1.07 to 1.12, **fetch only ×1.01** (command reads are not
  disturbed by the VI). On the NTSC console, after the first case (bar_i2274 full: ×1.104, ares ×1.106) the VI stops fetching (VI probe 93K = off,
  VI_CTRL stays at 0x3012), and from then on the game-mode values are the same as VI off. On PAL the VI stayed alive during V and stopped at W.
  This is thought to be an interaction between libdragon's VI interrupt handler and this ROM's direct register writes, and is unresolved (the same phenomenon as "the VI effect disappears partway" in v15/v17).

### Q: how DPC_CURRENT advances
When the CPU keeps reading DPC_CURRENT while bar_2316 runs (48.5K reads, 1 loop = 22 ticks = 2 reads, DPC_CURRENT + MI_INTR),
the value changes 2210 times, and **per change the advance is 128 B in 72% of cases (1586 times), 64 B 13%, 256 B 7%, ≤32 B 8%** (maximum 320 B; same on NTSC/PAL; same with and without VI).
The RDP reads commands in 128 B units, and DPC_CURRENT advances at that granularity. ares uses 8 B units (even when 64/256 is tried with the harness's `T_CURQ`,
the RDP time and BAR's frame count do not change). A by-product: the same loop takes 31 ticks/iteration in ares, so **CPU reads of RCP registers are 40% slower in ares**
(ares takes 22 cyc for both MI/DPC; on the console, per v14, DPC is 23 and MI is 11).

### W: thin triangles with vertical edges (shade+texture+z, 22 words, random placement, 4096 spans)
`T(w, H) = triangle fixed part + H × per-span cost`, separated from H = 1,2,3,4,16 (VI off, Z all pass, clk):

| | Triangle fixed part | Span w=1 | w=2 | w=4 | w=8 | w=16 | w=32 | w=64 |
|---|---|---|---|---|---|---|---|---|
| 2cyc_tex_blend_z, console | **36 to 51 (mean 45)** | 74 | 78 | 85 | 97 | 121 | 167 | 237 |
| same, ares v7 (table + 15 + prim_overhead) | −9 to +30 | 76 | 92 | 98 | 110 | 138 | 185 | 271 |
| 1cyc_tex_blend_z, console | **25 to 49 (mean 40)** | 74 | 81 | 85 | 97 | 118 | 154 | 223 |
| same, ares v7 | −9 to +2 | 76 | 93 | 96 | 110 | 136 | 175 | 252 |

- An isolated triangle has **a fixed cost of about 45 clk** (including ≈ 22 clk for reading 22 words = 176 B), which ares's rectangle-derived prim_overhead (−12 to +31) does not have.
  On the other hand, the per-span cost is **the table value itself (the spanPenalty of +15 is unnecessary)**, and for widths of 32 and above it is 10 to 20% cheaper than the table.
  A 1-span, 1-px triangle is 125.6 clk on the console versus 75.4 in ares (ares is 40% cheaper); they agree for w ≥ 16.
- Z: when everything fails, each span is **29 to 41 clk cheaper in 2cyc and 18 to 49 clk cheaper in 1cyc**, and half-fail (half) lands exactly in between (e.g. 2cyc H=3 w=8: pass 110.9 / half 93.3 / fail 75.3).
  ares's approach of "mixing the pass and fail tables linearly by the pass rate" is correct; the problem is only the estimate of the pass rate (as concluded in v15).
- Other modes (H=3): 2cyc_tex (blend, no Z) is 91 on the console versus 57 in ares at w=2 (missing fixed cost) and agrees at w=32. For 2cyc_tex_blend, ares is 10 to 15% higher.

### Applying W's per-span costs directly to the game lists does not fit
When the harness is given "triangle +45, triangle spans +3, −0.8 per pixel for widths above 16" (`T_TRIOV=45 T_TRISP=3 T_SPWIDE=0.8`), the W series fits to within ±5%, but for the V series the triangles come out at
**console/model = light 1.19 to 1.21, heavy 1.05 to 1.10, bar_2316 1.13, DK64 1.06**, i.e. the game's triangles cost 10 to 20% more than synthetic triangles
of the same width and height. The spanPenalty of 15 introduced in v9 had been absorbing this difference empirically.

Going through the differences between the game's triangles and W's triangles, the first thing found was **how the rasterizer counts pixels**. ares's timing model evaluates edges at the row center
and takes `x1 − x0` pixels, whereas the console (angrylion's edge walker) processes from the floor of the left end to the floor of the right end of the 4 sub-scanlines, **both ends inclusive**,
and draws a row if even one sub-scanline falls within it. Putting this in the harness (`T_RASTER=1`) increases the span counts of the game lists by +6 to 11% for BAR and
+17% for DK64, and the pixel counts by +6 to 14%; combined with W's per-span costs the results are 1.01 to 1.02 for heavy frames, 1.04 for bar_2316, 1.06 to 1.10 for light frames,
0.94 for bar_i2404/2410, and 0.85 for DK64. The remainder is either the Z pass rate (bar_i2404/2410 and DK64 may be failing more than estimated) or mode bits
(AA, perspective, LOD, dither, TLUT, etc., which W's triangles lack), and v23 is meant to separate these.

## Hardware test ROMs v23 / v24: what makes the game's triangles expensive (added 2026-11-01)

To find the cause of v22's "applying the per-span costs of isolated synthetic triangles to the game lists comes out 10 to 20% cheaper", I separated it in two directions: (X) replaying the game lists with their mode bits /
triangle commands / Z buffer / buffer placement rewritten, and (Y) adding properties to the synthetic triangles one at a time
(`hw/v23_*.txt`, `hw/v24_*.txt`; the 2 consoles, NTSC/PAL, agree to within 0.1%; VI off).

### X: game lists (difference from full, console)
| Change | bar_i2274 (light) | bar_i2314 | bar_i2404 | bar_2316 | dk64_797 |
|---|---|---|---|---|---|
| AA_EN off | −0.1% | +0.5% | −0.6% | −0.2% | **−3.2%** |
| TEX_PERSP / TEX_LOD / dither / point sampling / EN_TLUT off | all ±0.05% | | | | |
| ATOMIC_PRIM off (set only by DK64) | 0 | 0 | 0 | 0 | **−3.3%** (20 clk/triangle) |
| 2 cycles → 1 cycle | −1.3% | −1.3% | −2.8% | −1.8% | — |
| Coverage / alpha compare off | +0.7% | +2.3% | +0.5% | +1.7% | 0 |
| IMAGE_READ off | −12.7% | −23.1% | −19.3% | −22.7% | −19.3% |
| Z_COMPARE off | −14.7% | −19.3% | −14.2% | −18.8% | −19.6% |
| Z_UPDATE off | −7.4% | −5.9% | −2.5% | −5.9% | −5.9% |
| Z clear removed + initialized to farthest (z_far: the first draw all passes; only overdraw within the frame fails) | −4.7% | −1.9% | −2.1% | −1.8% | −3.7% |
| Z clear removed + initialized to nearest (z_near: everything fails) | −19.0% | −20.7% | −14.8% | −20.5% | −20.0% |
| Triangle slopes (shade / texture / Z) set to 0 | −0.2% | −0.6% | −0.4% | −0.6% | −1.6% |
| 0F triangle → 09 (edge + Z, remaining 16 words are NOP) | +0.3% | +0.3% | +0.9% | +0.3% | +0.7% |
| Color buffer to the first 1 MB of RDRAM / Z 1 MB higher | −0.1% / 0 | −0.1% / 0 | −0.2% / 0 | −0.1% / 0 | 0 / — |

- **The mode bits are not the cause.** Neither the number of words in the triangle command nor the slopes of the attributes matter (reading the command words overlaps with drawing, and
  ares's "1.73 clk per NOP word" is an overestimate for triangle words).
- **Z: the z_far difference equals only the Z-clear Fill** (BAR 25.6K ≈ model 24.8K, DK64 44K). That is, pixels that fail due to overdraw within the frame
  save nothing (with z_near, where everything fails, it is −15 to 21% = 15 to 31 clk cheaper per span). If even one pixel in a span passes,
  all the reads and writes for that span are performed, so **the Z saving occurs only when a whole span (block) fails**. For ares's estimate of "mixing the pass/fail
  tables by the pass rate", unless there is a primitive that fails entirely, **a pass rate of 1.0** is the correct answer (the reason f=1 fit in v15).
- Only DK64 has AA (1 cycle) and ATOMIC_PRIM at −3.2% each (equivalent to 20 clk/triangle or 3.5 clk/span). For BAR (2 cycles) the AA effect is 0.
- Going from 2 cycles to 1 cycle changes things by only −1 to 3%: BAR's triangles are **memory-bound**, not pipe-bound.

### Y: synthetic triangles (2cyc_tex_blend_z, H=8, w=4/12/32, console clk/triangle)
| Case | w=4 | w=12 | w=32 |
|---|---|---|---|
| base (v22 W: vertical edges, random placement) | 709.7 (v23: 719.0) | 927.1 | 1350.4 |
| AA / TEX_PERSP / LOD / dither / point / cvg / BAR's mode words / slopes / x+0.5 | all base ±0.5% | | |
| Tilt all edges by 0.5 / 2 px/row | −1.3% / +3.5% | +1.1% / +1.8% | +0.4% / +0.9% |
| Real triangles (apex at top, mean width w) | −3.7% | −3.6% | −1.5% |
| Adjacent along the row (strip) | −4.8% | −1.9% | −1.4% |
| **All in the same place** | **−43.5%** | −39% | −31% |
| Set_Other_Modes + Set_Combine every time / every 8 | +0.5% / 0 | +0.4% / 0 | +0.1% / 0 |
| Sync_Pipe every time / Set_Tile+Size+Sync_Tile every time / Load_Block 64 B every 8 | +0.5% / −0.8% / +1.3% | +1.0% / +0.1% / +1.1% | +1.6% / +0.7% / +0.8% |
| Z all fail / Z_COMPARE off / Z_UPDATE off / IMAGE_READ off | −36% / −23% / −16% / −28% | −32% / −21% / −13% / −25% | −18% / −13% / −3% / −26% |
| **Color and Z in the same 1 MB** (adjacent, +300K) | 758.3 (**+10%**) | 1033.4 (**+17%**) | 1503.6 (**+17%**) |
| Color and Z in different 1 MB (+1M, as in BAR 0x200000/0x3DA800, +2M, +4M) | 688.5 | 885.6 | 1290.2 |

- RDRAM holds pages per 1 MB (bank), and **when the color and Z buffers are in the same 1 MB it is 10 to 17% slower**. Both games
  place them in different MBs (BAR 0x200000 / 0x3DA800, DK64 0x1D240 / 0x5D2E00), so there is no actual impact, which is also why `color_low` / `z_up1m` in X are 0.
  The base of v22/v23's W/Y had the arena straddling a 1 MB boundary, and so fell between these two values (709 to 719).
- The reason the game lists' triangles are more expensive than synthetic triangles is none of mode bits, commands, or placement; what remains is **the way the triangles are ordered itself**
  (strips sharing edges, where the next triangle immediately reads pixels written by the previous one = the read-after-write mentioned in the comments of ares's model).
  A synthetic strip (adjacent horizontally, non-overlapping) is conversely cheaper, so the overlap of shared edges needs to be reproduced synthetically, which could not be confirmed in the present series.
- In v23, on both consoles the RDP stopped responding at Y's `ci4_tlut` (Load_TLUT + CI4 tile), the timeout handling did not work, and execution halted (v24 also
  halts at the same place). The effect of CI4/TLUT is already known to be 0 from the game-list side (`om_no_tlut` ±0), so this case is dropped.

### Candidates applied to the game itself in the harness (`hw/pair_v24_candidates.txt`)
| Candidate | DK64 error / 2-3-4 | BAR seg0/seg1 (console 225/306) | Crash shatter/text | tasklog seg0 (console 230) |
|---|---|---|---|---|
| A: v7 | 26 / 29-425-46 | 251/333 | +641/+667 | 250 |
| B: + Z pass rate fixed at 1.0 | 31 / 26-423-51 | 250/332 | +641/+667 | 250 |
| C: B + ATOMIC 20 + 1cyc AA 3.5/span | 35 / 24-424-52 | 250/332 | +641/+667 | 250 |
| D: C + console edge walker | **59** / 9-400-76 ✗ | **229/301** | **+647**/+667 | **226** |
| E: D + v22 W's per-span costs | 25 / 35-418-47 | 250/332 | +638/+666 | 248 |

The console's edge walker (D) puts BAR almost exactly on the console, but it breaks DK64 because the span count of its small triangles goes up by +17%, and on the standalone lists
both BAR and DK64 are 8 to 12% too large, so it is merely covering for some other missing part in the game (rejected). Fixing the Z pass rate at 1.0 (B) increases
DK64's 4 VI (DK64's scenes contain small triangles that are entirely hidden, confirmed in v25/v28). What was finally put into 0012 is
"for whole-span Z failure, apply the estimated pass rate only to spans of 16 px or less, and always use the pass cost for wider spans" + ATOMIC_PRIM 20 + 1-cycle AA 3.5, giving
DK64 26 (26-426-48) / BAR 251 in the harness, and DK64 29 (24-423-53) / BAR 249/331 on the clean core (equivalent to v7; `hw/pair_znarrow.txt`, `hw/pair_v3_v8b.txt`).

## The DK64 in-game timeline (dk64_tasklog), replaying the demo's frames (v25), and replaying triangles by width (v26 to v28) (added 2026-11-01)

### dk64_tasklog: a resident logger on the DK64 (J) console
I put the same logger as bar_tasklog into DK64 (J) (`dk64_tasklog/`). Because the libultra version is different, I swapped in `osSpTaskLoad` 0x800085FC / `send_mesg` 0x80009984 /
the `osSpTaskStartGo` jal 0x800087B4 / the `osSpTaskYield` jal 0x800087D8, and in v2 I also hooked `osEPiRawStartDma` 0x8000C090 / `__osPiRawStartDma` 0x800048B0
to record PI DMA starts, sizes and interrupts. The logger resides at RAM 0x80160000, in ROM tail padding at 0x1FF0000, with the CIC-6105 checksum recomputed.
Recording starts at the attract demo start right after the title's still frames (60 VI or more with no graphics task). It is at console VI 12658 to 12662 versus ares 12644, so the two differ by
only 14 to 18 VI and can be compared directly by frame number (`hw/dk64_tasklog_hw_001` (v1), `_002` (v2, with PI)).

| Frames 423 to 520 from demo start | Console | ares v7 |
|---|---|---|
| Frame period 2 / 3 / 4+ VI | 30 / 462 / 25 (v1) | 32 / 450 / 36 |
| RSP completion (from start) | 0.656 to 0.667 VI | 0.679 to 0.699 |
| DP interrupt (from start) mean / median | 2.667 / 2.80 | 2.604 / 2.76 |
| DP completion of RDP-bound frames (185) | 2.903 | 2.877 (−1%) |
| Period of CPU-bound frames (136) | 3.225 | 3.294 (+2%) |
| PI DMA (per-frame mean) | 4.3 transfers / 7.8 KB / 0.100 VI (4.67 MB/s) | 4.3 transfers / 7.8 KB / 0.099 VI (4.73 MB/s) |
| Total VI (423 frames) | 1302 | 1312 (+0.8%) |

- The DK64 demo is RDP-bound ("wait for DP completion, then start the next one 0.13 VI later"), and **ares v7 has an RDP that is 1% faster and a CPU that is 2% slower, for a total of +0.8%**.
  The difference in the number of 4 VI frames is a matter of which way frames that sit on the 3 VI boundary tip, so **for DK64, v7 as it stands is equivalent to the console**.
- The EverDrive's PI DMA speed matches ares's model (the PI timing in 0007) exactly.
- The demo's frames are **400 thousand pixels / 14 thousand spans**, twice the size of dk64_797 (an intro frame, 210 thousand pixels) that I had been measuring in v15 and v22 to v24.

### v25: replaying the demo's frames themselves (VI off, console / ares v7)
| | d6052 | d6110 | d6253 | d6373 | d6513 | bar_2316 |
|---|---|---|---|---|---|---|
| Overall | 1.013 | 0.975 | 0.917 | 0.943 | 1.040 | 1.001 |
| Triangles | 1.005 | 0.957 | 0.886 | 0.917 | 1.033 | 0.997 |
| Texture load | 1.06 | 1.07 | 1.07 | 1.06 | 1.07 | 1.03 |
| In-game VI / VI off (console / ares) | **1.101 / 1.101** | 1.101 / 1.101 | 1.101 / 1.101 | 1.098 / 1.099 | 1.097 / 1.100 | 1.110 / 1.101 |
| z_near / ATOMIC_PRIM off / AA off | −12% / −1.5% / −0.5% | −12% / −1.5% / +0.7% | −16% / −1.5% / −0.6% | −16% / −1.9% / −0.6% | −14% / −1.9% / −0.5% | −20% / 0 / −0.2% |

On the NTSC console the VI is live, and **the game-VI-mode factor of 1.10 is as the model has it**. ATOMIC_PRIM is constant at 20 clk per triangle.

### v26 / v27: synthetic triangles with wide spans (64 to 320 px)
In BAR's light frame (bar_i2274), 43% of the pixels are in spans 128 to 320 px wide (the sky and the road surface), and the table extrapolates from measurements up to a width of 64 px
using constants for every 32 px. Measurements (clk/span, console / table):

| | w=32 | 64 | 128 | 192 | 256 | 318 | Per pixel (64→318) |
|---|---|---|---|---|---|---|---|
| 2cyc_tex_blend_z | 161/192 | 228/289 | 371/488 | 514/688 | 660/887 | 775/1037 (0.75) | 2.15 / 2.94 |
| 2cyc blend only | 108/120 | 171/181 | 299/307 | 427/432 | 555/558 | 667/668 (1.00) | 1.95 / 1.92 |
| 2cyc z only | 121/151 | 181/229 | 321/384 | 463/540 | 608/695 | 734/815 (0.90) | 2.18 / 2.31 |
| 1cyc_tex_blend_z | 147/182 | 210/294 | 334/513 | 459/731 | 585/951 | 665/1120 (0.59) | 1.79 / 3.25 |

At widths of 64 px and above, **the console's synthetic triangles are 25 to 40% cheaper than the table** (only for blend+Z modes), and this does not change with any of the following: the texture stride (1 / 4 / 1/4 texel/px),
perspective, I8/RGBA32, no texture, BAR's mode words, strips, or real triangles (v27).

### v28: replaying the game list's triangles split by maximum width (console / ares v7, triangles only, with Set commands (state) subtracted)
| | Width ≥64 px | 16 to 63 | <16 | tri_only overall |
|---|---|---|---|---|
| bar_i2274 (light) | **1.136** | 1.017 | 1.081 | 1.063 |
| bar_i2314 | 1.003 | 0.952 | 0.963 | 0.974 |
| bar_2316 | 1.049 | 0.969 | 0.981 | 0.997 |
| dk64_d6373 | 1.010 | 0.923 | **0.834** | 0.917 |

**In the game, wide triangles do not become cheap the way synthetic ones do** (1.00 to 1.14), and in the light frame they are on the contrary +14%. In DK64's heavy frames the thin triangles are −17%
(small triangles that are entirely hidden, where Z makes the whole span fail; consistent with z_near −16%). In other words, the cost of a triangle has a **scene-content-dependent ±10 to 15%**
(how things overlap, drawing order) that cannot be explained by the factors that vary across synthetic triangles (width, height, mode,
texture, placement, commands, buffer layout). In BAR's heavy frames and in DK64 it cancels out and lands inside the v7 table, whereas in BAR's light frame it stacks up in the same direction. There is
no solution that fits both at once with a factor per width or per type (the factors in the table above do not point the same way), and closing this gap requires a pixel-level RDP simulation (a memory access
model equivalent to angrylion).

### series v8
I put in three points confirmed on the console as 0012 (within error on the DK64 / BAR game bodies, no regression from v7):
1. The Z pass-rate estimate (H(n)/n from the previous frame's overdraw) is dropped in favour of a **fixed 1.0** (overdraw within a frame saves nothing).
2. **+20 clk for ATOMIC_PRIM triangles**.
3. **+3.5 clk/span for AA_EN in 1-cycle mode** (0 in 2-cycle).

## The MGC 2011 demo's picture running ahead of its music (mgc_tasklog, rdp_timing v29-v32, series v9) (added 2026-11-03)

MGC 2011 (Retroactive) is a "frame based" demo (its nfo says so) whose music plays in real time: every scene lasts a fixed number of
rendered frames, so **a part that ares renders faster than the console runs ahead of the music**. Per part, console recording (59.94 fps) vs ares:

| Part | Console [VI] | ares v8 | v8 diff | **ares v9 (0013)** | v9 diff |
|---|---|---|---|---|---|
| 1 (house, weathervane, night) | 1428 | 1428 | ±0 | 1451 | +1.6% |
| 2 (mountains, cliffs) | ≈2150 | 2081 | −3% | 2132 | −0.8% |
| 3 (blobs, planet) | ≈2220 | 2160 | −3% | 2174 | −2% |
| **4 (speaker room)** | **1753** | 1541 | **−12%** | **1732** | **−1.2%** |
| **5 (glass grid)** | ≈1440 | 1328 | **−8%** | 1375 | −4.5% |
| RETRO logo starts | ≈9050 (151 s) | 8620 (144 s, **about 7 s ahead**) | | **8950 (149 s, about 1.7 s)** | |

### Isolating the cause
- **mgc_tasklog** (`mgc_tasklog/`: the dk64_tasklog resident logger patched into the demo's libultra; it records 96 KiB from the start of part 4
  (after VI 5000, the first graphics task 3.8 VI or more after the previous one) and writes it to the SRAM; `hw/mgc_tasklog_hw_001.srm`, ares `ares_v8.srm`):
  on the console as in ares **a frame = the time to the DP interrupt + 0.006 VI (RDP-bound)**, **the RSP time is identical within 1%**, and only the
  RDP time is longer on the console: ×1.15-1.20 in part 4, ×1.06-1.09 in part 5, ×1.08-1.09 in the RETRO logo.
- **v29** (12 lists of parts 1-6 replayed, NTSC/PAL agree, `hw/v29_*`): console/ares for the triangles alone **part 4 1.17-1.27, part 5 1.08-1.12,
  part 1 1.20, part 6 1.15, part 2 0.98, part 3 1.04**; loads, rectangles and state commands 1.00-1.03.
- **v30** (the part 4 lists with one attribute changed at a time, `hw/v30_*`): EN_TLUT off (CI8 + TLUT textures), TLUT on for part 2 (RGBA16), dither,
  point sampling, BI_LERP, perspective, KEY_EN, FORCE_BLEND, coverage: all **±0.1%**; slopes zeroed ±0, no texture (0E→0C) / no shade (0E→0A) +0.6%,
  edges only (0E→08) +2.2%, 2-cycle +31% (model +30%). The attributes are not the cause.
- **v31** (synthetic write-only spans, widths 8-64, `hw/v31_*`): a 640-pixel-wide colour image (the demo's 1280-byte rows) costs **+2 clk/span** over a
  320-pixel one; placement (same column / same rows / side by side / same place), real triangles, slopes, texture, 512 wide and other addresses ±3%.
  With IMAGE_READ (blend) 640 wide is +4-6 clk/span, cancelled by the table being +12 too high there (why parts 2/3 match).
- **v32** (spans 1-16 px wide and triangles of 1-32 spans, `hw/v32_*`): the demo's triangles have a median span width of 8 px (36% under 4 px) and a
  median of 12 spans. The console (640 wide, 32-span triangles) is **+2.8 clk/span over the table** (nearly constant over widths 1-16; +1.2 at 320),
  and **a one-span triangle costs 65 clk** (edges only 26, shade 35, shade + texture 65) against 20 / 23 / 23 in the tables: a write-only triangle
  pays for its command words (the 8 texture coefficient words alone +30 clk), which the tables did not have (in the modes with depth or blending it is
  hidden under the 38+ clk fixed cost per span).

### The fix (patch 0013: `n64: write-only spans and triangles cost more on the console`)
Write-only (no IMAGE_READ, no depth) 1/2-cycle spans get **+1.2 clk (colour image up to 320 px wide) / +2.8 clk (wider)** and write-only triangles
**+5 (edges) +7 (shade words) +30 (texture words)** (`writeOnlySpan` / `writeOnlySpanWide` / `writeOnlyTri` / `writeOnlyShade` / `writeOnlyTexture`
in `rdp.hpp`). The 12 lists come out at console/ares 0.96-1.06 (mean 1.02), the demo itself as in the table above, and DK64 / BAR are unchanged
(their triangles read the colour or depth buffer; the series v9 row of the results table). Harness knobs: `T_WOSPAN` / `T_WOSPANW` / `T_WOTRI` /
`T_WOSHADE` / `T_WOTEX`. The remaining −4.5% (part 5) is in scenes mixing 1CYC16_BLEND (read-modify-write) spans, where v31's blend640 +4-6 clk/span
is left to cancel against the table's excess.

## Test ROMs v33-v35: the structure of the triangle fixed cost, rectangles and loads, buffer placement (added 2026-11-08)

After MGC, three synthetic ROMs measured the remaining per-class residuals (BAR intro −10%, v15's rectangles 1.01-1.09 and loads 1.06-1.07,
v24's "same megabyte"): NTSC, `hw/v33_*`-`hw/v35_*`, sources in `rdp_timing/v33`-`v35`.

### v33: the fixed cost of a triangle and the span cost in the games' modes (depth and blending)
BAR's 2-cycle mode (0x182c0f / 0x0c182078) and DK64's 1-cycle mode (0x882c0f / 0x00552078), triangles of 1 / 2 / 4 / 8 / 32 spans × widths 1-32 px,
decomposed as `clk/triangle = T + H × s(w)`:

| | console T (fixed) | console s(w) − ares s(w) | one-span triangle (0F) console / ares |
|---|---|---|---|
| BAR 2cyc | 40-50 clk | −9 (w1) to −16 (w16), −26 (w32) | 116 / 72 |
| DK64 1cyc | 44-56 clk | −15 to −20, −36 (w32) | 124 / 93 |
| ares | −3 to +35 (about 0) | | |

- The console is "**about 45 clk per triangle + the span table without the +15**", ares is "fixed cost about 0 + span +15". For an ordinary triangle of
  8-15 spans the totals agree (why the game lists matched in v15/v22); lists with many small triangles come out cheap in ares, tall triangles dear.
  This is v22 W's conclusion again (fixed 45, no penalty); applying it to the game lists then came out 10-20% too cheap and still does (the games'
  triangles carry a content-dependent +10-20% the synthetic ones do not: v23/v24/v28). Switching to the synthetically correct structure would break
  the games, so **the structure of the triangle cost is left as it is**.
- The command-word fixed cost exists in the depth modes too (edges only 09: 88.5 → 0F: 116; ares 69 → 72).
- Spans whose depth test fails as a whole are 27-54 clk cheaper on the console (as v24). ares' estimator returns a pass rate of 1 on the synthetic
  frames and comes out higher instead; in the games it estimates from the previous frame's overdraw, a separate matter.
- MGC part 5's 1CYC16_BLEND (640 wide): one-span triangle console 73 / ares 49, and ares 6-8 clk/span too high from 8 spans up — the −4.5% of part 5.

### v34: rectangles (R) and texture loads (L)
- Rectangles (1x1-128x128, 1024 each, random positions, console/ares): write-only Fill / Texture 1- and 2-cycle / Copy / Flip are **within ±5%** from
  4x4 up (1x1 alone +18-40%: a missing fixed cost); Load_Tile / Sync_Pipe / Set_Tile per rectangle also ±5%. **Rectangles with IMAGE_READ + blend are
  10-20% too dear in ares, with primitive depth 25-35%** (their spans use the triangle table + 15); 640-wide fills are 5-15% too cheap. BAR's
  crash-scene rectangles were 1-9% dearer on the console in v15, the opposite direction, so no depth/blend rectangle correction is applied
  (rectangles have a content-dependent difference too).
- Loads (512 each, `hw/v34b`-`v34d`): Load_Tile (RGBA16 8x8-64x32, RGBA32, I8, CI8, CI4), Load_Block (every format), Load_TLUT 16 / 256,
  interleaved with drawing, without Sync_Tile — **all console/ares 1.00-1.03**. The only systematic difference is the **source row stride**: the same
  32x32 RGBA16 tile out of a 320-pixel-wide image (640 B rows) +4.8%, out of a 640-pixel-wide one (1280 B) +8.5% (+1.2 / +2.3 clk per row).
  DK64's +6-7% is explained by this (partial loads out of wide images).
- Load_Tile / Load_Block of 4-bit textures **hang the RDP on the console** when the tile and the image are declared 4-bit (v23's ci4_tlut, three
  times in v34/v34b/v34c). Declared as libultra's `_4b` macros do (Load_Tile: an 8-bit image/tile of half the width, Load_Block: a 16-bit image of
  width 1) they work and cost the same as 8-bit.

### v35: where the colour and depth buffers sit in RDRAM
| depth-tested spans (console, relative to another megabyte) | width 4 | 13 | 32 | 64 |
|---|---|---|---|---|
| BAR 2cyc, colour and depth in the same megabyte (150K-768K apart or depth below: identical) | ×1.10 | ×1.17 | ×1.165 | ×1.165 |
| DK64 1cyc, same megabyte | ×1.10 | ×1.16 | ×1.20 | ×1.20 |
| colour straddling a megabyte boundary (partly the same megabyte) | ×1.01-1.02 | | | |
| 640 wide, same megabyte | ×1.05 | ×1.11 | ×1.13 | ×1.175 |
| blend only (no depth) / write-only / texture source in the same megabyte | ×1.00 / ×1.00 / ×1.01 | | | |

Inside the megabyte the distance does not matter; what counts is whether the depth and colour read-modify-writes hit the same 1 MiB bank.

### The fix (patch 0014: `n64: depth buffer in the colour buffer's megabyte, Load_Tile source stride`)
1. Set_Mask_Image is parsed; a depth-tested span whose depth row and colour row are in the same megabyte costs **×1.10 (up to 8 px) / ×1.165 (2-cycle)
   / ×1.20 (1-cycle)** (`sameBankNarrow` / `sameBank2` / `sameBank1` in `rdp.hpp`). DK64 / BAR / MGC use different megabytes: unchanged. Many 4 MiB
   games are affected.
2. Load_Tile rows whose source stride is 512 B or more cost +1.2 clk each, 1024 B or more +2.3 (`loadStride` / `loadStrideWide`).
The 640-wide fill's +5-15% hit DK64's depth clear (it fills the depth buffer as a 640-wide image) and worsened its score 29 → 33, so it is left out.
Series v10 row of the results table: DK64 29 (24/423/53), BAR 251/331, MGC RETRO logo at 8950 — as v9. Harness knobs `T_BANKN` / `T_BANK1` /
`T_BANK2` / `T_LDSTRIDE` / `T_LDSTRIDEW`.

## Test ROMs v36-v38: eliminating the candidate reasons for the games' triangles costing more than synthetic ones (added 2026-11-09)

The "game lists' triangles are 10-20% dearer than synthetic triangles with the same span statistics" of v22 (v33's structure: the console is
about 45 clk per triangle + spans cheaper than the tables, ares is fixed cost about 0 + spans +15; the totals agree for ordinary triangles) was
attacked with three ROMs covering what the synthetic frames had not measured (NTSC, `hw/v36_*`-`hw/v38_*`, `rdp_timing/v36`-`v38`, ares = series v10).

### v36: one property at a time on synthetic triangles (BAR 2-cycle, 8 spans, widths 4 / 13 / 32 / 64)
| change | console (base 709 / 933 / 1347 / 1905 clk/triangle) | ares model |
|---|---|---|
| buffers off the 2 KB page (DK64's +576 B / +1536 B, +1 KB, +64 B) | all five **identical** (±0.1%) | no effect (same) |
| texture axes rotated 90° / 45°, 4 texels/px along T | **identical** (±0.1%) | no effect (same) |
| entirely right of the scissor (offscreen) | **88 clk** (2cyc) / **130** (DK64 1cyc) / 88 (write-only) | 0 / 10 / 51 |
| zero area (8 rows of width 0) | **436 clk** = about 44 per row + about 45 fixed | 0 |
| edges slanted 8 px per row (shear8) | **+19% / +13% / +8% / +5%** ≈ the span cost at width +6 px | −3 to −5% (centre-line width) |
| right half / left half outside the scissor | right: 795 at w4 (same as an unclipped one), 1163 at w64; left: 407 / 1674 — **asymmetric** | both equal (visible width only) |
| lower half of the rows outside | 0.83-0.96 × the four visible rows (the clipped rows are free) | free |

- Rows outside the scissor and zero-width rows are not free on the console (about 44 clk per row, 88-130 per triangle). Counted in the game lists,
  though, even where 33-43% of the rows (BAR's light frames) or 31% (DK64 d6373) lie outside the scissor, it comes to **1-2%** of the clocks, not 10-20%.
- The union of the four sub-scanlines' extents (shear8's +6 px) is how the rasterizer works (`T_WIDEN`); on the game lists it moves every list
  uniformly by −1 to −3% and does not narrow the differences between lists. That amount is, however, exactly what MGC's parts 4 and 5 lacked, and
  the comparison with the console's task log led to its adoption (the section below, patch 0015).

### v37: the games' lists with only the triangle shapes changed (console / ares v10, VI off)
| list | full | tri_vert (vertical edges) | halfslope (slopes halved) | snap (whole pixels) | nosmall (rows < 4 removed) | onlysmall | shrink (half size) |
|---|---|---|---|---|---|---|---|
| bar_2316 | 1.036 | 0.96 | 0.99 | 1.03 | 1.00 | 1.01 | **1.06** |
| bar_i2274 | 1.054 | 1.01 | 0.995 | 1.05 | 1.04 | 1.06 | **1.08** |
| bar_i2314 | 0.988 | 0.95 | 0.945 | 0.99 | 0.97 | 1.03 | 1.035 |
| dk64_797 | 1.004 | 0.96 | 0.955 | 0.99 | 0.95 | 1.09 | **1.12** |
| dk64_d6373 | 0.960 | 0.97 | 0.965 | 0.96 | 0.96 | 1.07 | 1.00 |
| mgc_p4_4316 | 1.072 | 1.06 | 1.05 | 1.07 | 1.07 | 1.01 | 0.96 |
| mgc_p2_1802 | 0.984 | 0.985 | 0.98 | 0.98 | 0.98 | 1.03 | 1.00 |

- Vertical edges, halved slopes and snapping move the console's cost by ±5% at most, and ares by about the same; the pattern of the per-list
  differences does not change.
- `tri_dblslope` (slopes doubled) makes triangles whose edges cross; ares draws the crossed spans with left and right swapped, the console draws
  nothing (the model alone grows to 1.06-1.34). Not a valid probe, so it is left out of the table.
- The only variant consistently on the console's side is `tri_shrink` (the smaller the triangles, the relatively dearer the console: 1.06-1.12 for
  BAR / dk64_797) = v33's per-triangle fixed cost.

### v38: the games' lists with only the drawing order changed (console / ares v10, VI off)
Runs of consecutive triangles between state commands were reversed (reverse), split into even then odd (interleave), sorted by top row (sorty),
and thinned to the first half / second half / every other triangle (every2: the overlap between neighbours mostly disappears).

| list | full | reverse | interleave | sorty | firsthalf | secondhalf | every2 |
|---|---|---|---|---|---|---|---|
| bar_2316 | 1.036 | 1.007* | 1.009* | 1.007* | 1.009 | 1.032 | 1.004 |
| bar_i2274 | 1.053 | 1.055 | 1.054 | 1.054 | 1.053 | 1.038 | 1.047 |
| bar_i2314 | 0.988 | 0.985 | 0.988 | 0.986 | 0.988 | 1.005 | 0.981 |
| dk64_797 | 1.003 | 1.004 | 1.004 | 1.002 | 1.018 | 1.017 | 1.017 |
| dk64_d6373 | 0.960 | 0.960 | 0.960 | 0.960 | 0.962 | 0.990 | 0.969 |
| mgc_p4_4316 | 1.072 | 1.072 | 1.072 | 1.072 | 1.096 | 1.031 | 1.066 |
| mgc_p2_1802 | 0.984 | 0.983 | 0.983 | 0.983 | 0.987 | 0.991 | 0.979 |

- The console is **the same within ±0.3%** whatever the order inside a run (all seven lists): there is no cost from neighbouring triangles
  touching the same pixels or the same RDRAM rows. The halves and every-other variants move the console and ares by the same ratios (every2's
  triangle part: console 0.55-0.62, ares 0.55-0.62).
- *ares alone moves +2.7% on bar_2316's reorderings because its depth pass-rate estimate changes with the order; the console does not move, so that
  much is noise in ares' pass-rate estimate.

### Conclusion
v36-v38 rule out placement, texture orientation, rows outside the scissor, zero-width rows, edge slope, snapping, size class, drawing order and
adjacency as the reason the games' triangles cost more than synthetic ones. The per-list residuals (MGC p4 +7%, BAR i2274 +5%, DK64 d6373 −4%) are
neither a property of individual triangles nor of their arrangement: they are constants over a whole list. What remains is v33's split between fixed
cost and span cost, which was re-examined against all of the v33, v37 and v38 console data with the harness knobs `T_TRIOV` (fixed cost per triangle)
and `T_TRISP` (added per triangle span, replacing the default spanPenalty of 15):

| fixed / per span (harness) | game lists (the 7 lists of v37 + v38, every variant: geometric mean of console/ares, spread) | synthetic (v33, 8 spans, console/ares) | one-span triangles (v33 h1) |
|---|---|---|---|
| 0 / +15 (series v10, current) | 1.01, σ 4.5% (full 0.96-1.07, shrink 1.03-1.12) | 0.88-0.93 (BAR), 0.88 (DK) | 1.25-1.6 (0F), 1.3 (09) |
| 45 / +3 (the synthetic structure of v22/v33) | **1.05, σ 4%** (full 1.01-1.12, shrink 1.04-1.10) | **0.93-0.95 / 0.90** | **0.95-1.0**, 09: 0.75 |
| 45 / +8 | 1.01, σ 3.9% (full 0.97-1.07, shrink 1.00-1.07) | 0.89-0.91 / 0.86 | 0.9-0.95 |
| 30 / +10 | 1.01, σ 3.9% | 0.89-0.91 / 0.86 | 1.0 |

- The split that fits the synthetic frames (45 / +3) fixes the one-span triangles (1.25-1.6 → 0.95-1.0) but puts **the game lists
  uniformly +5% off** (+12% for BAR 2316), and the split that fits the games leaves the synthetic 8-span triangles 10-14% too dear:
  the conflict of v22 stands. The fixed cost would also have to follow the command words (09 → 18, 0D → 30, 0F → 45, as 0013 does), which
  does not fill the games' +5-12% either. **The re-split of the tables (the 0015 candidate) is on hold**; the series stays at v10.
- What still differs between the synthetic frames and the games is **how the depth test fails**. The synthetic frames only measured "every pixel
  passes" and "every pixel fails" (the latter 27-54 clk/span cheaper), and ares blends the two tables linearly by the pass rate. In the games
  it is normal for only some of a span's pixels to fail (overdraw); if the console treats such a span like a passing one (memory traffic per
  block), the −10 clk/span ≈ 10% that ares subtracts at its estimated pass rate of about 0.7 is exactly the games' excess. The 2.7% that ares
  alone moved on bar_2316's reorderings in v38 comes from the same estimator. ROM v39 measures this (`rdp_timing/v39`: the depth buffer preset
  near on every 1 / 2 / 4 / 8 / 16 / 32 columns, every other row, random 25 / 50 / 75% of the pixels; BAR 2-cycle / DK64 1-cycle, widths 4-64).

The harness knob `T_WIDEN` (0: centre-line width = series v10, 1: the union = 0015's default, 2: whole pixels) is in 9999.

## The end of the MGC 2011 demo (the "@" and the "clank"), v39, patch 0015 (added 2026-11-09)

### Symptom and console
An "@" appears beside the RETRO logo and a "clank" sounds with it. In the console recording (`MGC2011__2_.mp4`, 60 fps) **the "@" appears at
156.70 s and fades at 158.3 s, and the spectrogram shows a harmonic percussive hit (the clank) at 156.7 s** — simultaneous on the console. The music
runs in real time and the "@" is placed by frame count, so in ares the "@" comes early by however much parts 4 onwards run faster than the console.

### Direct comparison with the console's task log (mgc_tasklog, start of part 4 to logo frame 2233)
The console log of `mgc_tasklog` (`hw_001_frames.txt`, 2234 frames from VI 5764) was matched frame by frame with the log the same ROM writes in
ares (`ares_v10_frames.txt` / `ares_v11_frames.txt`, cut out of a RAM dump; the part boundaries at frames 1217 / 1968 coincide, and the loading
gaps of 6.25 / 15.7 VI agree within 0.2 VI, so the PI side is right):

| | console | ares v10 | ares v11 (0015) |
|---|---|---|---|
| start of part 4 (the logger's VI number) | 5764 | 5751 (−13) | 5822 (+58) |
| part 4 (frames 0-1216) | 1813 VI, dp-go 1.482 | 1744 (**−3.8%**), 1.424 | 1782 (**−1.7%**), 1.455 |
| part 5 (1217-1967) | 1446 VI, dp-go 1.901 | 1396 (**−3.5%**), 1.835 | 1417 (**−2.0%**) |
| RETRO logo (1968-2233) | 362 VI | 365 (+0.8%) | 369 (+1.9%) |
| VI of frame 2233 (6 frames before the "@") | 9385 | 9256 (**−129 VI = 2.2 s early**) | 9390 (+5 VI) |

The RSP (go→done) agrees within ±1%; the whole difference is the RDP (dp-go). Per 100 frames ares/console is 0.93-0.98 in part 4 and 0.95-0.99 in part 5.

### v39: the cost of a span of which only some pixels fail the depth test
The remaining difference between the synthetic frames and the games, "how the depth test fails", was measured (`hw/v39_*`, `rdp_timing/v39`:
BAR 2-cycle / DK64 1-cycle, 8 spans, widths 4-64, the depth buffer preset in column / row / random patterns). The console's position between
pass (0) and fail (1) at the same width:

| pattern (failing pixels) | w4 | w8 | w13 | w32 | w64 |
|---|---|---|---|---|---|
| every other pixel (50%) | 0.06 | 0.05 | 0.04 | 0.02 | 0.03 |
| random 50% / 25% / 75% | 0.13 / 0.03 / 0.40 | 0.09 / 0.03 / 0.26 | 0.09 / 0.03 / 0.24 | 0.04 / 0.01 / 0.13 | 0.05 / 0.02 / 0.14 |
| blocks of 4 / 8 columns | 0.25 / 0.39 | 0.14 / 0.29 | 0.15 / 0.28 | 0.05 / 0.12 | 0.07 / 0.16 |
| blocks of 16 / 32 columns / every other row (whole spans) | 0.47 / 0.46 / 0.47 | 0.46 / 0.46 / 0.48 | 0.52 / 0.47 / 0.50 | 0.45 / 0.50 / 0.50 | 0.54 / 0.58 / 0.49 |
| random 8-pixel blocks 50% | 0.44 | 0.39 | 0.39 | 0.34 | 0.34 |

DK64's 1-cycle mode behaves the same. **Pixels failing individually save the console almost nothing (2-13% of the way from pass); the saving appears
only when 16 or more consecutive pixels fail (or the whole span does)**, and then it is linear (0.5). ares' "blend the pass and fail tables by the
pass rate" is right if the rate is read as the fraction of spans that fail as a whole, and too cheap if it is a per-pixel rate. Turning every
depth saving off in the replay harness (`T_ZPASS=1`) moves the game lists by only 0-3%, though, so this is not the main part of v37/v38's +5-12%
against the synthetic frames. Re-counting the estimator in runs of 16 pixels needs the in-game depth distribution and is deferred.

### The fix (patch 0015: `n64: triangle spans cover the union of the four sub-scanlines`)
As v36's shear8 showed (edges moving 8 px per row cost like a span 6 px wider: console +19/+13/+8/+5%, ares −3 to −5%), each row's span width
is now **the union of the four sub-scanlines' extents (row +0.125 / +0.375 / +0.625 / +0.875)** instead of the width at the row centre (the triangle
row loop in `timing.cpp`; the set of rows drawn stays the centre-line one). v36's shear8 goes from console/ares 1.22 / 1.04 / 0.94 / 0.86 to
0.97 / 0.92 / 0.87 / 0.81 (= the same ratios as the unslanted base). The 12 MGC lists (VI off): console/ares mean 1.025 → 1.005 (part 4 1.031 → 1.005,
part 5 1.040 → 1.024); the game lists (v37/v38) 1.01 → 0.99 (BAR 1.01-1.03, DK64 0.95-0.97, MGC p4 1.046). Results-table row series v11: DK64 36
(24/421/55: two more 3-VI frames), BAR 251/331 → 252/328, crash +642/+666 — all within one frame. MGC as in the table above (the "@" −129 VI → +5 VI).

### What is left
- Parts 4 and 5 are still −1.7 / −2.0% and the logo +1.9%. The "@" lands partly by cancellation against parts 1-3 (+58 VI in v11), so a **console log
  of parts 1-3** is needed: the two ROMs of `mgc_tasklog/v2` (`hook_a`: from the first task, `hook_b`: from the part 2 → 3 gap) join up into a
  timeline of the whole demo.
- In the console recording the black between parts 1 and 2 lasts 102 frames (ares' loading gap is 34 VI); whether the load is slower or black is being
  drawn, log a will tell.
- The factor for MGC's VI mode (0x3242, VI_WIDTH 1280): in the console replay the part 4-6 lists show **no VI effect** (game/vi_off 1.000, and 1.000 for
  everything on the PAL console), only the NTSC console's part 1-3 lists +3%; ares applies +2.5% to all of them → v40 / patch 0016 (next section).

## Console logs of parts 1-3, the VI slowdown by span class (v40), patch 0016 (added 2026-11-09)

### mgc_tasklog v2: the timeline of the whole demo
The console logs of `hook_a` (from the first graphics task) and `hook_b` (from the part 2 → 3 gap) (`hw/mgc_tasklog_hw_a_96k.srm` / `_b_96k.srm`,
`mgc_tasklog/hw_a_frames.txt` / `hw_b_frames.txt`), together with v1's parts 4-6, let the whole demo be matched to the console frame by frame
(`mgc_tasklog/parts.py`, result `parts_v10_v11_v12.txt`). The part boundaries fall on the same frame numbers as in ares in all three logs
(a: 1232, b: 1345, c: 1217 / 1968) and the loading gaps are the same length (a: 34 VI, b: 5 VI) — the 102 black frames of the console recording
were a fade the demo draws, not loading.

| segment (log: frames) | console VI | ares v10 | v11 (0015) | v12 (0016) |
|---|---|---|---|---|
| part 1 (a 0-1231) | 1413 | 1411 (−0.1%) | 1416 (+0.2%) | 1416 (+0.2%) |
| part 2 (a 1232-2129, 82%) | 1814 | 1803 (−0.6%) | 1831 (+0.9%) | 1829 (+0.8%) |
| part 3 (b 0-1344) | 2163 | 2183 (+0.9%) | 2215 (+2.4%) | **2238 (+3.5%)** |
| part 4 (c 0-1216) | 1813 | 1744 (−3.8%) | 1782 (−1.7%) | **1753 (−3.3%)** |
| part 5 (c 1217-1967) | 1446 | 1396 (−3.5%) | 1417 (−2.0%) | 1424 (−1.5%) |
| RETRO logo (c 1968-2232) | 362 | 365 (+0.8%) | 369 (+1.9%) | 365 (+0.8%) |
| total | 9011 | 8902 (−1.2%) | 9030 (+0.2%) | 9025 (+0.2%) |
| VI just before the "@" (c 2233) | 9385 | 9256 (−129) | 9390 (+5) | 9384 (−1) |

From the logo on (`hook_c`, `hw/mgc_tasklog_hw_c_96k.srm` / `mgc_tasklog/hw_c_frames.txt`, console VI 9021-11199, 1866 frames): the frame that
shows the "@" (c 274 = v1's 2242) is at **console VI 9393, ares v12 9393 (±0)**; the whole logo (c 0-265) console 362 / ares 365. The fade of the MGC
poster after it (c 400-999, texture rectangles with blending) is +5-8% in ares (v34's "blend rectangles 10-20% too dear"), +58 VI by the time the
poster stands still at VI 10334; from there 1-VI frames run to the end and the difference stops growing (nothing visible).

Parts 1 and 2 (60 fps / CPU-bound) are within 1%. The RSP is within ±2% in every part. What remains is all RDP (dp-go): part 3 (glass balls,
mostly blending) is too dear in ares and part 4 (write-only textured triangles) too cheap — up to v11 the wrong VI factor partly hid both (next item).

### v40: the VI's slowdown of the RDP by span class × VI mode × displayed buffer
`hw/v40_*`, `rdp_timing/v40`. 8 spans per primitive × 512, ratio to the VI off (console):

| span class | aa3 0x3302 | aa0 320 wide (BAR / DK64) | 640x480i aa0 (0x13052) | MGC 0x3242 (aa2, 2560 B/line) |
|---|---|---|---|---|
| with depth (BAR 2cyc width 13 / 32, DK64 1cyc) | **×1.006** | **×1.141 / 1.132 / 1.144** | ×1.43 / 1.36 / 1.41 | **×1.065 / 1.060 / 1.067** |
| IMAGE_READ + blend, no depth (320 / 640 wide) | ×1.005 / 1.004 | ×1.139 / 1.089 | ×1.315 / 1.377 | ×1.060 / 1.026 |
| write-only (320 / 640), Fill rectangles (320 / 640), Load_Block | **×1.000** (every mode, every displayed buffer) | | | |
| ares v11's factor (applied to every span and load) | ×1.004 | ×1.117 (on the table part) | ×1.324 | ×1.03 |

- ~~**The VI slows only the spans that read the colour or depth buffer back**; write-only spans, fills and TMEM loads are ×1.000~~ — **withdrawn
  (2026-11-10)**: the VI probe in each row (last column) shows that from `wo_tex*` / `fill*` / `load` and `blend640`'s `depth_buffer` on, the probe
  stayed at the VI-off value (≈93000): **the console's VI had stopped fetching** (a known effect of switching VI modes many times in this ROM series;
  v29's "1.000" for MGC parts 4-6 and v37's two MGC lists are the same, probe ≈93000). Only the measurements with the VI alive (probe 94100-122000)
  stand: depth ×1.14 (aa0) / ×1.065 (MGC), blend ×1.14 / 1.06 (320 wide), ×1.09 / 1.03 (640 wide), and **a write-only list (mgc_p4_4316, first in v42)
  ×1.035 in the MGC mode**, v29's part 1-3 lists ×1.03. So the VI factor does not depend on the span class: whole lists ×1.03-1.035 in the MGC mode,
  ×1.10-1.12 in the 320-wide aa0 modes.
- The factors themselves: 1.14 for aa0 at 320 wide (ares 1.117), 1.06 in MGC's mode (ares 1.03: aa2 grows too once the line is 2560 B), 1.32-1.43 at 640x480i (ares 1.324).
- Pointing the displayed buffer (VI_ORIGIN) at the colour buffer being drawn or at the depth buffer changes nothing for depth spans (±0.5%). Only the
  640-wide blend case loses its slowdown entirely (×1.000) when the VI displays the depth buffer (0x6A0000, the colour buffer's megabyte); unexplained,
  so the factor for a 4 MiB game whose displayed and drawn buffers share a megabyte cannot be settled from this alone.

### The fix (patch 0016: `n64: the VI slows only read-modify-write spans; AA0 x1.14, AA2 grows with the line`) — **reverted by 0017**
`timingScale` applies the VI factor to read-modify-write (memoryBound) spans only — not to write-only spans, fills, copies or TMEM loads.
`viFactorAA0` 1.117 → 1.14; aa2 grows by 0.33 per 640 bytes of line above 640 (`viWidthScaleAA2`: 1.06 at 2560 B). Results-table row series v12:
DK64 36 (24/420/56), BAR 252/328, crash +641/+667 — as v11. MGC as in the table above. Harness knobs `T_VIRMW=0` (old behaviour), `T_VIAA2` / `T_VIAA2WS`.

### v41: the cost of the RDP reading the list while the RSP writes it (the fifo microcodes' path)
The difference between a replay (the RDP fetching a list placed in RDRAM) and a real frame (the RSP's F3DEX fifo writing the list while the RDP
follows) was measured by feeding the same lists through a small RSP microcode (`rdp_timing/v41/rsp_xbus.S`) that DMAs each chunk into an RDRAM
output buffer and advances DP_END (`hw/v41_ntsc_003.txt`, 9 MGC + 3 BAR lists, chunks of 256 B to 2 KiB, VI off). Console `fifo / pad1k`:

| list | fifo2k | fifo1k | fifo512 | fifo256 | ares v12 (fifo1k) |
|---|---|---|---|---|---|
| MGC part 4 (3 lists) | +7.8-10.7% | +7.8-10.7% | +8.2-11.3% | +13.2% | +7.0-9.5% |
| MGC parts 5 / 1 / 2 / 3 / 6 | +3.2-9.7% | +2.8-9.7% | +3.3-9.9% | +4.4-12.4% | +2.7-8.2% |
| BAR (3 lists) | +3.6-4.2% | +4.0-4.2% | +5.0-5.6% | +5.2-5.4% | +4.2-4.4% |

- The RDP's loss per chunk pair (one DMA in, one out): 590-760 clk at 2 KiB on the console (ares 586-650), 330-390 at 1 KiB (ares 322-353), 190-225
  at 512 B (ares 177-266). ares' "9 + 1.2 clk per 8 B per DMA" is within ±3% for BAR (320 wide, with depth) and, for MGC (640 wide), 15% short on
  large DMAs and too high on small ones (the console: 1.3-1.5 clk per 8 B on the 640-wide lists, 0.9-1.0 on the 320-wide ones).
- In the demo's part 4 frames ares charges DMA contention worth 2.1% of the frame (870 DMAs per frame), so ×1.2 would add +0.4%: **part 4's −3.3%
  is not the cost of following the RSP's output** (the CPU's bus traffic remains the candidate: part 4 is the only part where the CPU runs the whole frame).
- The XBUS path (the RDP reading DMEM) could not be measured: the console's RDP stalled in the first chunk (DP_STATUS 0x760, CURRENT 0x7C0, the
  next DP_START/DP_END left pending) — not the handshake the .xbus microcodes use. A by-product: ares (upstream a774671d too) reads XBUS commands
  from DMEM **in the wrong byte order** (`render.cpp` reads through the `Memory::Writable` base class); MGC / BAR / DK64 are fifo, so unaffected;
  fixed in the harness.

### What is left (MGC)
- With the VI factor right, part 3 is +3.5% (640-wide 1CYC16_BLEND: in v33 too ares was 6-8 clk/span too dear from 8 spans up) and part 4 −3.3%
  (write-only textured triangles: 3% short in the demo's frames even after v32/0013, while the list replay (VI off) gives 1.005 — the difference between
  replay and demo, the RDP waiting on commands arriving from the RSP through the XBUS FIFO, is the candidate). Both are table-side fixes for MGC's two
  modes and do not touch DK64 / BAR.
- The "@" lands on its sound within ±5 VI (0.1 s) in both v11 and v12.

## v42 (replays under CPU / SP DMA loads), 0016 reverted (0017, series v13) (added 2026-11-10)

### Reverting 0016
v40's "the VI does not touch write-only spans, fills and loads" was, as the VI probe shows, **measured with the VI stopped** (note in the v40 section).
With the VI alive a write-only list is slowed ×1.035 in the MGC mode too (first list of v42, probe 94070), about the same ×1.03 as the depth lists.
0017 reverts 0016; the VI factors are series v11's again (every span and load: aa0 1.117 / aa2 1.03 / aa3 1.004, 640x480i 1.324).
MGC's six parts are as in v11 (+0.2 / +0.9 / +2.4 / −1.7 / −2.0 / +1.9%, +0.2% in total, the "@" +5 VI), DK64 36 (24/421/55), BAR 252/328.
The harness keeps the 0016 behaviour behind `T_VIRMW=1 T_VIAA0=1.14 T_VIAA2WS=0.33`. The VI stopping is a test-ROM problem (`set_vi` rewrites only
VI_CTRL / V_SYNC), so the VI-class measurement will be redone with a `set_vi` that rewrites every register and checks the probe (v43).

### v42: replays with CPU / SP DMA loads running beside the RDP (`hw/v42_*`, `rdp_timing/v42`)
The loads of v10-v12 (dependent CPU cache misses / instruction fetches / random writes / mixed / uncached writes, 64 B / 512 B RSP DMAs) beside 6 MGC
+ 2 BAR + 2 DK64 lists, `full` (RDRAM) and `fifo1k` (RSP feeding). RDP clocks lost per load operation (VI off, `full`, console | ares v12):

| load | console (depth lists: BAR / DK64 / MGC p2 p3 p5) | console (write-only: MGC p4 ×3) | ares |
|---|---|---|---|
| SP DMA 64 B (read) | 8.1-10.4 | 10.0-10.4 | 18.1 |
| SP DMA 512 B (read) | 69-73 | 72-74 | 85.6 |
| CPU dependent miss (16 B fill) | 8.5-10.6 | **11.4-11.8** | 10.1 |
| CPU instruction fetch (32 B) | 5.1-8.0 | 6.5-6.8 | 9.3 |
| CPU random write (fill + write-back) | 12.1-15.0 | 14.1-14.6 | 9.8 |
| CPU mixed (load/store/ALU/FPU) | 20.9-25.4 | 24.5-25.3 | 19.7 |
| CPU uncached write | 2.0-3.6 | 2.5-2.7 | 3.6 |

- The SP DMA numbers reproduce v19b's synthetic ones (64 B 10 / 512 B 76). ares' "9" in 9 + 1.2 clk per 8 B came from DK64 in-game (1164 DMAs per
  frame at 51 clk each, task_timing v20b) and stays: in-game DMAs cost more than the synthetic ones.
- CPU: dependent misses agree (+15% on the write-only lists only), instruction fetches are 40% too dear in ares, random writes (write-backs) 30% too
  cheap, mixed 15-20% too cheap, uncached writes match v17's VI-off value (2.7).
- Running the demo with coefficients set to v42 (`T_CPUXI=5.5 T_CPUXW=6 T_CPUXU=2.8 T_DMAXB=0.5 T_DMAX=1.12`) improves parts 1-3 to −0.3 / −0.3 /
  +1.3%, the logo to 0.0% and the start of part 4 to +1 VI, but leaves parts 4 and 5 at −2.6 / −2.7% (the "@" −85 VI), BAR seg1 328 → 332, DK64 27 → 30.
  Since it contradicts the in-game DMA calibration it is **not adopted**; kept as calibration data.
- `fifo1k` and the CPU loads are additive (±1%). The "game" VI mode in this ROM is alive only for the first list (mgc_p4_4316: ×1.035, ×1.064-1.081
  under the CPU loads); from the second list on the probe is at the VI-off value (the basis for reverting 0016).

### Blend table correction (tried, not adopted)
Applying v33 / v34 / v40's "blend-only (IMAGE_READ, no depth) spans: 12 (320 wide) / 7 (640 wide) of the +15 too much, and +27 fixed per triangle
missing" in the harness (`T_BLSP=12 T_BLSPW=7 T_BLTRI=27`) lines up the synthetic frames (blend640 8 spans 0.87-0.96 → 0.94-0.99, one span
1.17-1.48 → 0.96-1.06, 320-wide blend rectangles 0.80-0.97 → 0.98-1.04), but in the demo part 3 +3.5 → +1.7% and the poster +58 → +26 VI are paid
for with part 2 +0.8 → −1.5%, part 5 −1.5 → −3.4% and the "@" −1 → −122 VI. The games' blend spans are also 10-15% dearer than synthetic ones (the
same content dependence as the depth spans), so it is left out.

### What is left for MGC (as of 2026-11-10)
Part 4 −1.7%, part 5 −2.0% (write-only / blend at 640 wide: right in the replays, short in the demo's frames), part 3 +2.4%, logo +1.9%.
v41 (RSP feeding: +9.5% console, 8.8% ares) and v42 (CPU contention) account for under 1% of the replay-vs-demo difference; the rest is unexplained.
The "@" is at +5 VI (0.08 s).

## v43 (the VI factor re-measured; console data pending) and the MGC 2011 demo's memory bandwidth (added 2026-11-10)

### v43: ROM re-measuring the VI factor per span class (`rdp_timing/v43`, console data pending)
Because v40's write-only / fill / load / blend640 rows were measured with the VI stopped (the reason 0016 was reverted), the same measurement is
repeated with the VI checked alive. Every VI switch now goes through CTRL = 0 for two fields and rewrites **all 14 VI registers** (the procedure of
libdragon's `vi.c`; interlaced modes clear the low bit of V_TOTAL); a VI probe (4096 dependent line fills with the RDP idle: +0.4% to +31% dearer than
VI-off when the VI is fetching) is taken before and after each case, and the mode is re-initialised up to four times until the probe before the case
exceeds the VI-off value + 200 ticks. Rows whose `alive` flag (both probes above) is 0 are not used. Cases: bar_z / dk_z / blend320 / blend640 /
blend640_w64 / wo_tex320 / wo_tex640 / wo_tex640_w64 / fill320 / fill640 / load; VI modes off / aa3 / aa0 320 (BAR, DK64) / hires / MGC (VI_WIDTH 1280);
console display buffer only; order **pass × mode × case**, two passes (a stuck VI spoils one pass only). `ares_reference.txt` is the series v13 harness
(ares' factor depends on the mode only: Z ×1.10, write-only ×1.08-1.09, fill ×1.12 at aa0 320; ×1.02-1.03 in the MGC mode). The decision waits for the
console data: if the alive rows differ from ares per class, patch 0018 (a class-dependent VI factor); if not, no change (v42's first list suggests no
class dependence).

### The MGC 2011 demo's memory bandwidth
The demo's description says it was built by studying and optimising for the limits of the RDP/RSP and the memory bandwidth, so the RDRAM traffic was
counted over the whole demo with a new harness log (9999: the `BYTES` line of `ARES_SYNC_LOG` — per list: colour writes incl. fills, colour reads
(IMAGE_READ), depth reads/writes, TMEM loads, command fetch; SP DMA; CPU line fills (I 32 B, D 16 B, write-back 16 B) and uncached words; VI mode),
series v13, ares' part boundaries from the v13 mgc_tasklog runs (`mgc_tasklog/bw/bw.py`).

First, what the demo does: **the VI is 16 bpp, interlaced, aa2 (resample only, no AA), VI_WIDTH 1280 from start to end**; by pixel count **more than 98%
of the RDP's spans are of three kinds — Fill 16, 1-cycle 16 bpp write-only, 1-cycle 16 bpp IMAGE_READ blend** (depth-tested pixels: 2% at 130-150 s only;
20-30% of the Set_Other_Modes words do carry Z compare or AA_EN, but their pixel counts are small: the AA_EN pixel share was not counted).
Colour writes are 1.0-1.4 MB per list (1.7-2.4 × the 614 KB of a 640×480×16 bpp frame: a full clear plus the drawing); lists per VI match the console logs
at 0.5-0.87 (parts 1-6: 52 / 30 / 37 / 41 / 32 / 51 fps).

| part | lists | RDP busy | RDRAM total MB/s | RDP writes | RDP reads | commands | SP DMA | CPU | VI | B/clk while the RDP is busy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1225 | 79% | **103** | 59 | 10 | 4 | 9 | 4 | 18 | 1.39 |
| 2 | 903 | 92% | 86 | 34 | 6 | 8 | 17 | 3 | 18 | 0.68 |
| 3 | 1350 | 82% | 92 | 39 | 6 | 8 | 15 | 5 | 18 | 0.88 |
| 4 | 1213 | 91% | **112** | 59 | 1 | 11 | 18 | 4 | 18 | 1.06 |
| 5 | 756 | 93% | 97 | 45 | 13 (+Z 3) | 5 | 10 | 3 | 18 | 1.04 |
| logo | 275 | 91% | 109 | 52 | 7 | 10 | 18 | 3 | 18 | 1.04 |
| 6 (end) | 1549 | 52% | 71 | 33 | 9 | 2 | 6 | 3 | 18 | 1.29 |

(VI = 640 px × 2 B × 240 lines × 60 fields = 18.4 MB/s, computed. CPU from ares' cache-miss counts, verified on the console only indirectly through the
timing agreement of v14/v18.)

| part | clk/list | pixels/list | spans/list | px/span | clk/px | pipeline | per-span fixed cost | contention | fill pixels |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 952k | 568k | 12.4k | 46 | 1.68 | 43% | 42% | 5% | 38% |
| 2 | 1940k | 571k | 22.8k | 25 | 3.40 | 21% | 49% | 8% | 38% |
| 3 | 1408k | 530k | 18.4k | 29 | 2.66 | 26% | 65% | 8% | 41% |
| 4 | 1391k | 722k | 32.9k | 22 | 1.93 | 40% | 44% | 9% | 30% |
| 5 | 1815k | 700k | 27.5k | 25 | 2.59 | 30% | 61% | 5% | 31% |
| 6 | 631k | 317k | 4.9k | 64 | 1.99 | 24% | 59% | 5% | 69% |

Console figures to compare against:
- RDRAM peak: 250 MHz, 9-bit bytes at 500 M/s (N64 Programming Manual 3.7 "Clock Speeds and Bus Bandwidth") = **500 MB/s** (8-bit payload; 562.5 MB/s
  with the ninth bit) = 8 B per RCP clock (62.5 MHz).
- The fastest transfer observed on the console is the SP DMA: 3584 B in 581 RSP cycles (v19) = **6.2 B/clk ≈ 385 MB/s** (77% of the peak); 4.7 B/clk at
  512 B, 1.5 B/clk at 64 B (about 38 cycles of fixed cost per DMA).
- The RDP's write ceiling (the measured tables of v7/v8, measured up to 64 px and extrapolated per 32 px beyond): a 640-px Fill 16 span costs 362 clk =
  **3.5 B/clk ≈ 220 MB/s** (44% of the peak, 1.77 px/clk — not the "4 px/clk" of fill mode); 1-cycle write-only **2.0 B/clk** (= 1 px/clk, pipeline bound);
  IMAGE_READ blend **1.9 B/clk** read + write together (0.47 px/clk; v26 measured a 318-px 2-cycle blend span at 667 clk); with depth 2.6 B/clk.
- Fixed costs: 14-22 clk per fill / 1-cycle write-only span, about 40 clk per blend span (the tables' 8-px values).
- The VI's fetch (18.4 MB/s = 3.7% of the peak) slows the RDP by +2-3% in the MGC mode (v40's alive rows, v42's first list); SP DMA contention about 2%
  (v41/v42).

Reading:
1. **Bandwidth in bytes is not saturated.** The demo's whole RDRAM demand is 71-112 MB/s (14-22% of the peak); the RDP alone moves 0.7-1.4 B/clk while
   busy (20-40% of its own ceiling of 3.5 B/clk, 9-17% of the bus's 8 B/clk).
2. The RDP is busy 80-93% of the time and is what the demo is bound by. Of its time, **the per-span fixed cost (the memory-side transaction cost of a
   span) is 42-65%**, the pipeline (1 px/clk, halved by IMAGE_READ) 21-43%, and contention with the CPU / SP DMA / VI 5-9%. With spans averaging
   22-29 px (parts 2-5), the 14-40 clk per span outweighs the pixels. If "memory bandwidth optimisation" means anything here, it is the **number of
   spans (= memory transactions)**, not bytes. Parts 4-5 drawing write-only / blend into a 640-wide buffer fits this shape (no depth → the least memory
   traffic per pixel; write-only → the 1 px/clk pipeline).
3. The remaining differences from the console, part 4 −1.7% / part 5 −2.0% (ares faster), are within the estimation error of the model's 5-9% contention
   share. They need no saturation of the RDRAM bandwidth to explain, and the loads measured in v41 (RSP feeding) and v42 (CPU contention) filled under 1%.
   The remaining candidate is the write-only / fill cost of wide spans (128-638 px) measured directly (the tables extrapolate beyond 64 px, and v26
   measured the blend / depth classes only; part 4's clears and poster are 640-px spans). ROM **v44** measures it (`rdp_timing/v44`: fill320/640,
   wo_tex320/640, wo_shade640, blend640 × widths 16-638 px, VI off, about 30 s; console data pending; ares' extrapolation: fill 3.58 / write-only 1.98 /
   blend 1.92 B/clk).
4. The ceiling with several bus masters running together can be read from v19b's D series: continuous 3584-byte SP DMAs take 585 RSP cycles each
   (6.1 B/clk) and leave the RDP 36 clk per DMA (about 130 B of fill) → **about 6.3 B/clk ≈ 395 MB/s (79% of the peak)** is the whole-bus ceiling
   observed on the console. The RDP alone cannot exceed its own interface's 3.5 B/clk, and a running DMA takes the bus from it, so in scenes bound by
   the RDP like this demo, what acts as "bandwidth" is not bytes but arbitration (about 32 clk + 1.2 clk per 8 B lost by the RDP per DMA, the VI's
   line fetches, about 10 clk per CPU line fill).

Sources: N64 Programming Manual 3.7 ([ultra64.ca](https://ultra64.ca/files/documentation/online-manuals/man-v5-1/pro-man/pro03/03-07.htm)); this
report's v7/v8 (tables), v19/v19b (SP DMA), v26 (wide spans), v40-v42 (VI / DMA / CPU contention). The traffic count is in `mgc_tasklog/bw/`
(`sync.log`, `bw.py`).

## Console results of v43 / v44, and v45 (added 2026-11-10, part 2)

### v43 (`hw/v43_ntsc_001.txt`): the VI's slowdown splits by buffer, not by span class
Even with every register rewritten, the VI **stopped fetching in the middle of the hires mode (0x13052) and four re-initialisations did not revive it**;
all later rows (the rest of hires, all of the MGC mode, all of pass 2) are invalid (V_CURRENT keeps advancing at 750 lines/ms: the timing generator
lives, only the fetch stops). Alive: aa3, aa0 320 (BAR / DK64) and the first three hires rows.

| case (target) | aa3 | aa0 320 (BAR / DK64) | hires | ares v13 (aa0 320) |
|---|---|---|---|---|
| bar_z / dk_z (320 wide, first 1-2 MiB) | 1.006 | **1.143 / 1.144** | 1.39 | 1.101 / 1.099 |
| blend320 (same) | 1.007 | 1.115 | 1.31 | 1.094 |
| blend640 / blend640_w64 (0x600000) | — / 1.005 | 1.089 / 1.105 | — | 1.094 / 1.107 |
| wo_tex320 (first MiBs) | 1.003 | **1.195** | — | 1.087 |
| wo_tex640 / wo_tex640_w64 (0x600000) | 1.003 / 1.004 | **1.047** / 1.108 | — | 1.083 / 1.102 |
| fill320 (first MiBs) | 1.003 | **1.197** | — | 1.116 |
| fill640 (0x600000) | 1.004 | **1.053** | — | 1.117 |
| load | 1.005 | 1.133 | — | 1.109 |

- The BAR and DK64 modes agree within ±0.1% on every case; aa3 (the console display) is +0.3-0.7%, as in ares.
- At aa0 320, write-only / fill spans into **a 320-wide buffer in the same region as the VI's display buffer (the first 1-2 MiB)** are ×1.195, and into
  **the 640-wide buffer at 0x600000** ×1.05. The same class differs fourfold, so "split by class" (v40 / 0016) does not hold; what matters is either the
  position of the target relative to what the VI reads (the same megabyte or not) or the line stride. v43 changes both together → **v45** separates them.
- After the 0x600000 cases the probe drops (wo_tex640 101632, fill640 99368, others about 103000); cause unconfirmed (v45 will show whether it repeats).
- Patch 0018 (the VI factor) waits for v45. The only console figure for write-only / fill in the MGC mode is still v42's first list (×1.035).

### v44 (`hw/v44_ntsc_001.txt`): the byte rate of wide spans
| clk/span (console / ares v13) | w16 | w64 | w128 | w318 | w638 | console peak B/clk |
|---|---|---|---|---|---|---|
| fill320 | 22.3 / 21.3 | 49.0 / 47.1 | 85.4 / 82.5 | 185.4 / 179.9 | — | 3.43 |
| fill640 | 24.1 / 21.5 (1.12) | 50.6 / 47.2 | 87.3 / 82.8 | 194.2 / 186.6 | 369.3 / 356.8 (1.035) | **3.46** |
| wo_tex640 | 33.4 / 37.8 (0.88) | 77.9 / 83.0 | 142.4 / 146.5 | 334.3 / 335.4 | 649.2 / 644.9 (1.007) | 1.97 |
| wo_shade640 | 32.7 / 34.1 | 77.3 / 79.3 | 142.0 / 142.8 | 333.9 / 331.7 | 649.3 / 641.2 (1.013) | 1.97 |
| blend640 (read + write) | 74.8 / 80.1 (0.93) | 164.3 / 168.7 | 296.4 / 300.6 | 690.6 / 688.6 | 1344.8 / 1330.6 (1.011) | 1.90 |

- **The tables' extrapolation beyond 64 px is right** (write-only and blend within ±1.3% at 256-638 px; per-pixel increments 0.984 / 2.044 against ares'
  0.967 / 2.006). The RDP's measured ceilings are fill 3.46 B/clk (216 MB/s, 43% of the peak), write-only 1.97, blend 1.90 B/clk — as estimated in the
  bandwidth section (3.5 / 2.0 / 1.9).
- The synthetic triangles' 16-64 px spans being 4-5 clk cheaper than ares is the fixed-cost / span-cost split known since v33 (the game lists agree),
  not a new difference.
- **Fills cost more on the console**: +1.0 clk per span into a 320-wide buffer, +2.7 into a 640-wide one, and +0.014 clk per pixel. With these three in
  the harness (`T_FILLSP=1 T_FILLSPW=2.7 T_FILLPX=0.014`, off by default) all 14 fill points of v44 are within ±1%.
- Effect on the demo with those values: part 4 −1.7 → **−1.5%**, part 5 −2.0 → **−1.8%**, parts 1-3 +0.3 / +1.2 / +2.5% (0.1-0.3 worse); the "@" moves
  from +5 to +20 VI (relative to the start of part 4 it improves from −53 to −46 VI; the excess of parts 1-3 now shows). DK64 27 → 29, BAR 253 / 328
  (unchanged), crash +643 / +667. Being measured values they are a candidate for 0018, to be applied together with the VI term (v45) once the absolute
  position of the "@" can be judged. The series stays at v13.

### v45 (`rdp_timing/v45`, console data pending)
Every case draws at 0x400000 (depth 0x4A0000); VI_ORIGIN is **the same megabyte (0x450000) / the next (0x550000) / a far one (0x650000)**, the colour
image 320 / 640 wide; bar_z / blend / wo_tex / fill are measured in **the MGC mode first, then aa0 320** (hires dropped). Same VI liveness checks as
v43, two passes. A stopped VI may not come back with a reset: power-cycle before running.

## Console results of v45: the VI's slowdown depends on the megabyte it reads and the buffer width (added 2026-11-10, part 3)

`hw/v45_ntsc_001.txt`. Pass 1 is alive throughout (MGC mode and aa0 320); pass 2 up to the end of the MGC mode (the VI stopped at the second aa0 320 row).
The two passes agree within ±0.1%. Target at 0x400000; ratios to the VI off:

| case | MGC mode: same MiB | next / far MiB | aa0 320: same MiB | next / far MiB | ares v13 (MGC / aa0) |
|---|---|---|---|---|---|
| bar_z320 / 640 | 1.048 / 1.048 | 1.026 / 1.026 | 1.125 / 1.133 | 1.105 / 1.108 | 1.026 / 1.101 |
| blend320 / 640 | 1.070 / 1.045 | 1.036 / 1.022 | 1.115 / 1.114 | 1.088 / 1.089 | 1.024 / 1.094 |
| wo_tex320 / 640 | 1.080 / 1.037 | 1.031 / 1.029 | **1.196** / 1.079 | **1.156** / 1.047 | 1.022 / 1.085 |
| fill320 / 640 | 1.088 / 1.034 | 1.036 / 1.028 | **1.196** / 1.087 | **1.153** / 1.052 | 1.029 / 1.117 |

- The next megabyte (same 2 MiB) and a far one are the same within ±0.1%: the boundary is **1 MiB**. A VI reading the megabyte the RDP draws into costs
  an extra +2-5% (aa0) / +1-5% (MGC mode).
- At aa0 320, write-only / fill spans into a 320-wide buffer are ×1.155 even in another megabyte, against ×1.05 at 640 wide: v43's "×1.195 and ×1.05"
  were same-megabyte + 320 wide against other-megabyte + 640 wide. Depth and blend spans do not depend on the width.
- **The MGC demo is not affected.** It cycles three buffers at 0x500000 / 0x600000 / 0x700000 (plus a 640-wide buffer at 0x46A000 every frame), so the
  buffer the VI shows is never in the megabyte being drawn. The other-megabyte MGC-mode figures (depth 1.026, blend 1.022, write-only 1.029, fill 1.028 at
  640 wide) **agree with ares v13 within ±1%** (1.026 / 1.024 / 1.021 / 1.027): the VI term is ruled out as the cause of parts 4 / 5 (−1.7 / −2.0%).
- The harness has the class × width × same-megabyte factors (`T_VICLASS=1`, off by default); v45's 48 points fall within ±1.5%. Effect on the games: MGC
  unchanged in every part ("@" still +5 VI); BAR seg1 328 → **325** (console 306), seg0 253 → 252 (console 225), crash +643 / +667; DK64's delta pattern
  24/427/49 → 23/424/53, score 27 → **37** (still completes). BAR moves toward the console and DK64's score worsens, so **adoption is on hold** (the series
  stays at v13). With the fill correction of v44 on top: MGC −1.5 / −1.8%, "@" +20 VI, BAR seg0 255.

### What remains for MGC (as of v45)
The VI (v45), wide spans (v44), fills (+0.2%), RSP feeding (v41), CPU / DMA contention (v42) and triangle properties, order and depth (v36-v39) are all
measured, and under 0.5% of parts 4 / 5's −1.7 / −2.0% is explained. The next step is not a new ROM: the console task log already in hand (mgc_tasklog c)
will be compared frame by frame with ares' log (RSP busy, dp-go → done, CPU gap) to see whether the shortfall sits in the RDP, the RSP or the CPU.

## What remains of MGC parts 4 / 5: the frame breakdown, the replays revisited, v46 (added 2026-11-10, part 4)

### Frame breakdown from the console task log (mgc_tasklog c, `mgc_tasklog/audio_split.py`)
| mean per frame (VI) | part 4 console / ares v13 | part 5 console / ares v13 |
|---|---|---|
| graphics task (go → RSP done) | 0.6457 / 0.6421 (−0.6%) | 0.5350 / 0.5388 (+0.7%) |
| RSP done → DP done (the RDP's remainder) | 0.8359 / 0.8136 (**−2.7%**) | 1.3663 / 1.3242 (**−3.1%**) |
| audio task (during a yield: ack → done) | 0.0538 / 0.0558 | 0.0536 / 0.0559 |
| audio task (during the RDP's remainder: VI → done) | 0.1179 / 0.1189 | 0.1186 / 0.1188 |

- A frame starts when the previous DP finishes (DP bound), so the missing period is exactly the missing time of **the stretch where the RDP alone
  drains the FIFO after the RSP has finished**. The RSP task and audio task durations agree with the console, and the shortfall is −0.024 to −0.04 VI
  whatever the number of audio tasks in the frame (0 / 1 / 2): the cause is the RDP list estimate, not the RSP or the audio.

### The replays (v29) revisited with series v13 (`rdp_timing/v29`, VI off)
| list | console / ares (full) | tri_only | triangles | difference (clk) |
|---|---|---|---|---|
| p1_0152 | 1.072 | 1.075 | 554 | +59k |
| p2_1802 | 0.969 | 0.949 | 1299 | −66k |
| p3_2555 | 0.998 | 0.995 | 4782 | −5k |
| p4_3794 / 4055 / 4316 / 4544 / 4772 | 0.999 / **1.053** / **1.046** / 1.017 / 0.994 | 0.996 / 1.054 / 1.047 / 1.015 / 0.990 | 1836 / 1572 / 2205 / 1246 / 2086 | −1k / +59k / +62k / +25k / −7k |
| p5_5075 / 5360 / 5549 | 1.055 / 1.039 / 1.040 | 1.046 / 1.040 / 1.041 | 910 / 613 / 755 | +93k / +62k / +69k |
| p6_5810 | 0.990 | 0.977 | 1227 | −11k |

- Same direction as the part residuals (+0.2 / +0.9 / +2.4 / −1.7 / −2.0%): the five part-4 lists average +2.2% and the three part-5 lists +4.5% on the
  console's side. The earlier "the replays agree" holds for p4_3794 / 4772 only; **the residual was in the replays all along** (correction).
- The difference is in the triangles (tri_only has the same ratio; state only and fills + loads are +2-3% = a few thousand clocks). The five part-4
  lists share the mode word (1-cycle, write-only, texture + shade) and the number of loads (103) yet split into 0% and +5%, in line with none of the
  triangle count, span count, mean width or rows outside the scissor.
- The exact rasterizer (a row is drawn when any sub-scanline touches it: `T_RASTER=1`) moves every list by −4 to −5% and does not narrow the spread
  (σ 3.1% → 4.2%).

### v46 (`rdp_timing/v46`, console data pending)
The triangles of seven lists (p4_3794 / 4055 / 4316 / 4772, p5_5075, p1_0152, p2_1802) are cut into 12 consecutive chunks and each list is replayed with
one chunk kept and the rest replaced by NOPs, to find which triangles hold the console − ares difference and put their property into the model.

## Console results of v46: the difference sits in the large triangles that extend beyond the scissor; v47 (added 2026-11-10, part 5)

`hw/v46_ntsc_001.txt`. The chunks' (clock − tri_none) add up to full − tri_none (additive), and all seven lists show the same pattern.

| list | chunk00 (the large triangles at the start), console − ares | chunks 01-11 (small triangles), per triangle |
|---|---|---|
| p4_3794 / 4055 / 4316 / 4772 | **+54k / +84k / +140k / +62k clk** (ratio 1.15-1.38) | −23 to −63 clk (ratio 0.58-0.99) |
| p1_0152 | +64k (1.25) | −10 to −86 |
| p5_5075 | +97k (1.36), chunks 01 / 02 +27k / +16k | −60 to −230 |
| p2_1802 | +53k (1.32) | −10 to −284 (ratio 0.86-0.97) |

- The per-list residuals (0% to +5%) were **how far the shortfall on the large leading triangles (ares too cheap) and the excess on the small ones (ares
  about 40 clk per triangle too dear) cancel**.
- The leading chunk is the background's large triangles, whose spans run beyond the screen (the scissor). With the lists parsed
  (`mgc_tasklog/v46an/chunks.py`), a regression of the difference over the five write-only lists × 12 chunks leaves 10.1k clk rms on triangles, spans and
  pixels alone, and 2.5k clk once **the pixels cut off by the scissor on the left and right** are added (0.32 and 0.31 clk per pixel, −32 clk per
  triangle). ares counts the pixels outside the scissor as free (the term v36 saw — "spans sticking out left or right cost more" — and set aside as
  1-2% of the games' lists).
- Provisional harness result (`T_CLIPPX`, off by default; v46's 84 chunks and v29's 12 lists):

| setting | rms of the v46 chunk differences | the five part-4 lists (console/ares) | mean / σ of the 12 lists |
|---|---|---|---|
| series v13 | 25.9k clk | 0.999 / 1.053 / 1.046 / 1.017 / 0.994 | 1.023 / 3.1% |
| 0.32 clk per outside pixel | 11.8k | 0.968 / 0.989 / 0.957 / 0.984 / 0.965 | 0.978 / 2.1% |
| 0.32 + write-only triangle texture term 30 → 0 | 11.3k | **1.005 / 1.029 / 1.001 / 1.009 / 1.016** | **1.001 / 2.6%** |
| 0.40 + texture and shade terms → 0 | 9.3k | 1.006 / 1.023 / 0.990 / 1.006 / 1.021 | 0.995 / 2.8% |

  The "+42 clk per write-only triangle" of 0013 was standing in for this cost by the triangle count. The coefficient will be measured directly rather
  than taken from the regression → v47.

### v47 (`rdp_timing/v47`, console data pending)
A 640-wide buffer with the scissor at X = 128..512: spans 16 / 64 px visible extend 16-512 px beyond the scissor's left / right edge (or lie wholly
outside), in wo_tex640 / blend640 / bar_z640 / fill640, to measure the clocks per pixel outside the scissor.

## Console results of v47: pixels outside the scissor cost at the pipeline rate on the major-edge side only; v48 (added 2026-11-11)

`hw/v47_ntsc_001.txt` (scissor X = 128..512, rectangle-shaped triangles with the major edge on the left, 8 rows). Clocks per span above the uncut case:

| case (16 px visible) | 16 / 64 / 256 / 512 px beyond the left edge | 16-512 px beyond the right edge | wholly left of the scissor (64 / 256 px + width, touching the edge) | wholly right |
|---|---|---|---|---|
| wo_tex640 (1-cycle) | +15.3 / +63.6 / +255.5 / +511.0 (**1.00 clk/px**) | −0.7 (free) | 83.8 / 275.8 clk (width × 1.0 + 4) | 11.0 clk |
| blend640 (1-cycle) | −7.0 / +39.4 / +231.4 / +491.1 (1.0/px − about 15) | +0.6 | 91.5 / 283.7 | 11.0 |
| bar_z640 (2-cycle, depth) | −25.1 / +70.7 / +455.3 / +966.8 (**2.0/px** − about 42) | −26.8 (placement) | 198.6 / 581.4 (width × 2.0 + about 37) | 11.1 |
| fill640 (fill-mode triangles) | +8.9 / +24.8 / +115.9 / +237.4 (about 0.5/px) | +5.7 | 39.8 / 106.6 (width × 0.35 + 12) | 10.9 |

- The slopes are the same at 64 px visible (1.00 / 0.97 / 1.87-2.0 / 0.5). The RDP walks a span from the major edge and advances through the pixels
  before the scissor at the pipeline rate (1 clk in 1-cycle, 2 in 2-cycle); what sticks out on the far side is free, and a row wholly beyond the far
  edge costs 11 clk (v36's "88 clk for an offscreen triangle" = 8 rows). ares v13 charges none of this.
- With the rule in the harness (`T_CLIPMODEL=1`, off by default; the write-only per-triangle terms 5 / 7 / 30 → 0 / 0 / 6), 24 of v47's 26 wo_tex640 points
  fall within ±1 clk and the two 512-px ones at +8 clk (1.4%).
- **It does not carry over to the games' lists as is**: v46's leading chunks go from +54k..+140k to −19k..−88k clk (ares now too dear), and the mean of
  v29's 12 lists from 1.023 to 0.965 (σ still 3.0%). Regressing over the parsed lists, pixels partially cut on the major-edge side cost 1.08 clk/px (as
  in v47), but **rows wholly outside on the major-edge side are almost free** (v47's "wholly outside" spans touched the scissor edge, possibly a
  different case), and pixels cut on the far side carry 0.3-0.6 clk/px (only the 5-7 leading chunks inform this, so it is not conclusive). ROM v48
  measures the shapes v47 did not. Patch 0018 waits for it (the series stays at v13).

### v48 (`rdp_timing/v48`, console data pending)
Same scissor and three cases (wo_tex640 / blend640 / bar_z640): **triangles with the major edge on the right**, rows wholly outside 32 px away from the
scissor edge (and touching it), edges slanted by ±2 px per row, spans cut on both sides. `ares_clipmodel.txt` holds the harness with v47's rule.

## Console results of v48 and the rule for pixels outside the scissor, the games, v49 (added 2026-11-11, part 2)

### v48 (`hw/v48_ntsc_001.txt`)
| shape (wo_tex640, 16 px visible, clk/span) | major edge on the left | major edge on the right |
|---|---|---|
| inside the scissor (baseline) | 32.5 | 37.0 |
| 64 / 256 px beyond the left edge | **96.9 / 288.9** | 30.7 / 30.7 |
| 64 / 256 px beyond the right edge | 32.7 / 32.7 | **83.8 / 275.8** |
| wholly outside (32 px away, either side, 64 / 256 wide) | 11.0 | 11.0 |
| wholly left, touching the edge (64 px) | 67.8 | 11.8 |
| 64 / 256 px beyond both edges (384 px visible, baselines 401.3 / 399.4) | 465.6 / 659.3 | 455.9 / 648.6 |
| edges slanted ±2 px/row, about 64 px cut on the major-edge side | 89.9 / 91.5 (baseline 32.8) | 88.9 (baseline 33.8) |

blend640 and bar_z640 follow the same pattern (slopes 1.0 / 2.0 clk/px). The rule:
1. The RDP walks a span **from the major edge (XH)** and advances through the pixels before the scissor at 1 clk/px in 1-cycle, 2 clk/px in 2-cycle
   (about 0.5 in fill); what sticks out on the far side is free.
2. **A row without a single visible pixel costs 11 clk whatever its width and side** (not walked). A row whose last pixel lies on the scissor edge is
   walked (v47's "wholly outside" spans were of this kind).
3. IMAGE_READ spans overlap their read start-up with the walk: about 15 clk × rate cheaper, 6 clk × rate more with depth.

### With the rule in place (harness `T_CLIPMODEL=1`, off by default)
The write-only per-triangle terms go from 5 / 7 / 30 to 0 / 0 / 6 (the uncut synthetic triangles of v44 / v47 / v48: +42 clk was too much), blend-only spans
−12 clk (−7 at 640 wide; the synthetic frames of v33 / v40 and v46's part-2 chunks).

| | series v13 | outside pixels + write-only triangles | + blend spans | + 28 per Z-compare-only + IMAGE_READ span (provisional) |
|---|---|---|---|---|
| rms of v46's 84 chunk differences | 25.9k clk | 10.1k | 8.4k | 6.9k |
| v29's 12 lists (console/ares mean / σ) | 1.023 / 3.1% | 1.005 / 2.4% | 1.022 / 1.5% | 1.013 / 1.1% |
| MGC parts 1 / 2 / 3 | +0.2 / +0.9 / +2.4% | +0.6 / +3.9 / +4.6% | **0.0 / +0.2 / −0.5%** | 0.0 / +0.2 / −0.5% |
| MGC parts 4 / 5 / logo | −1.7 / −2.0 / +1.9% | −0.6 / +0.1 / −0.3% | **−0.6** / −1.8 / **−0.6%** | −0.6 / +1.3 / −0.3% |
| the "@" (against console VI 9385) | +5 | +174 | −50 | **−4** |
| BAR seg0 / seg1 (console 225 / 306 frames) | 253 / 328 | 255 / 325 | 254 / 325 | 234 / 313 |
| DK64 score (delta pattern) | 27 (24/427/49) | 43 | 39 (20/426/54) | 39 (20/422/58) |

- With the outside-pixel rule and the blend correction, **parts 1-4 and the logo are within ±0.6%** (v13's "@" at +5 VI was +58 VI from parts 1-3 cancelling
  −53 VI from parts 4-5).
- What remains of part 5 (−1.8%) reads, from v46's chunk differences, as about +28 clk per span missing on **narrow Z-compare-only (no Z update) +
  IMAGE_READ spans** (the particles, 2.7 px per span on average; chunks 00-02 of p5_5075). Putting that in gives part 5 +1.3%, the "@" at −4 VI and a sum
  of 0.0% — but the same term worsens the replays of the BAR / DK64 lists (bar_2316 console/ares 1.014 → 0.972, dk64_d6373 0.932 → 0.918), so it cannot go
  in as is; BAR's frame counts moving toward the console is another shortfall being made up.
- DK64's score worsening (27 → 39, still completes) comes from the outside-pixel cost landing on depth spans whose price in ares was already high
  (the dk64_d6373 replay at 0.95).
- So patch 0018 will go in **together with measured prices for narrow depth / blend spans from v49** (the series stays at v13; the harness knobs are off by
  default).

### v49 (`rdp_timing/v49`, console data pending)
Spans 2-32 px wide in the demo's mode word (1-cycle) with Z_COMPARE / Z_UPDATE / IMAGE_READ switched six ways, and DK64's (1-cycle) and BAR's (2-cycle)
mode words with and without Z update, with the depth test all passing / all failing. ares v13 has "Z compare + IMAGE_READ" (57 clk at 4 px) cheaper than
"Z compare alone" (71 clk), an inversion.

## Console results of v49, patches 0018 / 0019 (series v14) (added 2026-11-14)

### v49 (`hw/v49_ntsc_001.txt`): narrow spans by Z compare / Z update / IMAGE_READ
Console span prices (clk per span, 8-row triangles, 1-cycle = the MGC demo's mode word) and ares v13 (with the depth test forced to pass / fail: `rdp_timing/v49`
run with `T_ZPASS=1 / 0`):

| configuration | 2 / 4 / 8 / 16 / 32 px wide (passing) | (failing) | difference from ares v13 (pass / fail, 4 px) |
|---|---|---|---|
| write-only | 19.5 / 22.8 / 29.0 / 33.4 / 47.1 | — | −3.7 |
| IMAGE_READ | 51.8 / 56.1 / 59.4 / 74.8 / 101.8 | — | −6.4 |
| Z compare | 56.3 / 60.3 / 64.2 / 81.9 / 114.0 | 37.7 / 39.5 / 41.0 / 50.9 / 71.2 | −10.7 / −31.5 |
| Z compare + Z update | 70.3 / 74.3 / 84.4 / 107.3 / 138.9 | 37.6 / 38.3 / 40.7 / 50.8 / 71.2 | −8.4 / −16.1 |
| **Z compare + IMAGE_READ (the demo's particles)** | **79.4 / 84.4 / 92.8 / 118.0 / 165.3** | 63.0 / 66.2 / 72.1 / 90.1 / 127.0 | **+27.3 / +9.1** |
| Z compare + Z update + IMAGE_READ | 93.4 / 97.3 / 113.4 / 144.8 / 193.9 | 63.1 / 63.6 / 71.8 / 89.9 / 127.4 | −9.2 / −14.5 |
| DK64's mode (1-cycle): with / without update | 97.9 / 87.5 (4 px) | 68.9 / 71.5 | −14.4 / +24.5 |
| BAR's mode (2-cycle): with / without update | 97.8 / 81.7 (4 px) | 63.1 / 62.9 | −9.0 / +23.5 |

- The console's prices add up (4 px: write-only 22.8, + IMAGE_READ 33, + Z compare 37.5, + Z update 14, − 9 where the two reads overlap). A failing span costs the
  same with and without Z update.
- **"Z compare without update + IMAGE_READ" was 24-27 clk (4 px) too cheap in ares**: the old path added "compare-only minus pass" to the blend-only table and
  came out cheaper than a span without a depth test (it matches the +28 clk inferred from v46's chunks; the same in DK64's and BAR's modes).
- Every other depth configuration is **too dear in ares** (up to 16 px: 7-16 clk passing, 12-20 failing, the same in all three modes): the span surcharge (+15)
  was making up for the pixels outside the scissor that were not being counted — the same picture as the +42 per write-only triangle and the blend-only spans.

### Patch 0018: `n64: span pixels cut off by the scissor are walked from the major edge`
The rule of v47 / v48 as is (`timing.cpp` `timingTriangle`): pixels in front of the scissor on the major-edge (XH) side cost 1 clk in 1-cycle / 2 clk in 2-cycle /
0.5 clk in fill and copy; the far side is free; a row without a visible pixel costs 11 clk (`clipRow`). IMAGE_READ spans overlap 15 clk × rate with the walk,
depth spans 6 clk × rate more.

### Patch 0019: `n64: span and triangle prices from the synthetic frames now that the scissor cost is modelled`
- Write-only triangles: +42 clk each (0013) → +6 clk with texture (the uncut 8-row triangles of v44 / v47 / v48), and instead **a least time per triangle**:
  edges 15 + shade 9 + texture 31 + IMAGE_READ 10 + 11 clk per row (v32: one-row write-only triangles 26 / 35 / 65 clk, two rows 77, four 103, a one-row blended one 76).
- Blend-only spans: −12 clk (320 wide) / −7 clk (640 wide) (v33 / v40 / v49).
- Z compare without update + IMAGE_READ: the blend-only span plus the depth read (passing 27.6 / 28.3 / 33.4 / 43.2 / 63.5, failing 11.3 / 10.1 / 12.6 / 15.3 / 25.2 clk
  at 2 / 4 / 8 / 16 / 32 px).
- Depth spans: −10 clk (`depthSpan`, v49).

### Results (against the console's task logs: `tools/run_set.sh`, `mgc_tasklog/parts.py`, `tools/games.py`)
| | series v13 | 0018 + 0019 without the depth −10 | **series v14 (0001-0019)** | console |
|---|---|---|---|---|
| MGC parts 1 / 2 / 3 | +0.2 / +0.9 / +2.4% | +0.1 / +0.2 / −0.5% | **+0.1 / +0.2 / −0.5%** | — |
| MGC parts 4 / 5 | −1.7 / −2.0% | −0.4 / +2.3% | **−0.4 / +1.0%** | — |
| MGC "@" (console VI 9393, log c) | +6 VI | +17 VI | **−2 VI** | — |
| MGC frames up to the end of log c (VI 11199) | 1811 | 1842 | **1860** | 1865 |
| DK64 demo, 424 frames: delta 2 / 3 / 4+ | 5 / 378 / 41 (+1.4% slow) | 1 / 377 / 48 (+2.6%) | **7 / 377 / 39 (+1.2%)** | 9 / 390 / 25 |
| DK64: frames whose delta differs from the console | 49 | 56 | **45** | — |
| BAR intro seg0 (frames / VI) | 254 / 465 (−9.4%) | 229 / 466 (+0.7%) | 253 / 465 (−9.1%) | 231 / 467 |
| BAR intro seg1 | 329 / 708 (−1.2%) | 305 / 708 (+6.6%) | **321 / 707 (+1.1%)** | 325 / 708 |

- MGC: every part within ±1.0% (logo +0.0%, total 9013 / 9011 VI = +0.0%), the "@" 2 VI (0.03 s) ahead of the console (−4 VI counted in the main log, where v13 is at +5 VI). v13's "@" at +5 VI was the excess of parts 1-3 cancelling the shortfall of parts 4-5.
- DK64 and BAR seg1 move a little toward the console. **The first scene of the BAR intro (seg0) stays at −9% as in v13.** Without the depth −10 seg0 agrees (+0.7%)
  but seg1, DK64 and MGC part 5 come out too slow. The residual of seg0 turned out to be on the RSP side, not in the RDP (addendum below).
- Change of method: the old "DK64 score" and the BAR crash-scene scripts (silhouettes against the recording) were lost with the working environment, so DK64 / BAR
  are now compared by running the resident loggers (dk64_tasklog / bar_tasklog) in the harness against the console's logs (BAR's segments are now cut "where no
  graphics task runs for 10 VI", so the console's figures read 231 / 325 instead of 225 / 306). The lists of the replay ROMs (v29 / v38 / v46) were taken again from the harness and re-run (addendum below).

### Addendum (2026-11-14): the replay tests restored, where the BAR seg0 residual sits, rows above the scissor, v50 / v51
**The replay tests are back.** The games' lists that were lost can be taken again with the harness' `ARES_DUMP_LISTS`. In all three games a frame's content is
decided by its frame number, so the 12 MGC lists come out at their old Sync_Full numbers, BAR's three at numbers shifted by the input script (1727 / 1767 / 1772)
and DK64's demo frame at 6372, each with the size of the original (`tools/lists.sh`). With the regenerated lists, v29 under the series v13 harness reproduces the
clocks of the stored `rdp_timing/v29/ares_reference_v13.txt` in all 372 rows (only `dk64_797` is not recovered). The console data of v29 / v38 / v42 / v46 can be
compared again.

Replayed lists (full, VI off), ares − console:

| list | console [clk] | series v13 | **series v14** |
|---|---|---|---|
| mgc_p1_0152 / p2_1802 / p3_2555 | 884230 / 2040183 / 1900155 | −6.7 / +3.2 / +0.2% | **−0.7 / −1.1 / −4.2%** |
| mgc_p4_3794 / 4055 / 4316 / 4544 / 4772 | 1415614 / 1180179 / 1410843 / 1492491 / 1181463 | +0.1 / −5.0 / −4.4 / −1.7 / +0.6% | **−0.4 / −1.5 / −1.7 / −1.5 / −0.8%** |
| mgc_p5_5075 / 5360 / 5549, p6_5810 | 1775535 / 1656639 / 1774534, 1029615 | −5.2 / −3.8 / −3.9, +1.1% | **−2.0 / +0.1 / −0.3, −1.1%** |
| bar_i2274 (light) / bar_i2314 / bar_2316 (heavy) | 536542 / 1384114 / 1400974 | −3.3 / +3.2 / −1.3% | **−1.2 / +2.5 / −1.4%** |
| dk64_d6373 | 2075193 | +4.7% | **+1.6%** |

- The lists were spread over −6.7 to +4.7% in v13; in v14 all but p3_2555 (−4.2%) are within −2.0 to +2.5%.

**The −9% of the BAR intro's first scene (seg0) is not in the RDP's span prices.** The RDP time of a light seg0 frame itself (bar_i2274 = frame 22 of the intro)
is right to −1.2% (−0.3% for the triangles alone). Matching the task logs by frame number (seg0 is 465 VI long on both sides and its content goes by frame
number), in the light frames without a yield (12 frames, numbers 4-17) **the RSP finishes 7.0% earlier than on the console** (0.753 against 0.810 VI) and the DP
completion follows 4.5% earlier; the heavy frames (0-3) agree to 1-2%. The console's light frames sit on the 1 VI boundary (DP done at 0.87-0.92 VI plus
0.13-0.2 VI of CPU), ares' at 0.80-0.85 VI, so more of them fit in 1 VI. This is the residual already found in series v7 from the CPU-idle log ("the RSP finishes
8.6% later on the console in light frames"), and it is separate from the RDP model (seg0 agreeing "without the depth −10" in the table above only made up for it
by an RDP 3% too slow).

**The layout of v49.** v49 had the depth buffer in the colour buffer's megabyte (0x600000 / 0x6A0000, 240 rows). All three games keep it in another megabyte
(MGC: colour 0x500000 / 0x600000 / 0x700000, depth 0x46A000; DK64: 0x01D240 / 0x042A40, 0x5D2E00; BAR: 0x1DA800 / 0x200000, 0x3DA800). ares makes depth spans
10-20% dearer in the same megabyte (0014), so "depth spans −10 clk" and the "Z compare without update + blend" table carry differences measured in the
same-megabyte layout over to games in the other. The replayed lists (other megabyte) agree better with v14, as the table shows, so the direction is right; the
values will be re-measured by layout (series F of v50).

**Rows above the scissor (new, not adopted yet).** Looking at v46's chunk replays again under v14, only the leading chunk (the large background triangles that
extend beyond the screen) is dearer on the console, by 9,201 to 39,470 clk, while in the other chunks ares is 5-15 clk per triangle too dear. ares clamps a
triangle's rows to the scissor's Y range and charges nothing for the rows cut off. With a harness knob for the clocks per row cut off above / below
(`T_CLIPYA` / `T_CLIPYB`), fitted to v46's 84 chunks:

| | rows above the scissor | rows below the scissor |
|---|---|---|
| least squares (84 chunks) | **9.9 clk/row** | −0.7 clk/row |
| the leading chunks only (7 lists) | **9.8 clk/row** | −0.1 clk/row |

| leading chunk | p4_3794 | p4_4055 | p4_4316 | p4_4772 | p5_5075 | p1_0152 | p2_1802 |
|---|---|---|---|---|---|---|---|
| rows above / below the scissor | 1492 / 2337 | 912 / 4130 | 4011 / 1247 | 1805 / 2078 | 1864 / 1261 | 1881 / 494 | 1810 / 0 |
| console − ares v14 [clk] | +14516 | +9201 | +39470 | +18350 | +15469 | +17369 | +20290 |
| predicted by 9.8 clk per row above [clk] | 14498 | 8681 | 39298 | 17585 | 18216 | 18436 | 17774 |
| console / ares with 10 clk per row above | 0.999 | 1.000 | 0.999 | 1.001 | 0.992 | 0.995 | 1.010 |

- This reads as **the RDP stepping a triangle down from its top (YH) one row at a time (about 10 clk per row above the scissor, nearly the 11 clk of a row
  without a visible pixel) and stopping once it is past the scissor's bottom.** The asymmetry is the same in seven lists, so it is not taken for a chance fit.
- On its own, though, it removes the cancellation against the small triangles' excess (5-15 clk each) and everything gets too slow: the replays average +0.6%
  over the 12 MGC lists (−1.8 to +1.5%), and in the games MGC reads +0.5 / +1.5 / +0.4 / +1.5 / +2.8% with the "@" at +114 VI (main log; −4 VI in v14), DK64 +1.5%, BAR seg0 −8.0% /
  seg1 +1.6%. **It has to go in together with the small triangles' price**, so it is not in series v14. The values will be set from the direct measurement
  (series G of v50) and the chunk replays of BAR / DK64 / part 3 (v51), and go into the next version.

**Next console measurements (two ROMs).** `rdp_timing_v50` (G: rows above / below the scissor; F: depth spans by the depth buffer's megabyte; 6-7 minutes) and
`rdp_timing_v51` (chunk replays of three BAR lists, one of DK64 and MGC parts 3 / 5 / 6; 3-4 minutes). Both NTSC, 8 MB, VI off.

**Commit messages tidied (2026-11-14).** The `Co-Authored-By` / `Claude-Session` lines left in the commit messages of patches 0001-0011 are removed (the diffs are unchanged; 0012 and later never had them).

**Correction of the harness patch.** The `9999` of the first series v14 package lacked `headless/` (the runner itself, two files): the repository's `.gitignore`
is `/*` and `git add -A` skipped them. The `9999` here has 36 files, and applying it with `git apply` to the tree after `0019` gives the harness' working tree
exactly. The core patches 0001-0019 are unchanged.

## Console results of v50 / v51, patch 0020 (series v15) (added 2026-11-14, part 2)

### v50 G (`hw/v50_ntsc_001.txt`): triangle rows above / below the scissor
640-wide buffer, scissor Y 80..160, 256 rectangles, VI off. Clocks per primitive (16 px wide; the rows above / below cost the same at 64 px):

| geometry | wo_tex640 | blend640 | fill640 | what it shows |
|---|---|---|---|---|
| in8 (all 8 rows inside) | 268.8 | 607.6 | 214.0 | reference |
| above4 / 16 / 64 + in8 (minus in8) | +29.2 / +137.7 / +566.9 | −21.5 / +88.6 / +512.3 | +23.3 / +132.4 / +559.8 | **9.0 clk per row above** (16→64 rows: 8.9 / 8.8 / 8.9 clk/row; 9.0 for triangles wholly above) |
| in8 + below4 / 16 / 64 (minus in8) | −2.3 / −1.0 / −1.2 | −23.6 / −14.4 / −17.0 | −11.9 / −6.2 / −6.6 | **rows below cost nothing** |
| all_above16 / 64 | 159.7 / 591.7 | 159.7 / 591.7 | 159.5 / 591.5 | 15.7 + 9.0 × rows, whatever the mode and width (+43 in DK64's mode = ATOMIC_PRIM) |
| all_below16 / 64 | 66.8 / 66.9 | 66.8 / 66.9 | 23.5 / 23.5 | a constant = the intake of the command words (76.5 with depth, 0x0F) |
| above16 + in80 + below16 (minus in80) | +137.9 | +100.5 | +147.7 | the 16 rows above only |

- The addendum's estimate from v46 ("about 10 clk per row above, nothing below") is confirmed directly: **9.0 clk per row**. The RDP steps a triangle down from
  its top one row at a time and stops at the scissor's bottom.
- With visible rows, the first span's start-up overlaps the walk: about 7 clk (write-only, fill), 36 (IMAGE_READ), 45 (depth) — which is why blend640's above4
  adds nothing (the 15-20 clk by which blend / fill in8 stand above the other rows is the different random X placement; ares shows the same).
- **A triangle without a single visible span** cost 0.6-16 clk in ares; on the console it is 23.5 (edges only) / 66.8 (shade + texture) / 76.5 (+ depth), the
  one-row triangles of v32.
- The absolute values of `bar_z640` / `dk_z640` cannot be compared: the 256 primitives overlap in the same rows at one depth, so most of the later ones fail the
  depth test (below).

### v50 F: depth spans by the depth buffer's megabyte
- **A caveat of the measurement.** With Z update, the 512 triangles overlap at one depth, so even with the buffer cleared far the pixels a later span shares
  with an earlier one fail (5 / 12 / 18 / 33 / 54 / 74% of the pixels 320 wide, 4 / 6 / 9 / 17 / 34 / 53% 640 wide, at 2 / 4 / 8 / 16 / 32 / 64 px); v49 had the
  same. Reproducing the placement and correcting for it shows that **a wide span that fails in part does save part of its writes** (of the pass − fail
  difference times the failed share: about a quarter at 16 px, half at 32, three quarters at 64). "Any passing pixel costs the full span" (v24) holds up to
  16 px only.
- Cases without Z update (every pixel passes) and near (every pixel fails) are not affected. Measured with the depth buffer in another megabyte (the games'
  layout) [clk/span at 2 / 4 / 8 / 16 / 32 / 64 px]:

| configuration (the MGC mode word) | 320 wide | 640 wide |
|---|---|---|
| write-only | 17.9 / 21.2 / 27.7 / 32.0 / 45.7 / 77.2 | 19.5 / 22.8 / 29.0 / 33.4 / 47.1 / 77.8 |
| IMAGE_READ | 48.5 / 52.4 / 56.4 / 70.4 / 97.9 / 159.7 | 51.8 / 56.1 / 59.4 / 74.8 / 101.8 / 164.4 |
| Z compare (passing) | 49.6 / 53.1 / 57.7 / 74.2 / 104.5 / 173.9 | 52.8 / 56.6 / 60.9 / 77.5 / 106.7 / 176.6 |
| Z compare (failing; the same with Z update) | 36.1 / 37.5 / 39.0 / 49.2 / 70.3 / 110.3 | 37.7 / 39.4 / 41.0 / 50.9 / 71.2 / 111.4 |
| Z compare + IMAGE_READ (passing) | 70.5 / 74.5 / 83.5 / 104.9 / 146.4 / 238.9 | 75.8 / 80.3 / 88.2 / 110.6 / 151.6 / 244.7 |
| Z compare + IMAGE_READ (failing; the same with Z update) | 56.3 / 57.9 / 62.9 / 78.8 / 111.3 / 174.5 | 59.2 / 61.4 / 66.3 / 81.9 / 113.7 / 177.0 |
| Z compare + Z update + IMAGE_READ (2 / 4 px only: overlap 5% or less) | 82.0 / 86.0 | 89.3 / 92.6 |

- Same megabyte / other megabyte (console): Z compare + update + IMAGE_READ 1.13 / 1.11 / 1.13 / 1.18 / 1.20 / 1.20 at 320 wide (0014's 1.10 / 1.20), 1.05 /
  1.05 / 1.08 / 1.12 / 1.16 / 1.19 at 640 wide; Z compare without update + IMAGE_READ 1.09-1.12 (320) and 1.05-1.10 (640); failing spans without IMAGE_READ 1.000.
- In the other megabyte series v14 was 3-7 clk (up to 16 px) / 10-30 clk (32-64 px) too dear for Z compare without update + IMAGE_READ, 5-8 clk too cheap for
  depth spans 640 wide, 2.3 clk too cheap for IMAGE_READ 320 wide and 2-8 clk too dear for failing spans (v49 had measured the same megabyte).

### v51 (`hw/v51_ntsc_001.txt`) and v32 read again
- MGC part 3 (−4.2%) was short by its leading chunk (+44,688 clk = the rows above the scissor) and by +2,000 to 4,000 clk in each of the other 11.
- The chunk replays of BAR / DK64 turned out not to be usable for lists with depth: a chunk drawn alone has nothing underneath, so the depth test passes and
  fails differently from the frame (BAR's chunk 08 and DK64's 04-06 cost 0.71-0.88 of ares on the console, while the whole lists agree to ±2.5%).
- **What was left in v46 / v51 (write-only triangles about 12 clk too dear each) came from the widening of spans by slanted edges (0015).** v32's apex triangles
  (`wo_apex640`: a point at the top, 2w px at the bottom) cost no more on the console than rectangles of their mean width (8 rows: 131 / 146 / 170 / 176 / 201 /
  207 / 261 clk against 147 / 156 / 171 / 178 / 210 / 235 / 269). 0015's widening was measured in BAR's mode (spans that read memory) and does not apply to
  narrow write-only spans.

### A correction to how the replays are scored
ares estimates the depth pass rate from the previous frame, so the "smallest of 3 runs" of a replay ROM could be a first run made with the pass rate of the
list before it (−1.7% for mgc_p5_5075, −2.9% for bar_i2274). From here on ares is scored at its steady state, the third run (`tools/runrom.sh` keeps the
clocks of every run, `tools/evalsum.py` applies it).

### Patch 0020: `n64: triangle rows above the scissor, span-less triangles, depth spans by the depth buffer's megabyte`
1. Rows above the scissor: 9 clk each (`rowAbove`) minus the overlap with the first span, 7 / 36 / 45 clk; a triangle wholly above costs 16 + 9 per row
   (+43 with ATOMIC_PRIM); rows below cost nothing.
2. Least time of a triangle without a span: 23.5 / 66 / 76 clk.
3. Blend-only spans (320 wide): −12 → −10 clk. Depth spans (640 wide): −10 → −5 clk (`depthSpanWide`).
4. Z compare without update + IMAGE_READ: the blend-only span + 23.0 / 23.2 / 28.0 / 35.1 / 49.2 / 79.8 clk passing, + 7.6 / 5.4 / 6.7 / 7.8 / 12.6 / 13.7 clk
   failing, at 2 to 64 px.
5. Failing spans: 4 clk (320 wide) / 6 clk (640 wide) cheaper than the tables' difference; failing also applies to Z compare without IMAGE_READ.
6. Depth spans wider than 16 px: minus the estimated failing share × (pass − fail) × 0.25 (16 px) to 0.5 (32 px) to 0.75 (64 px and more).
7. Same-megabyte factors: 640 wide 1.05 up to 8 px and 1.115 up to 16; Z compare without update + IMAGE_READ 1.10 at 320 wide (1.115 beyond 16 px), 1.05 at
   640 wide (1.09 beyond 16 px).
8. Fill spans: +1.0 clk (320 wide) / +2.7 clk (640 wide) / +0.014 clk per pixel (measured by v44 and not adopted then; v50's fill triangles show the same shortfall).
9. Write-only spans narrower than 16 px are not widened by slanted edges (v32).

The model after 0020 against v50: G's wo_tex within ±6.4 clk per primitive over all 30 rows (269 to 6568 clk), triangles wholly above / below within ±1.1 clk.
F (other megabyte, with the pass rate forced to all / none): Z compare + IMAGE_READ ±1.7 clk (320 wide) / +0.7 to +4.5 clk (640 wide), failing spans up to
16 px ±2.1 clk, Z compare + update up to 8 px ±2.5 clk (DK64's mode −4 to −7 clk: rectangles do not show the cost of anti-aliasing).

### Results
Replayed lists (full, VI off, ares − console, steady state):

| list | series v13 | series v14 | **series v15** |
|---|---|---|---|
| mgc_p1_0152 / p2_1802 / p3_2555 | −6.7 / +3.2 / +0.2% | −0.7 / −1.1 / −4.2% | **+1.3 / +0.2 / −1.8%** |
| mgc_p4_3794 / 4055 / 4316 / 4544 / 4772 | +0.1 / −5.0 / −4.4 / −1.7 / +0.6% | −0.4 / −1.5 / −1.7 / −1.5 / −0.8% | **−0.5 / −0.8 / −0.4 / −0.1 / −0.6%** |
| mgc_p5_5075 / 5360 / 5549, p6_5810 | −4.3 / −3.8 / −3.9, +1.1% | −0.5 / +0.1 / −0.3, −1.1% | **+0.8 / +0.6 / +1.1, −0.3%** |
| the 12 MGC lists: mean / rms / worst | −2.04 / 3.56 / 6.7% | −1.14 / 1.56 / 4.2% | **−0.05 / 0.84 / 1.8%** |
| bar_2316 / bar_i2274 / bar_i2314 | +0.9 / −3.3 / +3.2% | +1.2 / −1.2 / +2.5% | +1.2 / +0.2 / +2.5% |
| dk64_d6373 | +4.7% | +1.6% | +1.6% |

Games (against the console's task logs):

| | series v14 | **series v15** | console |
|---|---|---|---|
| MGC parts 1 / 2 / 3 / 4 / 5, logo | +0.1 / +0.2 / −0.5 / −0.4 / +1.0, +0.0% | +0.4 / +1.4 / +0.5 / +0.2 / +2.4, +0.3% | — |
| MGC total (9011 VI) / the "@" (main log) | +0.0% / −4 VI | **+0.9% / +79 VI** | VI 9385 |
| DK64 demo, delta 2 / 3 / 4+ (frames differing from the console) | 7 / 377 / 39 (45) | 6 / 377 / 40 (55) | 9 / 389 / 25 |
| BAR intro seg0 / seg1, frames | 253 / 321 (−9.1 / +1.1%) | 252 / 324 (−8.7 / +0.3%) | 231 / 325 |

- **The RDP's lists on their own now agree in v15 (all 12 MGC lists within ±1.8%, mean −0.05%), but in the game MGC runs 0.9% slow overall and the "@" moves
  from v14's −4 VI to +79 VI.** v14's −4 VI was lists 1.1% fast on average cancelling terms that act only in the game and are about 1% slow.
- That in-game excess shows with the same size in v13 (game minus replayed lists in v13 / v14 / v15: part 3 +2.2 / +3.7 / +2.3 points (one list), part 5 +2.0 / +1.2 / +1.6, part 4 +0.4 / +0.8 / +0.7). Matched
  by frame number, ares' RSP is busy 0.223 / 0.928 / 0.761 / 0.351 VI per frame in parts 1-4 against the console's 0.220 / 0.902 / 0.754 / 0.342 (+1 to 3%,
  the same in v14). The VI factor is not the cause (VI on / off ratio of v29's lists: console 1.026-1.031, ares 1.025). What remains are the bus contention
  with the CPU / RSP and the RSP's own time: the next item. Together with seg0 of the BAR intro (the RSP 7% faster than the console in light frames),
  **what is left is on the RSP side.**
- Which one to use: **v15 is right list by list; for the position of the MGC demo's "@", v14 is at present closer to the console** (v14 = `ares_rdp_timing_v14b.zip`,
  the tree after 0001-0019).

## Console results of v52, the DPC_CLOCK undercount, patch 0021 (series v16) (added 2026-11-14, part 3)

Sources: `hw/v52_ntsc_001.txt` (NTSC console, 8 MB), `hw/v42_ntsc_001.txt` and `hw/v41_ntsc_003.txt` (read again), the console task log `hw/mgc_tasklog_hw_001.srm`.
Tools: `tools/v52cmp.py`, `tools/clkratio.py`, `tools/v42cmp.py`, `tools/v41cmp.py`, `mgc_tasklog/buffers.py`.

### R: the RSP's instruction timing matches the console
In all 30 loops (scalar / vector, load-use, branches, COP0 / COP2 moves, a vertex transform) the RSP cycles per iteration differ by at most 0.006
(for example: empty loop 3.999, load_use4 15.999, vec_dep8 28.001, scalar_vector4 4.000, vertex_transform 23.001). **Nothing to change on the instruction side of the RSP.**

### S: what an SP DMA started by the CPU takes from the RDP
| | 64 B | 168 B | 344 B | 512 B | 1024 B |
|---|---|---|---|---|---|
| S0: duration with the RDP idle (ticks, read / write) | 49.2 / 42.4 | 63.9 / 57.0 | 85.1 / 70.1 | 98.4 / 97.9 | 156.4 / 152.6 |
| S: RDP clocks lost per transfer (back to back, read / write, mean of 4 lists) | 12.2 / 11.2 | 28.5 / 25.5 | 50.6 / 48.7 | 74.3 / 71.5 | 150 / 143 |
| charged by series v15 (9 + 1.2 clk per 8 B, both directions) | 17.2 | 32.5 | 59.2 | 83.9 | 160.9 |

- A write (DMEM to RDRAM) ends about 7 ticks (28 system clocks) sooner than a read. The RDP loses 3.5 + 1.11 clk per 8 B to a read and 2.4 + 1.07 to a write,
  nearly independent of the list (DK64 about 3 clk less). A pause before the transfer adds a little (read 168 B: 28.3 back to back, 32 to 33 after 6400 clk; write 344 B: 47.7, 50 to 52).
- The DMA itself hardly slows down while the RDP draws (x1.00 to 1.09).

### ares' DPC_CLOCK ran 1.7 to 2.5% slow (a defect since 0002)
The S lines hold DPC_CLOCK and the CPU's count (COP0 Count) over the same interval. DPC_CLOCK / (count x 4/3):

| | mean | range (100 conditions) |
|---|---|---|
| console | 1.0006 | 1.0001 to 1.0042 |
| ares series v15 | 0.9753 | 0.9690 to 0.9872 |
| ares series v16 (fixed) | 1.0006 | 1.0001 to 1.0047 |

The cause is `RDP::main`. When 0002 made the RDP thread advance in steps bounded by the work in flight, upstream's `command.clock += clocks / 3;` stayed,
dropping up to two system clocks per step. It happens only while the RDP draws: the register reads 1.72% low in the test ROMs' polling loop (`dp_finish`)
and 2.5% low beside a busy CPU.

**Consequence**: every price of the list model (the tables, the additions per span, triangle and command) was set by matching this DPC_CLOCK to the console's.
With the register 1.72% low, the model's real time was 1.72% long. This was the main cause of series v14 / v15 agreeing list by list while the games ran about 1% slow
(the "excess of the terms that act only in the game").

### Patch 0021: `n64: DPC_CLOCK keeps the remainder of every step; the list prices recalibrated; CPU bus losses`
1. **DPC_CLOCK**: the remainder carries over (`timing.clockRemainder`, saved in states).
2. **Calibration**: `timingAdd` multiplies by `timing.calibration = 0.9828`. What the ROMs read does not move (12 MGC lists: mean -0.04%, rms 0.84% (v15: -0.05 / 0.84); all of v29 +0.005%;
   the 558 synthetic cases of v50 x1.00003), and only the RDP's real time gets 1.72% shorter.
3. **CPU bus contention (v42 read again)**: the losses are real clocks and are not scaled. With the fixed register v42 (VI off, 9 lists) reads as in the table below;
   the constants become 10.4 (data cache fill) / 5.8 (instruction cache fill) / 5.0 (write-back) / 2.6 (uncached write) clk (were 10.0 / 8.5 / 1.0 / 4.0).
   v42's "title VI mode" had the VI stopped for all but the first list (confirmed by the VI probe), so only the VI-off values are used.
4. **SP DMA**: a write to RDRAM ends 28 system clocks sooner (S0). The RDP's loss (9 + 1.2 clk per 8 B) stays (next section).

| RDP clocks lost per operation (VI off, mean of 9 lists) | console | series v15 | **series v16** |
|---|---|---|---|
| data cache fill (pointer chase) | 10.39 | 10.19 | 10.46 |
| instruction cache fill | 6.38 | 9.59 | 6.27 |
| random store (fill + write-back) | 13.63 | 9.94 | 13.53 |
| mixed loop (load + store + ALU + FPU) | 23.56 | 19.96 | 23.24 |
| uncached write | 2.58 | 3.68 | 2.62 |

### Why the SP DMA loss stays (v52 against v41 / v20b)
The v52 figures (the CPU starts the transfer, the whole list is already in RDRAM) fall short of what the RDP loses when the RSP feeds it through an output buffer (v41).

| RDP clocks lost per chunk (read + write) | 2 KiB | 1 KiB | 512 B |
|---|---|---|---|
| console v41, 9 MGC lists | 724 to 785 | 374 to 402 | 194 to 227 |
| console v41, 3 BAR lists | 597 to 612 | 338 to 344 | 212 to 231 |
| sum of the v52 figures (CPU-started DMA) | 566 | 287 | 148 |
| series v16 (9 + 1.2 clk per 8 B), MGC / BAR | 612 to 682 / 615 to 620 | 338 to 359 / 369 to 370 | 185 to 230 / 260 to 278 |

- A real task (F3DEX2, v20b) also costs 51 clk per transfer on average, more than the v52 figures (its two main kinds: 28.5 for a 168 B read, 48.7 for a 344 B write).
  With the v52 figures in the harness the MGC demo runs 1.3% fast ("@" -131 VI) and part 4's "RSP done to DP done" is 3.9% short. **The games' path loses more than the v52 figures**,
  so the constants stay at the values that fit the real path (v41 / v20b).
- The roughly 100 clk per KiB that only MGC loses in v41 scales with the chunk size, not with the number of transfers. In v41 the output buffer of the MGC lists (0x680400)
  shared a megabyte with their colour buffer (0x600000 to 0x680200); that of BAR did not. Together with the console data below, **which megabyte is used by whom** is what to measure next.

### On the console the frame time depends on where the colour buffer is (MGC, console task log)
The MGC demo draws into 0x700000 / 0x500000 / 0x600000 in turn; its command buffer is at 0x79xxxx and its depth buffer at 0x46A000 (`ARES_IMG_LOG` of the harness).
Comparing each frame with the mean of its two neighbours, whose content is nearly the same (`mgc_tasklog/buffers.py`; graphics task start to DP interrupt, difference from the mean of the three):

| part 4 (405 frames each) | 0x500000 | 0x600000 | 0x700000 |
|---|---|---|---|
| console | +1.7k ± 0.8k clk | -8.2k ± 0.8k | +6.7k ± 1.0k |
| ares series v16 | +0.7k ± 0.8k | -0.5k ± 0.9k | -0.1k ± 0.9k |

- The console's three buffers differ by up to 15k clk (1.0% of the frame); ares' do not. In the 0x700000 frames the colour buffer and the command buffer share a megabyte;
  in the 0x500000 frames the VI is showing 0x700000 (the command buffer's megabyte); only the 0x600000 frames have nothing shared.
- The replay ROMs (list at 0x700000, colour at 0x600000, VI off) are the "nothing shared" layout, so the game's average is about 8k clk (0.55% of a part 4 frame) slower than a replay.
- In the DK64 demo the command buffer (0x53xxxx) shares a megabyte with the depth buffer (0x5D2E00); BAR has nothing shared (commands at 0x0Fxxxx).
- This looks like the known "depth buffer in the colour buffer's megabyte costs more" (v33 to v35, v49 / v50), but for the command fetch, the SP DMA and the VI it has not been measured (v53).

### The games (series v16)
| | series v15 | **series v16** | console |
|---|---|---|---|
| MGC parts 1 / 2 / 3 / 4 / 5, logo | +0.4 / +1.4 / +0.5 / +0.2 / +2.4, +0.3% | -0.3 / -0.1 / -0.6 / -1.1 / +0.8, -0.8% | — |
| MGC total (9011 VI) / "@" (main log) | +0.9% / +79 VI | **-0.3% / -36 VI** | VI 9385 |
| MGC parts 4 / 5, "RSP done to DP done" (VI per frame) | 0.8406 / 1.4081 (+0.6 / +3.1%) | 0.8220 / 1.3786 (-1.7 / +0.9%) | 0.8359 / 1.3663 |
| DK64 demo, deltas 2 / 3 / 4+ (frames that differ from the console, VI count) | 6 / 377 / 40 (55, +1.4%) | 8 / 380 / 37 (**46**, +1.0%) | 9 / 389 to 391 / 25 |
| BAR intro, frames in seg0 / seg1 | 252 / 324 (-8.7 / +0.3%) | 253 / 330 (-9.1 / -1.7%) | 231 / 325 |

- Every MGC part is within ±1.1% (v15: up to +2.4%), the total -0.3%. DK64's differing frames go from 55 to 46. BAR's seg1 moves from +0.3 to -1.7%; seg0 (-9%) does not change.
- What remains: MGC part 4 (-1.1%: the RDP gets further than the console's while the RSP runs), BAR seg1 (-1.7%), BAR seg0 (-9%, on the RSP side; by R it is not the instruction timing).
  All of them are on the side of "what the RDP loses when the RSP / CPU / VI use the same RDRAM", where the placement effect above is not modelled.

### Next ROM: rdp_timing v53 (`rdp_timing/v53`, `rdp_timing_v53.zip`)
Four lists (MGC parts 4 / 5, BAR, DK64) with only the placement changed. `P`: where the RDP reads its commands from (0x700000 / the colour buffer's megabyte / the depth buffer's / another),
`F`: where the output buffer is that the RSP fills 1 KiB at a time, back to back (= v41) and spread over about 60% of the list's time, `Q`: where the CPU-started SP DMA reads and writes,
`V`: where the buffer is that the VI shows. Under a minute on the console. ares does not tell the places apart, so whatever differs on the console is what to model next.

## Console results of v53: the effect of placement in RDRAM, patch 0022 (series v17) (added 2026-11-15)

Sources: `hw/v53_ntsc_001.txt` (NTSC console, 8 MB), the console task log `hw/mgc_tasklog_hw_001.srm`. Tools: `tools/v53cmp.py`, `mgc_tasklog/buffers.py`.
v53 replays four lists (MGC parts 4 / 5, BAR, DK64) with nothing changed but where things are in RDRAM.

### P: where the commands are read from (VI off; clocks more than with the list at 0x700000)
| list (primitives / of which use depth) | the colour buffer's megabyte | the depth buffer's megabyte | another megabyte |
|---|---|---|---|
| MGC p4_4316 (2206 / 0) | **+20951** (ares v17: +20938) | -92 (0) | +47 |
| MGC p5_5075 (911 / 368) | **+7946** (+8642) | +3715 (+3538) | -175 |
| BAR 2316 (909 / 866) | **+11631** (+8623) | +9838 (+8314) | -189 |
| DK64 d6373 (1227 / 1179) | **+9134** (+11639) | +9690 (+11310) | -109 |

- With the command list in the megabyte of the colour buffer, each primitive costs 7.4 to 12.8 clk more (9.5 over the four lists). In the megabyte of the depth buffer it is
  8.2 to 11.4 clk per primitive that uses depth (9.6 overall) and nothing for a list without depth (p4). Any other megabyte is the same within 0.2k clk.
- The cost follows the number of primitives, not the words, rows or cycle type (as if the row of that megabyte had to be reopened at every change between fetching commands and drawing).
  Why the lists differ (12.8 per primitive in BAR, 7.4 in DK64) is not known.

### F / Q: where the RSP feeds and where the SP DMA goes
| RDP clocks lost per 1 KiB read + write | back to back | spread over about 60% of the list's time |
|---|---|---|
| console, output buffer in another megabyte / in the colour buffer's | 298 to 305 | 249 (p4) / 272 (p5) / 294 to 299 (BAR) / 278 to 283 (DK64) |
| console, output buffer in the depth buffer's megabyte | 311 to 315 | as above |
| sum of the v52 figures (CPU-started) | 293 | — |
| ares series v17 (9 + 1.2 clk per 8 B) | 326 | 326 |

- **Where the RSP's output buffer is makes no difference beyond P (the commands then being read from there).** The feed through the RSP costs what CPU-started transfers cost (300 against 293),
  and less when spread over the list.
- **Correction to the v16 section**: what MGC seemed to lose on top in v41 was (1) P, because its output buffer shared the colour buffer's megabyte (21k clk for p4_4316), and
  (2) dividing by the unpadded size (there are 6% more chunks). v41 is no evidence that the RSP path loses more than the v52 figures.
- Q: a CPU-started SP DMA whose 64 KiB sit in the colour buffer's megabyte costs +1.4 to 4.1 clk per 1024 B transfer and +0.6 to 2.0 per 168 B / 344 B transfer (1 to 5%); not modelled.

### V: where the buffer is that the VI shows (VI on; clocks more than with the ROM's own display buffer)
| list | VI on / VI off (ares v17) | the command list's megabyte | the colour buffer's | the depth buffer's | another |
|---|---|---|---|---|---|
| MGC p4_4316 | 1.0323 (1.0230) | **+12.0k** (ares +11.7k) | +34.9k (+2.4%) | +5.0k | +0.1k |
| MGC p5_5075 | 1.0292 (1.0255) | **+4.6k** (+4.8k) | +45.5k (+2.5%) | +11.4k | -1.0k |
| BAR 2316 | 1.1100 (1.1076) | **+13.0k** (+7.5k) | +44.9k (+2.9%) | +38.9k (+2.5%) | +0.3k |
| DK64 d6373 | 1.1015 (1.1016) | **+4.5k** (+10.1k) | +61.8k (+2.7%) | +66.3k (+2.9%) | -4.0k |

- The VI showing the megabyte of the command list costs 5.3 clk per primitive in the MGC demo's mode and 8.2 in the 320x240 modes (14.3 in BAR and 3.7 in DK64 taken together).
- The VI showing the megabyte being drawn into (colour or depth) costs +2.4 to 2.9%. This is the term measured in v45; the harness's `T_VICLASS` (v45's factors from synthetic spans) gives only
  40 to 80% of it for the v53 lists. Among the games only DK64 has this layout (both colour buffers in one megabyte), and switching it on makes DK64's deltas 6 / 373 / 42, so it stays on hold (table below).

### Patch 0022: `n64: a primitive whose commands come from the megabyte it draws into costs more`
- For every triangle and rectangle, if the megabyte of the command fetch (DPC_CURRENT) is (1) that of the colour buffer at the primitive's row: 9.5 clk; (2) that of the depth buffer and the primitive uses depth: 9.6 clk;
  (3) that of the buffer the VI shows: 5.3 clk (AA modes 2 / 3) or 8.2 clk (AA modes 0 / 1) (`listColor` / `listDepth` / `listVi` / `listViAA` in `rdp.hpp`).
- These are real clocks and are not scaled by the calibration (0.9828): `timingAdd(cycles, measured)`. Lists read through the XBUS (from DMEM) are not charged.
- The replay ROMs (v29 and the others) read the same as before (nothing shares a megabyte there). v53's P is within ±27% as in the table above (within 0.2% of the list).

### The games (series v17)
| | series v16 | **series v17** | v17 with the SP DMA at the v52 figures (harness `T_DMAMODEL=1`) | console |
|---|---|---|---|---|
| MGC parts 1 / 2 / 3 / 4 / 5, logo | -0.3 / -0.1 / -0.6 / -1.1 / +0.8, -0.8% | **-0.3 / +0.3 / +0.0 / -0.6 / +1.0, -0.6%** | -0.5 / -0.8 / -0.8 / -1.5 / +0.4, -1.9% | — |
| MGC total (9011 VI) / "@" | -0.3% / -36 VI | **+0.0% / -1 VI** | -0.8% / -77 VI | VI 9385 |
| MGC parts 4 / 5, "RSP done to DP done" | -1.7 / +0.9% | -0.6 / +1.2% | -2.4 / +0.3% | 0.8359 / 1.3663 VI |
| DK64 deltas 2 / 3 / 4+ (frames that differ, VI count) | 8 / 380 / 37 (46, +1.0%) | 8 / 380 / 37 (51, +0.8%) | 11 / 375 / 37 (48, +0.8%) | 9 / 389 to 391 / 25 |
| BAR intro, frames in seg0 / seg1 | 253 / 330 | 253 / 330 | 255 / 332 | 231 / 325 |

- **MGC: total +0.0%, the "@" 1 VI early (0.02 s), every part within ±1.0%.** DK64's VI count goes from +1.0 to +0.8% (same deltas; differing frames 46 to 51); BAR does not change (nothing shares a megabyte there).
- **The SP DMA constants stay.** The v52 / v53 figures (read 3.5 + 1.11 clk per 8 B, write 2.4 + 1.07) fit every ROM condition within ±1 clk (harness `T_DMAMODEL=1`), but in the games they make
  the MGC demo 0.8% fast ("@" -77 VI). The present 9 + 1.2 clk per 8 B was set to a real task (F3DEX2, v20b: 51 clk per transfer on average) and is 15 to 25% above the pure bus loss at F3DEX2's sizes (168 B / 344 B).
  So **a real task still loses about 10 clk per transfer that has not been measured** (candidates: the DP_END writes, the AI's audio DMA). v54 measures these apart; then the DMA constants go to the v52 figures and the rest becomes a term of its own.
- Frame time by colour buffer (part 4, difference from the mean of the three): console 0x500000 / 0x600000 / 0x700000 = +1.7k / -8.2k / +6.7k; v17 = -5.6k / -1.8k / +7.5k (v16: all within ±0.7k).
  The 0x700000 frames (colour and commands in one megabyte) now agree. The other two are swapped because the demo switches the displayed buffer at the vertical retrace:
  ares tests VI_ORIGIN when the commands arrive (the start of the frame), while on the console the buffer shown for most of the time the RDP really draws is the one after the switch.
  The average per frame is not affected (the totals are as in the table), but the test belongs where the work is consumed; that is the next thing to do.

### Next ROM: rdp_timing v54 (`rdp_timing/v54`, `rdp_timing_v54.zip`)
`A`: the AI playing from RDRAM beside the list (22050 / 32000 / 44100 Hz, three places for its buffer). `G`: the RSP feeding the list 344 B at a time (what F3DEX2 flushes; one DP_END write per chunk).
Neither is in any replay ROM and both are always there in the games. Under a minute on the console.

## Console results of v54: the AI's audio DMA and the 344 B feed, patch 0023 (series v18) (added 2026-11-15, part 2)

Source: `hw/v54_ntsc_001.txt` (NTSC console, 8 MB). Tools: `rdp_timing/v54/compare.py`, `tools/v52cmp.py`, `tools/v53cmp.py`.

### A: the lists beside the AI playing from RDRAM (VI off; clocks more than with the AI idle, per sample played, clk / sample)
| where the AI's buffer is | MGC p4 | MGC p5 | BAR | DK64 | all four |
|---|---|---|---|---|---|
| another megabyte (22050 / 32000 / 44100 Hz) | 3.0 / 3.2 / 2.8 | 3.0 / 2.8 / 2.8 | 2.3 / 2.5 / 3.0 | 2.2 / 2.7 / 2.5 | **2.7** |
| the colour buffer's megabyte (44100 Hz) | 5.9 | 5.6 | 6.7 | 5.6 | **5.9** |
| the command list's megabyte (44100 Hz) | 5.8 | 5.1 | 6.1 | 4.9 | **5.4** |

- It scales with the rate: +0.2% of the list at 44100 Hz (+0.4% in a shared megabyte).
- What the games do (`ARES_IMG_LOG` of the harness): MGC 44.1 kHz, buffers at 0x08xxxx to 0x0Axxxx (shared with nothing); DK64 22 kHz, 0x79xxxx (nothing);
  BAR 22 kHz, 0x07xxxx to 0x08xxxx (**the megabyte of its command buffer, 0x0Fxxxx**). Of the time the RDP is busy: MGC +0.19%, DK64 +0.10%, BAR +0.19%.

### G: the RSP feeding 344 B at a time (RDP clocks lost per 344 B read + 344 B write + DP_END write)
| | MGC p4 | MGC p5 | BAR | DK64 |
|---|---|---|---|---|
| console, back to back | 101.0 | 101.1 | 97.1 | 81.1 |
| console, spread over about 30% / 60% of the list's time | 81.9 / 82.5 | 83.6 / 92.3 | 98.8 / 99.4 | 87.1 / 89.3 |
| sum of the v52 figures (CPU-started, no DP_END) | 99.3 | 99.3 | 99.3 | 94.6 |
| series v17 (9 + 1.2 clk per 8 B) | 121.3 | 121.3 | 121.3 | 121.3 |
| **series v18** | 99.8 to 102.0 | 99.8 to 102.8 | 99.8 to 102.8 | 99.8 to 102.9 |

- **A DP_END write costs nothing.** At the size F3DEX2 flushes, the feed through the RSP costs what CPU-started transfers cost (v52) or less.
  v52 (CPU-started, 4 lists x 25 conditions), v53 (1 KiB feed) and v54 (344 B feed) all give the same figures, so the SP DMA loss is settled at the v52 values.

### Patch 0023: `n64: SP DMA and AI losses of the RDP as measured, VI factor of AA mode 2 on the whole list`
1. **SP DMA**: the RDP loses 3.5 + 1.11 clk per 8 B to a read and 2.4 + 1.07 to a write; the first transfer after a pause of 38 clk or more costs 0.55 (read) / 0.3 (write) clk more per doubling
   of the pause (v52's spacing series; no more beyond 8192 clk). Against v52's 25 conditions (the p4 list) the difference is within ±3 clk up to 344 B and -3 to +5 clk at 1024 B.
   The former 9 + 1.2 clk per 8 B was 15 to 25% too much at F3DEX2's sizes.
2. **AI**: while the RDP has work in flight, each sample the AI fetches from RDRAM costs it 2.7 clk (5.9 with the AI's buffer in the colour buffer's megabyte, 5.4 in the command list's).
3. **VI (AA mode 2)**: the factor 1.03 applies to the whole list, not only to spans and loads. VI on / VI off on the console is 1.031 / 1.030 / 1.026 (p1 to p3 of v29), 1.032 / 1.029 (p4 / p5 of v53)
   and 1.035 (p4 of v42) for the MGC lists, whatever they consist of; v17, with the factor on spans and loads, gave 1.023 to 1.026 (least for part 4's lists of small triangles).
   The 320x240 modes (AA modes 0 / 1) stay as they are (BAR 1.110 against 1.108, DK64 1.1015 against 1.1016). In v29's "title VI mode" the VI had stopped from the fourth list on, so only the first three are used.

The replayed lists (VI off) read the same as in v17.

### The games (series v18)
| | series v17 | v17 with only the SP DMA at the v52 figures | **series v18** | console |
|---|---|---|---|---|
| MGC parts 1 / 2 / 3 / 4 / 5, logo | -0.3 / +0.3 / +0.0 / -0.6 / +1.0, -0.6% | -0.5 / -0.8 / -0.8 / -1.5 / +0.4, -1.9% | **-0.4 / -0.2 / -0.4 / -0.8 / +1.0, -1.4%** | — |
| MGC total (9011 VI) / "@" | +0.0% / -1 VI | -0.8% / -77 VI | **-0.3% / -29 VI** | VI 9385 |
| MGC parts 4 / 5, "RSP done to DP done" | -0.6 / +1.2% | -2.4 / +0.3% | -1.1 / +1.2% | 0.8359 / 1.3663 VI |
| DK64 deltas 2 / 3 / 4+ (frames that differ, VI count) | 8 / 380 / 37 (51, +0.8%) | 11 / 375 / 37 (48, +0.8%) | 11 / 374 / 38 (49, +0.8%) | 9 / 389 to 391 / 25 |
| BAR intro, frames in seg0 / seg1 | 253 / 330 | 255 / 332 | 255 / 331 | 231 / 325 |

- **MGC: total -0.3%, the "@" 29 VI early (0.5 s), every part within ±1.0% (logo -1.4%).** The headline is a little behind v17 (+0.0%, -1 VI), but v17's agreement rested on an SP DMA constant
  now known to be 15 to 25% too high. In v18 everything the RDP loses to the other bus masters (CPU, SP DMA, AI, VI, placement in RDRAM) is at its measured value; no fitted constant is left.
- The measured SP DMA figures alone make the demo 0.8% fast; the AI and the VI (AA mode 2) give 0.5% back. What the remaining -0.3% consists of is not known.
- DK64 and BAR hardly change (DK64: 2-VI deltas 8 to 11, the console has 9; 4+ deltas 37 to 38).

### What remains
- **MGC**: part 5 +1.0%, part 4 -0.8%, logo -1.4%. The replayed lists themselves are -0.4 to -0.8% (part 4) and +0.6 to +1.1% (part 5) (v29, VI off), so what is specific to the game is within ±0.5%.
- **When VI_ORIGIN is tested** (the VI term of 0022: it belongs where the work is consumed, not where the commands arrive) is not done yet. It does not change the average per frame.
- **v20b (51 clk per transfer in a real task)**: about 10 clk above the v52 to v54 figures (about 40 clk for F3DEX2's mix) and unexplained. The v20b ROM did not control where things were in RDRAM,
  so it may have included the placement cost (0022).
- **BAR intro**: seg0 -9.6%, seg1 -2.0%. Neither the RSP's instruction timing (v52 R) nor the SP DMA, the AI or the placement moves it; the cause is elsewhere. Next comes splitting it
  with the frame breakdown of the console task logs at hand (`hw/bar_tasklog_hw_*.srm`); a new ROM follows once that says what to measure.

## Console results of task_timing v21: real tasks of the BAR intro, patch 0024 (series v19) (added 2026-11-15, part 3)

Sources: `hw/task_timing_v21_ntsc_001.txt` (NTSC console, 8 MB, all 144 runs without a timeout), the console task logs `hw/bar_tasklog_hw_*.srm`.
Tools: `tools/tt21cmp.py`, `rdp_timing/task_timing_v21/compare.py`, `bar_tasklog/seg0.py`. The ROM replays six graphics tasks of the first scene of the BAR (J) intro
(list numbers s1716 / s1727 / s1745 / s1767 / s1772 / s1905) on the console, each with the game's memory image.

### What the task logs and ares said beforehand
- Frame by frame against the console log of the variant whose CPU stays off the bus, ares (v18) finishes the RSP 1.4 to 7.8% early in seg0 and its "RSP done to DP done" is 0.01 to 0.15 VI longer.
- One BAR graphics task has 740 to 1160 SP DMAs (6 to 11% of the RSP's busy time), 180 to 490 DP_END writes and **20 to 60 thousand reads of DPC_CURRENT** (counted in ares).
  The RSP keeps waiting for room in its 8 KiB output buffer; when it finishes is decided by how the RDP gets on.

### Result 1: on the console the RSP is never more than 8 KiB ahead of the RDP (game VI mode, CPU polling only, in VI)
| dump | console: RSP done / DP done / largest lead of the RSP | series v18 | **series v19 (0024)** |
|---|---|---|---|
| s1716 (light) | 0.660 / 0.716 / 8.2 KiB | 0.642 / 0.702 / 8.2 KiB | 0.642 / 0.702 / 8.2 KiB |
| s1727 (light) | 0.573 / 0.626 / 8.1 KiB | 0.552 / 0.609 / 8.1 KiB | 0.552 / 0.609 / 8.1 KiB |
| s1745 (light) | 0.795 / 0.851 / 8.2 KiB | 0.722 / 0.836 / **29.3 KiB** | 0.783 / 0.836 / 8.2 KiB |
| s1767 | 1.559 / 1.612 / 8.3 KiB | 1.439 / 1.573 / **39.6 KiB** | 1.523 / 1.573 / 8.2 KiB |
| s1772 | 1.566 / 1.616 / 8.3 KiB | 1.499 / 1.573 / **20.3 KiB** | 1.526 / 1.574 / 8.2 KiB |
| s1905 (heavy) | 0.854 / 0.903 / 8.3 KiB | 0.837 / 0.901 / **16.2 KiB** | 0.852 / 0.901 / 8.3 KiB |

- The "largest lead" is the largest difference between DP_END (what the RSP has handed over) and DPC_CURRENT (what the RDP has fetched) as the CPU kept reading both. On the console it never
  exceeds the buffer (8.1 to 8.3 KiB); in v18 it grew in steps of 8 KiB up to 39.6 KiB.
- The cause is START_VALID in DPC_STATUS. F3DEX2 waits for the bit to clear before it rewinds its output buffer; ares cleared it at the next DP_END write, so the RSP could keep rewinding
  while the modelled RDP was still laps behind.

### Patch 0024: `n64: DPC_START stays pending until the RDP has worked through what came before`
For a range that began at a new DPC_START, START_VALID reads 1 until the work queued before it is done (which is when the RDP switches to the new start address).
The RSP's lead becomes 8.2 to 8.3 KiB as on the console and its task ends 0.2 to 3.7% before the console's (v18: 2 to 9%). The replayed lists and the MGC demo read the same.

### Result 2: the breakdown (game VI mode / VI off, in VI)
| dump | RSP alone: console / ares | RDP alone: console / ares | the RSP beside the RDP (DP done with a 1 MiB FIFO - RDP alone): console / ares | the same, VI off: console / ares |
|---|---|---|---|---|
| s1716 | 0.306 / 0.316 | 0.669 / 0.682 | +0.048 / +0.040 | +0.039 / +0.032 |
| s1727 | 0.293 / 0.303 | 0.570 / 0.570 | +0.054 / +0.039 | +0.036 / +0.032 |
| s1745 | 0.421 / 0.434 | 0.786 / 0.787 | +0.066 / +0.049 | +0.048 / +0.040 |
| s1767 | 0.498 / 0.507 | 1.510 / 1.510 | +0.099 / +0.063 | +0.068 / +0.051 |
| s1772 | 0.517 / 0.527 | 1.517 / 1.509 | +0.098 / +0.064 | +0.068 / +0.051 |
| s1905 | 0.339 / 0.353 | 0.835 / 0.856 | +0.059 / +0.045 | +0.044 / +0.037 |

- **The RDP alone (the list) agrees** (-0.5 to +2.5%). The coupling through the 8 KiB FIFO itself costs next to nothing (console: DP done within ±0.01 VI between the game's FIFO and a 1 MiB one).
- **What the RSP beside the RDP costs is short in ares**: 75 to 89% of the console's with the VI off, 64 to 83% in the game's VI mode. Per SP DMA that is about 51 to 61 clk on the console
  against about 45 in ares with the VI off. The v52 to v54 figures (CPU-started transfers, RSP feed) are 10 to 30% below a real task, and v20b's "51 clk per transfer" was right.
- **The VI's multiplier**: on the console the game's VI mode costs 1.23 to 1.50 times the VI-off loss (1.39 on average); in ares 1.22 to 1.25.
- **The RSP alone is 1.8 to 4.1% slow in ares** (0.009 to 0.014 VI). v52's 30 instruction loops agree, so it is an instruction outside them or the waits around the DMA (not identified).
- What is left of DP done (console - v19: +0.014 / +0.017 / +0.015 / +0.039 / +0.042 / +0.002 VI) is about what the shortfall above and the list differences add up to.

s1772 is the frame with the list number of v20b's `bar_2316`, but it was dumped by a different ares, the task differs a little (RSP alone 0.517 against 0.494 VI) and the figures cannot be compared with v20b's directly.

### The games (series v19)
| | series v18 | **series v19** | console |
|---|---|---|---|
| MGC total / "@" | -0.3% / -29 VI | -0.3% / -29 VI (the same) | VI 9385 |
| DK64 deltas 2 / 3 / 4+ (frames that differ) | 11 / 374 / 38 (49) | 11 / 370 / 38 (49) | 9 / 385 to 389 / 25 |
| BAR intro, frames in seg0 / seg1 | 255 / 331 (-9.6 / -2.0%) | **252** / 331 (-8.7 / -1.8%) | 231 / 325 |

- 0024 corrects a structural error; BAR's seg0 goes from 255 to 252 frames. What remains of seg0 (-8.7%) is, as far as can be seen, the shortfall above (around 0.015 VI = 2 to 3% in the light frames)
  tipping frames that sit at the boundary between a 1 VI and a 2 VI period to the 1 VI side.

### Next ROM: rdp_timing v55 (`rdp_timing/v55`, `rdp_timing_v55.zip`)
Two candidates for why a real task's SP DMA costs more than the v52 figures: (1) its RDRAM side in the megabyte of the depth buffer (BAR's display list and vertices are; v53 covered the colour buffer's megabyte only),
(2) the multiplier with the VI on (CPU-started transfers with the VI off and in each title's mode). The result file is `rdp_timing_v55_NNN.txt` (the name is built from the ROM's version definition).

## Console results of v55: where the RDRAM side of an SP DMA is (added 2026-11-15, part 4; no change to the model)

Source: `hw/v55_ntsc_001.txt` (NTSC console, 8 MB). Tool: `rdp_timing/v55/compare.py`.

**A flaw of this measurement**: the "game" rows of BAR and DK64 were taken with the VI not running (the list's own clock is the VI-off one: BAR 1404890 against 1404897; the VI probe 93006 against 93020).
That is the ROM's fault, and there are no VI-on figures for the 320x240 modes. The two MGC lists did have the VI running and are valid (list x1.032 / x1.029). In v53's V series the same modes started;
the only difference is that it wrote VI_ORIGIN after VI_CTRL. v56 starts the VI the same way and checks with the probe that it is running before it measures.

### RDP clocks lost per SP DMA (VI off, CPU-started, about 400 clocks after the previous one)
| where the transfer's 64 KiB are | MGC p4: 168 B read / 344 B write | MGC p5 | BAR 2316 | DK64 d6373 |
|---|---|---|---|---|
| 0x7D0000 (the megabyte of the command list; where v52 measured) | 29.9 / 48.3 | 28.8 / 48.0 | 30.5 / 50.8 | 27.4 / 46.1 |
| the colour buffer's megabyte | 32.1 / 49.4 | 31.9 / 50.5 | 34.1 / 53.7 | 30.7 / 49.4 |
| the depth buffer's megabyte | 28.0 / 46.3 (a list without depth) | 28.5 / 48.0 | 33.0 / 52.4 | 30.9 / 48.9 |
| ares series v19 | 28.8 / 49.5 | 28.8 / 49.5 | 28.8 / 49.5 | 28.8 / 49.5 |

- With its RDRAM side in the megabyte of the colour buffer or of the depth buffer (for lists that use depth) a transfer costs +2.5 to 3.6 clk (168 B read) and +1.7 to 3.3 clk (344 B write).
- For MGC p4 (no depth) "the depth buffer's megabyte" is 2 clk cheaper than 0x7D0000, so **where v52 measured (the megabyte of the command list) is itself about 2 clk dearer than a megabyte nothing else uses**.
- **The effect is small and does not explain the shortfall of real tasks (task_timing v21).** Half of a BAR task's transfers reading from the depth buffer's megabyte is worth about 1k clk per frame (0.001 VI);
  the shortfall is 2 to 16k clk with the VI off and 6 to 34k clk in the game's VI mode. It is not modelled (under 0.1%).
- **The VI's multiplier in the MGC demo's mode agrees**: console x1.07 to 1.10 (8 conditions), ares x1.07.

### An addition to task_timing v21's breakdown (without the time the RDP was out of commands)
Taking the time the RDP was out of commands (console 0.008 to 0.009 VI, ares 0.006 VI) off "the RSP beside the RDP" with the 1 MiB FIFO:
- VI off: console 0.030 / 0.028 / 0.039 / 0.060 / 0.060 / 0.036 VI, ares 0.027 / 0.026 / 0.034 / 0.045 / 0.045 / 0.031 VI: ares has 86 to 93% of the console's figure for the light frames and 75% for the heavy ones (s1767 / s1772).
- Game VI mode / VI off: console 1.33 to 1.64 (1.47 on average), ares 1.26 to 1.29. **Most of what remains is the VI's multiplier in the 320x240 modes**, which v56 measures directly.

### Next ROM: rdp_timing v56 (`rdp_timing/v56`, `rdp_timing_v56.zip`)
With the VI confirmed running by the probe: what SP DMA (168 B reads / 344 B writes / alternating; in the megabyte of the command list and in one the list does not use) and CPU loads (cache fills and so on)
cost the RDP with the VI off and in each title's mode. v42's "title VI mode" for the CPU losses also had the VI stopped for all but the first list, so those are measured again. The result file is `rdp_timing_v56_NNN.txt`.

## Console results of v56: the VI's multiplier on bus contention, patch 0025 (series v20) (added 2026-11-15, part 5)

Source: `hw/v56_ntsc_001.txt` (NTSC console, 8 MB). Tool: `rdp_timing/v56/compare.py`. For all 80 rows the VI was confirmed running at the first attempt (the probe is +8 to 11% over VI off in the modes of BAR / DK64 and +1.2 to 1.3% in the MGC demo's).

### RDP clocks lost per transfer of another bus master: VI on / VI off
| kind of transfer | 320x240 16-bit, AA 0/1 (BAR / DK64) | the MGC demo's mode (AA 2) | series v19 | series v20 (0025) |
|---|---|---|---|---|
| SP DMA (168 B read / 344 B write / alternating, two places) | x1.19 to 1.28 (1.23 on average) | x1.07 to 1.14 (1.09) | x1.28 / x1.07 | x1.23 / x1.10 |
| CPU data cache fill (chase), random store (rwrite) | x1.27 to 1.36 (1.32) | x1.10 to 1.16 (1.13) | x1.28 / x1.07 | x1.32 / x1.13 |
| CPU instruction cache fill (icache, 32 B) | x1.56 / x1.57 | x1.24 / x1.38 | x1.28 / x1.07 | x1.57 / x1.31 |

- **How much the VI adds to a contention loss depends on the kind of transfer.** The model had one factor (1 + (VI factor - 1) x 2.4); instruction cache fills were far too cheap (at 320x240 the console loses 11.0 / 9.6 clk, the model 7.9 / 7.7) and SP DMA slightly too dear.
- v42's "title VI mode" rows (CPU contention) and v55's rows for BAR / DK64 had the VI stopped, so these are the first measurements for the 320x240 modes.
- With the VI off the values are as before: instruction cache fill 5.5 to 7.1 clk (model 5.8), data cache fill 8.4 to 11.5 (10.4), a 168 B SP DMA read in the list's megabyte 27.1 to 30.3 (28.8).
- **An SP DMA in a megabyte the list does not use is 1.5 to 4.0 clk cheaper than in the list's megabyte** (168 B read: 25.5 to 28.5 clk), as v55 suggested. The model keeps the value of the list's megabyte (v52): the RSP's output is written to the command buffer, which is that case.

### Patch 0025: `n64: the VI raises what a bus transfer costs the RDP by a different amount for each kind of transfer`
`RDP::contendScale()` takes the kind of transfer (SP DMA / CPU data / CPU instruction fill) and uses a scale per kind and per class of VI mode (AA 0/1 and AA 2/3).
The scales are the measured factors minus one, divided by the VI factor's excess (0.117 / 0.03): SP DMA 1.94 / 3.2, CPU data 2.75 / 4.2, CPU instruction 4.83 / 10.3.
v56 in the series v20 harness gives SP DMA x1.23 / x1.10, chase x1.32 / x1.12, icache x1.56 to 1.65 / x1.37, rwrite x1.33 / x1.12 (`rdp_timing/replay_v20/v56_v20.txt`).

### Results in the games (harness)
| | series v19 | **series v20** | console |
|---|---|---|---|
| MGC parts 1 to 5, VI taken | -0.4 / -0.2 / -0.4 / -0.8 / +1.0% | **-0.1 / +0.1 / -0.0 / -0.6 / +1.2%** | - |
| MGC logo / total / the "@" frame | -1.4% / -0.3% / -29 VI | **-0.8% / +0.0% / -3 VI** | VI 9385 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ from the console) | 11 / 370 / 38 (49) | **6 / 381 / 36 (51)** | 9 / 385-391 / 25 |
| BAR intro, frames in seg0 / seg1 | 252 / 331 | **255 / 327** | 231 / 325 |

- The MGC demo as a whole now takes as long as on the console ("@" -3 VI). DK64's count of delta-3 frames moves towards the console's; the number of frames that differ does not change. BAR's seg1 moves towards the console (-1.8% to -0.8%); seg0 is 3 frames worse.
- **Why BAR's seg0 does not improve**: seg0's difference is set by the SP DMA loss, and its VI multiplier went down to the measured value (x1.28 to x1.23). In real tasks (task_timing v21) "the RSP beside the RDP" grows by x1.33 to 1.64 (1.47 on average) with the VI on,
  which the x1.23 of isolated transfers does not explain. A real task's transfers read the display list and vertices from the depth buffer's megabyte and follow each other without a pause; neither has been measured with the VI on (v55's rows for it were void).
- The replay of the 12 lists (VI off) is not affected by 0025 (VI off; not re-run). The core was only syntax-checked for the three changed files (rdp / rsp / cpu); the results are the harness's.

### Next ROM: rdp_timing v57 (`rdp_timing/v57`, `rdp_timing_v57.zip`)
With the VI confirmed running: where the SP DMA's RDRAM side is (a megabyte the list does not use / the colour buffer's / the depth buffer's) and back-to-back transfers (168 B reads, 344 B writes, 1024 B reads), with the VI off and in each title's mode,
and also with the VI showing the megabyte the DMA uses. The result file is `rdp_timing_v57_NNN.txt`.

## Console results of v57: where the RDRAM side of an SP DMA is (VI on), patch 0026 (series v21) (added 2026-11-15, part 6)

Source: `hw/v57_ntsc_001.txt` (NTSC console, 8 MB). Tool: `rdp_timing/v57/compare.py`. The VI was confirmed running for all 184 rows (one condition of MGC p5 at the second attempt, the rest at the first).

### RDP clocks lost per SP DMA (a megabyte the list does not use / the colour buffer's megabyte / the depth buffer's megabyte)
| condition | BAR 2316: VI off | BAR: title VI mode -> the VI showing that megabyte | DK64 d6373: VI off | DK64: title VI mode -> the VI showing that megabyte |
|---|---|---|---|---|
| 168 B read, about 400 clk after the last | 28.6 / 34.3 / 33.0 | 34.2 -> 41.9 | 25.8 / 30.9 / 30.8 | 31.1 -> 38.1 |
| 344 B write, about 400 clk after the last | 48.7 / 53.4 / 52.6 | 57.6 -> 64.2 | 45.2 / 49.5 / 49.2 | 53.0 -> 59.5 |
| 168 B read, back to back | 28.8 / 34.2 / 33.2 | 36.5 -> 40.6 | 27.4 / 31.9 / 31.2 | 34.2 -> 38.9 |
| 344 B write, back to back | 48.5 / 53.2 / 52.4 | 60.3 -> 64.9 | 45.8 / 50.4 / 49.3 | 57.9 -> 62.6 |
| 1024 B read, back to back | 150.3 / 154.9 / 154.5 | 179.6 -> 189.9 | 145.0 / 148.7 / 149.5 | 174.5 -> 186.5 |

- **The colour buffer's megabyte: +3.0 to 6.2 clk (4.7 on average). The depth buffer's megabyte (lists that use depth): +3.5 to 5.1 clk (4.3).** An addition per transfer, whatever its length. For the MGC lists, which hardly use depth, +0.4 to 1.8 clk.
- **With the VI showing the megabyte the DMA uses there is more**: at 320x240 AA 0/1, +4.1 to 4.7 for 168 B / 344 B back to back, +10.3 / +12.0 for 1024 B, +6.5 to 10.4 after a pause; in the MGC demo's mode (AA 2) +1.9 to 2.4, +4.3 / +4.8, +1.5 to 3.5 clk.
- **The VI's multiplier is the same for back-to-back transfers**: x1.15 to 1.27 at 320x240, x1.06 to 1.14 in the MGC demo's mode (0025's x1.23 / x1.10 stand).

### Where the games' transfers are (counted in the harness; transfers while the RDP is busy)
| title | reads | writes |
|---|---|---|
| BAR intro | the list's megabyte 19%, **the colour buffer's 21%, the depth buffer's 38%, the megabyte the VI shows 21%** | all in the list's megabyte (the command FIFO) |
| MGC demo | nearly all in megabytes nothing else uses (the depth buffer's 5%) | a third of the output (0x79xxxx) in the colour buffer's megabyte, a third in the one the VI shows (three colour buffers in turn) |
| DK64 | megabytes nothing else uses | the output in the megabyte of the list and the depth buffer |

### Patch 0026: `n64: an SP DMA costs the RDP more in the megabyte of the colour buffer, of the depth buffer or of the VI's picture`
`RSP::dmaContention()` adds the terms above to the first row of a transfer (colour +4.7 and depth +4.3 get the transfer's VI multiplier; the term for the megabyte the VI shows is added in clocks of that VI mode).
Whether the list uses depth is decided per frame (a depth primitive in this frame or the one before it).
v57 in the series v21 harness gives, for BAR, colour 33.5 / 54.2, depth 33.1 / 53.9, VI showing 42.7 / 67.3 clk (console 34.3 / 53.4, 33.0 / 52.6, 41.9 / 64.2) (`rdp_timing/replay_v21/v57_v21.txt`).

### Results in the games (harness)
| | series v20 | **series v21** | console |
|---|---|---|---|
| MGC parts 1 to 5, VI taken | -0.1 / +0.1 / -0.0 / -0.6 / +1.2% | **-0.1 / +0.2 / +0.0 / -0.4 / +1.2%** | - |
| MGC logo / total / the "@" frame | -0.8% / +0.0% / -3 VI | **-1.1% / +0.1% / +4 VI** | VI 9385 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ from the console) | 6 / 381 / 36 (51) | **8 / 381 / 33 (46)** | 9 / 385-391 / 25 |
| BAR intro, frames in seg0 / seg1 | 255 / 327 | **253 / 326** | 231 / 325 |

- DK64 and BAR move towards the console; MGC is about as close as before. BAR's seg1 is one frame from the console (-0.3%).
- **The cause of BAR's seg0 (-9.1%) is still there.** "The RSP beside the RDP" of task_timing v21 is, in series v21, 0.034 / 0.033 / 0.042 / 0.053 / 0.053 / 0.038 VI with the VI off (console 0.039 / 0.036 / 0.048 / 0.068 / 0.068 / 0.044)
  and 0.040 / 0.039 / 0.050 / 0.063 / 0.063 / 0.045 VI in the game's VI mode (console 0.048 / 0.054 / 0.066 / 0.099 / 0.098 / 0.059): the placement terms are worth only 0.001 to 0.002 VI per frame.
  The measurements of isolated, CPU-started transfers (v52 to v57: length, spacing, place, VI) thus explain 78 to 92% of what a real task costs with the VI off and 64 to 83% in the game's VI mode.
  Note that task_timing's "game VI mode" has only the VI's mode in common with the game: what it shows is the ROM's own picture (0x7xxxxx), so the effect of a task's transfers being in the megabyte the VI shows is not part of this ROM's measurements.
- The replay of the 12 lists (VI off, no SP DMA) is not affected by 0026 (not re-run). The core was only syntax-checked for the changed files (rdp / rsp).

### Next ROM: task_timing v22 (`rdp_timing/task_timing_v22`, `task_timing_v22.zip`)
The SP DMA transfers of a real task (address, length, direction, pause since the previous one; recorded in the series v20 harness) are repeated **without the task** by a small microcode, beside the RDP working through the finished list.
- `+replay`: at the task's own addresses (only the output is written 0x80000 further on). `+replay_neutral`: all of them in 64 KiB nothing else uses (0x480000). `+replay_skip`: the pauses without the transfers (the baseline for reading the table).
- If on the console the repeated transfers cost about what the real task costs, the cause is the transfers themselves (their sequence and combination of places) and the DMA term can be corrected. If they cost less, the cause is elsewhere
  (the RSP running, the DP_END updates, the fine structure of the RDP waiting for commands). The result file is `task_timing_v22_NNN.txt`.

## Console results of task_timing v22: a real task's transfers repeated without the task (added 2026-11-15, part 7; no change to the model)

Source: `hw/task_timing_v22_ntsc_001.txt` (NTSC console, 8 MB). Tool: `rdp_timing/task_timing_v23/compare.py` (it reads v22 files too).

**A flaw of this measurement**: in the "game" (VI on) rows the VI had no effect on the RDP, except for the two conditions of the first dump with the game's FIFO (`bar_s1716` idle / chase): the RDP alone takes what it takes with the VI off (0.603 / 0.603 VI and so on).
After the VI was switched off for the first time it never came back to a state in which it loads the bus. The ROM did not check that the VI was running, the same kind of flaw as rdp_timing v55 (the check went into rdp_timing from v56 on, but not into task_timing).
The only difference between v21 and v22 here is where the ROM's own picture, which the VI shows, is (about 0x3C000 higher in v22); the cause is not identified. **What follows uses the VI-off rows only.**

### Time added to the RDP alone (VI off, in VI)
| dump | real task (1 MiB FIFO) | of which waiting for commands | the transfers alone (own places / neutral place) | RSP running without transfers (`+replay_skip`) | remainder |
|---|---|---|---|---|---|
| bar_s1716 | console 0.0399 / ares 0.0337 | 0.0083 / 0.0059 | 0.0274, 0.0268 / 0.0280, 0.0268 | 0.0032 / 0.0007 | 0.0010 |
| bar_s1727 | 0.0369 / 0.0333 | 0.0083 / 0.0059 | 0.0253, 0.0246 / 0.0277, 0.0259 | 0.0024 / 0.0003 | 0.0009 |
| bar_s1745 | 0.0496 / 0.0418 | 0.0082 / 0.0059 | 0.0343, 0.0330 / 0.0361, 0.0340 | 0.0042 / 0.0008 | 0.0028 |
| bar_s1767 | 0.0696 / 0.0527 | 0.0082 / 0.0059 | 0.0475, 0.0459 / 0.0470, 0.0449 | 0.0050 / 0.0011 | 0.0088 |
| bar_s1772 | 0.0691 / 0.0528 | 0.0082 / 0.0059 | 0.0477, 0.0463 / 0.0471, 0.0456 | 0.0052 / 0.0011 | 0.0080 |
| bar_s1905 | 0.0459 / 0.0381 | 0.0084 / 0.0059 | 0.0320, 0.0311 / 0.0324, 0.0309 | 0.0033 / 0.0007 | 0.0022 |

(Each cell is console / ares series v21. "Remainder" is the console's real task minus (waiting + transfers alone + RSP running without transfers). The replay took 97 to 99% of the RSP alone on the console and 99 to 100% in ares.)

- **The SP DMA model holds for a real task's sequence.** The transfers alone cost console / ares = 0.91 to 1.01 (ares 2 to 9% high for the light frames). The difference of place (own - neutral) is 0.0006 to 0.0016 VI on the console and 0.0012 to 0.0021 VI in ares.
  So the SP DMA terms are not where the shortfall of real tasks comes from.
- **Without any transfer, the RSP merely running slows the RDP.** With `+replay_skip` (the pauses, and one to three 3 KiB DMAs that read the table) the console adds 0.0021 to 0.0041 VI more than the table's DMAs cost (ares's value),
  which is 0.84 to 0.96% of the time that RSP program ran. ares has no such term.
- **The RDP waits longer for commands on the console**: 0.0082 to 0.0084 VI against 0.0059 VI in ares.
- These three explain the real task of the light frames to within 0.001 to 0.003 VI. **The heavy frames (s1767 / s1772, 470 to 480 output writes) keep a remainder of 0.008 to 0.009 VI**, which grows with the amount of output.

### "0.92% while the RSP runs" tried in the harness (not adopted)
| | series v21 | + the RDP loses 0.92% while the RSP runs | console |
|---|---|---|---|
| BAR intro seg0 / seg1 | 253 / 326 | 249 / 323 | 231 / 325 |
| DK64 deltas 2 / 3 / 4+ (frames that differ) | 8 / 381 / 33 (46) | 7 / 376 / 40 (56) | 9 / 385-391 / 25 |
| MGC total / the "@" frame | +0.1% / +4 VI | +0.5% / +39 VI | VI 9385 |

BAR's seg0 improves, DK64 and MGC get worse. What was measured is a microcode that waits by reading DMA_BUSY and by delay loops; it is not known whether the loss comes from the RSP executing at all
or from its register reads (mfc0). In the second case the term has a different shape (per read), so **it goes in once that is settled, and the series stays v21** (the harness keeps it as the knob `T_RSPRUN=0.0092`).

### Next ROM: task_timing v23 (`rdp_timing/task_timing_v23`, `task_timing_v23.zip`)
- Before every run a CPU-side probe checks that the VI is really fetching, and the start is repeated if not (last column of a `T` line = attempts, 0 = void). The ROM's own picture is back where v21 had it.
- Three more conditions with the RSP busy and no transfer: an empty loop (`+rsp_spin`), a loop reading DP_STATUS every time (`+rsp_dpstatus`), a loop reading SP DMA_BUSY every time (`+rsp_dmabusy`).
  F3DEX2 reads DP registers at every output, and with the game's 8 KiB FIFO it reads them tens of thousands of times per task while waiting for room. If a read costs the RDP something, that is a strong candidate for BAR's seg0 (whose difference is largest with the VI on).
- The result file is `task_timing_v23_NNN.txt`.

## Console results of task_timing v23: what the RDP loses while the RSP runs, patch 0027 (series v22) (added 2026-11-16, part 8)

Source: `hw/task_timing_v23_ntsc_002.txt` (NTSC console, 8 MB). Tool: `rdp_timing/task_timing_v24/compare.py` (it reads v22 / v23 files too).
The VI was confirmed running at the first attempt for all 360 rows (probe 104879 to 106056 against 93220 with the VI off, +12.5% or more). The VI-on rows that were void in v22 are now there.

### Time the RDP loses with the RSP busy and no transfer (share of the time that RSP program ran)
| RSP program | VI off (6 dumps) | BAR's VI mode (320x240 AA) |
|---|---|---|
| an empty loop (`+rsp_spin`) | 0.80 to 0.87% | 1.26 to 1.59% |
| a loop reading DP_STATUS every time (`+rsp_dpstatus`) | 0.76 to 0.90% | 1.34 to 1.58% |
| a loop reading SP DMA_BUSY every time (`+rsp_dmabusy`) | 0.74 to 0.88% | 1.31 to 1.58% |
| average | 0.82% | 1.43% |

- **The RSP merely executing slows the RDP, whatever it executes** (it is not a cost of register reads). With the VI on it is 1.75 times as much.
- **The transfers alone agree with the VI on too**: `+replay` - `+replay_skip` is console / ares (series v21) = 0.93 to 1.01 (VI off) and 0.93 to 0.98 (VI on). The console's VI multiplier is 1.17 to 1.25, as 0025's 1.23.
- The RDP waits for commands 0.0082 to 0.0084 VI on the console with the VI on or off, 0.0059 to 0.0060 VI in ares.
- What remains of the real task with the 1 MiB FIFO after "transfers + waiting + RSP running" is 0.0007 to 0.0039 VI (light frames) / 0.0093 to 0.0096 VI (heavy frames) with the VI off and 0.0059 to 0.0082 VI with the VI on.

### Patch 0027: `n64: the RSP running beside the RDP costs the RDP a share of that time`
`RDP::main()` adds 0.82% of the work the RDP gets through to its work in flight while the RSP is not halted; with the VI running the share grows with the VI factor's excess (x1.75 = 1.43% at a factor of 1.117).
Only VI off and BAR's mode are measured; **the value for the MGC demo's mode (AA 2), 0.98%, is an extrapolation**.

When a game waits for room in its 8 KiB output FIFO the RSP keeps running for most of the frame; this is the part that was missing. DP done of task_timing's "game FIFO, game VI mode" (VI):

| | bar_s1716 | s1727 | s1745 | s1767 | s1772 | s1905 |
|---|---|---|---|---|---|---|
| console (v23) | 0.717 | 0.620 | 0.851 | 1.603 | 1.609 | 0.902 |
| series v21 | 0.701 | 0.610 | 0.837 | 1.574 | 1.574 | 0.901 |
| **series v22** | **0.711** | **0.618** | **0.848** | **1.596** | **1.595** | **0.914** |

With the VI off: console 0.643 / 0.557 / 0.765 / 1.433 / 1.441 / 0.815, series v22 0.654 / 0.553 / 0.764 / 1.433 / 1.436 / 0.825 (v21 0.649 / 0.549 / 0.759 / 1.423 / 1.426 / 0.820).
s1716 and s1905 are on the slow side because their lists by themselves (the RDP alone) are 2 to 3% slow in ares.

### Results in the games (harness)
| | series v21 | **series v22** | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 253 / 326 | **246 / 320** | 231 / 325 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ from the console) | 8 / 381 / 33 (46) | **5 / 376 / 40 (56)** | 9 / 385-391 / 25 |
| MGC parts 1 to 5, VI taken | -0.1 / +0.2 / +0.0 / -0.4 / +1.2% | **-0.1 / +0.7 / +0.5 / +0.1 / +1.5%** | - |
| MGC logo / total / the "@" frame | -1.1% / +0.1% / +4 VI | **-0.3% / +0.5% / +41 VI** | VI 9385 |

- **BAR's seg0 improves from -9.1% to -6.5%** (what is left is its last 60 VI: 39 frames against the console's 29). seg1 becomes slightly slow (+1.4%).
- **DK64 and the MGC demo move away from the console.** 0027 is a measured term and is in; these two had looked right without it, so another term is too dear for them.
  DK64 has too many frames of 4 VI or more (40 against 25). The candidate is the bus contention with the CPU: for DK64's kind of list the console loses less than the model
  (rdp_timing v56, VI off: a cache fill 8.4 clk against the model's 10.4, a random store 11.9 against 13.5). For the MGC demo the extrapolated VI value (above) is unverified as well.
  `T_RSPRUN=0` in the harness gives series v21's behaviour.
- The replay of the 12 lists and rdp_timing's CPU-started measurements have the RSP halted and are not affected by 0027 (not re-run). The core was only syntax-checked for the changed file (rdp).

### Next ROM: task_timing v24 (`rdp_timing/task_timing_v24`, `task_timing_v24.zip`)
It looks at the two things left.
- The RDP waiting for commands: every stretch of waiting is recorded (`E` lines). In ares there is a single one right after the first DP_END (about 0.006 VI).
- The remainder that only the real task with the 1 MiB FIFO has: the DP_END write after every output (`+replay_dpend`) and the output being written just ahead of where the RDP reads (`+replay_inplace`, with `+replay_pairref` as its reference), each added to the replayed transfers.
- The result file is `task_timing_v24_NNN.txt`.

## Console results of task_timing v24, and what is left of the BAR intro (added 2026-11-16, part 9; no change to the model)

Source: `hw/task_timing_v24_ntsc_001.txt` (NTSC console, 8 MB; the VI confirmed running at the first attempt for all 396 rows). Tool: `rdp_timing/task_timing_v24/compare.py`.

### What v24 shows
- **A DP_END write after every output costs the RDP nothing**: `+replay_dpend` - `+replay` = -0.0006 to +0.0004 VI (12 conditions).
- **Writing the output just ahead of where the RDP reads costs nothing either**: `+replay_inplace` - `+replay_pairref` = -0.0008 to +0.0010 VI.
- **The difference in "the RDP waiting for commands" (console 0.0083 VI, ares 0.0060 VI) is not a real loss.** On the console there are two stretches with DPC_CURRENT == DPC_END:
  about 4350 to 4450 ticks right after the first DP_END (which hands over nothing), and about 1930 to 1990 ticks right after the first 352 bytes were handed over. ares has only the first.
  Those 352 bytes hold two fills of 276x148 pixels (clearing depth and colour); within the roughly 300 ticks after they were handed over they can be read but not drawn.
  So **the console's DPC_CURRENT is a fetch position that runs ahead of execution**, and the second stretch is not waiting but the time spent executing commands already fetched.
  The time at which the first commands are handed over agrees: 0.0067 VI on the console, 0.0071 VI in ares.
- The share lost to the RSP running (`+rsp_spin`) is 0.66 to 0.88% (0.80% on average) with the VI off and 1.04 to 1.58% (1.19%) in BAR's VI mode this time; with v23 that makes 0.81% / 1.37%, and 0027's values (0.82% / 1.43%) stand.
- The real task with the 1 MiB FIFO (DP done - RDP alone) is, console / series v22, 0.0387 / 0.0364, 0.0363 / 0.0359, 0.0478 / 0.0455, 0.0679 / 0.0571, 0.0685 / 0.0574, 0.0455 / 0.0410 VI with the VI off
  and 0.0527 / 0.0448, 0.0481 / 0.0440, 0.0621 / 0.0564, 0.0782 / 0.0710, 0.0778 / 0.0715, 0.0563 / 0.0507 VI with it on. Only this condition keeps a remainder of 0.000 to 0.011 VI;
  with the game's own 8 KiB FIFO series v22 is within +-0.012 VI of the console (part 8). The task by itself is now reproduced about as well as its list.

### What is left of BAR's seg0: what only the game adds
The task by itself agrees, but in the game's log "task go -> RSP done" is still short (matched by frame number, series v22 - console):
frames 0-19 -2.5%, 20-39 -6.8%, 40-59 -1.0%, 60-79 -2.8%, 80-99 -0.3%, 100-119 -0.1%, 120-139 -2.5%, 140-159 +0.1%, 160-179 -1.9%, 180-199 -3.3%, 200-219 -7.1%.

For frames 20 to 39 (console 0.825 VI, series v22 0.768 VI), with one term of the harness switched off at a time:

| term switched off | task go -> RSP done (VI) | that term's part |
|---|---|---|
| none (series v22) | 0.768 | - |
| bus contention with the CPU | 0.632 | 0.136 |
| SP DMA losses | 0.723 | 0.045 |
| RSP running (0027) | 0.757 | 0.011 |
| placement terms of the SP DMA | 0.765 | 0.003 |
| AI | 0.767 | 0.001 |

A task of these frames replayed by itself (s1727) takes 0.562 VI on the console and 0.561 VI in series v22. The game adds 0.263 VI on the console and 0.207 VI in ares, and **most of that is the bus traffic of the game's CPU beside the RDP**.

### What a CPU instruction cache fill costs depends on the list (v42 read again)
| list (v42, VI off, clocks per transfer) | data fill | instruction fill | random store |
|---|---|---|---|
| MGC, 6 lists | 9.2 to 11.8 | 5.1 to 6.8 | 12.4 to 14.6 |
| DK64, 2 lists | 8.5 / 10.6 | 6.1 / 6.6 | 12.1 / 15.0 |
| **BAR, 2 lists (2316 / i2274)** | 10.4 / 9.7 | **7.1 / 8.0** | 14.4 / 13.4 |
| model | 10.4 | 5.8 | about 13.5 |

Beside BAR's lists an instruction fill costs 22 to 38% more than the model, and the same holds with the VI on (v56: console 11.0 clk, model 9.6 clk).
With the instruction fill set to 7.4 clk in the harness, BAR's seg0 goes from 246 to 236 frames (console 231, -2.1%), but seg1 from 320 to 313 (console 325, +3.7%), which is too far.
The natural reading is that **the loss depends on what the RDP is drawing at that moment**, and one value cannot fit. What decides it is not known, so nothing goes into the model yet (the series stays v22).

### Next ROM: rdp_timing v58 (`rdp_timing/v58`, `rdp_timing_v58.zip`)
CPU data fills, instruction fills and random stores beside synthetic triangle lists that change one property at a time (2-cycle / 1-cycle, depth compare and update on or off, the blender's read on or off, no texture, depth always failing, BAR's / DK64's modes, drawing over the same place),
measuring what each transfer costs the RDP, with the VI off and in BAR's VI mode (the VI confirmed running). The result file is `rdp_timing_v58_NNN.txt`.

## Console results of v58: what a CPU bus transfer costs is decided by where in RDRAM it is (added 2026-11-16, part 10; no change to the model)

Source: `hw/v58_ntsc_001.txt` (NTSC console, 8 MB). Tool: `rdp_timing/v58/compare.py`. Of the 160 rows the last 4 (`same_place`, width 32, VI on) are void: the VI could not be confirmed running in four attempts (the check doing its job).

### RDP clocks lost per transfer beside synthetic triangles (VI off; data fill / instruction fill / random store)
| case | width 4 | width 32 |
|---|---|---|
| base (2-cycle, blended, depth) | 12.5 / 11.4 / 17.6 | 7.8 / 9.2 / 10.8 |
| 1cyc_base | 12.6 / 12.1 / 17.8 | 8.8 / 10.6 / 11.5 |
| no_imrd (the blender does not read) | 12.2 / 10.7 / 18.0 | 6.5 / 5.3 / 9.2 |
| op_08_plain (no texture, no depth) | 9.8 / 10.7 / 14.3 | 8.9 / 11.4 / 11.3 |
| zfail (depth always fails) | 10.4 / 9.7 / 14.3 | 7.0 / 8.5 / 9.5 |
| **fbzb_bar_om (colour / depth buffers at 0x300000 / 0x4DA800)** | 12.5 / **8.2** / 17.7 | 7.8 / **4.7** / 10.8 |
| ares series v22 (any case) | 10.4 / about 6 / 13.5 | the same |

- The cases differ a lot, and from the values beside the game lists as well (v42: 8.5 to 11.8 / 5.1 to 8.0 / 12.1 to 15.0). **What decides is not the drawing mode but the place.**
- The loads of this ROM (and of v42 / v56) read: **data fills and stores at 0x7D0000, the megabyte of the command list**; **instruction fills in the ROM's own code, megabyte 0**.
  The synthetic cases have their colour buffer in megabyte 0 (0x0E2800); only `fbzb_bar_om` has it elsewhere.
  - Instruction fills: 9.2 to 13.2 clk in the megabyte of the colour buffer, 4.7 (width 32) / 8.2 (width 4) clk with the colour buffer elsewhere. **A fill in the megabyte of a colour buffer that is read and written costs 3 to 5 clk more.**
    With a colour buffer that is only written (`no_imrd`, width 32) it is 5.3 clk, about the value of an unrelated place.
  - Data fills (in the list's megabyte) do not change when the colour buffer moves (12.5 / 7.8 either way).
- Small triangles (width 4) cost more for every load (the command fetches are more frequent, presumably).

### Where the games' CPU cache fills are (counted in the harness, while the RDP is busy)
| title | data fills | instruction fills |
|---|---|---|
| BAR intro | **the list's megabyte 64%**, colour 15%, the one the VI shows 17%, depth 4% | **the list's megabyte 86%**, colour 7%, the one the VI shows 6% |
| DK64 | **megabytes the RDP does not use 83%**, list + depth 12%, colour + VI 4% | **unused 80%**, colour + VI 17%, list + depth 3% |
| MGC demo | **unused 100%** | **unused 94%**, the list's 6% |

The model's constants are where v42 measured them: 10.4 clk for a data fill is the value in the list's megabyte, 5.8 clk for an instruction fill the value in a megabyte the RDP does not use.
- 86% of BAR's instruction fills are in the list's megabyte (the game's code and its command FIFO share megabyte 0) but get the value of an unused place: too little is lost, and **ares is fast** (seg0).
- Nearly all data fills of DK64 and the MGC demo are in unused megabytes but get the value of the list's megabyte: possibly too much is lost, and **ares is slow** (what series v22 brought out).

The direction of what is left in all three titles fits. The values for "a data fill in an unused megabyte", "an instruction fill in the list's megabyte" and so on have not been measured, so nothing goes into the model yet (the series stays v22).

### Next ROM: rdp_timing v59 (`rdp_timing/v59`, `rdp_timing_v59.zip`)
Data fills, instruction fills and random stores beside the four game lists, **with the load in different places**: a megabyte the RDP does not use / the list's megabyte / the colour buffer's / the depth buffer's,
and with the VI showing the megabyte the load uses. The instruction fills run generated code at the chosen place (1024 lines, each jumping to the next). VI off and each title's VI mode (confirmed running). The result file is `rdp_timing_v59_NNN.txt`.

## Console results of v59: what a CPU cache line transfer costs, place by place; patch 0028 (series v23) (added 2026-11-16, part 11)

Source: `hw/v59_ntsc_001.txt` (NTSC console, 8 MB; the VI confirmed running at the first attempt for all 120 rows). Tool: `rdp_timing/v59/compare.py`.

### RDP clocks lost per transfer (VI off; a megabyte the RDP does not use / the list's / the colour buffer's / the depth buffer's)
| list | data fill | instruction fill | random store |
|---|---|---|---|
| MGC p4 | 10.8 / 11.6 / 13.5 / 11.4 | 6.8 / 7.6 / 13.8 / 7.7 | 13.4 / 14.3 / 19.5 / 14.2 |
| MGC p5 | 10.2 / 10.5 / 12.4 / 10.8 | 6.1 / 6.5 / 12.9 / 7.6 | 13.0 / 13.3 / 18.0 / 14.2 |
| BAR 2316 | 10.1 / 10.5 / 12.4 / 11.4 | 6.5 / 7.0 / 11.7 / 9.7 | 14.0 / 14.4 / 17.1 / 16.9 |
| DK64 d6373 | 8.1 / 8.4 / 10.6 / 9.8 | 5.0 / 5.4 / 9.3 / 8.7 | 11.6 / 12.0 / 14.5 / 16.0 |

- **The list's megabyte adds only 0.3 to 0.9 clk.** It is not the main cause of BAR's difference.
- **The colour buffer's megabyte is dear**: data fill +2.3 to 2.7, instruction fill +4.3 to 7.0, store +2.9 to 6.2 clk.
- **The depth buffer's megabyte** (BAR / DK64, which use depth): data fill +1.3 / +1.7, instruction fill +3.1 / +3.7, store +2.9 / +4.4 clk; +0.5 to 1.4 clk for the MGC lists.
- **The VI showing the load's megabyte** (VI on, against the VI showing another): data fill +0.7, instruction fill +1.8, store +0.9 clk in the modes of BAR / DK64; +0.4 / +0.85 / +0.6 clk in the MGC demo's.
- **The instruction fill's VI multiplier is that of the data transfers**: x1.33 to 1.38 at 320x240 and x1.10 to 1.14 in the MGC demo's mode when the VI shows another megabyte.
  The multiplier 0025 gave instruction fills alone (x1.57 / x1.31) came from v56's load (the ROM's own code) being in megabyte 0, the megabyte the VI was showing. 0028 takes it back.
- **A difference between lists remains, apart from the place**: beside DK64's d6373 every load costs about 0.8 times what it costs beside the others.

### Patch 0028: `n64: what a CPU cache line transfer costs the RDP depends on where in RDRAM it is`
`RDP::timingCpu()` takes the kind of transfer and its RDRAM address (the program counter for the recompiler's instruction fill) and adds the term of its place to the value of an unused megabyte (data fill 9.95, instruction fill 6.1, write-back 5.0 clk).
The terms: the list's megabyte +0.45 / +0.5 / 0, colour +2.4 / +5.8 / +1.9, depth (frames that use depth) +1.5 / +3.4 / +2.15 (data fill / instruction fill / write-back);
the megabyte the VI shows, in clocks of that VI mode, +0.72 / +1.8 / +0.2 (AA 0/1) and +0.38 / +0.85 / +0.2 (AA 2/3). The instruction fill's own VI scale is gone.
v59 in the series v23 harness is within 1.6 clk of the console for BAR in every condition, within 1 clk for MGC p5 except in the depth buffer's megabyte (about 2 clk high), 5 to 10% low for MGC p4 and 15 to 25% high for DK64 d6373 (`rdp_timing/replay_v23/v59_v23.txt`).

### Results in the games (harness)
| | series v22 | **series v23** | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 246 / 320 | **236 / 316** | 231 / 325 |
| BAR seg0, frames per 60 VI | 39 30 28 26 24 27 33 39 | **39 29 28 27 24 25 34 30** | 39 28 27 26 24 25 33 29 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ from the console) | 5 / 376 / 40 (56) | **7 / 371 / 42 (46)** | 9 / 385-391 / 25 |
| MGC parts 1 to 5, VI taken | -0.1 / +0.7 / +0.5 / +0.1 / +1.5% | **-0.1 / +0.6 / +0.4 / -0.1 / +1.5%** | - |
| MGC total / the "@" frame | +0.5% / +41 VI | **+0.4% / +32 VI** | VI 9385 |

- **BAR's seg0 goes from -6.5% to -2.3%**; every 60 VI block is within one frame of the console (series v15 had 252 frames).
- BAR's seg1 (+2.7%), DK64 (42 frames of 4 VI or more against 25) and the MGC demo (+0.4%) are left on the side of **ares being slow**,
  the direction of the difference between lists (beside DK64's d6373 the console loses about 0.8 times as much). With the place taken out, the next measurement looks for what decides that.
- The replay of the 12 lists (no CPU load) is not affected by 0028 (not re-run). The core was only syntax-checked for the changed files (rdp / cpu / rsp).

### Next ROM: rdp_timing v60 (`rdp_timing/v60`, `rdp_timing_v60.zip`)
The three loads beside synthetic triangles (7 kinds x span widths 2 / 4 / 8 / 16 / 32 / 64), **in a megabyte no buffer shares** (colour 0x300000, depth 0x400000, commands 0x700000, loads 0x600000).
In v58 the loss differed 1.5 times between widths 4 and 32 and with the mode; with the place taken out this gives the dependence on span length and on what is drawn. The result file is `rdp_timing_v60_NNN.txt`.

## Console results of v60: what a CPU transfer costs changes with the spans the RDP is drawing (added 2026-11-16, part 12; no change to the model)

Source: `hw/v60_ntsc_001.txt` (NTSC console, 8 MB, VI off, 222 rows, no timeouts). Tool: `rdp_timing/v60/compare.py`.
The loads are in a megabyte no buffer shares (0x600000), so the place has no part in these values.

### RDP clocks lost per transfer (data fill / instruction fill / random store), by the span width of the synthetic triangles
| case | width 2 | width 4 | width 8 | width 16 | width 32 | width 64 |
|---|---|---|---|---|---|---|
| base (2-cycle, textured, blended, depth compare + update) | 12.7 / 7.3 / 18.2 | 12.0 / 6.6 / 16.9 | 10.9 / 6.4 / 15.8 | 9.6 / 6.0 / 12.8 | 7.2 / 4.9 / 10.3 | 5.9 / 3.8 / 7.3 |
| 1cyc_base (1-cycle) | 11.0 / 5.5 / 16.0 | 12.0 / 6.8 / 17.1 | 10.9 / 6.4 / 15.6 | 9.5 / 6.0 / 13.3 | 8.2 / 5.6 / 10.9 | 7.0 / 4.5 / 8.9 |
| no_imrd (no blender read) | 9.8 / 5.6 / 16.0 | 11.2 / 6.6 / 17.4 | 10.1 / 6.1 / 15.7 | 8.7 / 5.5 / 11.5 | 5.2 / 3.2 / 8.3 | 3.1 / 1.8 / 4.5 |
| zfail (every pixel fails the depth test) | 9.7 / 5.1 / 14.2 | 9.3 / 6.3 / 13.7 | 8.6 / 6.1 / 11.9 | 8.4 / 5.8 / 10.2 | 6.3 / 4.5 / 9.0 | 5.2 / 3.4 / 6.2 |
| no_zupd (depth compare only) | 10.7 / 6.7 / 15.8 | 11.7 / 7.3 / 16.5 | 11.1 / 7.4 / 15.4 | 10.4 / 6.5 / 14.1 | 9.0 / 5.8 / 12.4 | 9.2 / 5.6 / 12.5 |
| no_zcmp (no depth compare) | 10.3 / 5.9 / 14.0 | 11.0 / 6.2 / 15.0 | 10.9 / 6.3 / 16.4 | 10.8 / 6.9 / 16.5 | 9.6 / 5.9 / 14.2 | 10.4 / 6.3 / 14.7 |
| op_08_plain (no texture, no depth) | 8.4 / 4.3 / 14.2 | 9.5 / 4.9 / 14.2 | 9.5 / 6.3 / 14.1 | 10.2 / 6.2 / 13.3 | 8.7 / 5.4 / 11.2 | 8.9 / 3.9 / 9.4 |
| ares (series v23, every case) | 10.0 / 6.1-7.9 / 13.0-13.2 | same | same | same | same | same |

- **Beside spans with depth compare + update, a transfer costs less the wider the spans are**: a data fill 12.7 clk at width 2, 5.9 at width 64 (0.46 times). Instruction fills and stores fall the same way.
- **Beside spans without depth compare the loss hardly depends on the width** (data fill 9.6 to 11.0 clk). Depth compare without update lies between, 9.2 clk at width 64.
- Without the blender's read (no_imrd) wide spans cost still less (3.1 clk at width 64). Spans that fail the depth test behave like depth compare + update.
- ares has one value per kind of transfer: too little beside narrow spans, too much beside wide ones.
- **The additions for the place stay additions at every width** (base, difference to the unused megabyte): the list's megabyte +0.5 to 0.7 for a data fill;
  the colour buffer's megabyte +1.3 to 2.1 for a data fill and +3.4 to 5.1 for an instruction fill; the depth buffer's megabyte +0.7 to 2.1 and +2.2 to 4.3 clk. The form of 0028 (a base value plus an addition for the place) stays.

### A rule built from this, tried in the harness (not adopted)
For spans with depth compare, a multiplier on the base value of a CPU transfer from the span width w, averaged over the frame's spans by their time
(depth compare + update: 1.1 x min(1, (8 / w)^0.37); depth compare only: 1.07 x min(1, (8 / w)^0.1); anything else 1; base values 10.4 clk for a data fill, 6.3 for an instruction fill).

| data fill, unused megabyte, clk | console | series v23 | the rule |
|---|---|---|---|
| synthetic base, width 2 / 8 / 16 / 32 / 64 | 12.7 / 10.9 / 9.6 / 7.2 / 5.9 | 10.0 (constant) | 11.5 / 11.5 / 9.0 / 7.0 / 5.3 |
| BAR 2316 | 10.1 | 10.1 | 10.1 |
| MGC p4 4316 | 10.8 | 10.0 | 10.5 |
| MGC p5 5075 | 10.2 | 10.0 | 10.6 |
| **DK64 d6373** | **8.1** | 10.0 | **10.2** |

| games (harness) | series v23 | the rule | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 236 / 316 | 248 / 321 | 231 / 325 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ) | 7 / 371 / 42 (46) | 7 / 372 / 42 (48) | 9 / 385-391 / 25 |
| MGC total / the "@" frame | +0.4% / +32 VI | +0.5% / +39 VI | VI 9385 |

- The rule fits the synthetic triangles and the BAR / MGC lists, but **DK64's list stays high** (10.2 against the console's 8.1). The mean span of DK64 d6373 is 18.6 pixels,
  about that of BAR 2316 (21.9) and MGC p5 (22.4): span width and depth compare alone do not give DK64's 0.8.
- In the games BAR's seg0 gets worse (236 to 248 frames) and DK64 and the MGC demo do not move. **Not adopted; the series stays v23** (patches 0001 to 0028 and 9999 are those of the last delivery).
  The harness work tree keeps the rule as a knob, `T_CPUSHARE=1` (off by default, and with it off every row of v60 equals series v23; it is not part of 9999).

### The multiplier each list needs (data fills of v42 / v59, unused megabyte, relative to 10.4 clk)
| MGC p2 | MGC p3 | MGC p4 (3 lists) | MGC p5 | BAR 2316 | BAR i2274 | DK64 797 | DK64 d6373 |
|---|---|---|---|---|---|---|---|
| 0.84 | 0.92 | 1.06-1.09 | 0.96 | 0.96 | 0.89 | 0.97 | 0.77 |

It moves inside one title as well (DK64 0.77 to 0.97, MGC 0.84 to 1.09). Ten game lists have been measured on the console so far, too few to tell what decides it.

### Next ROM: rdp_timing v61 (`rdp_timing/v61`, `rdp_timing_v61.zip`)
The same loads beside **29 game lists** (9 of the MGC demo, 14 from two scenes of BAR's intro, 6 of DK64); VI off, the loads in a megabyte the list does not use.
- The loss per list is set against what the harness counts in the list (pixels, spans, primitives, where the time goes: `rdp_timing/v61/list_stats.txt`).
- `DPC_PIPEBUSY` / `DPC_TMEM` / `DPC_BUFBUSY` of every run are recorded: if a console counter follows the loss, the model can use that quantity.
- Rows with one data fill every ~250 / ~1000 CPU cycles were added: the games' transfers are sparse, and this checks that the per-transfer value taken back to back holds for them.

The result file is `rdp_timing_v61_NNN.txt`. ares (series v23) gives 10.0 clk for a data fill, 6.1 to 7.1 for an instruction fill and 13.1 to 13.4 for a store beside every list (`rdp_timing/v61/ares_reference.txt`).

## Console results of v61: what a CPU transfer costs falls into groups by scene (added 2026-11-16, part 13; no change to the model)

Source: `hw/v61_ntsc_001.txt` (NTSC console, 8 MB, VI off, 174 rows, no timeouts, the loads in a megabyte the RDP does not use for every list). Tool: `rdp_timing/v61/compare.py`; what the lists hold is counted by the series v23 harness (`ARES_FEAT_LOG=1`).

### RDP clocks lost per transfer (mean and range per scene)
| scene (lists) | data fill, back to back | data fill, sparse (slope) | instruction fill, back to back | store, back to back |
|---|---|---|---|---|
| MGC write-only: p1 / p4 / p6 (5) | 11.0 (10.5-11.3) | 9.7 (9.4-9.9) | 7.0 (6.8-7.1) | 13.6 (13.1-14.0) |
| MGC p5 (2) | 10.4 (10.2-10.6) | 9.4 (9.3-9.6) | 6.3 (6.2-6.3) | 13.2 (13.0-13.3) |
| MGC blended: p2 / p3 (2) | 9.0 (8.8-9.2) | 9.0 (8.9-9.0) | 5.7 (5.4-6.0) | 12.4 (12.0-12.8) |
| BAR intro, first scene (8) | 9.6 (9.3-10.2) | 9.9 (9.4-10.4, one list 8.2) | 6.7 (6.2-7.2) | 13.2 (12.8-14.1) |
| BAR intro, second scene (6) | 8.8 (8.7-9.0) | 8.8 (8.7-9.0) | 5.8 (5.7-6.0) | 11.8 (11.5-12.2) |
| DK64 DKTV (6) | 8.2 (7.9-8.7) | 8.7 (8.2-8.9) | 5.2 (5.0-5.4) | 11.7 (11.1-12.2) |
| ares (series v23, all 29) | 10.0-10.1 | 9.3-10.6 | 6.1-7.1 | 13.1-13.4 |

- **The loss falls into groups by scene, in the direction of what is left in the games.** BAR's first scene, where series v23 agrees with the console, has 9.9 clk, the model's base value (9.95); BAR's second scene, DK64 and the MGC demo's blended parts, where ares is slow, have 8.7 to 9.0 clk (0.88 to 0.91 times).
- **Back-to-back and sparse transfers give different values.** Back-to-back data fills spread over 7.9 to 11.3 clk, sparse ones (one every ~250 / ~1000 CPU cycles) over 8.2 to 10.4 (MGC's write-only parts 11.0 -> 9.7, DK64 8.2 -> 8.7). The games' transfers are sparse, so the sparse values are the reference.
- The sparse rows carry something that does not depend on the number of transfers (0 to 6000 clk per run; about as much in ares); the difference of the two rates (the slope) removes it. The gap1000 rows have few transfers: the slopes are good to 0.3 to 0.5 clk.
- **The console's `DPC_TMEM` (time spent loading textures) equals the model's texture load time**: as a share of the list the 29 lists differ by 0.3 points on average and 0.8 at most (MGC p2: console 22.8% / model 22.9%; DK64 s6310: 20.0% / 19.6%). `DPC_PIPEBUSY` and `DPC_BUFBUSY` are about `DPC_CLOCK` and tell nothing.
- The lists alone: console / ares 0.945 to 1.014 (mean 0.983).

### Which properties of a list go with the loss (regression; cause not confirmed)
The share of the list's time spent in each render mode as the explaining variables, the loss of the 29 lists as the target (every set of 1 to 3 variables, compared by leave-one-out cross-validation).
The same set explains sparse data fills, instruction fills and stores best.
| property (share of the time) | BAR first | BAR second | DK64 | MGC p2 / p3 | coefficient (data fill gap250) |
|---|---|---|---|---|---|
| drawn with alpha compare | 0.06 | 0.31 | 0 | 0 | -3.55 clk (per share of 1) |
| drawn with ATOMIC_PRIM | 0 | 0 | 0.76 | 0 | -1.47 |
| blended without depth | 0.03 | 0.08 | 0.19 | 0.61 | -1.80 |

Constant 10.31 clk, residual rms 0.22 clk (cross-validated 0.25; the losses' standard deviation is 0.62). Neither v60's span-width rule nor the split of the time into spans, triangles, loads and fills separates BAR's two scenes (rms 0.37 clk).

**This is a statistical association only.** Only DK64 uses ATOMIC_PRIM and only BAR's second scene has much alpha compare, so neither can be told from other properties of that title or scene.
Beside v60's synthetic triangles blending without depth (9.6 to 11.0 clk) was not cheaper than write-only (8.4 to 10.2), which does not fit the third term.

### The regression tried in the harness (adoption on hold)
The base value of a CPU transfer multiplied by 0.70 for the share of the frame drawn with alpha compare, 0.87 with ATOMIC_PRIM and 0.85 blended without depth (`T_CPUMODE=1`; base values as series v23).
| games (harness) | series v23 | the rule | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 236 / 316 | 239 / **324** | 231 / 325 |
| BAR seg1, frames per 60 VI | 25 21 23 26 30 30 30 27 28 26 26 24 | 25 21 24 27 30 30 30 28 30 26 28 25 | 26 24 24 29 30 30 30 26 29 24 28 25 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ) | 7 / 371 / 42 (46) | 7 / 377 / 37 (48) | 9 / 385-391 / 25 |
| MGC "@" frame / start of part 4 | +32 / +14 VI | **+17 / +2 VI** | VI 9385 / 5764 |

- BAR's seg1 (316 -> 324, console 325) and the MGC demo (+32 -> +17 VI) come much closer and DK64 loses 5 of its long frames (42 -> 37). **This confirms that what is left is the difference between lists in what a CPU transfer costs.**
- BAR's seg0 gets 3 frames worse (236 -> 239; the first scene has 2 to 15% alpha compare too). DK64 gets x0.87, which is what its sparse data fills measure (0.88), yet 37 long frames remain (console 25).
  Instruction fills have only been measured back to back (0.76 times for DK64); v62 measures them sparse.
- Which bit is the cause has not been checked on the console, so **the rule is not in the series** (patches 0001 to 0028 and 9999 are those of the last delivery; the knob is in the harness work tree only, off by default).

### Next ROM: rdp_timing v62 (`rdp_timing/v62`, `rdp_timing_v62.zip`)
The same lists with one thing changed, to find the cause.
- **K**: 8 game lists, each as it is and with one bit changed in every Set_Other_Modes (DK64: ATOMIC_PRIM off / 2-cycle / AA off / IMAGE_READ off / no texture loads; BAR's second scene: alpha compare off / ATOMIC_PRIM on and others; BAR's first scene and the MGC demo: ATOMIC_PRIM on, alpha compare on and others; 37 variants).
- **L**: synthetic triangles (v60's layout, widths 4 / 16 / 64) with ATOMIC_PRIM and with alpha compare (every pixel passing / failing).
- Loads: data fills back to back and one every ~125 / ~250 / ~500 CPU cycles (the slope over the three gives the loss per transfer), instruction fills back to back and one every ~250 CPU cycles (sparse instruction fills are new).

The result file is `rdp_timing_v62_NNN.txt`. If turning ATOMIC_PRIM off raises DK64's loss to that of BAR's first scene and turning it on there lowers it, the ATOMIC_PRIM term goes in with the measured value; the same for alpha compare.

## Console results of v62: a CPU transfer costs little while a texture is loaded; alpha compare and ATOMIC_PRIM are not the cause (added 2026-11-17, part 14; no change to the model)

Source: `hw/v62_ntsc_001.txt` (NTSC console, 8 MB, VI off). **24 of the 37 variants** (two DK64 lists and two of BAR's second scene): the ROM stopped in one of the four variants of `bar_2316`, and L (synthetic) was not measured.
There is no timeout line; the file holds what the last save (after variant 24) wrote. In ares the ROM runs to its end, so the cause is not known. Tool: `rdp_timing/v62/compare.py`.

### Relative to the list as it is (variant / full)
| changed | lists | list alone | data fill, back to back | data fill, sparse (slope) | instruction fill, back to back | instruction fill, sparse |
|---|---|---|---|---|---|---|
| **no texture loads** | DK64 d6373 / s6340 | x0.81 / x0.80 | **x1.11 / x1.12** | x1.19 / x1.15 | x1.02 / x1.06 | x1.09 / x1.05 |
| ATOMIC_PRIM off | DK64 d6373 / s6340 | x0.98 / x0.98 | x1.03 / x1.02 | x1.05 / x1.03 | x1.03 / x1.09 | x1.02 / x0.97 |
| ATOMIC_PRIM on | BAR s1960 / s2120 | x1.03 / x1.03 | x0.97 / x0.96 | x0.93 / x1.00 | x0.95 / x0.89 | x1.08 / x0.88 |
| alpha compare off | BAR s1960 / s2120 | x1.00 / x1.00 | x1.01 / x1.00 | x1.00 / x1.00 | x1.00 / x1.00 | x0.99 / x0.99 |
| TLUT off | BAR s1960 / s2120 | x1.00 / x1.00 | x1.00 / x1.00 | x1.00 / x1.01 | x1.00 / x1.00 | x1.03 / x1.02 |
| AA off | 2 DK64 / 2 BAR lists | x0.99-1.00 | x0.98-0.99 | x0.93-1.00 | x0.91-1.03 | x0.94-1.03 |
| IMAGE_READ off | BAR s1960 / s2120 | x0.83 / x0.84 | **x0.79 / x0.80** | x0.80 / x0.87 | x0.90 / x0.84 | x0.85 / x0.85 |
| IMAGE_READ off | DK64 d6373 / s6340 | x0.86 / x0.85 | x0.98 / x1.00 | x0.92 / x0.80 | x0.92 / x1.00 | x0.88 / x0.79 |
| 1-cycle made 2-cycle | DK64 d6373 / s6340 | x1.03 / x1.04 | x0.97 / x0.96 | x0.89 / x0.78 | x0.92 / x0.91 | x0.95 / x0.78 |

- **Alpha compare changes nothing** (neither the list's time nor the loss): the strongest term of v61's regression was not a cause. Neither are TLUT and AA.
- **ATOMIC_PRIM is worth 3 to 4% only** (the loss is lower with it on); it does not give DK64's 0.88.
- **Without the texture loads a transfer costs more** (back-to-back data fills 8.1 -> 9.0 and 8.0 -> 8.9 clk). The loads are 18.1% / 19.3% of the DK64 lists' time (the console's DPC_TMEM), so
  **a transfer during a load costs about 4 clk** (4.1 / 4.1 from back-to-back data fills, 4.7 / 3.7 from back-to-back instruction fills, 3.1 / 3.9 from sparse instruction fills) against 9.0 clk (5.2 to 5.5 for an instruction fill) in the rest of the list.
- **Without IMAGE_READ the loss is lower** (about 0.8 times in BAR's second scene, as `no_imrd` in v60's synthetic triangles): spans that read the colour buffer are where a transfer costs most.
- Sparse instruction fills (measured for the first time): 5.0 / 4.9 clk beside DK64, 5.3 / 5.35 beside BAR's second scene, about the back-to-back values (5.1 / 5.0, 5.7 / 5.95) or a little less. The model's base value is 6.1 clk.

### Regression over 53 lists (v61's 29 and v62's 24)
With the loss during loads fixed at the measured value (4.2 clk for a data fill, 4.0 for an instruction fill) and the rest split into "time in fill rectangles" and "everything else",
instruction fills give 4.8 clk for everything else and 22.8 clk during fills, residual rms 0.34 clk, with the scenes' mean residuals at -0.19 (MGC), -0.09 (BAR first), -0.13 (BAR second) and +0.05 clk (DK64)
(a constant 6.1 clk leaves -0.43 / -0.64 / +0.30 / +0.94). Back-to-back data fills give 8.45 clk and 25.9 during fills, sparse ones 8.44 and 21.5.
With the load term alone BAR's first scene (7 to 18% of its time in fills) comes out about 1 clk low.
**The loss during fill rectangles (about 22 clk) is a regression value, not a direct measurement.** After v61's regression (alpha compare was not a cause) it does not go into the model before it is measured.

### Tried in the harness (adoption on hold)
4.2 / 4.0 clk during loads, 22 clk during fills, 8.45 / 4.8 clk otherwise (data / instruction fill), weighted by the previous frame's time (`T_CPUACT=1 T_CPUX=8.45 T_CPUXI=4.8`).
| games (harness) | series v23 | v61's regression (last time) | **loads + fills** | console |
|---|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 236 / 316 | 239 / 324 | **237 / 323** | 231 / 325 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ) | 7 / 371 / 42 (46) | 7 / 377 / 37 (48) | **9 / 381 / 34** (50) | 9 / 385-391 / 25 |
| DK64, VI taken | +1.3% | +0.9% | **+0.7%** | - |
| MGC "@" frame / start of part 4 | +32 / +14 VI | +17 / +2 VI | +21 / +6 VI | VI 9385 / 5764 |

- The physical split of the time (loads, fills) alone comes about as close as last time's regression. DK64's long frames go from 42 to 34 and its delta-2 frames to the console's 9; BAR's seg0 hardly moves (236 -> 237).
- Not in the series (patches 0001 to 0028 and 9999 are those of the last delivery; the knob is in the harness work tree only, off by default).

### Next ROM: rdp_timing v63 (`rdp_timing/v63`, `rdp_timing_v63.zip`)
- **M**: 17 lists in which the RDP does one thing only (two kinds of fill rectangles, two of Load_Block, Load_Tile, Load_TLUT, two kinds of texture rectangles, state commands, v60's triangles of four kinds at widths 8 / 32): the loss per activity, measured directly.
- **K**: the lists v62 did not reach (`bar_2316`, `bar_s1716`, `mgc_p3`, `mgc_p4`) and `mgc_p2`, as they are, without loads and without IMAGE_READ (13 variants).
- A start line is written and saved before every list, so a stop leaves its place in the file. The alpha-compare-on variant, new in v62, is left out.

The result file is `rdp_timing_v63_NNN.txt`. With direct values for fills, loads and the kinds of spans, "the loss weighted by the previous frame's time" becomes the patch (series v24).

## Console results of v63: what a CPU transfer costs, measured directly for each thing the RDP does (added 2026-11-17, part 15; no change to the model)

Source: `hw/v63_ntsc_001.txt` (NTSC console, 8 MB, VI off; all 17 lists of M and 13 variants of K, no timeouts). Tools: `rdp_timing/v63/compare.py`; the prediction is `tools/v61_analysis/activity.py`.

### RDP clocks lost per transfer while the RDP does one thing only
| the RDP is doing | data fill, back to back / sparse (slope) | instruction fill, back to back / sparse |
|---|---|---|
| fill rectangles 320x240 | **12.2 / 10.6** | 12.3 / 7.6 |
| fill rectangles 32x240 | 7.1 / 5.6 | 5.6 / 6.8 |
| Load_Block, 2048 bytes | **4.0** / (gap125: 4.6) | 4.5 / 4.75 |
| Load_Block, 512 bytes | 3.6 / 3.7 | 2.7 / 4.35 |
| Load_Tile 32x32, Load_TLUT 256 | 0.8 / (scattered) | 0.7-1.3 / 1.6-5.6 |
| texture rectangles 32x32 (1-cycle, write-only) | 8.8 / 11.5 | 4.6 / 4.3 |
| texture rectangles 32x32 (copy mode) | 6.1 / 5.8 | 4.6 / 4.35 |
| state commands only | 4.6 | 4.4 |
| triangles, depth compare + update + blend, width 8 / 32 | 11.1 / 11.5, 7.3 / 8.6 | 6.55 / 7.0, 5.0 / 4.5 |
| triangles without IMAGE_READ, width 8 / 32 | 10.1 / 10.2, 5.4 / 5.3 | 6.2 / 7.3, 3.3 / 3.4 |
| triangles blended without depth, width 8 / 32 | 11.0 / 10.9, 9.7 / 10.3 | 6.4 / 7.5, 6.0 / 6.8 |
| triangles without texture and depth, width 8 / 32 | 9.5 / 9.5, 8.9 / 8.2 | 6.4 / 6.6, 5.5 / 4.9 |

- **During fill rectangles (320 pixels wide) a transfer costs 12 clk**, half of the 22 clk of last time's regression: **the regression value was off again** (the direction was right: more than during anything else). 32-pixel fills cost 7 clk: for fills the loss rises with the width.
- **During texture loads it costs 4 clk**, the indirect value of v62 (4.1). Removing the loads from game lists gives the same (BAR 2316 / BAR s1716 / MGC p2 / MGC p4: 5.5 / 4.6 / 4.5 / 4.8 clk for a data fill, 5.8 / 3.9 / 2.8 / 5.0 for an instruction fill).
- **For triangles back-to-back and sparse transfers cost about the same** (mostly within 0.5 clk), so v60's back-to-back table (7 kinds x widths 2 to 64) holds for sparse transfers.
- Beside BAR 2316, too, the loss falls to 0.86 times without IMAGE_READ and to 0.97 with ATOMIC_PRIM (as in v62).

### The loss of each list predicted from the direct values alone (nothing fitted)
The list's time split into spans by kind and width, fills, loads and the rest (counted by the harness), each weighted with the value of the table above and of v60's. No coefficient is fitted.
| lists | predicted - console (data fill, back to back) | constant 9.95 - console | predicted - console (instruction fill) | constant 6.1 - console |
|---|---|---|---|---|
| DK64 (6) | **+0.04** | +1.71 | +0.18 | +0.94 |
| BAR intro, second scene (6) | **-0.06** | +1.13 | -0.20 | +0.30 |
| v62's 24 variants (DK64, BAR's second scene) | **+0.09** | +1.60 | +0.10 | +0.80 |
| BAR intro, first scene (8) | -1.34 | +0.33 | -1.14 | -0.64 |
| MGC (9) | -1.15 (sparse -0.19) | -0.47 (sparse +0.49) | -0.72 | -0.43 |
| v63's 13 variants (BAR's first scene, MGC) | -0.91 (sparse -0.25) | +0.02 | -0.71 | -0.38 |

- **DK64 and BAR's second scene come out within 0.1 clk on average from the direct values alone**: the two scenes where series v23 is off on the slow side (+1.1 to +1.7 clk with the constant).
- **BAR's first scene comes out 1.3 clk low.** 19 to 28% of its time is in **depth-buffered spans wider than 64 pixels** (0 to 13% elsewhere). The triangles were measured up to 64 pixels only and wider spans use the value at 64 (5.9 clk).
  For fills the loss rises with the width (7 clk at 32, 12 at 320), so depth-buffered spans may cost more again beyond 64 pixels (about 10 clk from the residuals of the 66 lists, which is a regression value).
- The MGC lists come out 1.15 clk low back to back and 0.19 low sparse. Write-only textured spans (part 4) and 1-cycle blending (parts 2 / 3) have not been measured on synthetic triangles.

So **the form of the model (the loss per activity weighted by where the list's time goes) is confirmed, but the values for wide spans are missing.** Putting it in now would lose BAR's first scene, which agrees today; the series stays v23 (patches 0001 to 0028 and 9999 are those of the last delivery).

### Next ROM: rdp_timing v64 (`rdp_timing/v64`, `rdp_timing_v64.zip`)
The missing values, measured directly (21 lists and 4 variants of a BAR list).
- Triangles 64 / 128 / 256 pixels wide: depth compare + update + blend, without IMAGE_READ, blended without depth; at 128 also 1-cycle, every pixel failing, depth compare only.
- Write-only textured triangles (4 / 16 / 64 / 256 pixels), 1-cycle blended ones (8 / 32).
- Fill rectangles 64 / 128 / 320 pixels wide.
- A light frame of BAR's first scene (`bar_s1716`) without its rectangles and loads (triangles only: do the wide-span values explain it?).

The result file is `rdp_timing_v64_NNN.txt`. With the table complete, the loss per activity becomes patch 0029 (series v24).

## Console results of v64: wide spans do not cost more; what lowered the loss was the depth test failing, not the span width (added 2026-11-18, part 16; no change to the model)

Source: `hw/v64_ntsc_001.txt` (NTSC console, 8 MB, VI off; all 21 lists of N and 4 variants of K, no timeouts). Tool: `rdp_timing/v64/compare.py`.

### RDP clocks lost per transfer (back to back; data fill / instruction fill)
| the RDP is drawing | width 4 | 8 | 16 | 32 | 64 | 128 | 256 / 320 |
|---|---|---|---|---|---|---|---|
| depth compare + update + blend (base) | | | | | 6.3 / 3.65 | 6.9 / 4.1 | 6.5 / 3.4 |
| the same without IMAGE_READ | | | | | 3.35 / 1.9 | 3.8 / 2.2 | 2.2 / 1.4 |
| blended without depth (2-cycle) | | | | | 10.5 / 6.4 | 11.3 / 7.15 | 12.0 / 8.1 |
| blended without depth (1-cycle) | | 11.55 / 6.7 | | 10.9 / 7.0 | | | |
| write-only textured (1-cycle) | 13.6 / 9.4 | | 12.9 / 7.2 | | 14.0 / 5.8 | | 14.1 / 3.6 |
| 1-cycle base / every pixel failing / depth compare only | | | | | | 6.6 / 3.6, 5.7 / 2.9, 9.6 / 5.9 | |
| fill rectangles | | | | (v63: 7.1 / 5.6) | 12.1 / 11.5 | 12.2 / 12.0 | 12.2 / 12.3 |

- **Depth-buffered spans wider than 64 pixels do not cost more** (6.3 to 6.9 clk): last time's "about 10 clk" (a regression value from the residuals of 66 lists) was wrong.
- Fills cost a constant 12.1 to 12.2 clk from 64 pixels on (7.1 at 32); the 320-pixel value reproduces v63's (12.18 then, 12.17 now).
- Write-only textured spans cost 13 to 14 clk and blending without depth 10.5 to 12, however wide.

### A light frame of BAR's first scene (`bar_s1716`) taken apart
| variant | list alone | data / instruction fill (back to back) |
|---|---|---|
| as it is | 627956 | 9.37 / 6.83 |
| without the rectangles (depth buffer preset farthest) | 534137 | 8.82 / 6.57 |
| without rectangles and loads (triangles only) | 423377 | **9.89 / 7.28** |

- From the differences a transfer costs 12.5 clk during the rectangles (15% of the time) and 4.5 during the loads (20%): **the directly measured values** (fills 12.1, Load_Block 4.0).
- **The triangles alone cost 9.9 clk.** About 70% of their time is 46 large triangles (depth compare + update + blend, antialiased; many spans wider than 64 pixels), for which the table (6.3 to 6.9 clk) gives about 7.5 clk for the frame.

### "The wider the span, the smaller the loss" in the synthetic lists was "the more spans fail the depth test"
The synthetic triangles of v60 / v63 / v64 all have the same depth and are drawn on top of each other at random places. The wider they are the more they overlap, and what is drawn later fails the depth test.
| span width | pixels drawn / pixels of the screen | list time: base / zfail | base / depth compare only | loss: base / zfail / depth compare only |
|---|---|---|---|---|
| 2 | 0.4 | 1.52 | 1.09 | 12.7 / 9.7 / 10.7 |
| 8 | 1.7 | 1.38 | 1.05 | 10.9 / 8.6 / 11.1 |
| 32 | 6.8 | 1.07 | 0.90 | 7.2 / 6.3 / 9.0 |
| 64 | 13.7 | 1.03 | 0.87 | 5.9 / 5.2 / 9.2 |
| 128 | 6.8 | 1.06 | 0.91 | 6.9 / 5.7 / 9.6 |

- At width 2 base takes 1.52 times as long as zfail; from 32 pixels on only 1.03 to 1.07 times, and 9 to 13% less than "depth compare only", which writes no depth and always passes: **most spans of the wide base lists fail the depth test.**
- The loss follows: 9 to 12 clk at every width for what always passes (depth compare only, blending without depth), and the lower the wider for spans that fail (5.2 clk at 64).
- The large triangles of BAR's light frame are drawn onto a depth buffer that has just been cleared, so they pass. DK64 and BAR's second scene have much overdraw, so many of their spans fail:
  this fits last time's table giving DK64 and BAR's second scene and missing BAR's first. It is the quantity found with v15 (2026-11-01): the pass rate of the depth test, which depends on the scene's drawing order.

This is a re-reading of the data at hand: the loss beside passing spans has not been measured width by width (base mixes passing and failing spans from 8 pixels on). The model is unchanged (patches 0001 to 0028 and 9999 are those of the last delivery).

### Next ROM: rdp_timing v65 (`rdp_timing/v65`, `rdp_timing_v65.zip`)
- **P**: 17 synthetic lists in which every triangle is nearer than the one before, so that every pixel passes whatever it lies on (base at widths 4 to 256, without IMAGE_READ, 1-cycle, the modes of BAR's / DK64's antialiased surfaces).
- **K**: `bar_s1716`, `bar_2316` and `dk64_d6373` with the depth buffer preset farthest (every first draw passes) / nearest (everything fails), as they are and with the triangles only: the same triangles with nothing but passing / failing changed.

The result file is `rdp_timing_v65_NNN.txt`. With the values for passing and failing spans, and the per-frame estimate of the pass rate that ares already has, the loss becomes patch 0029 (series v24).

## Console results of v65: beside spans that pass the depth test a transfer costs 10 to 12 clk at every width (added 2026-11-18, part 17; no change to the model)

Source: `hw/v65_ntsc_001.txt` (NTSC console, 8 MB, VI off; all 17 lists of P and 12 variants of K, no timeouts). Tools: `rdp_timing/v65/compare.py`; the prediction is `tools/v61_analysis/activity2.py`.

### Depth-buffered triangles whose pixels all pass (back to back; data fill / instruction fill, clk)
| case | width 4 | 8 | 16 | 32 | 64 | 256 |
|---|---|---|---|---|---|---|
| depth compare + update + blend (passing) | 12.4 / 6.9 | 11.9 / 6.9 | 11.2 / 7.1 | 9.8 / 6.2 | 10.5 / 6.65 | 11.5 / 8.0 |
| the same without IMAGE_READ (passing) | 11.8 / 7.1 | | 11.0 / 6.7 | | 10.5 / 6.3 | 12.3 / 8.9 |
| 1-cycle (passing) | | 11.8 / 6.4 | | | 10.2 / 6.2 | |
| the mode of BAR's antialiased surfaces (passing) | | 11.9 / 6.9 | | | 10.5 / 6.6 | 11.5 / 8.0 |
| the mode of DK64's antialiased surfaces (passing) | | 11.7 / 6.5 | | | 10.2 / 6.3 | |
| (for reference) every pixel failing (v60 / v64) | 9.3 / 6.3 | 8.6 / 6.1 | 8.4 / 5.8 | 6.3 / 4.5 | 5.2 / 3.4 | at 128: 5.7 / 2.9 |

- **As read last time**: beside passing spans the loss is 10 to 12 clk (6.2 to 8.0 for an instruction fill) from 4 to 256 pixels and does not fall with the width. v60's "5.9 clk at 64 pixels" was the value of failing spans.
- The mode (1-cycle, BAR's / DK64's antialiased surfaces) makes 0.5 clk at most.

### Game lists with nothing but passing / failing changed (back to back; data fill / instruction fill, clk)
| list | triangles only: depth preset farthest (first draws pass) -> nearest (everything fails) | whole list: farthest -> nearest |
|---|---|---|
| BAR s1716 (first scene, light) | 9.65 / 7.20 -> 5.56 / 5.54 | 9.14 / 6.87 -> 6.71 / 5.67 |
| BAR 2316 (first scene, heavy) | 10.53 / 6.58 -> 7.92 / 5.05 | 10.13 / 6.65 -> 7.80 / 5.44 |
| DK64 d6373 | 8.80 / 5.23 -> 6.17 / 3.27 | 8.02 / 5.02 -> 5.74 / 3.51 |

The same triangles cost 2.6 to 4.1 clk less per data fill and 1.5 to 2.0 less per instruction fill when everything fails.

### Prediction from the table rebuilt on passing / failing (nothing fitted)
The loss beside depth-buffered spans as "pass rate x passing value + (1 - pass rate) x failing value", with the pass rate ares already estimates for every frame (from the amount of overdraw).
| lists | predicted - console (data / instruction fill) | constant - console | ares' estimate of the pass rate |
|---|---|---|---|
| BAR intro, first scene (8) | **+0.22 / +0.07** | +0.33 / -0.64 | 0.91-1.00 |
| MGC (9) | +0.54 / +0.18 | -0.47 / -0.43 | - |
| v63's 13 variants (BAR's first scene, MGC) | +0.48 / +0.24 | +0.02 / -0.38 | |
| v65, preset farthest (6) | +0.68 / +0.31 | +0.57 / -0.16 | |
| v65, preset nearest (6; pass rate 0) | +0.94 / +0.22 | +3.30 / +1.35 | 0 |
| BAR intro, second scene (6) | +1.53 / +0.69 | +1.13 / +0.30 | 0.94-0.97 |
| DK64 (6) | +0.95 / +0.98 | +1.71 / +0.94 | 0.77 |
| v62's 24 variants (DK64, BAR's second scene) | +1.42 / +0.97 | +1.60 / +0.80 | |

- **BAR's first scene now agrees** (-1.34 / -1.14 clk with last time's table) and the MGC lists are within 0.5 clk.
- **BAR's second scene and DK64 come out 1.0 to 1.5 clk high.** Last time's table (values falling with the width) had these two right by chance.
  ares estimates a pass rate of 0.94 to 0.97 for BAR's second scene, nearly everything passing; whether more fails in reality or another property is at work is not known.
  DK64 comes out about 2 clk high even with everything failing (8.25 against 6.17), so the pass rate alone does not explain it.

Putting this table in now would move BAR's second scene further off, so the model is unchanged (patches 0001 to 0028 and 9999 are those of the last delivery).

### Next ROM: rdp_timing v66 (`rdp_timing/v66`, `rdp_timing_v66.zip`)
Seven game lists (two of BAR's second scene, two of its first, two of DK64, MGC p5) **as they are / with the depth compare off (every pixel written) / with the depth buffer preset nearest (nothing written)**.
Where the list as it is lies between "everything written" and "nothing written" comes out of both the list's time and the loss: this separates the pass rate from another property as the reason for the low values of BAR's second scene and DK64. The result file is `rdp_timing_v66_NNN.txt`.

## Console results of v66: BAR's second scene loses as little with every pixel written (added 2026-11-18, part 18; no change to the model)

Source: `hw/v66_ntsc_001.txt` (NTSC console, 8 MB, VI off). **Two of the seven lists (the six variants of BAR's second scene)**: the ROM stopped with the start line of `bar_2316 full` as its last.
The same place as v62 (both times `bar_2316 full` right after `bar_s2120`; in v63 / v65 `bar_2316` ran in another order). There is no timeout line and ares runs the ROM to its end, so the cause is not known.

### BAR's second scene (back to back; data fill / instruction fill, clk)
| list | variant | list alone | loss |
|---|---|---|---|
| bar_s1960 | as it is | 1361329 | 8.92 / 5.65 |
| | depth compare off (every pixel written) | 1194865 | **8.81 / 5.51** |
| | depth buffer preset nearest (nothing written) | 1162186 | 6.75 / 4.21 |
| bar_s2120 | as it is | 1210074 | 8.81 / 5.87 |
| | depth compare off (every pixel written) | 1051693 | **8.52 / 5.51** |
| | depth buffer preset nearest (nothing written) | 1026301 | 6.60 / 4.64 |

- **The lists as they are lose what they lose with everything written** (at 1.05 / 1.15 of the way from "nothing written" to "everything written"): nearly every pixel of this scene passes, so ares' estimate of the pass rate (0.94 to 0.97) is right.
- The loss is still 8.5 to 8.9 clk, 1.6 to 1.9 below what the passing / failing table predicts (10.4 to 10.6). **What makes BAR's second scene low is not the pass rate but another property.**
- The two scenes the prediction misses (BAR's second scene, DK64) spend 13 to 22% of their time in **triangles and texture rectangles that do not use the depth buffer** (mostly wide spans); BAR's first scene, which agrees, 0 to 9%.
  The table gives that part 11 to 14 clk (the synthetic values for blending without depth and for write-only spans). If it is low in reality, that is the size of what is missing for the two scenes (1.0 to 1.9 clk). This is a reading, not a measurement.

The model is unchanged (patches 0001 to 0028 and 9999 are those of the last delivery).

### Next ROM: rdp_timing v67 (`rdp_timing/v67`, `rdp_timing_v67.zip`)
Five game lists (`bar_2316`, two of DK64, two of BAR's second scene) **as they are / without what is drawn without depth / without what is drawn with depth**: the loss of each part, measured directly.
Against the stop: `bar_2316` is measured first, and the file is saved right after each list is loaded as well (a stop then shows whether it happened while loading or while measuring). The result file is `rdp_timing_v67_NNN.txt`.

## Console results of v67: what DK64 draws without the depth buffer is not cheap; a lead on the stops (added 2026-11-18, part 19; no change to the model)

Source: `hw/v67_ntsc_001.txt` (NTSC console, 8 MB, VI off). **Three of the five lists (`bar_2316` and the two of DK64, 9 variants)**: the ROM stopped after loading `bar_s1960 full`, while measuring it (the third stop after v62 and v66).

### The lists taken apart (back to back; data fill / instruction fill in clk, the list's own time in brackets)
| list | as it is | without what uses no depth | without what uses depth | loss during what uses no depth, from the difference |
|---|---|---|---|---|
| bar_2316 | 10.15 / 6.56 (1404718) | 10.18 / 6.48 (1300605) | 8.13 / 7.18 (397881) | 9.8 / 7.6 |
| dk64_d6373 | 8.07 / 5.05 (2075301) | 8.02 / 5.33 (1730157) | 6.32 / 4.75 (943377) | 8.3 / 3.65 |
| dk64_s6340 | 7.96 / 4.96 (1617693) | 7.75 / 5.30 (1272741) | 6.62 / 4.67 (839649) | 8.7 / 3.7 |

- **Last time's reading (a transfer costs little during what is drawn without the depth buffer) is wrong for DK64**: that part costs 8.3 to 8.7 clk per data fill, and removing it leaves the list's loss where it was (8.07 -> 8.02, 7.96 -> 7.75).
- What is low in DK64 is **the depth-buffered part itself**: without the loads (4.2 clk) it costs about 9.1 clk in DK64 d6373 and about 10.8 in BAR 2316.
- With v65's values (DK64's triangles alone: 8.80 clk with the first draws passing, 6.17 with everything failing) the effective pass rate of DK64's depth-buffered spans comes to about 0.56 (passing 11.7, failing 5.4 clk).
  **ares' estimate (0.77) is too high for DK64**, which has much overdraw: more of it fails than the estimate, which assumes a random drawing order, gives.
- BAR's second scene (low even with every pixel written, part 18) stays open: v67 did not reach it.

### A lead on the stops
While v68 was being made, a ROM with a save before every load raised an exception in the emulator. The cause: **enabling interrupts while the VI is off crashes**, and the RDP timeout handler did just that up to v67.
An RDP timeout on the console therefore crashed the ROM before the timeout line was saved, which fits v62 / v66 / v67 stopping between two saves without such a line.
RDP timeouts on the console are the likely cause, not a confirmed one (they do not happen in ares). v68 has the handler fixed, so the next one leaves the RDP's state in the file.

The model is unchanged (patches 0001 to 0028 and 9999 are those of the last delivery).

### Next ROM: rdp_timing v68 (`rdp_timing/v68`, `rdp_timing_v68.zip`)
- Two lists of BAR's second scene and a light frame of its first, as they are / without what uses no depth / without what uses depth (what v67 did not reach).
- The timeout handler is fixed and a marker is saved before every load: a stop leaves the list, the load and the RDP's state in the file.

The result file is `rdp_timing_v68_NNN.txt`.

## Console results of v68, and the loss-per-activity model tried in the harness (added 2026-11-18, part 20; the series stays v23)

Source: `hw/v68_ntsc_001.txt` (NTSC console, 8 MB, VI off; all 9 variants, no timeouts). Tools: `rdp_timing/v68/compare.py`; the prediction is `tools/v61_analysis/activity2.py`.

### BAR's lists taken apart (back to back; data fill / instruction fill in clk, the list's own time in brackets)
| list | as it is | without what uses no depth | without what uses depth | loss during what uses no depth |
|---|---|---|---|---|
| bar_s1960 (second scene) | 8.90 / 5.66 (1361101) | 9.38 / 6.26 (1170913) | 6.28 / 4.47 (496345) | **5.9 / 2.0** |
| bar_s2120 (second scene) | 8.80 / 5.88 (1210009) | 9.67 / 6.54 (970717) | 6.17 / 4.37 (499681) | **5.3 / 3.2** |
| bar_s1716 (first scene) | 9.32 / 6.77 (627829) | the same (nothing drawn without depth) | 8.36 / 7.73 (237397) | - |

- **In BAR's second scene a transfer costs 5.3 to 5.9 clk during what is drawn without the depth buffer (14 to 20% of the time)**, measured directly; the table had 11 to 14 clk there.
  That part is mostly spans wider than 64 pixels; v64's synthetic write-only triangles 256 pixels wide cost 5.4 to 6.9 clk under sparse loads.
- The depth-buffered part costs 10.1 / 10.5 clk without the loads (4.2 clk), close to BAR 2316's 10.8.
- The part drawn without depth costs 9.8 clk in BAR 2316 and 8.3 to 8.7 in DK64 (v67), in line with their span widths.

### Two corrections to the table, and the check on 78 lists
1. Write-only spans wider than 64 pixels: 5.5 / 3.6 clk (v68, v64's sparse rows).
2. For the loss, the pass rate of depth-buffered spans is taken as 1 / (layers of overdraw) (DK64: 0.56 measured, 0.57 so; the estimate used for the spans' time stays as it is).

| lists | predicted - console (data / instruction fill) | constant (series v23) - console |
|---|---|---|
| BAR intro, first scene (8) | -0.01 / -0.06 | +0.33 / -0.64 |
| MGC (9) | -0.29 / +0.18 | -0.47 / -0.43 |
| BAR intro, second scene (6) | +0.79 / +0.52 | +1.13 / +0.30 |
| DK64 (6) | +0.48 / +0.70 | +1.71 / +0.94 |
| v62's 24 variants | +0.70 / +0.70 | +1.60 / +0.80 |
| v63's 13 variants | -0.05 / +0.16 | +0.02 / -0.38 |
| v65, preset farthest (6) / nearest (6) | +0.33 / +0.11, +0.94 / +0.22 | +0.57 / -0.16, +3.30 / +1.35 |
| rms over all 78 | **0.73 / 0.63** | 1.56 / 0.88 |

No coefficient is fitted; every value is a direct console measurement. BAR's second scene and DK64 still come out 0.5 to 0.8 clk high.

### In the harness (games)
The table weighted by where the previous frame's time went gives the base value of a CPU transfer in the next frame (`T_CPUACT=2`; the additions for the place and the VI multipliers as series v23).
| games (harness) | series v23 | **loss per activity** | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 236 / 316 | 238 / **321** | 231 / 325 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ) | 7 / 371 / 42 (46) | **10 / 378 / 35** (53) | 9 / 385-391 / 25 |
| DK64, VI taken | +1.3% | **+0.8%** | - |
| MGC "@" frame / start of part 4 | +32 / +14 VI | +34 / +11 VI | VI 9385 / 5764 |

- DK64 (long frames 42 -> 35, VI +1.3% -> +0.8%) and BAR's seg1 (316 -> 321) improve, BAR's seg0 gets 2 frames worse (236 -> 238), the MGC demo does not move.
- A table of measured values alone does about as well as the earlier trials (regressions). As the MGC demo does not move, what is left of it (+0.4%) is not this term.
- **Not in the series yet.** The next step is the implementation in the core as patch 0029 (series v24: the table, the sums, the average at Sync_Full, the saved state). Patches 0001 to 0028 and 9999 are those of the last delivery; the implementation tried here is a knob in the harness work tree (off by default).

### Next ROM: rdp_timing v69 (`rdp_timing/v69`, `rdp_timing_v69.zip`)
The measurements for the model are complete for now. v69 runs the sequences at which v62 / v66 / v67 stopped on the console once more, with the timeout handler fixed and a marker saved before every load, to record why they stopped (RDP timeouts on the console, as it seems),
and measures what v66 did not reach (BAR 2316 and DK64 with everything / nothing written). The result file is `rdp_timing_v69_NNN.txt`.

## Console results of v69 (the stop reproduced), patch 0029 (series v24), and a correction to how DPC_CLOCK was read (added 2026-11-18, part 21)

Source: `hw/v69_ntsc_001.txt` (NTSC console, 8 MB, VI off; nine variants measured, then it stopped right after loading `bar_s1960 full`). Evaluated with `rdp_timing/v69/compare.py`.
Harness outputs are in `rdp_timing/replay_v24/` (series v24) and `rdp_timing/replay_v23/` (series v23).

### v69: the values of v67 again, and the stop at the same place
| list, variant | v67 (data fill / instruction fill clk, the list alone) | v69 |
|---|---|---|
| bar_2316 full | 10.15 / 6.56 (1404718) | 10.20 / 6.56 (1404994) |
| bar_2316 drop_noz | 10.18 / 6.48 (1300605) | 10.25 / 6.46 (1300821) |
| bar_2316 drop_z | 8.13 / 7.18 (397881) | 8.13 / 7.18 (397701) |
| dk64_d6373 full | 8.07 / 5.05 (2075301) | 8.12 / 5.05 (2075565) |
| dk64_d6373 drop_noz | 8.02 / 5.33 (1730157) | 8.02 / 5.34 (1729761) |
| dk64_d6373 drop_z | 6.32 / 4.75 (943377) | 6.31 / 4.76 (943269) |
| dk64_s6340 full | 7.96 / 4.96 (1617693) | 7.95 / 4.97 (1617770) |
| dk64_s6340 drop_noz | 7.75 / 5.30 (1272741) | 7.74 / 5.31 (1272624) |
| dk64_s6340 drop_z | 6.62 / 4.67 (839649) | 6.59 / 4.67 (839865) |

- **Repeatability**: over the two runs the losses agree within 0.07 clk and the lists' own times within 0.05%.
- **The stop is reproduced at the place of v67** (after `dk64_s6340 drop_z` was measured, right after `bar_s1960 full` was loaded and its `XI` line saved, before the first `KC` line was saved).
- **No timeout line was left.** The timeout handler fixed in v68 was now exercised in ares with a list cut short (the `# RDP TIMEOUT` line is written and the results are saved).
  So on the console the CPU never reaches the handler: this is not "the RDP hangs and the CPU gives up waiting", but **the CPU itself stopping** or **a CPU exception**.

The sequences that stopped and those that did not, from all files so far:
| previous list (variant) -> next list | result |
|---|---|
| `bar_s2120` (om_no_tlut: v62, z_near: v66) -> `bar_2316 full` | **stopped** (both times) |
| `dk64_s6340 drop_z` -> `bar_s1960 full` (v67, v69) | **stopped** (both times) |
| `dk64_s6340 no_load` -> `bar_s1960 full` (v62) | ran |
| `bar_s1960` (om_no_tlut / z_near / drop_z) -> `bar_s2120 full` (v62 / v66 / v68) | ran |
| `bar_s2120 drop_z` -> `bar_s1716 full` (v68) | ran |
| `bar_2316 drop_z` -> `dk64_d6373 full`, `dk64_d6373 drop_z` -> `dk64_s6340 full` (v67 / v69) | ran |
| the 28 changes of list of v61, v63 (13 variants), v65 (12 variants) | ran |

- The stop does not follow the number of lists run (the 7th / 10th / 25th) or the size of the output (8 to 14 KB at the stops, 14 to 21 KB in the runs that finished): it is reproduced by **particular pairs of lists**.
- ares does not stop, and a list's own time there does not depend on what ran before it (within 25 clk, the differences following the ROM version only).
- The cause is not known yet. The next ROM (v70) is to find the command at which it stops.

### Patch 0029: `n64: what a CPU cache line fill costs the RDP depends on what the RDP is doing` (series v24)
The model evaluated in part 20 as a harness knob (`T_CPUACT=2`) is now in the core.
- `RDP::timingSpan()`: a table of the RDP clocks lost per CPU data fill / instruction fill by kind of span (fill / copy / write-only / blended / depth compare and update, passing and failing) and width (the table of part 20: measured in v63 to v65 and v68).
  A depth-compared span mixes the passing and the failing value by 1 / overdraw of the last frame (v65 / v67).
- Texture loads 4.2 / 4.0 clk, everything else (triangle set-up, state commands) 4.6 / 4.4 clk (`cpuLoad` / `cpuRest` in `rdp.hpp`).
- `RDP::timingLoss()` sums each frame's time by kind; at Sync_Full the average becomes the base value of the CPU's transfers beside the next frame (`RDP::timingCpu()`).
  The additions for the place (0028) and the VI multipliers (0025) are unchanged. The other data transfers (write-back, uncached write) follow the data fill in proportion.
- The sums and the averages are in the save state (the version stays v154: it is raised once in the series).

Checked:
1. The core passes a syntax check of the translation units that include the changed files (rdp / cpu / rsp / system).
2. The harness (series v24, default) gives **exactly the values** of the trial of part 20 (`T_CPUACT=2`): the 174 J rows of rdp_timing v61 and every figure of the three games.
3. With `T_CPUACT=0` the harness gives **exactly the values** of series v23 (the 63 K rows of rdp_timing v68).
4. 0001 to 0029 applied with `git am` and 9999 with `git apply` to upstream a774671d give the core and harness trees. Patches 0001 to 0028 are those of series v23 except for the numbering in the subject (`NN/28` -> `NN/29`).

| games (harness) | series v23 | **series v24** | console |
|---|---|---|---|
| BAR intro, frames in seg0 / seg1 | 236 / 316 | 238 / **321** | 231 / 325 |
| DK64 frame deltas 2 / 3 / 4+ (frames that differ) | 7 / 371 / 42 (46) | **10 / 378 / 35** (53) | 9 / 385-391 / 25 |
| DK64, VI taken | +1.3% | **+0.8%** | - |
| MGC "@" frame / start of part 4 | +32 / +14 VI | +34 / +11 VI | VI 9385 / 5764 |

rdp_timing v61 (29 game lists) run in the harness (automatic depth pass rate; means by scene, the difference to the console in brackets):
| clocks lost per transfer | console | series v23 | **series v24** |
|---|---|---|---|
| data fill, MGC (9 lists) | 10.42 | 10.04 (-0.39) | 10.24 (-0.18) |
| data fill, BAR's first scene (8) | 9.62 | 10.06 (+0.44) | 9.74 (+0.12) |
| data fill, BAR's second scene (6) | 8.82 | 10.15 (+1.33) | 9.79 (+0.98) |
| data fill, DK64 (6) | 8.23 | 10.04 (+1.81) | 8.83 (+0.59) |
| data fill, rms over the 29 | - | 1.20 | **0.67** |
| random store, rms over the 29 | - | 1.12 | **0.77** |
| instruction fill, rms over the 29 | - | 0.76 | 0.93 |

- Data fills and stores improve. BAR's second scene stays +1.0 clk high (the remainder known from parts 18 to 20).
- **Instruction fills do not improve** (MGC +0.63, BAR's first scene +0.47, its second +1.14, DK64 +1.03). The reason turned out to be the way of measuring, not the model (next section).

### A correction to how DPC_CLOCK was read: the delay until the load loop notices the end of the list
With the loss of an instruction fill set to 0 in the harness, the ROM still reported 0.2 to 0.8 clk lost per instruction fill. The cause is how the ROM measures.
- DPC_CLOCK keeps counting after the RDP has finished the list, until the ROM reads it (on the console and in ares).
- The ROM's load loop looks at the DP interrupt only after each chunk of the load (1024 instruction fills, 256 data fills).
  So DPC_CLOCK of a run with a load contains **the delay until the end is noticed** (0 to 1 chunk: up to about 30000 clk for the instruction load, about 7000 for the data loads).
- **DPC_PIPEBUSY on the console stops when the RDP finishes** (without a load it reads 95 to 173 clk less than DPC_CLOCK). DPC_BUFBUSY keeps counting like DPC_CLOCK.
  The J rows of v61 hold DPC_PIPEBUSY, so the losses were read again from it.

| v61, console, means by scene (clk) | MGC | BAR first | BAR second | DK64 |
|---|---|---|---|---|
| data fill back to back: from DPC_CLOCK / from DPC_PIPEBUSY | 10.42 / 10.39 | 9.62 / 9.49 | 8.82 / 8.78 | 8.23 / 8.20 |
| data fill, one every ~250 cycles | 9.88 / 9.61 | 10.14 / 9.76 | 9.00 / 8.56 | 8.83 / 8.60 |
| data fill, one every ~1000 cycles | 11.26 / 9.68 | 11.54 / 9.75 | 9.58 / 8.58 | 9.20 / 8.68 |
| instruction fill back to back | 6.53 / 6.27 | 6.74 / 6.37 | 5.80 / 5.31 | 5.16 / 4.96 |
| random store back to back | 13.22 / 13.05 | 13.25 / 13.07 | 11.81 / 11.74 | 11.69 / 11.57 |

- **This is a flaw in the design of the ROM** (runs with a load were read from DPC_CLOCK). Every loss per CPU transfer so far contains this delay:
  0.03 to 0.13 clk for data fills back to back, **0.2 to 0.5 clk for instruction fills**, 0.2 to 1.8 clk for the sparse loads with their few operations.
- **"Sparse transfers cost more than back-to-back ones" (part 13) was mostly this delay.** Read from DPC_PIPEBUSY, one fill every ~1000 cycles costs 9.7 to 9.75 clk (it had looked like 11.3 to 11.5), within 0.7 clk of the back-to-back values.
- The table of patch 0029 holds values read from DPC_CLOCK. Its data-fill column is hardly affected; **its instruction-fill column is 0.2 to 0.5 clk high**.
- ares does not count DPC_PIPEBUSY (it reads 1). The ROM run in the harness contains ares' own delay (a fixed amount per run), so the comparison of instruction fills in the table above is uncertain by 0.5 clk.
- The series of synthetic lists (the M / N / P rows of v63 to v65) did not record DPC_PIPEBUSY. Reading the table again needs them measured again with DPC_PIPEBUSY (the synthetic lists run to their end on the console).

So series v24 is the model as evaluated in part 20, put into the core; the correction of the instruction-fill column (0.2 to 0.5 clk) is for the next series.
It needs (1) ares counting DPC_PIPEBUSY (while the RDP has work) and (2) the table measured again and read from DPC_PIPEBUSY. (1) needs no console measurement.

### Next ROM: rdp_timing v70 (`rdp_timing/v70`, `rdp_timing_v70.zip`, a diagnostic)
It is to find the command at which the console stops. It takes no new measurements.
- After the nine variants of v69, **the list that stopped (`bar_s1960 full`) is fed to the RDP 2 KiB at a time, with a line saved before every step**. If it stops, the last line names the step.
- After a stop, switching the console off and on and **running the same ROM again** makes it read the previous file from the SD card and feed that step **one command at a time** (the command words are recorded).
- A CPU exception is written to the file (`# CPU EXCEPTION: ... EPC ... BadVAddr ...`) before libdragon's exception screen (checked in ares).
- Then the same sequence in one go (with marker lines), and the sequence at which v62 / v66 stopped (`bar_s2120 z_near` -> `bar_2316 full`) in the same way.
- The result files are `rdp_timing_v70_NNN.txt` (`_001` for the first run, `_002` for the second). **What the screen shows when it stops** (black / the exception screen / the progress line) is needed as well.

## Console results of v70 / v71: the stops came from the padding the ROM put after each list's Sync_Full (added 2026-11-19, part 22; no change to the model)

Source: `hw/v70_ntsc_001.txt`, `hw/v70_ntsc_002.txt` (v70 run twice), `hw/v71_ntsc_001.txt` (NTSC console, 8 MB, VI off). Evaluated with `rdp_timing/v71/compare.py` and `tools/v61_analysis/tail.py`.

### v70: the stop is at the start of a new list, and not every time
| | first run | second run |
|---|---|---|
| `bar_s1960` fed 2 KiB at a time (after `dk64_s6340 drop_z`) | all 108 steps ran | stopped in the first step (nothing recorded) |
| `dk64_s6340 drop_z` -> `bar_s1960 full` (in one go) | ran | not reached |
| `bar_2316` fed 2 KiB at a time (after `bar_s2120 z_near`) | the RDP stopped in the first step: DPC_STATUS 160, DPC_CURRENT = start + 20 words; the CPU kept running | not reached |

- The first 22 words of both lists are state commands only (Set_Other_Modes, Set_Scissor, Set_Combine, Set_Fill_Color, Set_*_Image, Set_Tile, Set_Tile_Size): the cause is not in the list.
- DPC_STATUS read 140 (DMA busy) after every list fed in one go, and 040 after a list fed 2 KiB at a time.

### v71, series T: the RDP leaves words after a Sync_Full unread and reads them when the next list is started
List A (fills, Sync_Full, then n No_Op words) was run; after its DP interrupt the n words were overwritten with commands that fill one marker pixel each, and list B was started.
| body of A | n | DPC_STATUS at the interrupt | words after the Sync_Full already read | markers set after B |
|---|---|---|---|---|
| 1 fill | 0 | 080 | - | 0 |
| | 8 | 0C0 | 8 (all) | 0 |
| | 24 / 54 / 106 / 127 | 140 | 23 | 1 / 31 / 83 / 104 (from the 23rd to the last) |
| 401 fills | 0 / 8 | 080 / 0C0 | all | 0 |
| | 24 | 040 | 24 (all) | 0 |
| | 54 / 106 / 127 | 140 | 29 | 25 / 77 / 98 (from the 29th to the last) |

- **The RDP stops reading 23 to 29 words after the Sync_Full.** The rest stays pending with DMA busy (DPC_STATUS 140): DPC_CURRENT does not move after 1 ms or 100 ms, and the rewritten markers are not executed by waiting 50 ms.
- **When the next list is started (DPC_START / DPC_END written), the remaining words are read from RDRAM as it is then, and executed.** It is the same with B at another address and at A's address. The words that had been read keep what they held when read (No_Op).
- With nothing after the Sync_Full, DPC_STATUS is 080 and DPC_CURRENT = END at the interrupt.

### v71, series K: without the padding everything runs; with it the stop is back
- The 13 variants with every list ending at its Sync_Full (`dk64_s6340 drop_z` -> `bar_s1960` x 3 -> `bar_s2120` x 3 -> `bar_2316` x 3 -> `dk64_d6373` -> `dk64_s6340` x 2, including both pairs that had stopped) **all ran**. Before each list's first run: DPC_STATUS 080, DPC_CURRENT = the end of the list before.
- The pair with only the padding put back (`bar_s2120 z_near` -> `bar_2316 full`) **stopped as before**: DPC_STATUS 140 and DPC_CURRENT = Sync_Full + 28 words before the start (26 words left unread), then `# RDP TIMEOUT: DP_STATUS 0x160 ... CURRENT 7000A0`.
- Once the lists are exchanged, those 26 positions hold triangle coefficients from the middle of `bar_2316`. Read as commands: a Set_Other_Modes (cycle type = copy), a Set_Color_Image (an arbitrary address), a shaded triangle (0x0C), No_Op. The RDP stopped there.

**Conclusion**: the stops of v62 / v66 / v67 / v69 / v70 came from my ROM **putting 10 to 127 No_Op words after the Sync_Full** of every replayed list, to round it to 1 KiB (padding introduced in v41 for the XBUS variants).
While the same list is repeated the unread words are its own No_Op and do no harm; when a longer list replaces it, the unread positions hold the middle of the new list, which is executed as arbitrary commands
(the RDP alone stops, or a draw into an arbitrary colour image takes the CPU down: both were seen). Neither the games' lists nor the ares model are at fault.
- The values of the runs that finished stand (the first 22 words of a list set the state again before every run and the depth buffer is restored; the same list gives the same values within 0.1% across ROM versions and orders).
- ares executes a range to its end when DPC_END is written, so it does not behave like this. The games' microcodes end their lists with Sync_Full, so the model is unchanged (recorded as console behaviour).
- Added to the rules for test ROMs: **no command words after a list's Sync_Full**.

### What v69 could not measure (back to back; data fill / instruction fill clk, the list alone in brackets)
| list | full | om_no_zcmp (every pixel written) | z_near (every test fails) |
|---|---|---|---|
| bar_s1960 | 8.90 / 5.66 (1361281) | 8.78 / 5.49 (1195213) | 6.73 / 4.20 (1162237) |
| bar_s2120 | 8.79 / 5.88 (1210102) | 8.50 / 5.51 (1051657) | 6.57 / 4.63 (1026229) |
| bar_2316 | 10.20 / 6.56 (1404817) | 9.60 / 6.16 (1141619) | 7.81 / 5.37 (1119493) |
| dk64_d6373 | 8.07 / 5.06 (v70) | 8.21 / 5.08 (1693030) | - (v65: 5.74 / 3.51) |
| dk64_s6340 | 7.96 / 4.97 (v70) | 7.79 / 4.82 (1388509) | 5.65 / 3.50 (1363909) |

- DK64 loses about as much with every pixel written as it does as it is (8.2 against 8.1, 7.8 against 8.0).
- **(Corrected 2026-11-19) The reading below was wrong: see "Correction" in part 23.**
- So **the reading of part 19, "DK64 behaves as if 0.56 of its depth-compared part passed", is not supported.** Patch 0029 takes 1 / overdraw (0.57 for DK64) as the passing share and gets DK64's value about right (+0.6 clk), but failing spans are not why DK64 is low.
  Why DK64 loses less during passing spans than the synthetic lists do (10 to 12 clk) is not known; it is taken up after the table has been read again from the next measurement (v72).

### Next ROM: rdp_timing v72 (`rdp_timing/v72`, `rdp_timing_v72.zip`)
The table of losses per kind of work, measured again without the flaw of part 21 (the delay in DPC_CLOCK): the series of synthetic lists of v63 to v65 (M / N / P) as they were, with DPC_PIPEBUSY and the loss read from it in every row.
Of the three runs the one with the smallest DPC_PIPEBUSY is kept. No game data is used. The result file is `rdp_timing_v72_NNN.txt`.

## Console results of v72 / v73 and patch 0030 (series v25): the loss table read again from DPC_PIPEBUSY, spans blended without depth measured (added 2026-11-19, part 23)

Source: `hw/v72_ntsc_001.txt` (the series of synthetic lists M / N / P measured again), `hw/v73_ntsc_001.txt` (series Q); NTSC console, 8 MB, VI off, both ran to the end.
Evaluated with `rdp_timing/v72/compare.py`, `rdp_timing/v73/compare.py`, `tools/v61_analysis/activity3.py` to `activity5.py`. Harness outputs are in `rdp_timing/replay_v25/`.

### v72: the table measured again (with DPC_PIPEBUSY)
- Read from DPC_CLOCK the values repeat those of v63 to v65 within 0.05 clk (55 cases). The exception is back-to-back data fills beside wide spans that nearly all fail the depth test (64 / 128 / 256 pixels: 6.0 / 5.7 / 4.0 now, 6.3 / 6.9 / 6.5 in v64; the sparse loads agree in both runs).
- Read from DPC_PIPEBUSY a back-to-back data fill costs 0.03 to 0.1 clk less and an instruction fill 0.02 to 0.7 less (1.6 beside 4-pixel write-only spans).
- **Beside spans of 128 pixels and more, what a transfer costs depends strongly on how closely the transfers follow each other** (the same in both measurements).

| spans of 256 pixels (data fill, clk) | back to back | one every ~125 cycles | 250 | 500 |
|---|---|---|---|---|
| depth compare + update, passing | 11.5 | 13.8 | 13.8 | 12.9 |
| depth compare + update, nearly all failing | 4.0 | 6.9 | 3.6 | 3.6 |
| write-only (textured) | 14.1 | 5.3 | 6.8 | 5.2 |
| blended without depth, 2-cycle (v73) | 9.1 | 3.1 | 2.4 | 2.4 |
| blended without depth, 1-cycle (v73) | 8.9 | 11.4 | 9.1 | 7.4 |

### A mix-up in the table, and v73
The table's row for spans that blend with the colour buffer without the depth buffer held the values of `no_zcmp`; but `no_zcmp` (and `1cyc_no_zcmp`) only switch the depth compare off and **still update the depth buffer**: another kind of span (my mix-up).
v73 measured the kind in question (back to back; data fill / instruction fill clk).
| | 4 px | 16 | 64 | 256 |
|---|---|---|---|---|
| 2-cycle, textured | 9.7 / 4.5 | 10.5 / 6.2 | 9.0 / 4.0 | 9.1 / 1.7 |
| 1-cycle, textured | 9.6 / 4.4 | 10.4 / 6.1 | 10.3 / 4.9 | 8.9 / 3.2 |
| no texture / shaded (2-cycle) | - | - | 9.0 / 3.9 | 9.0 / 1.6 |
| DK64's two modes (1-cycle) | - | - | 10.3 / 5.0 | 8.9 / 3.2 |
| texture rectangles 320 pixels wide | 2-cycle 8.8 / 1.3 | 1-cycle 8.8 / 3.0 | write-only 13.6 / 2.7 | |

- Lower than the old row (10.3 to 11.6 / 5.9 to 7.6); the instruction fill falls a lot for wide spans. Texture makes no difference; the cycle type does.
- In the games: 13 to 17% of a DK64 frame (a few large triangles), 6% of BAR's second scene (a texture rectangle 320 pixels wide), 55% of part 2 of the MGC demo (2 to 32 pixels).

### Patch 0030: `n64: the losses per CPU cache line fill read from DPC_PIPEBUSY; spans blended without depth measured`
- The table in `timingSpan` holds the DPC_PIPEBUSY values (the widths v72 did not measure again keep v60's). Loads 3.8 / 3.55, everything else 4.25 / 4.2, fills 12.1 / 11.7, copy 6.0 / 4.3.
- The row for blending without depth holds v73's values (alike up to 32 pixels, by cycle type from 33 on, wide texture rectangles apart).
- For wide spans the back-to-back values are used, as before (write-only spans beyond 64 pixels keep the sparse value, as in 0029). The dependence on the spacing of the transfers is not in the model.
- The share of depth-compared spans taken to pass stays 1 / overdraw of the last frame. **That share is not measured** (see the correction below): it is the choice with which the game lists agree best.

Prediction against the console over the 78 lists (the console read from DPC_PIPEBUSY too; nothing fitted): rms **0.62 / 0.45 clk** (the table of 0029: 0.70 / 0.70; the constants: 1.61 / 1.04). DK64 +0.02 / +0.10, BAR's first scene -0.03 / 0.00, its second +0.51 / +0.35, MGC -0.48 / -0.25.

| games (harness) | series v24 | **series v25** | console |
|---|---|---|---|
| MGC "@" frame / start of part 4 | +34 / +11 VI | **+19 / -1 VI** | VI 9385 / 5764 |
| MGC, VI taken by parts 1 to 5 | 0.0 / +0.3 / +0.4 / +0.2 / +1.5% | -0.1 / +0.2 / +0.1 / +0.1 / +1.5% | - |
| DK64 frame deltas 2 / 3 / 4+, VI taken | 10 / 378 / 35, +0.8% | 10 / 376 / 34, +0.7% | 9 / 386-389 / 25 |
| BAR intro, frames in seg0 / seg1 | 238 / 321 | 239 / **323** | 231 / 325 |

| rdp_timing v61 in the harness (rms over its 29 lists, clk) | series v23 | series v24 | **series v25** |
|---|---|---|---|
| data fill | 1.20 | 0.67 | **0.55** |
| instruction fill | 0.76 | 0.93 | **0.48** |
| random store | 1.12 | 0.77 | 0.83 |

- The MGC demo improves a lot (parts 1 to 4 within 0.2%). BAR's seg1 is 323 / 325, DK64 a little better, BAR's seg0 one frame worse (239 / 231).
- DK64 is compared over the 420 frames both logs hold this time (423 for series v24).
- Checked: syntax of the core; 0001 to 0030 with `git am` and 9999 with `git apply` on upstream a774671d give the core and harness trees. 0001 to 0029 are those of series v24 except for the numbering in the subject (`NN/30`).

### Correction: the reading about the depth pass rate (part 22)
Part 22 said "DK64 loses as much with every pixel written, so most of its depth-compared spans pass; the 0.56 of part 19 is not supported". **That reading was wrong.**
- With `om_no_zcmp` (depth compare off) the spans drawn are of another kind, updating the depth buffer without comparing; they cost 10.4 to 11.8 clk in the synthetic lists, which cannot be told from passing spans. It is no measurement of the pass rate.
- `z_far` (the depth buffer preset to the farthest value) keeps the failures of the list's own overdraw, so it has to equal the list as it is.
- So the share of a game list's spans that pass is not measured directly. 1 / overdraw is used because the 78 lists agree clearly better with it than with a share of 1 (rms 0.62 / 0.45 against 0.90 / 0.62).

### What is left
- BAR's second scene +0.5 clk (data fill), MGC -0.5 clk, BAR's seg0 +8 frames (-3.6%), part 5 of the MGC demo +1.5%.
- The dependence of the loss beside wide spans on the spacing of the transfers (table above): the games' transfers are not back to back, so an error remains there.
- ares does not count DPC_PIPEBUSY: when a ROM is run in the harness, the ares side alone is read from DPC_CLOCK.

## Console results of v74, task_timing v25 / v26 and logger v5; patches 0031 / 0032 (series v26): the VI uses the bus only while it reads the picture (added 2026-11-19, part 24)

Sources: `hw/v74_ntsc_001.txt`, `hw/task_timing_v25_ntsc_001.txt`, `hw/task_timing_v26_ntsc_001.txt`, `hw/bar_tasklog_hw_007_v5_normal.srm`, `hw/bar_tasklog_hw_008_v5_idle.srm`
(all NTSC console, 8 MB) and the earlier `hw/bar_tasklog_hw_005_normal` / `hw_006_idle`. Evaluated with `rdp_timing/v74/compare.py`, `rdp_timing/task_timing_v26/compare.py`,
`bar_tasklog/idle_cmp.py`, `bar_tasklog/counters_cmp.py`, `tools/tt26_blank.py`. Harness output in `rdp_timing/replay_v26/`.

The subject is what series v25 still missed in the first scene of BAR's intro (seg0: ares 239 frames, console 231).

### 1. v74: the "place" of a CPU transfer is a megabyte, not an RDRAM chip
A test of the idea that the place terms of patch 0028 act per RDRAM chip (2 MiB). Colour buffer 0x300000, depth buffer 0x400000, commands 0x700000; the CPU load in each megabyte from 1 to 7
(VI off, losses from DPC_PIPEBUSY, clk per transfer, data fill / instruction fill).

| list | 1 (nothing) | 2 (beside colour) | 3 (colour) | 4 (depth) | 5 (beside depth) | 6 (beside list) | 7 (list) |
|---|---|---|---|---|---|---|---|
| passing spans, 16 px | 11.04 / 6.78 | 11.05 / 6.78 | 14.03 / 12.59 | 12.82 / 11.67 | 11.05 / 6.77 | 11.04 / 6.78 | 11.36 / 7.38 |
| passing spans, 64 px | 10.41 / 6.51 | 10.41 / 6.48 | 14.41 / 13.19 | 13.07 / 12.79 | 10.41 / 6.48 | 10.42 / 6.48 | 10.55 / 6.75 |
| the same without IMAGE_READ, 16 px | 10.88 / 6.55 | 10.89 / 6.55 | 14.02 / 11.66 | 13.19 / 12.50 | 10.89 / 6.54 | 10.92 / 6.54 | 11.35 / 7.31 |
| fill 320x240 | 12.07 / 12.07 | 12.07 / 12.07 | 17.04 / 19.01 | 12.07 / 12.07 | 12.07 / 12.07 | 12.07 / 12.07 | 12.07 / 12.07 |
| Load_Block 2048 B | 3.91 / 3.70 | 3.91 / 3.70 | 3.93 / 3.70 | 3.92 / 3.70 | 3.93 / 3.70 | 3.92 / 3.70 | 3.94 / 3.71 |

- The other megabyte of a chip (2, 5, 6) equals the megabyte nothing uses (1) within 0.04 clk. **The idea was wrong; the megabyte test of 0028 is right.**
- A place term is there only while the RDP reads or writes that buffer. Colour megabyte: spans +3.0 to 4.0 / +5.1 to 6.7, fill +5.0 / +6.9, loads 0. Depth megabyte: passing spans +1.8 to 2.7 / +4.9 to 6.3, fills and loads 0.
  The model (0028) adds +2.4 / +5.8 (colour) and +1.5 / +3.4 (depth) throughout the frame, during loads too. In BAR this is about 0.001 VI per frame; not changed here (remaining issue).

### 2. The earlier logs read again: what is missing is not CPU contention but about 0.03 VI per task that only the game has
- The content of a frame is fixed by its number (in ares the RDP work of every frame is identical in the normal and the CPU-waiting log), so console and ares, normal and waiting can be compared frame by frame.
- "Normal minus waiting" (the CPU's part in the game), ares over console per 20 frames: 1.02 / 0.90 / 1.13 / 1.02 / 1.04 / 0.97 / 0.87 / 0.91 (series v25). **The CPU contention was right.**
- With the CPU waiting, go to RSP done in series v25: -1.6 / **-4.8** / -2.4 / -2.8 / -0.6 / +0.1 / +0.9 / +2.1%. The shortfall is there with the CPU stopped.
- The same tasks replayed (task_timing) agree. Tasks were matched to frames by their RDP work (s1716 = frame 10, s1727 = 21, s1745 = 39, s1772 = 66).

| task | replayed (the game's VI mode) console / v25 | in the game, CPU waiting console / v25 | game minus replay console / v25 |
|---|---|---|---|
| s1716 | 0.659 / 0.670 | 0.689 / 0.675 | +0.030 / +0.005 |
| s1727 | 0.562 / 0.561 | 0.587 / 0.564 | +0.025 / +0.003 |
| s1745 | 0.796 / 0.794 | 0.826 / 0.797 | +0.030 / +0.003 |
| s1772 | 1.559 / 1.546 | 1.599 / 1.551 | +0.040 / +0.005 |

(Go to RSP done, VI. Console replay values from task_timing v26.)

### 3. task_timing v25 / v26: what the VI shows, and the AI
The same tasks replayed with only the VI's origin and the AI changed (8 KiB FIFO, the CPU polling). In v25 the VI had no effect from the 27th condition on (cause unknown; that ROM switched the AI's DMA off in mid-play),
leaving nine valid conditions. v26 runs the AI conditions last and lets the AI play out first; all 66 conditions are valid. Increase over `game` (the VI showing the ROM's own picture, in a megabyte the task does not touch), go to RSP done, VI:

| condition | s1716 | s1727 | s1745 | s1767 | s1772 | s1905 | series v25 |
|---|---|---|---|---|---|---|---|
| `game_own` (what the game showed) | +0.0041 | +0.0021 | +0.0037 | +0.0020 | +0.0051 | +0.0010 | +0.0004 to 0.0020 |
| `game_colour` (the buffer being drawn) | +0.0172 | +0.0163 | +0.0264 | +0.0457 | +0.0451 | +0.0270 | +0.001 to 0.002 |
| `game_z` (the depth buffer) | +0.0174 | +0.0134 | +0.0223 | +0.0356 | +0.0388 | +0.0255 | +0.001 |
| `game_mb5` (an empty megabyte) | -0.0013 | +0.0002 | +0.0002 | -0.0002 | -0.0017 | +0.0001 | 0.0000 |
| `game_mb0` (the megabyte of the output FIFO) | +0.0041 | +0.0028 | +0.0042 | +0.0065 | +0.0089 | +0.0014 | +0.003 to 0.008 |
| `game+ai` | +0.0066 | +0.0036 | +0.0045 | +0.0073 | +0.0076 | +0.0037 | +0.0008 to 0.0016 |
| `vi_off+ai` (over `vi_off`) | +0.0026 | +0.0026 | +0.0032 | +0.0060 | +0.0062 | +0.0022 | +0.0009 to 0.0022 |

- Showing what the game showed adds only 0.001 to 0.005 VI, nearly all of it in DPC_TMEM (texture load time +3.4%). Of the 115 texture images a BAR frame sets, 113 are in the megabytes of the two frame buffers.
- The AI (22047 Hz and the buffer addresses of the game) adds 0.004 to 0.008 VI, three to five times the model's (5.4 clk per sample from v54). v54 handed the list to the RDP whole; beside a task with the RSP running the loss is larger.
- Together 0.005 to 0.013 VI: a fifth to two fifths of the game's +0.025 to 0.040 VI.
- The VI showing the buffer being drawn or the depth buffer: +0.013 to 0.046 VI (the +2.4 to 2.9% of v53; not in the model; an arrangement BAR and MGC do not have).
- The console's DPC counters beside a real task: 0x14 (BUFBUSY) counts while commands are waiting or in work; 0x18 (PIPEBUSY) runs with DPC_CLOCK from the first DP_END write on.

### 4. Logger v5: the RDP's counters in the game
The resident logger now records DPC_CLOCK / BUFBUSY / PIPEBUSY / TMEM at every DP interrupt (`bar_tasklog/hook.S`, formats 5 / 6). The waiting log repeats the earlier one (hw_006) within 0.002 VI.

| task | replay `game_own+ai`: RSP done / BUFBUSY / TMEM | in the game, CPU waiting | difference |
|---|---|---|---|
| s1716 | 0.670 / 753623 / 132243 | 0.688 / 791395 / 135501 | +0.018 / +37772 / +3258 |
| s1727 | 0.567 / 644796 / 119813 | 0.588 / 686821 / 124295 | +0.021 / +42025 / +4482 |
| s1745 | 0.804 / 889442 / 144523 | 0.825 / 931604 / 149163 | +0.021 / +42162 / +4640 |
| s1772 | 1.572 / 1686895 / 151083 | 1.601 / 1735799 / 156601 | +0.029 / +48904 / +5518 |

In the game the RDP's own working time is longer (BUFBUSY +38k to 49k clk; the part after RSP done is the CPU starting up again) and the texture loads take 2.5 to 3.7% longer. The difference hardly depends on the length of the task.

### 5. The cause: during vertical blanking the VI does not read RDRAM
- The replay ROM restarts the VI and starts the task a fixed time later, so every run starts on the same line (recorded by v26: VI_V_CURRENT 330 of 525, 1 ms before the start).
  The picture is half-lines 37 to 511, so the VI is outside it from 0.339 to 0.436 VI into a run and again one VI later. The replayed tasks spent 12 to 17% of their time in the blanking.
- The game starts its task 0.08 VI after the VI interrupt, where the picture begins. A task shorter than 0.89 VI never meets a blanking.
- From v26's progress traces (bytes the RDP has fetched, every 1/50 VI), the RDP's speed relative to the run with the VI off (`tools/tt26_blank.py`):

| task | before the blanking | during | after |
|---|---|---|---|
| s1767 (first / second blanking) | 0.894 / 0.853 | **1.007 / 0.990** | 0.858 / 0.892 |
| s1772 (first / second blanking) | 0.891 / 0.863 | **0.964 / 0.993** | 0.907 / 0.870 |

  Only during the blanking does the RDP run as fast as with the VI off.
- The model's VI factors are averages over whole lists or tasks ("VI on over VI off") and were applied at every moment alike. For a short task in the game that never meets a blanking the VI's part was too small.

### 6. Patch 0031: `n64: the VI loads the bus only while it reads the picture`
- `VI::picture()`: the VI is on a line of the picture (VI_V_VIDEO).
- Spans and loads are priced as if the VI read the picture all the time; what the VI adds is kept apart (`timing.pendingVi`) and is not spent while the VI is outside the picture (`timingStep`).
- The VI scale of the transfer losses (`contendScale`), the megabyte-of-the-VI terms (CPU, SP DMA), the SP DMA's own duration and the CPU's VI stall apply inside the picture only.
- Inside the picture every VI part is 1.16 times what it was (`timing.viPicture`). With that the replays of task_timing v26 fit as before (`game` rows +0.10% on average; series v25 +0.11%).
  It is 1.16 and not the nominal 525 / 474 = 1.11 because the old factors had been fitted to replays that spend 12 to 17% of their time in the blanking.

### 7. Patch 0032: `n64: the AI beside a running graphics task; texture loads from the megabyte the VI shows`
- With the AI's buffer in the megabyte of the command list and the RSP running, a sample costs 14 clk (scaled by the VI as an SP DMA is). Over v26's six tasks: VI off +0.0023 to 0.0059 (console +0.0022 to 0.0062), the game's VI mode +0.0029 to 0.0074 (+0.0036 to 0.0076).
- A texture load whose source is in the megabyte the VI shows takes 5% longer inside the picture (AA modes 0 / 1). `game_own` minus `game`: +0.0010 to 0.0052 (console +0.0010 to 0.0051).

### 8. Results (series v26)
| | series v25 | 0031 alone | **series v26** | console |
|---|---|---|---|---|
| BAR intro seg0 / seg1 frames | 239 / 323 | 232 / 319 | **230 / 317** | 231 / 325 |
| DK64 DKTV demo, deltas 2 / 3 / 4+ | 10 / 376 / 34 | 11 / 378 / 33 | 11 / 378 / 33 | 9 / 388 / 25 |
| MGC demo parts 1 to 5 | -0.1 / +0.2 / +0.1 / +0.1 / +1.5% | -0.1 / +0.2 / +0.1 / +0.1 / +1.3% | the same | — |
| MGC "@" / start of part 4 | +19 / -1 VI | +20 / 0 VI | +20 / 0 VI | VI 9385 / 5764 |

- BAR seg0, go to RSP done (ares minus console, per 20 frames): +0.1 / -3.4 / +1.2 / -1.7 / +0.9 / +1.2 / +0.6 / +1.2 / -1.3 / -0.9 / +0.1% (series v25: -1.0 / -6.2 / -0.3 / -2.8 / +0.1 / +0.1 / -2.6 / +0.2 / -1.8 / -2.6 / -4.7%).
  Frames per 60 VI: 39 / 29 / 26 / 27 / 24 / 25 / 33 / 27 (console 39 / 28 / 27 / 26 / 24 / 25 / 33 / 29).
- CPU waiting: -1.6 / -4.8 / -2.4 / -2.8 / -0.6 / +0.1 / +0.9 / +2.1% becomes +0.6 / -2.8 / -1.0 / -1.8 / +0.5 / +1.4 / +2.1 / +3.1%. "Game minus replay": console +0.025 to 0.040 VI, model +0.017 to 0.024 VI (series v25: +0.003 to 0.005).
- The replays of task_timing v26 are within -0.8 to +1.7% of the console in every condition but `game_colour` / `game_z` (the six tasks' own differences).
- **BAR seg1 got worse (323 to 317; console 325).** That scene has something that puts ares on the slow side (a CPU transfer costs more than on the console: "BAR's second scene +0.5 clk" in part 23),
  which in series v25 was cancelling against the missing VI part.
- Checked: syntax of the core; 0001 to 0032 with `git am` and 9999 with `git apply` on upstream a774671d give the core and harness trees. 0001 to 0030 are those of series v25 except for the numbering in the subject.
  With `T_VIPICM=0 T_AITASK=0 T_LOADVI=0` the harness gives series v25's values (every T row of task_timing v26 equal). Replays with the VI off (the 12 MGC lists, v61 to v74) are unaffected (not run again).

### 9. What remains
- The rest of "game minus replay" (console +0.025 to 0.040, model +0.017 to 0.024 VI); frames 20 to 39 at -3.4%.
- ares is slow in BAR seg1 (317 against 325) and in DK64 (33 frames of 4 VI or more against 25). MGC part 5 +1.3%.
- Place terms by what the RDP is doing (v74). The VI showing the buffer being drawn or the depth buffer (+2 to 3% in v26). The share of pixels passing the depth test is still not measured directly.
- Why the VI stopped in v25 is not known (v26 did not repeat it). In v25 / v26 the progress picture (green bars) was not visible on the console although the measurement ran to the end; to be fixed in the next ROM.

## Changes (patches 0001 to 0032, harness 9999)

(`0001` is an independent bug fix: in `ares/n64/rdp/io.cpp` `RDP::readWord`, the DPC_TMEM_BUSY check was written as `if(data == 7)`, a typo that made the
TMEM busy counter unreadable; it is corrected to `if(address == 7)`.)

Files changed by `0002` (the RDP model): `ares/n64/rdp/timing.cpp` (new), `ares/n64/rdp/timing-model.hpp` (new, the console-measured table),
`rdp.hpp`, `rdp.cpp`, `render.cpp`, `io.cpp`, `serialization.cpp`, `vulkan/vulkan.cpp`, `system/serialization.cpp` (state version v154),
`n64/CMakeLists.txt`. Files changed by `0003` (CPU stalls): `ares/n64/cpu/cpu.hpp`, `cpu.cpp`, `dcache.cpp`, `recompiler.cpp`.
All of them have been confirmed to apply as-is to upstream master (a774671d, as of 2026-10-26) (0001 to 0032 with `git am`, 9999 with `git apply`; the resulting trees equal the core and harness work trees).
`0012` is the model from the v24/v25/v28 sections (Z makes the whole span fail only for spans of 16 px or less, ATOMIC_PRIM at 20 clk/primitive, 1-cycle AA at 3.5 clk/span,
and `zFailWidth` / `atomicCost` / `aaSpanCost` in `rdp.hpp`), `0013` is the model of the MGC 2011 section (write-only span / triangle costs), `0014` the model of the v33-v35 section (colour/depth in the same megabyte, Load_Tile source stride), `0015` the model of the v36 / MGC-end sections (span width = union of the four sub-scanlines), `0016` the model of the v40 section (the VI slows read-modify-write spans only), `0017` its revert (v40 had been measured with the VI stopped; 0016 and 0017 applied together give series v13), and `9999` is the verification harness (a fixed number, so it does not change from series to series).
`0007` is the PI timing fix that gets past the MGC 2011 demo's emulator detection (`n64: make cartridge ROM ignore PI accesses faster than a mask ROM can serve`, in `cartridge.hpp` and `pi/device.hpp`), `0008`/`0009` are the models from the v17 to v19 sections above, `0010` is the v20 section's, and `0011` is the v20b section's.

1. **RDP execution time model** (`timing.cpp` + `timing-model.hpp`)
   - On both the parallel-rdp path and the software path, it parses the same command stream that is passed to the renderer.
   - The cycle count per command uses the table from the **console-measured model (rdp_timing v3 to v6, NTSC/PAL, read via `DPC_CLOCK`)**
     as is (`rdp_timing_model.h` is bundled as `timing-model.hpp`):
     - Triangles / rectangles: `prim_overhead(mode, max width)` (plus +3 for shaded/textured triangles) and, per scanline,
       `span(mode, RDRAM address of the first pixel, width)`. A triangle's spans are computed per row from the edge factors (with scissor applied),
       and the address is computed from the Set_Color_Image address, width and depth (the phase within a 64-byte block matters).
     - Mode: one of the table's 26 mode rows is selected from cycle type x color depth x imageRead x zCompare/zUpdate.
       For 4/8bpp color buffers and 32bpp copies there are no measurements, so the 16bpp table is reused with a byte-count conversion.
     - Z test: the table has two series, "all pixels pass" and "all pixels fail", so it is interpolated by pass rate.
       The pass rate is estimated as H(n)/n from the **previous frame's overdraw** (Z-tested pixel count / scissor area = n)
       (the sum of the probability 1/k that the k-th layer passes when n layers are drawn in random order) (1 layer = 1.0, 2 layers = 0.75, 3 layers ≈ 0.6).
       From 0012 on, this estimate is applied **only to spans of 16 px or less** (on the console a partial per-pixel Z fail saves nothing,
       and it is cheaper only when the whole span is hidden: v24/v28).
       Compare-only / update-only scale the 16bpp 1-cycle measured row by the zpass ratio at the same width.
     - Load_Block / Load_Tile (8B word count and 64B block count from each row's RDRAM address) / Load_TLUT,
       Sync_Pipe/Tile/Load 50/33/25, other Set commands 2.62 (1.73 for lists from DMEM),
       and a base list cost of 93 per Sync_Full.
   - The table is measured with "stacked drawing, CPU/RSP almost stopped", so three corrections measured on the console are applied (previous section):
     spans that read color/Z get `spanPenalty`(40) added and are multiplied by `contentionZ`(1.13), and during VI scanout are further multiplied by `viFactor`(1.07);
     write-only spans and TMEM loads are multiplied by `contentionPlain`(1.19).
2. **The RDP thread consumes work**: `RDP::main()` consumes pending work in step with the clock (stopped while FREEZE is set).
   Sync_Full is queued, and when the work up to that point has completed it raises the DP interrupt and clears the busy flag.
3. Added state to the save state and updated the version to v154.
4. **CPU stalls** (`0003`): while the RDP model has unconsumed work and FREEZE is not set, +10 cycles are added to CPU icache fills,
   +4 to dcache fills, and +12 to dcache writebacks (`CPU::rdpBusyStall`). A call to `CPU::icacheStall()` was added to the
   icache fill path that the recompiler inlines. If the RDP model is disabled, the CPU side also behaves as before.

### Applying and building

```sh
git clone https://github.com/ares-emulator/ares.git && cd ares
git am 0001-n64-fix-DPC_TMEM_BUSY-register-read.patch        # io.cpp readWord: if(data == 7) → if(address == 7)
git am 0002-n64-model-RDP-execution-time-so-frame-pacing-matches.patch
git am 0003-n64-stall-CPU-cache-line-transfers-while-the-RDP-is-.patch
git am 0004-n64-RDP-timing-VI-mode-factors-per-transfer-bus-cont.patch
git am 0005-n64-CPU-instruction-timing-from-hardware-cpu_timing-.patch
git am 0006-n64-SP-DMA-timing-from-hardware-rsp_timing-v16.patch
git am 0007-n64-make-cartridge-ROM-ignore-PI-accesses-faster-tha.patch   # MGC 2011 emulator detection (PI timing)
git am 0008-n64-CPU-write-buffer-FPU-operand-costs-and-cache-op-.patch   # v18/v17
git am 0009-n64-SP-DMA-slowed-by-the-VI-and-by-CPU-line-transfer.patch   # v19/v19b
git am 0010-n64-DPC_CURRENT-follows-the-modelled-RDP-progress-so.patch   # v20: RSP waits for the RDP on the FIFO
git am 0011-n64-SP-DMA-and-CPU-bus-contention-against-the-RDP-re.patch   # v20b: re-measurement of the time SP DMA / CPU fills take from the RDP
git am 0012-n64-depth-test-savings-only-for-short-spans-ATOMIC_P.patch   # v24/v25/v28: depth-test savings only for spans up to 16 px, ATOMIC_PRIM, 1-cycle AA
git am 0013-n64-write-only-spans-and-triangles-cost-more-on-the-.patch   # v29-v32: write-only span / triangle costs (MGC 2011)
git am 0014-n64-depth-buffer-in-the-colour-buffer-s-megabyte-Loa.patch   # v34/v35: colour/depth in the same megabyte, Load_Tile source stride
git am 0015-n64-triangle-spans-cover-the-union-of-the-four-sub-s.patch   # v36: span width = union of the four sub-scanlines (MGC parts 4 and 5)
git am 0016-n64-the-VI-slows-only-read-modify-write-spans-AA0-x1.patch   # v40: the VI slows read-modify-write spans only, AA0 x1.14, AA2 grows with the line
git am 0017-Revert-n64-the-VI-slows-only-read-modify-write-spans.patch   # 0016 reverted (the VI had stopped during v40)
git am 0018-n64-span-pixels-cut-off-by-the-scissor-are-walked-fr.patch   # v46-v48: pixels outside the scissor are walked from the major edge
git am 0019-n64-span-and-triangle-prices-from-the-synthetic-fram.patch   # v32/v44/v47/v49: span and triangle prices from the synthetic frames
git am 0020-n64-triangle-rows-above-the-scissor-span-less-triang.patch   # v50/v51: rows above the scissor, span-less triangles, depth spans by the depth buffer's megabyte
git am 0021-n64-DPC_CLOCK-keeps-the-remainder-of-every-step-the-.patch   # v52/v42: DPC_CLOCK's remainder, the list prices recalibrated, CPU bus losses
git am 0022-n64-a-primitive-whose-commands-come-from-the-megabyt.patch   # v53: commands read from the megabyte being drawn into or shown by the VI
git am 0023-n64-SP-DMA-and-AI-losses-of-the-RDP-as-measured-VI-f.patch   # v52-v54: SP DMA and AI losses as measured, the VI factor of AA mode 2 on the whole list
git am 0024-n64-DPC_START-stays-pending-until-the-RDP-has-worked.patch   # task_timing v21: START_VALID stays set until the RDP switches
git am 0025-n64-the-VI-raises-what-a-bus-transfer-costs-the-RDP-.patch   # rdp_timing v56: the VI multiplier of contention per kind of transfer
git am 0026-n64-an-SP-DMA-costs-the-RDP-more-in-the-megabyte-of-.patch   # rdp_timing v57: where the SP DMA is (megabyte of the colour / depth buffer / the VI's picture)
git am 0027-n64-the-RSP-running-beside-the-RDP-costs-the-RDP-a-s.patch   # task_timing v22 / v23: what the RDP loses while the RSP runs
git am 0028-n64-what-a-CPU-cache-line-transfer-costs-the-RDP-dep.patch   # rdp_timing v58 / v59: what a CPU cache line transfer costs, place by place
git am 0029-n64-what-a-CPU-cache-line-fill-costs-the-RDP-depends.patch   # rdp_timing v60 to v68: what a CPU cache line fill costs, by what the RDP is doing
git am 0030-n64-the-losses-per-CPU-cache-line-fill-read-from-DPC.patch   # rdp_timing v72 / v73: the loss table read from DPC_PIPEBUSY, spans blended without depth measured
git am 0031-n64-the-VI-loads-the-bus-only-while-it-reads-the-pic.patch   # task_timing v26: the VI loads the bus only while it reads the picture (not during vertical blanking)
git am 0032-n64-the-AI-beside-a-running-graphics-task-texture-lo.patch   # task_timing v26: the AI beside a running task, texture loads from the megabyte the VI shows
# Build as usual (cmake -B build && cmake --build build)
```

`9999` is the headless verification harness (`headless/`, plus counting, dumping and environment-variable knobs on the core side), and is not meant for upstream (apply it on top of 0032 with `git apply`).
It removes the GUI (hiro/desktop-ui) from the top-level CMake and links only the N64 core, and
`headless <rom> <number of VI frames> --watch ADDR[:fmt] --save/--load/--poke ...` lets you observe RAM
(it is for analysis in a Linux environment without a GUI).

## Caveats and remaining issues

- (added 2026-11-09) The **games' triangles costing 5-12% more than synthetic ones**: v36-v38 rule out placement, texture orientation, rows
  outside the scissor, zero-width rows, edge slope, snapping, size class, drawing order and adjacency (the excess is a constant over each list).
  v39 also measured partial depth failure (pixels failing individually save the console nothing; only runs of 16 or more pixels or whole spans
  save, linearly), worth 0-3% in the replays, so not the main cause. The re-split of fixed cost / span cost is on hold because the synthetic
  frames and the games still disagree; 0015 is the sub-scanline union (v36).
- (added 2026-11-01, overall status) **The DK64 demo is within 1 to 2% of the console with series v7/v8** (dk64_tasklog: RDP-bound frames −1%, CPU-bound frames +2%,
  total +0.8%, and the PI DMA speed is the same). **The BAR intro is still at seg0 249 vs 230 frames (ares is 8% faster)**, and the remainder is
  (1) the part that only appears in-game (RSP DMA contention, FIFO coupling, CPU contention, together +2 to +4%), and (2) **the ±10 to 15% "scene content" dependence of the triangle cost**
  (v28: determined by overlap and drawing order, and not explained by width, mode, texture, placement or commands. It cancels out in BAR's heavy frames and in DK64, and
  stacks up in the same direction in BAR's light frame). (2) cannot be closed with the table, and requires a pixel-level RDP simulation (a memory access model equivalent to
  angrylion). Any change that fits BAR, such as a uniform factor or the console's edge walker, breaks DK64 (the v24 candidate table).
- (added 2026-10-31) BAR intro: the mechanism was established in v20 (the RSP waits for the RDP on the FIFO → patch 0010). With v20b, the RSP+RDP of frame 2316 got as close as
  ares 1.553 against the console's 1.587 VI (−2%, versus −3.4% for v6), but the intro as a whole is still 249 vs 225 frames (+11%). That is more than the per-frame
  RSP/RDP time difference (0.03 to 0.08 VI) can explain, and the remainder lies **on the CPU side (game logic), or in the ordering of the per-frame RSP tasks (including yields
  caused by audio tasks)**. What is needed next is a ROM that measures on the console "how many VI the CPU uses per frame" (the time from the SP interrupt to the next task start),
  or a ROM that dumps and replays BAR's CPU processing itself.
- v20b revealed a difference between the two consoles: under VI game mode, the time the RSP's DMA takes from the RDP is 71 clk/DMA on the NTSC console and 47 on the PAL console (51 on both with VI off),
  and the RDP-alone VI factor is also NTSC +11.3% / PAL +18.6%. The model leans towards the NTSC console (factor x1.28, +11.7%).
- `tex` (lists containing texture loads) + VI game mode + CPU misses + continuous 3.5 KB SP DMA makes the console's RDP stall (reproduced on both consoles, v19c).
- The VI factor is the NTSC console's value. The PAL console was 5 to 15% heavier in the same modes, so fitting a PAL ROM on a PAL console would need PAL values for `viFactor*`.
- RCP register reads are uniformly 23 (only MI is 11), and uncached RDRAM reads are 32. The FPU fast paths (0.0 / 1.0 / 2^n operands) are not modelled (the normal value is charged).
- The VI factor is the NTSC console's value (on the PAL console the VI's effect is 5 to 26% larger: v15/v19).

- The calibration is based on one scene (Jungle Japes) recorded from the console in this demo. The same factors are not necessarily optimal for other scenes or other titles.
  If a similar 60fps recording is available, the same procedure can verify it with `analyze.py` (recording → display interval series) and `score.py` (block comparison with the ares log).
- Because RDP time goes from 0 to realistic values, **there may be an effect on the frame rate of other titles**
  (especially 60fps titles with a high RDP load). I only have this ROM on hand, so other titles are unverified.
  If problems appear, adjust `spanPenalty`/`contentionZ`/`contentionPlain` in the `Timing` struct in `rdp.hpp` and the
  `*Stall` constants in `cpu.hpp` (all of them are console-measured values, and no free parameters are left).
- In this environment Vulkan (parallel-rdp) could not be initialized (SwiftShader does not support 16-bit storage),
  so I could not check the screen visually. Verification was done with the coordinates and state transitions in RAM (Y oscillation in the rope section, cannon launch, map transition to the next demo).
- The same mechanism should apply to the US/PAL versions (with different addresses), but this is unverified. PAL is 50Hz, so the delta threshold (20ms) differs,
  and the calibration may fit differently.

## References

- [ares #1652](https://github.com/ares-emulator/ares/issues/1652), [ares #1175](https://github.com/ares-emulator/ares/issues/1175)
- [simple64 v2022.07.7 release notes](https://github.com/m64p/m64p/releases/tag/v2022.07.7), [simple64 #173 "DK64 runs too well"](https://github.com/simple64/simple64/issues/173)
- [Donkey Kong 64: Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled) (`patches/patches_main.c`, `patches/timing_fixes.c`)
- [n64brew: Reality Display Processor/Pipeline](https://n64brew.dev/wiki/Reality_Display_Processor/Pipeline)
- [wScottSh/ares Issue #1 (research notes on a hardware-accurate timing model)](https://github.com/wScottSh/ares/issues/1)
