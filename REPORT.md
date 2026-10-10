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

## Feature-4: Horizontal Column Display -x (v1.3.0)

### Q1. Compare the complexity of the "down then across" and "across" printing logic. Which needs more pre-calculation and why?

The "down then across" (vertical) layout needs more pre-calculation. Before
printing anything the program must know the number of columns
(terminal width / column width) and the number of rows
(ceil(total files / columns)), because the item at row `r` and column `c` is
`names[c * rows + r]`. The rows must be known up front, since the first line
already needs items that are `rows` positions apart in the array.

The horizontal layout (-x) is simpler. The array is walked once in order, and
the program only keeps track of the current horizontal position. Each name is
printed and padded to the column width, and when the next column slot would not
fit in the terminal width, a newline is printed and the position is reset. No
row count is needed, and no index arithmetic is required.

### Q2. How did you manage the display modes (-l, -x, default)? How does the program decide which function to call?

I used an enum `display_mode_t` with the values `MODE_DEFAULT`, `MODE_LONG` and
`MODE_HORIZONTAL`. In `main()` the `getopt()` loop (option string "lx") sets a
single variable `mode` when it sees -l or -x. If both are given, the last one
wins, like in the real ls. The default value is `MODE_DEFAULT`.

`mode` is passed to `do_ls()`, which first reads all names into a dynamic
array. After that it checks the mode: `MODE_LONG` calls `display_long()`,
`MODE_HORIZONTAL` calls `display_horizontal()`, and otherwise
`display_columns()` (down then across) is called. Reading the directory is
shared by all modes, and only the printing function differs.

## Feature-5: Alphabetical Sort (v1.4.0)

### Q1. Why must all directory entries be read into memory before sorting? What are the drawbacks for directories with millions of files?

`readdir()` returns entries one at a time in the order the file system stores
them, which is not alphabetical. The smallest name could be the very last entry
returned, so the program cannot print anything until it has seen all the
names. A sort needs every element available at once to compare and rearrange
them, so all names are first stored in a dynamic array and then sorted with
`qsort()`.

For a directory with millions of files this has drawbacks:
- Memory use grows with the number of entries (the pointer array plus a copy of
  every name made with `strdup`), which can be hundreds of megabytes.
- Nothing is printed until the whole directory has been read and sorted, so the
  user waits a long time and sees no output.
- Sorting costs O(n log n) time on top of the read time.
- With `-l`, every file is also `lstat()`ed, which adds a lot of I/O.
  The real ls has an unsorted option (-f) for this reason, which prints entries
  as they are read without storing them.

### Q2. What is the purpose and signature of the comparison function for qsort()? Why const void *?

`qsort()` is a generic function: it can sort an array of any element type, but
it does not know how to compare two elements. The caller supplies a comparison
function with the signature

    int compare(const void *a, const void *b);

It must return a negative number if the first element should come before the
second, zero if they are equal, and a positive number if the first should come
after the second.

The arguments are `const void *` because `qsort()` has to work with any type,
and `void *` is the only pointer type that can point to anything. `const`
promises that the comparison does not modify the elements. `qsort()` passes the
address of each array element. In my program each element is a `char *`, so the
arguments are really `char **`. The function first casts them
(`*(const char **)a`) to get the string and then returns `strcmp(s1, s2)`.
