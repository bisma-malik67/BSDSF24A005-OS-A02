# REPORT.md - OS Programming Assignment 02

## Feature-2: Long Listing Format (v1.1.0)

### Q1. What is the difference between stat() and lstat()? When is lstat() more appropriate in ls?

Both system calls fill a `struct stat` with a file's metadata. The difference
shows up when the path is a symbolic link:

- `stat()` follows the link and returns information about the file the link
  points to.
- `lstat()` does not follow the link. It returns information about the link
  itself (its type, its size, its permissions, its timestamps).

In `ls -l`, `lstat()` is the right choice. A symbolic link must be shown with
type `l`, and its size is the length of the target path, not the size of the
target file. `lstat()` also works for broken links, where `stat()` would fail
with ENOENT because the target does not exist. In my code, `print_long()`
uses `lstat()` and then `readlink()` to print `name -> target`.

### Q2. How can bitwise operators and macros be used to extract file type and permissions from st_mode?

`st_mode` packs several pieces of information into the bits of one integer:
the upper bits store the file type, and the lower bits store the permissions
(plus the setuid, setgid and sticky bits).

- File type: `st_mode & S_IFMT` isolates the type bits. Comparing the result
  with a constant such as `S_IFDIR` or `S_IFREG` tells the type, e.g.
  `(st_mode & S_IFMT) == S_IFDIR`. The macros `S_ISDIR(m)`, `S_ISREG(m)` and
  `S_ISLNK(m)` do exactly this.
- Permissions: each permission is one bit, so ANDing with a mask tests it.
  `st_mode & S_IRUSR` is non-zero if the owner has read permission, so I print
  `r`, otherwise `-`. The same is done with `S_IWUSR`, `S_IXUSR` for the owner,
  `S_IRGRP`, `S_IWGRP`, `S_IXGRP` for the group, and `S_IROTH`, `S_IWOTH`,
  `S_IXOTH` for others.
- Special bits: `S_ISUID`, `S_ISGID` and `S_ISVTX` are tested the same way and
  change the `x` position to `s`/`S` or `t`/`T`.

The AND operator works because masking with a single-bit constant keeps only
that bit and clears all the others.
