Quake (TyrQuake-Miyoo) for the Miyoo Mini Plus with OnionOS, without RetroArch
https://github.com/ksielick/tyrquake-miyoo (branch standalone)

The TyrQuake-Miyoo core run by its own small program (tqrun): about 20 % faster than in
RetroArch, and every frame shown in time with the screen (59.67 Hz).

Quake's data (not included): copy id1/pak0.pak (shareware or full game) and id1/pak1.pak
(full game) into this folder: Roms/PORTS/Games/TyrQuake-Miyoo/id1/. Music (optional): the
CD tracks as id1/music/track02.ogg ... track11.ogg. If you already set up Quake in Onion's
Ports Collection (Roms/PORTS/Games/Quake Collection (TyrQuake)/id1), that data is used.

Buttons: as in the RetroArch version.
Menu (short press): Onion's Game Switcher, as in RetroArch games. Picking Quake there puts
  you back where you were.
Settings: Quake's own menu, Options -> Video Options:
  Resolution: 640x480 or 320x240. Choose with left/right, apply with A: the game starts again
    and comes back where you were.
  Max FPS: applies at once. 30: every frame shown for two of the screen's refreshes; 60: one;
    Unlimited: the game runs as fast as it can, the screen still shows up to 59.67 a second.
  Show FPS: the game's frames per second, top right.
  Resolution and Max FPS are kept in tyrquake.opt, Show FPS in Quake's config.cfg.
Leaving with the Game Switcher, turning the console off or putting it to sleep keeps your
place: the next start puts you back there. Quitting from Quake's own menu starts afresh next
time. Quake's own savegames work as usual.

Files kept here: saves/ (savegames, Quake's config.cfg, states), tyrquake.opt (settings;
delete it to get the defaults back), log.txt (the last run's log).

Licence: GPL-2.0-or-later (TyrQuake and Quake's engine, id Software). The sleep save follows
mr-finster's miyoo64 ports (MIT).
