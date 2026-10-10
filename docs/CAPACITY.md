# Capacity: what's free on the CHOMPI, and how to make room

What a new feature can still use, measured on 2026-10-10 after the split between code and
data moved (#26, `FRIZZ.bin` md5 `ff53db75`). `make` prints the memory table after every
build; the numbers below come from it and from `build/FRIZZ.map`.

## Memory

| Region | Size | Used | Free | What's in it |
|---|---|---|---|---|
| `SRAM_EXEC` (AXI SRAM) | 272 KB | 216,588 B (78 %) | **~60 KB** | all the code, and the initial values of `.data` |
| `SRAM` (AXI SRAM) | 240 KB | 226,064 B (92 %) | ~19 KB | `.data` and `.bss`: the engine (101 KB), the two MIDI receive queues (`midi_clock`, 74 KB), the event log's index (9.5 KB), the scene store (5.7 KB), libDaisy's buffers |
| `DTCMRAM` | 128 KB | 65,584 B | ~62 KB, shared with the stack | the reverb (64 KB); the stack grows down from its top |
| `RAM_D2` | 32 KB | 23,808 B | ~8 KB | DMA buffers (audio, LEDs, SD), not cached |
| `RAM_D2CACHE` | 256 KB | 0 | **256 KB** | nothing (the `.d2_bss` section points there) |
| `RAM_D3`, `ITCMRAM` | 64 KB each | 0 | 64 KB each | nothing |
| `SDRAM` | 64 MB | 42.7 MB | ~21 MB | the loop (31.7 MB), the delay (3.8 MB), the tape stop (4 MB), the freezer (1.9 MB), the event log (2 MB) |

**The bench build hits the walls first.** `FRIZZ-bench.bin` carries the bench as well: its
code is at 234,220 B (84 %, ~43 KB free) and its data at 237,876 B (97 %, **~7.7 KB free**).
So a feature of more than ~40 KB of code, or ~7 KB of data in internal RAM, needs room made
first (below), or the bench build stops linking before `FRIZZ.bin` does. Data grows into
SDRAM or DTCM where it can; for internal RAM, step 4 below frees 64 KB.

The split moved by 40 KB (step 1 below) after the FX knobs' page 2 (#35) had left ~21 KB of
code, ~3.4 KB in the bench build. The scene work (`Recall`, `Morph`, the scene file) is
compiled `-Os` and out of line (`FX_SCENE_ONCE`), as the event log's and MIDI's are.

Where the code goes (`.text` + `.rodata` by object, from the map):

| | Bytes |
|---|---|
| FRIZZ itself (`chompi_main.o`: one translation unit, `-O3`) | 96,600 |
| libDaisy and the STM32 HAL | 87,700 |
| FatFs (`ff.o`, `ccsbcs.o`, `diskio.o`) | 14,700 |
| newlib-nano, libm, libgcc | 11,400 |

The biggest single functions are `FxChain::Process` (11 KB, everything inlined into it),
`main` (7.8 KB), `NormalPage::OnButton` (6.8 KB), `PassthroughEngine::Process` (6.1 KB),
`Shifter::Process` (5.6 KB) and `NormalPage::Draw` (5.1 KB).

To see it again after a build (`firmware/code/src`):

```bash
awk '/^Linker script and memory map/{go=1}
go && /^ \.(text|rodata)/ { if (NF>=4) {sz=strtonum($3); f=$4} else {getline; sz=strtonum($2); f=$3}
  sub(/.*\//, "", f); s[f]+=sz }
END {for (k in s) print s[k], k}' build/FRIZZ.map | sort -rn | head -20
arm-none-eabi-nm -S --size-sort -C build/FRIZZ.elf | tail -30      # largest symbols
```

## CPU

The audio callback, not memory, is what limits FRIZZ today: about 45 % of each 0.5 ms block
idle, about 100 % with a loop playing and ~11 effects on, where it starts to crackle (a known
limit, parked). See `CLAUDE.md`, *The audio callback is CPU-bound*, and measure with the bench.
A feature that runs per sample costs CPU in that budget; one that runs in `MainLoop()` doesn't.

## Making room for code, the biggest lever first

Step 1 is done (#26); the rest isn't built yet: do it when a feature needs it, each step on
its own branch with a device test (the layout can change the timing: b5c658c).

1. **Move the split between code and data** (done, #26: 232 KB / 280 KB became 272 KB /
   240 KB in `chompi_sram.lds`). The bootloader copies up to 480 KB of image into the 512 KB
   AXI SRAM (`reference/firmware/chompi-bootloader-v6.4-beta/shared/bootloader.cpp:102`,
   `sram_program[SRAM_SPACE - 32768]`), and the launcher takes images up to 512 KB
   (`ImageLooksValid` in its `chainload.h`, staged in a 512 KB SDRAM buffer). What they copy
   is the image, the code and `.data`'s initial values (217 KB), not the split, which is
   FRIZZ's own choice. It stopped at 40 KB because the bench build's data needs the rest;
   step 4 or 2 frees data so it can move further.
2. **Move cold data out of AXI SRAM** into the unused 256 KB of D2 (`.d2_bss`, an
   `__attribute__((section(".d2_bss")))` on the global), so step 1 can move further: the MIDI
   receive queues (`midi_clock`, 74 KB, read once per block), the scene store, the event log's
   index. Not the engine: it's read every sample and AXI SRAM is the fastest after the TCMs.
   D2 is reachable by DMA but is cached, so nothing a DMA writes may go there. To check
   first: libDaisy enables the D2 SRAM clocks only with `DATA_IN_D2_SRAM`
   (`libs/libDaisy/src/sys/system_stm32h7xx.c:203`); `RAM_D2`'s DMA buffers already work at
   0x30000000, but SRAM2/3 (0x30020000 up) may need `__HAL_RCC_D2SRAM2_CLK_ENABLE()` and
   `__HAL_RCC_D2SRAM3_CLK_ENABLE()` at boot. A zeroing loop is needed too: startup code
   clears only `.bss`.
3. **Build rarely run code for size.** `EventLog.h` (`EVENT_LOG_ONCE`) and `MidiControl.h`
   (`MIDI_CONTROL_ONCE`) mark such functions `noinline, optimize("Os")`. The same on the play
   page's handlers and drawing, the card code (`SceneStore.h`, `MasterSettings.h`,
   `FxScenes.h`), the boot and init code would save an estimated 10-20 KB; measure with the
   map. The per-sample path stays `-O3`.
4. **Shrink the MIDI queues.** libDaisy's `MidiHandler` keeps `FIFO<MidiEvent, 256>`
   (`libs/libDaisy/src/hid/midi.h:261`), each event with a 128-byte SysEx buffer: 37 KB per
   transport. FRIZZ drains them every block, so 32 events would do: -64 KB of data. It's a
   change to vendored libDaisy: rebuild `libdaisy.a` with GCC 10.3 and list it in
   `THIRD_PARTY.md`.
5. **Trim libDaisy and the HAL**: the USB host driver (`stm32h7xx_hal_hcd.o`, 2.2 KB), USB CDC,
   the QSPI driver (3.7 KB) are linked though FRIZZ doesn't use them. ~5-8 KB, all in vendored
   code: worth it last.

Not recommended: link-time optimisation (GCC 10.3, and it would merge the `-O0` workarounds
into their callers), and running code from QSPI flash (slow, and it's the bootloader's).

**For the CPU, not for room:** ITCM (64 KB, unused) is the core's fastest code memory, with no
cache to miss. Moving the per-sample code there (`FxChain::Process`, the effects,
`PassthroughEngine::Process`: ~30-40 KB) may cut the load, if I-cache misses are part of it.
The linker script loads `.itcmram` from internal flash, which a `BOOT_SRAM` app can't use: it
would need to load from `SRAM_EXEC` and be copied at boot by FRIZZ, so it saves no code room.
