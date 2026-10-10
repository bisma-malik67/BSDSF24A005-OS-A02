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

## Feature-3: Column Display - Down Then Across (v1.2.0)

### Q1. What is the logic for "down then across" printing? Why is a single loop insufficient?

First all filenames are read into an array and the longest name is found.
Then the layout is calculated:

- column width = longest name + 2 spaces
- number of columns = terminal width / column width (at least 1)
- number of rows = ceil(total files / number of columns)

In "down then across" order, the list is filled column by column: the first
`rows` names go in column 0 from top to bottom, the next `rows` names go in
column 1, and so on. Since the terminal prints one row at a time, the program
loops over rows (outer loop) and columns (inner loop), and for row `r` and
column `c` it prints `names[c * rows + r]`.

A single loop over the array is not enough because neighbouring items on one
printed line are not neighbours in the array: they are `rows` positions apart.
A single loop would print the names in left-to-right order, which is the
"across" layout, not "down then across". The row and column counts must be
known before printing the first line, so the whole list has to be stored first.

### Q2. What is the purpose of ioctl here? What are the limits of a fixed 80-column fallback?

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` asks the terminal driver for the
current window size, and `ws.ws_col` gives the number of columns. The program
uses this to decide how many columns of names fit on one line.

With only a fixed width of 80:
- on a wider terminal, space is wasted and fewer columns are shown than could fit;
- on a narrower terminal, lines are longer than the window, so the terminal
  wraps them and the columns become misaligned and hard to read;
- if the user resizes the window, the output does not adapt.

The fallback is still useful when ioctl fails, for example when the output is
redirected to a file or a pipe.
