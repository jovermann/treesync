treesync
========

Diff or synchronize two directory trees.

* Find structural differences between two directory trees, optionally ignoring the file content:
  * `treesync SRCDIR DSTDIR`
  * `treesync -C SRCDIR DSTDIR`
  * Differences are shown "from `DSTDIR` to `SRCDIR`". Think of `treesync NEW OLD`, unlike   `diff`, but consistent with "what do I need to do to `DSTDIR` to make it look like `SRCDIR`".
* Copying new and updated files to a copy of a directory, optionally ignoring the modification time:
  * `treesync -U SRCDIR DSTDIR`
  * `treesync -UT DIR1`
* Delete files in copy of a directory which are not (or no longer) present in the source:
  * `treesync -D SRCDIR DSTDIR`
* Synchronize a copy of a directory through a slow link using a filesystem which does not have valid timestamps:
  * `treesync -UDCT SRCDIR DSTDIR`

Add `-v` (or even `-vv` or `-vvv`) to see what is going on.

Usage
-----

```
>treesync --help
treesync: Sync or diff two directory trees, recursively.

Usage: treesync [OPTIONS] SRCDIR DSTDIR


File/dir processing options:
  -N --new                Copy files/dirs which only appear in SRCDIR into DSTDIR.
  -D --delete             Delete files/dirs in DSTDIR which do not appear in SRCDIR.
  -U --update             Copy files/dirs which either only appear in SRCDIR or which are newer (mtime)
                          than the corresponding file in DSTDIR or which differ in type into DSTDIR.
                          Implies --new.
     --diff               Print differences and do not change anything. This is also the default if none
                          of --new/--delete or --update are specified. Note: Differences are printed in
                          the view of going from DSTDIR to SRCDIR, so usually treediff NEW OLD (unlike
                          diff OLD NEW).
     --ignore-dirs        Just process the two specified directories. Ignore subdirectories.
     --ignore-special     Just process regular files, dirs and symbolic links. Ignore block/char devices,
                          pipes and sockets.
     --follow-symlinks    Follow symlinks. Without this (default) symlinks are compared as distinct
                          filesystem objects.
  -c --create-missing-dst Create DSTDIR if it does not exist for --new/--update.

Matching options:
  -C --ignore-content     Ignore file content when comparing files. Just compare their size and assume
                          files with the same size are identical.
  -T --ignore-mtime       Ignore mtime for --update and always assume the SRC to be newer than DST if they
                          are different, e.g. always overwrite DST with SRC if SRC and DST are different.

Verbose / common options:
     --show-matches       Show matching files for --diff instead of only showing differences (default).
     --show-subtree       For new/deleted dirs show all files/dirs in these trees (default is to just show
                          the new/deleted dir itself).
  -v --verbose            Increase verbosity. Specify multiple times to be more verbose.
  -n --no-color           Do not color output.
  -d --dummy-mode         Do not write/change/delete anything.
  -h --help               Print this help message and exit. (set)
     --version            Print version and exit.

treesync version 0.1.0 *** Copyright (c) 2022-2023 Johannes Overmann *** https://github.com/jovermann/treesync
```

Building
--------

```
make
```

The output is in the current direcory.

If this fails (for example because you do not have GNU make) use:

```
c++ -std=c++17 src/*.cpp -o treesync
```

`treesync` requires a C++17 compatible compiler (since it intensively uses `std::filesystem`).
It is known to build on Linux using GCC >= 9.4.0 and macOS using clang 12.0.0.

Motivation
----------

I wrote this tool to be able to sync audio files onto my sons cheap Android tablet. The Android MTP protocol has limitations, and/or all implementations accessing the tablets filesystem and the particular use-pattern of the tablet introduce further limitations. In particular:

  * Reading and writing files is slow. It is not feasible to overwrite files just because the modification times are broken, or even to copy the entire directory tree.
  * The timestamps of the tablets filesystem are always outdated since the tablet does not have a correctly synchronized clock since it is virtually never connect to the WiFi.

So I needed a tool which:
  * Copies all new files.
  * Deletes all deleted files.
  * Copies all files which are different is size.
  * Ignores timestamps.
  * Never compares the actual files contents.
  * Can show the differences without modifying anything.

It turns out neither `cp`, `rsync` not `diff` can do this. `cp -ru` copies too many files (broken timestamps) and does not delete deleted files and `rsync -r --delete` compares all files contents and `diff -qr` compares the files contens. So I thought, how hard can it be! :-)




