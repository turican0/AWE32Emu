# Magic Carpet 2 – real card against AWE32Emu

Short excerpts (30 s) of the Magic Carpet 2 music: the game recorded on a
real Sound Blaster AWE32, and the same music rendered by AWE32Emu.

| part | real card | AWE32Emu |
|---|---|---|
| intro | [mc2_intro_card.flac](mc2_intro_card.flac) | [mc2_intro_awe32emu.flac](mc2_intro_awe32emu.flac) |
| menu | [mc2_menu_card.flac](mc2_menu_card.flac) | [mc2_menu_awe32emu.flac](mc2_menu_awe32emu.flac) |
| level 1 | [mc2_level1_card.flac](mc2_level1_card.flac) | [mc2_level1_awe32emu.flac](mc2_level1_awe32emu.flac) |

How they were made:

- **card** – the tester ran the game (DOS, `SBAWE32.MDI`) and recorded the
  line output.
- **AWE32Emu** – the XMI from the game rendered with
  `--driver dos --conf conf/mc2.conf`, the ROM, the game driver's GM bank
  (`--sf SBAWE32.MDI`) and the game bank `BULLFROG.SBK`. The intro uses the
  master volume 100 from the conf, the menu and level 1 `--master-volume 127`.
- Both are aligned in time (onset cross-correlation) and matched in loudness
  (the render is scaled to the RMS of the card), with 0.5 s fades.

What to keep in mind when listening:

- The dry right channel of the tester's card is dead; its right output
  carries only the reverb and chorus returns. The render has both channels.
- The level 1 recording went through a darker recording chain than the
  others; the difference in the highs is mostly the recorder.
- In level 1 the game keeps the "fight" tracks (channels 7-9) silent until a
  fight starts; the render does the same (`trigger_mute` in the conf).
