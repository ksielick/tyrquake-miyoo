Quake (TyrQuake-Miyoo) for the Miyoo Mini Plus with OnionOS, without RetroArch
https://github.com/ksielick/tyrquake-miyoo (branch standalone)

The TyrQuake-Miyoo core run by its own small program (tqrun): about 20 % faster than in
RetroArch, and every frame shown in time with the screen (59.67 Hz).

Quake's data (not included): copy id1/pak0.pak (shareware or full game) and id1/pak1.pak
(full game) into this folder: Roms/PORTS/Games/TyrQuake-Miyoo/id1/. Music (optional): the
CD tracks as id1/music/track02.ogg ... track11.ogg. If you already set up Quake in Onion's
Ports Collection (Roms/PORTS/Games/Quake Collection (TyrQuake)/id1), that data is used.

Buttons: as in the RetroArch version. Menu opens this port's menu:
  RESUME, SAVE STATE / LOAD STATE (slots 1-9, left/right), EXIT GAME.
Leaving with EXIT GAME, turning the console off or putting it to sleep keeps your place:
the next start puts you back there. Quitting from Quake's own menu starts afresh next time.
Quake's own savegames work as usual.

Files kept here: saves/ (savegames, Quake's config.cfg, states), tyrquake.opt (settings;
delete it to get the defaults back), log.txt (the last run's log).

Licence: GPL-2.0-or-later (TyrQuake and Quake's engine, id Software). The menu's design and
the sleep save follow mr-finster's miyoo64 ports (MIT); the 8x8 font is public domain.
