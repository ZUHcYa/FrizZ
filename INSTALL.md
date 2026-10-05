# Installing FRIZZ

FRIZZ installs from the SD card, the same way as any CHOMPI firmware update. You don't need any
tools or drivers, and the CHOMPI's bootloader is never touched.

## What you need

- A CHOMPI. It doesn't matter which firmware it runs now (TAPE, TEMPO or WAVE).
- Its microSD card, or any FAT32-formatted microSD card.
- A computer with a card reader.
- `FRIZZ.bin`. Download it from the [Releases page](https://github.com/ZUHcYa/FrizZ/releases).
  Until the first release is published, build it yourself as described in
  [`firmware/README.md`](firmware/README.md).

## Install

1. **Back up the card.** Copy everything on it to your computer: your samples, `presets.json`
   and `options.json`. FRIZZ doesn't touch them, but you'll want them if you go back.
2. **Remove the old firmware.** Delete every `.bin` file in the top folder of the card, for
   example `CHOMPI_TAPEv2_0.bin`. There must be exactly one `.bin` on the card. Everything else
   can stay.
3. **Copy `FRIZZ.bin`** to the top folder of the card, not into a subfolder.
4. **Eject the card** properly, put it in the CHOMPI and switch it on.
5. **Wait.** A slow rainbow LED pattern means the CHOMPI is writing FRIZZ into its memory.
   Don't switch it off while that runs. FRIZZ starts by itself when it's done.

FRIZZ doesn't use the card while it runs. You can leave it in or take it out.

## Check that it works

1. Plug a stereo source into the **AUX input**, and headphones or a mixer into the outputs.
   (FRIZZ doesn't use the built-in microphone.)
2. Turn the **VOLUME** knob. You should hear your source, and the VOLUME LED moves with the level.

If you hear nothing, press VOLUME once and turn it to raise the input gain. Press it twice more
to get back to output volume. The [quick guide](QUICKSTART.md) takes it from here.

## Updating FRIZZ

Same as installing: replace `FRIZZ.bin` on the card with the new one and switch on.

## Going back to stock firmware

1. Delete `FRIZZ.bin` from the card.
2. Copy back your backup, or a factory card from
   [`reference/firmware/card-profiles/`](reference/firmware/card-profiles/) (also in
   [CHOMPI Club's repo](https://github.com/CHOMPI-Club/CHOMPI/tree/main/firmware/card-profiles)): `tape-2.0/`,
   `tempo-1.0/` or `wave-1.0/`. Copy the folder's *contents* to the top folder of the card.
   Each one includes its firmware `.bin`.
3. Switch on and wait for the rainbow pattern to finish.

## Troubleshooting

| Problem | Fix |
|---|---|
| No rainbow, the old firmware starts | Check that `FRIZZ.bin` is in the top folder, that it's the only `.bin` there, and that the card is FAT32 |
| Rainbow, but then nothing works | Copy `FRIZZ.bin` to the card again (the copy may be damaged) and switch on again |
| Silence | Check the input gain (press VOLUME, then turn) and the input/loop mix (SHIFT + turn VOLUME, turn towards green) |
| The CHOMPI doesn't respond at all | The bootloader may be damaged. Reinstall CHOMPI's own bootloader (`firmware/bin/CHOMPI_Bootloader_V6_2_0.bin`) as described in [`firmware/README.md`](firmware/README.md#4-put-it-on-the-chompi), then install FRIZZ again. Don't install the generic Daisy bootloader instead |
