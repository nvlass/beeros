# Packages

Status: implemented (`lib/ustar.beer`, tar paths in `lib/fs.beer`,
`lib/pkg.beer`, sample `packages/hello`); remapping is not. Builds on beerlang's existing project tooling (`beer.edn`,
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

## Using it

```clojure
(beer.pkg/installed)              ; {"hello" {:path "/pkg/hello.tar" :version "0.1.0" ...}}
(beer.pkg/run "hello")            ; => banner, "Hello, WORLD!" (:args from beer.edn)
(beer.pkg/run "hello" "ada")      ; -main gets "ada"
(beer.pkg/exec "hello" "you")     ; /bin/hello.beer, beer.pkg/*args* = ["you"]
(beer.pkg/resource "hello" "assets/banner.txt")
(beer.fs/ls "/pkg/hello.tar/hello")      ; ("core.beer" "fmt.beer")
(beer.pkg/rescan!)                ; after copying a tar into /pkg
```

How it fits together: `beer.fs` reads paths that go through a tar file
(`/pkg/hello.tar/hello/core.beer`) out of the archive (`beer.ustar`,
cached until the tar's inode changes). `rescan!` (also at boot) sets
`beer.core/*load-path*` to `["/lib/" "/pkg/hello.tar/" ...]`, so
beerlang's own `require` finds package namespaces through `*fs*` with no
tar knowledge of its own, and warns when two tars carry the same
namespace file. `make disk` turns each `packages/<name>/` (`beer.edn`,
`src/`, optional `assets/`) into `/pkg/<name>.tar`.

## Installing

Copy the tar into a package directory on the load path:

```clojure
@beer.fs/load-path   ; ["/lib" "/pkg"]
```

At boot (and on `(beer.pkg/rescan!)`), every `*.tar` in a `pkg-path`
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

## Emergency drop to REPL (done)

A running program owns the console; the user needs a way back that the
program can't block.

- **Keys**: `Ctrl-\` or a serial BREAK always; `Ctrl-C` too while a form
  runs (at the prompt it cancels the line), unless the program turned it
  off with `(beer.tty/ctrl-c-interrupts! false)`, as an editor that binds
  `C-c` would. On keyboards the same, plus Ctrl-Alt-Esc.
- **Seen by**: the UART receive interrupt (`kernel/console.c`; bytes go
  into a ring the serial tty reads, so the keys work while a program hogs
  the CPU or never reads input) and, for keyboards, a `beer.input/intercept`
  hook (`beer.tty/interrupt-key?`) in the virtio-input task. Either drops
  the bytes typed ahead.
- **Stop**: beerlang's `vm_request_interrupt()` / `(request-interrupt!)`:
  the scheduler unwinds the foreground task (the REPL's form, and loads it
  runs) between quanta, running `finally` blocks but skipping `catch`.
  Background tasks keep running.
- **Recover**: the REPL prints `[beeros] interrupted`, drops the rest of
  the input and calls `(beer.tty/recover!)`: console back to the serial
  tty, attributes reset, cursor shown, Ctrl-C interrupting again.
  `(beer.pkg/stop "snake")` (later) would track and stop a program's
  spawned tasks.
- Not covered: a task stuck inside a C native that never returns, and a
  background task that hogs the CPU (nothing is foreground to stop).

## Implementation order

1. ~~Tar reader over `beer.fs/read-bytes`~~ (`beer.ustar`: ustar, pax
   `path`/`size`, GNU long names).
2. ~~Tars on the load path~~ (tar paths in `beer.fs` + `*load-path*`).
3. ~~`beer.pkg`: `run`, `exec`, `resource`, `rescan!`, `installed`~~.
4. ~~UART RX interrupt + ring buffer; break-key detection; wire the
   interrupt~~ (done).
5. ~~Move require onto beerlang's file system hook, deleting the beeros
   wrappers~~ (done); tars from memory need beerlang's
   `tar_index_add_buffer` / `beer run app.tar` (its TODO, item 5).

Until `beer build` writes `beer.edn` into the tar, a package can be made
with plain `tar -cf snake.tar beer.edn -C src .`.

## Starting a package like a program

`(beer.pkg/run "x" & args)` calls the package's `-main` in the caller's
task (like bash's `source`: same namespace, output and tty).
`(beer.pkg/start "x" & args)` runs it as a program instead: its own task,
fresh bindings (`*ns*` user); under the window manager in a terminal
window of its own, or, when its `beer.edn` has `:terminal false` (a
program that opens a canvas, like `bounce`), as a background task.
`:window {...}` in `beer.edn` passes options to that window (`:cols`,
`:rows`, ...).
