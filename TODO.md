# beeros TODO

## Soon-ish

### Browsable documentation: next steps

Done (2026-10-19): `beer.help` (`describe`, `apropos`, `namespaces`,
`browse`), F1 / Ctrl-Alt-H in the WM, docs pages in `/share/doc`;
`:arglists` and docs on natives (beerlang 4a58848: beer.core and the
beerlang libs; beeros's own `beer.mem`/`beer.gfx`/`beer.tty`/
`beer.blk.native` through `docs_register` tables). Next:

- beermacs `C-h f` / `C-h a` / `C-h k` into a `*Help*` buffer, and F1 on
  the name at point.
- Docstrings for the `lib/*.beer` and `/lib/beer/*.beer` fns that lack one.

### Live patching: a reload helper

Redefining a var takes effect at the next call, but whatever captured
the old fn value keeps it: beermacs keymaps (fn values, not names), the
WM's input loop, a term's renderer, tasks already running a loop. Wanted:

- `(beer.reload/ns! 'beer.wm)`: re-load a namespace from its file
  (`/lib/...` or a package), then restart what registered as depending on
  it. Long-running parts register a restart fn (`(beer.reload/on-reload!
  'beer.wm restart-fn)`): the WM restarts its input task, beermacs
  re-reads its keymaps, terms restart renderers.
- Calls by name inside a fn body already see redefinitions (as in
  Clojure); only stored fn values don't. Store vars instead (`#'f`;
  beerlang 4a58848 has callable vars): beermacs's keymaps do. Still to
  look at: the WM's handlers, term tty maps, `spawn` of long loops.
- Embedded namespaces (`lib/*.beer` in the kernel image): a disk copy in
  `/lib/beer/` that `/etc/init.beer` loads over the embedded one, so a
  patch to a driver persists; `reload` finds it there.
- beermacs: `C-c C-k` / `M-x reload` for the buffer's namespace.

### ext2: rename, and the rest of writes

Writing files, mkdir, rm, rmdir work (2026-10-19). Still to do: `mv`
(rename, also across directories, fixing `..`), symlinks, triple-indirect
files (> 64 MiB with 1 KiB blocks), growing a directory past 12 blocks,
and a write-back cache (every write goes to the device now).

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

## beermacs

- Undo: redo (undo as an edit, like Emacs), smaller groups of
  self-inserts, `modified?` back to false when undo reaches the saved state.
- `C-x 2` windows, `M-x`, `C-r`, query-replace, Lisp indentation, colours.
