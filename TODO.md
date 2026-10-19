# beeros TODO

## Soon-ish

### Browsable documentation: next steps

Done (2026-10-19): `beer.help` (`describe`, `apropos`, `namespaces`,
`browse`), F1 / Ctrl-Alt-H in the WM, docs pages in `/share/doc`. Next:

- beermacs `C-h f` / `C-h a` / `C-h k` into a `*Help*` buffer, and F1 on
  the name at point.
- Docstrings for the native namespaces (`beer.core`, `beer.gfx`, ...) and
  the `lib/*.beer` fns that lack one; arglists (beerlang keeps only
  `:doc` in meta; ask for `:arglists`).
- `all-ns` and qualified `meta` from beerlang would replace the
  `*loaded-libs*` + native-list guess and the `binding *ns*` trick.

### ext2: rename, and the rest of writes

Writing files, mkdir, rm, rmdir work (2026-10-19). Still to do: `mv`
(rename, also across directories, fixing `..`), symlinks, triple-indirect
files (> 64 MiB with 1 KiB blocks), growing a directory past 12 blocks,
and a write-back cache (every write goes to the device now).

### Games: canvas windows, colour blits, full screen

What's there: raw key down/up and held keys (`beer.input/down?`), mouse
and tablet events, fills, 1-bit scaled blits, rect copies, surfaces,
`nano-time`/`sleep`. Missing, all small:

1. `beer.wm/canvas!`: a window that is just a surface, with its key AND
   mouse events (window-relative) on a channel; today the WM keeps mouse
   events and only hosts terminals.
2. `beer.gfx/blit-pixels!` (XRGB ByteBuffer rect -> surface, optional
   colour key) and a scaled present (draw at 320x200, show at 1280x720);
   a QOI/BMP loader in beerlang.
3. Full screen under the WM (a borderless window over everything) and
   without it (draw on surface 0).
4. Later: sound (virtio-sound), vsync-ish frame pacing.

Speed: game logic in beerlang is fine (~3 us per loop iteration under
QEMU TCG); per-pixel work must go through C natives or a low-res surface
scaled up. Real RISC-V hardware should be 10-30x faster than TCG.

### A graphics editor for serious work

Next to beermacs (which stays the small, hackable one): draws straight
into a `beer.wm` surface — fonts, colours, split views, mouse — on the
same text model (`beermacs.buffer`). Wants a PSF font in `/share/fonts`
(e.g. Terminus) chosen in `/etc/beeros.edn`.

### `beer.pkg/start`: run a package like a program

`run`/`exec` run in the caller's task (like bash's `source`). Add
`(beer.pkg/start "beermacs" & args)`: a new task with fresh bindings
(`*ns*` user, its own output); under beer.wm in a new window, else in the
background (like `&`).

### Actors and named channels

- `beer.hive`'s local half (actors, `register`/`whereis`, supervisors)
  needs no network, but the namespace requires `beer.hive.node`
  (`beer.tcp`, `beer.crypto`) at load, so it doesn't load with
  `BEER_PLATFORM=none`. Open in beerlang's TODO; wanted: load the node
  part lazily (on `start-node!`).
- Named channels instead of Unix sockets: everything shares one image, so
  a registry name -> channel is enough (hive's `register`/`whereis` is
  that for actors). Maybe later a Plan 9-ish twist: channels under a path
  (`/run/clipboard`) through a beer.fs mount.
- Real sockets come with networking (virtio-net + a TCP/IP stack,
  docs/network.md); then hive's distribution works between machines.

## beermacs

- Undo: redo (undo as an edit, like Emacs), smaller groups of
  self-inserts, `modified?` back to false when undo reaches the saved state.
- `C-x 2` windows, `M-x`, `C-r`, query-replace, Lisp indentation, colours.
