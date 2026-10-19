# beeros TODO

## Soon-ish

### Browsable documentation: next steps

Done (2026-10-19): `beer.help` (`describe`, `apropos`, `namespaces`,
`browse`), F1 / Ctrl-Alt-H in the WM, docs pages in `/share/doc`;
`:arglists` and docs on natives (beerlang 4a58848: beer.core and the
beerlang libs; beeros's own `beer.mem`/`beer.gfx`/`beer.tty`/
`beer.blk.native` through `docs_register` tables). Next:

- beermacs `C-h f` / `C-h a` / `C-h k`: done (2026-10-20; F1 itself stays
  the WM's help window).
- Docstrings for the `lib/*.beer` and `/lib/beer/*.beer` fns that lack one.

### Live patching: next steps

Done (2026-10-20): `beer.reload` (`ns!`, `defonce`, `reloading?`,
`on-reload!`), state in the libraries kept across reloads, boot side
effects guarded, built-in lib sources on the disk, `:reload` in
`/etc/beeros.edn`, beermacs keymaps as vars. Next:

- beermacs `C-c C-k`: reload the buffer's namespace (save, then `ns!`).
- `defonce` from beerlang instead of `beer.reload/defonce` (requested).
- Hooks where an old fn value is held: none needed so far (the WM loop,
  renderers and the editor call by name), but watch for `spawn` of
  closures.

### ext2: what's left

Done: writing files, mkdir, rm, rmdir (2026-10-19); mv (rename and move,
`..` fixed), symlinks, directories past 12 blocks (2026-10-20). Left:
triple-indirect files (> ~64 MiB with 1 KiB blocks), hard links, a
write-back cache (every write goes to the device), and a journal-less
crash story beyond "run e2fsck on the host".

### Games: next steps

Done (2026-10-19): `beer.wm/canvas!` (own surface, scaled `show!`, events
with keys up/down, buttons, pointer, wheel, close; `:fullscreen`),
`beer.gfx/blit-pixels!` and `blit-surface!`, `beer.image` (BMP),
`beer.pkg/start`, the `bounce` demo. Next:

- Sound (virtio-sound); vsync-ish pacing (ramfb has no vblank: sleep).
- QOI and PNG-less sprite sheets; `beer.image/sub` for sheet cells.
- Pointer grab / hide for full-screen games; relative mouse motion
  events for mouse-look.
- Speed: `show!` copies the whole canvas each frame (fine at 320x200);
  dirty rectangles if needed.

### A graphics editor for serious work

Next to beermacs (which stays the small, hackable one): draws straight
into a `beer.wm` surface — fonts, colours, split views, mouse — on the
same text model (`beermacs.buffer`). Wants a PSF font in `/share/fonts`
(e.g. Terminus) chosen in `/etc/beeros.edn`.

### Actors and named channels

- `beer.hive` works on beeros (beerlang ca2635b loads the node layer only
  on first remote use; `/lib/beer/hive*.beer` are on the image): local
  actors, `ask`, `register`/`whereis`, supervisors, monitors.
- Named channels instead of Unix sockets: everything shares one image, so
  a registry name -> channel is enough (hive's `register`/`whereis` is
  that for actors). Maybe later a Plan 9-ish twist: channels under a path
  (`/run/clipboard`) through a beer.fs mount.
- Real sockets come with networking (virtio-net + a TCP/IP stack,
  docs/network.md); then hive's distribution works between machines.
- Once networking is up: a small network game on `beer.hive` (two
  beeros machines, or QEMU instances, playing `bounce`-style over remote
  actors) — a demo of distribution and a good test of hive on beeros.

## beermacs

- Undo: redo (undo as an edit, like Emacs), smaller groups of
  self-inserts, `modified?` back to false when undo reaches the saved state.
- `C-x 2` windows, `M-x`, `C-r`, query-replace, Lisp indentation, colours.
