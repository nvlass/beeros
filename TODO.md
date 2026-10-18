# beeros TODO

## Soon-ish

### Browsable documentation for everything hackable

A help system you can walk around in, from beermacs and from any REPL:
every namespace beeros loads (drivers, `beer.fs`, `beer.term`, `beer.wm`,
beermacs itself, packages) with its public names and docstrings, plus the
docs/*.md pages.

- beermacs: `C-h f` describe a name (docstring, arglists, home namespace),
  `C-h a` apropos (substring over all public names), `C-h n` a namespace
  index; all into a `*Help*` buffer where RET on a name follows it, `l`
  goes back. `C-h k` describe a key (the keymaps are data already).
- REPL: `(beer.help/describe 'beer.fs/ls)`, `(beer.help/apropos "mount")`,
  `(beer.help/namespaces)`; the same data as the editor.
- `/share/doc`: the docs/*.md pages on the disk image, readable in
  beermacs (`C-h d`).
- Docstrings: most `lib/*.beer` fns have them; fill in the rest.

Found while looking (beerlang 3d07ed0): `(meta 'sym)` gives `{:doc ...}`
only for an unqualified symbol in the current namespace;
`(binding [*ns* 'some.ns] (meta (symbol "name")))` works for others.
There is no `all-ns`: list namespaces from `(keys *loaded-libs*)` plus
the native ones (`beer.core`, `beer.gfx`, `beer.mem`, `beer.tty`,
`beer.bytes`, ...), or ask beerlang for `all-ns` / qualified `meta`.

### ext2 writes

So edits to `/etc`, `/lib` and packages survive a reboot (today only
`/tmp`, a RAM fs, is writable): block and inode allocation, directory
entries, `beer.fs/spit!` on ext2. Then beermacs can save anywhere.

### A graphics editor for serious work

Next to beermacs (which stays the small, hackable one): draws straight
into a `beer.wm` surface — fonts, colours, split views, mouse — on the
same text model (`beermacs.buffer`). Wants a PSF font in `/share/fonts`
(e.g. Terminus) chosen in `/etc/beeros.edn`.

## beermacs

- Undo: redo (undo as an edit, like Emacs), smaller groups of
  self-inserts, `modified?` back to false when undo reaches the saved state.
- `C-x 2` windows, `M-x`, `C-r`, query-replace, Lisp indentation, colours.
