# Filesystem choice

Status: **decided: ext2**, implemented in stages. Stage 1 (read-only,
`lib/ext2.beer`) and stage 2 (`lib/fs.beer`, mount table + beerlang's file hook) are done. This
sits on the block-device map from `docs/block-device.md`; a filesystem only
ever calls `:read!` / `:write!` / `:flush!`.

## What we wanted

- Unix-style paths (`/`), case-sensitive names.
- Small enough to write and read in beerlang, so it stays hackable from the
  REPL like the drivers.
- Images buildable on the Mac without mounting anything or needing root.
- A read-only subset that's useful on its own (load `.beer` code and
  assets from disk), with writes added later.

## The candidates

| | Paths & names | Size in beerlang (rough) | Building images on macOS | Readable elsewhere | Power-loss safe |
|---|---|---|---|---|---|
| **FAT32** | `/` is fine (`\` is a DOS API convention, not on-disk format); case-insensitive; long names are a bolt-on (UTF-16 + 8.3 aliases + checksums) | ~1k lines with long names | built in (`newfs_msdos`, `hdiutil`) | everything | no |
| **exFAT** | long names native, no 8.3 aliases | more than FAT (allocation bitmap, more metadata) | built in | everything | no |
| **ext2** | Unix: `/`, case-sensitive, permissions, symlinks | read-only ~500, read-write ~1.5k | `e2fsprogs`: `mke2fs -d dir` builds an image from a folder; `debugfs` inspects or edits it | Linux, U-Boot; macOS only with add-ons | no (no journal) |
| **Minix** | Unix-ish, 30/60-character name limit | smallest read-write (~700) | `mkfs.minix` is awkward to get on macOS | Linux | no |
| **littlefs** | `/`, simple directories | largest (~3-4k; the C reference is ~5k) | `littlefs-python` / `littlefs-fuse` | nothing without its tools | **yes** |
| **romfs / tar** | read-only | tiny (tar already exists in beerlang) | `tar` / `genromfs` | yes | n/a |

Notes:

- **FAT32** wins on reach (any computer reads the card; many boards' boot
  ROMs and U-Boot expect a FAT boot partition), but brings
  case-insensitivity and the long-name machinery.
- **littlefs** only pays off when power loss mid-write matters. Its wear
  levelling is redundant on SD/eMMC, which have their own FTL.
- **Minix** is the smallest read-write option but feels like a teaching
  filesystem (name limits, tooling).

## Decision

**ext2** as beeros's own filesystem: Unix semantics, a well-documented
layout, and a clean split where the read-only half is small. The cost is
that the Mac can't browse the card directly; `debugfs` covers inspecting
and editing images on the host.

If real hardware needs a FAT boot partition, that lives next to the ext2
one (MBR/GPT partitions are wrapped block-device maps). tar stays available
for read-only bundles.

## ext2 subset

Supported (what `mke2fs -t ext2` produces by default):

- Revision 0 and 1 superblocks; block sizes 1 KiB to 4 KiB (64 KiB maybe later).
- Direct, single-, double- and triple-indirect block maps.
- Linear directory scan. `dir_index` (htree) is a compat feature: htree
  directories are still valid linear directories, so reads ignore it.
- The `filetype` incompat feature (directory entries carry a type byte), on or off.
- `sparse_super`, `large_file` (64-bit sizes), `ext_attr` and
  `resize_inode` (ignored on read).
- Fast symlinks (target inside `i_block`) and slow ones.

Refused at mount time: any other incompat feature (`extents`, `64bit`,
`meta_bg`, journal recovery, compression, ...). This means ext3/ext4
images are rejected rather than misread. For writes, unknown ro_compat
features also make the mount read-only.

## Stages

1. **Read-only** (`lib/ext2.beer`, `beer.ext2`): mount a block-device map,
   read inodes, resolve paths, list directories, read files into
   ByteBuffers or strings, follow symlinks. Enough to `load` code from disk.
2. **A `beer.fs` facade**: `mount!` at a path, `ls`, `slurp`, `load-file`
   over whichever filesystem is mounted, so tar/FAT could slot in later.
3. **Writes**: overwrite in place, then allocate blocks and inodes from the
   bitmaps, create and unlink files and directories, update group
   descriptors and the superblock counts. Mark the fs not-clean while
   mounted read-write, so `e2fsck` on the host catches crashes.
4. **Caching**: a small block cache (bitmaps, inode table blocks,
   directory blocks) once the uncached version works.

## Stage 1 API (`beer.ext2`)

```clojure
(def fs (beer.ext2/mount (beer.blk/device "virtio0")))  ; throws :not-ext2 / :unsupported-features
(beer.ext2/ls fs "/lib")             ; sorted names, no . / ..
(beer.ext2/slurp fs "/lib/demo.beer") ; string (UTF-8)
(beer.ext2/read-bytes fs "/big.bin")  ; ByteBuffer of exactly :size bytes
(beer.ext2/stat fs path)  (beer.ext2/lstat fs path)  (beer.ext2/readlink fs path)
```

Errors are `ex-info` with `{:op :path :reason}` (`:not-found`,
`:not-a-directory`, `:not-a-file`, `:too-many-links`, ...). Each indirect
block is read once per file, and runs of contiguous blocks go to the device
as one transfer (up to 64 KiB). Tested on 1 KiB and 4 KiB block images:
nested paths, `.`/`..`, fast/slow/relative/absolute symlinks,
double-indirect files, sparse files, empty files, multi-block directories;
ext4 and blank disks are refused. A 400 KB file reads in ~50 ms (4 KiB
blocks) / ~135 ms (1 KiB blocks) under QEMU with the beerlang driver.

## Stage 2 API (`beer.fs`)

At boot the first block device holding ext2 is mounted at `/`.

```clojure
(beer.fs/mounts)                       ; {"/" "virtio0 (ext2)"}
(beer.fs/mount! "/mnt" (beer.ext2/mount (beer.blk/device "virtio1")))
(beer.fs/ls "/lib")  (beer.fs/slurp p)  (beer.fs/read-bytes p)
(beer.fs/stat p)  (beer.fs/exists? p)  (beer.fs/umount! "/mnt")
(load-file "/lib/demo.beer")           ; beerlang's own, through *fs*
```

Paths are absolute; the longest matching mount point wins. A filesystem
type plugs in through `beer.fs/impls` (`{:ext2 {:ls ... :slurp ...}}`).

### `require`, `load-file`, `slurp` from the filesystem

beerlang reads files through a hook, `beer.core/*fs*` (nil on a host).
`beer.fs` installs `{:read hook-read :write! hook-write!}` and sets
`beer.core/*load-path*` to `["/lib/"]`, so beerlang's own `require`
(`foo.bar` from `/lib/foo/bar.beer`, vector or flat specs, `ns`),
`load-file`, `slurp` and `spit` work on these paths unchanged. `:read`
returns nil for a missing file; `:write!` throws until ext2 writes exist.
Namespaces from the embedded libraries (`beer.input`, `beer.fs`, ...)
have no file under `/lib`, so the boot loader marks them in
`beer.core/*loaded-libs*`; requiring them is a no-op plus `:as` /
`:refer`.

### Root filesystem

`make disk` builds `build/disk.img` (ext2, 16 MB) from `rootfs/` plus
beerlang's pure libraries (`beer.string`, `json`, `test`, `digest`, `crypto`, `tar` into
`/lib/beer/`; natives that open host files, like `beer.digest/random-bytes`
reading `/dev/urandom`, still fail); `make run-disk` boots with it. Layout: `/etc` (`init.beer`,
`beeros.edn`), `/lib`, `/bin`, `/share/fonts`, `/var`. After all embedded
libraries load, `/etc/init.beer` runs (`beer.fs/run-init!`); errors in it
are printed, not fatal.

beerlang's `load-file` leaves a file's `(ns ...)` current afterwards
(Clojure restores it); `run-init!` restores it itself.

## Building test images

```bash
brew install e2fsprogs      # keg-only: /opt/homebrew/opt/e2fsprogs/sbin
mke2fs -t ext2 -b 1024 -d rootdir/ disk.img 8M
debugfs -R 'ls -l /' disk.img
make run BOARD=virt-riscv DISK=disk.img
```

After write support lands, `e2fsck -fn disk.img` checks what beeros wrote.
