# Packages

Status: design. Builds on beerlang's existing project tooling (`beer.edn`,
`beer build`, `beer ubertar`, tar index); the beerlang side is requested in
beerlang's `TODO.md` ("beeros requests", item 5).

Goal: "I wrote a great program and want to hand it to someone as one
`.tar`." The tar carries the program's namespaces, its dependencies and
how to start it; beeros can install it by copying a file and run it by
name.

## No new manifest: `beer.edn` travels with the code

beerlang projects already have a project file, a plain beerlang map read
with `read-string`:

```clojure
{:name         "snake"
 :version      "0.3.0"
 :paths        ["src" "lib"]
 :main         snake.core            ; ns with -main
 :dependencies ["vendor/tiles.tar"]}
```

A package is what `beer build` (just the project) or `beer ubertar`
(project + dependencies merged) writes, **plus that `beer.edn` at the tar
root**. Entries are namespace paths relative to the `:paths` roots:

```
snake.tar
├── beer.edn
├── snake/core.beer        ; snake.core
├── snake/board.beer
└── tiles/sprites.beer     ; from a dependency (ubertar)
```

Keys beeros reads beyond `:main` (all optional, added when needed):
`:description`, `:args` (default `-main` arguments), `:requires-beeros`
(minimum version), `:assets` (non-code files to extract, e.g. fonts or
levels, see below).

## Installing

Copy the tar into a package directory on the load path:

```clojure
@beer.fs/load-path   ; ["/lib" "/pkg"]
```

At boot (and on `(beer.pkg/rescan!)`), every `*.tar` in a load-path
directory is indexed: its `.beer` entries become requirable namespaces,
exactly like beerlang's `BEERPATH` on a host. Plain directories and tars
mix freely (directories for development, tars for distribution); the
first match on the load path wins, so `/lib` can override a package.

No unpacking: sources are read from the tar on `require`. Assets are
read from the tar too, through `(beer.pkg/resource "snake" "levels/1.edn")`.

## Running

```clojure
(beer.pkg/run "snake")                 ; installed package by :name
(beer.pkg/run "/pkg/snake.tar" "--fast") ; or a path, with args
```

`run` reads `beer.edn` from the tar, requires `:main`, and calls
`(-main & args)` in a fresh task. The REPL waits for it; the emergency
key (below) stops it.

`/bin` holds launchers so programs run by name, and a `bin-path` atom
(default `["/bin"]`) is searched like `load-path`:

- `/bin/snake.beer` — a one-liner `(beer.pkg/run "snake")`, or any script;
- `(beer.pkg/exec "snake" & args)` finds and runs it.

## Namespaces and conflicts

Namespaces are global in beerlang, so two packages can't both own
`util.core`. For now, by convention:

- a package owns a unique root namespace (`snake.*`), like Java packages
  or Clojure libraries;
- one version of a namespace per system. `ubertar` already merges
  dependencies into the package; when two installed packages bundle the
  same namespace, the first on the load path wins and `rescan!` warns.

**Remapping (later, if real conflicts appear).** The loader is ours and
reads all of a file's forms before evaluating them, so it could load a
package's namespaces under a prefix: rewrite `(ns snake.core ...)` to
`(ns pkg.snake-0.3.snake.core ...)`, rewrite `:require` clauses that
point inside the package, and add aliases so qualified symbols
(`snake.board/step`) inside the package resolve to the prefixed
namespaces. Known gaps: syntax-quote expansions, symbols kept as data,
`resolve`/`ns-publics` by name. Not worth it until a conflict forces it.

## Emergency drop to REPL

A running program owns the console; the user needs a way back that the
program can't block.

- **Keys**: on serial, `Ctrl-\` (0x1C) or a serial BREAK; on keyboards, a
  Ctrl-Alt-Esc chord. Seen by the UART receive interrupt (bytes go into
  a ring buffer, so the check works even while a program hogs the CPU)
  and by the keyboard task.
- **Stop**: beerlang's requested `vm_request_interrupt()` (TODO item 4)
  raises an interrupt in the task the REPL is waiting on. Until it
  exists, beeros can only flag the request; a program that never
  yields can't be stopped.
- **Recover**: point `beer.tty/current` back at the console, re-enable
  `beer.lineedit`, reset the terminal (`ESC c` / clear), print
  `[beeros] interrupted`, return to the prompt. Spawned background tasks
  of the program keep running; `(beer.pkg/stop "snake")` (later) would
  track and stop them.

## Implementation order

1. Tar reader in beerlang over `beer.fs/read-bytes` (ustar: 512-byte
   headers, name/size/type) → `{path {:offset :size}}` index per tar.
2. `beer.fs` require wrapper consults tar indexes on load-path; `/pkg`
   added to load-path and the rootfs.
3. `beer.pkg`: `run`, `exec`, `resource`, `rescan!`, `installed`.
4. UART RX interrupt + ring buffer; break-key detection.
5. Wire the interrupt once beerlang has it; move tar reading and require
   onto beerlang's file system hook when that lands, deleting the beeros
   wrappers.

Until `beer build` writes `beer.edn` into the tar, a package can be made
with plain `tar -cf snake.tar beer.edn -C src .`.
