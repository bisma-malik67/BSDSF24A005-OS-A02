/*
* Programming Assignment 02: ls v1.2.0
* Features: -l (long listing), default "down then across" columns
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>

extern int errno;

/* maximum column widths for the long listing */
typedef struct
{
    int nlink;
    int user;
    int group;
    int size;
} widths_t;

/* which output format was requested */
typedef enum
{
    MODE_DEFAULT,
    MODE_LONG,
    MODE_HORIZONTAL
} display_mode_t;

void do_ls(const char *dir, display_mode_t mode);
void print_long(const char *dir, const char *name, const widths_t *w);

int main(int argc, char *argv[])
{
    int opt;
    display_mode_t mode = MODE_DEFAULT;

    while ((opt = getopt(argc, argv, "lx")) != -1)
    {
        switch (opt)
        {
        case 'l':
            mode = MODE_LONG;
            break;
        case 'x':
            mode = MODE_HORIZONTAL;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l | -x] [dir...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind == argc)
    {
        do_ls(".", mode);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], mode);
            puts("");
        }
    }
    return 0;
}

/* ---------- helpers ---------- */

static int num_digits(unsigned long long n)
{
    int d = 1;
    while (n >= 10)
    {
        n /= 10;
        d++;
    }
    return d;
}

static const char *user_name(uid_t uid, char *buf, size_t n)
{
    struct passwd *pw = getpwuid(uid);
    if (pw)
        return pw->pw_name;
    snprintf(buf, n, "%u", (unsigned)uid);
    return buf;
}

static const char *group_name(gid_t gid, char *buf, size_t n)
{
    struct group *gr = getgrgid(gid);
    if (gr)
        return gr->gr_name;
    snprintf(buf, n, "%u", (unsigned)gid);
    return buf;
}

static void free_names(char **names, int count)
{
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* ---------- reading the directory into a dynamic array ---------- */

/* Reads all non-hidden names. On success returns 0 and gives back the array,
   the number of names and the length of the longest name. */
static int read_names(const char *dir, char ***out, int *count, int *maxlen)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return -1;
    }

    int cap = 16, n = 0, longest = 0;
    char **names = malloc(cap * sizeof(char *));
    if (names == NULL)
    {
        perror("malloc");
        closedir(dp);
        return -1;
    }

    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (n == cap) /* array is full: double its size */
        {
            cap *= 2;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (tmp == NULL)
            {
                perror("realloc");
                free_names(names, n);
                closedir(dp);
                return -1;
            }
            names = tmp;
        }

        names[n] = strdup(entry->d_name);
        if (names[n] == NULL)
        {
            perror("strdup");
            free_names(names, n);
            closedir(dp);
            return -1;
        }

        int len = strlen(names[n]);
        if (len > longest)
            longest = len;
        n++;
    }

    if (errno != 0)
        perror("readdir failed");

    closedir(dp);
    *out = names;
    *count = n;
    *maxlen = longest;
    return 0;
}

/* ---------- column display: down then across ---------- */

static int terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return 80; /* fallback */
    return ws.ws_col;
}

static void display_columns(char **names, int count, int maxlen)
{
    if (count == 0)
        return;

    int width = terminal_width();
    int col_width = maxlen + 2; /* 2 spaces between columns */

    int cols = width / col_width;
    if (cols < 1)
        cols = 1;
    int rows = (count + cols - 1) / cols; /* ceiling division */

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int idx = c * rows + r; /* down the column first */
            if (idx >= count)
                break;

            printf("%s", names[idx]);

            /* pad only if another item follows on this row */
            int next = (c + 1) * rows + r;
            if (c + 1 < cols && next < count)
            {
                int pad = col_width - (int)strlen(names[idx]);
                for (int k = 0; k < pad; k++)
                    putchar(' ');
            }
        }
        putchar('\n');
    }
}

/* ---------- horizontal display: left to right (-x) ---------- */

static void display_horizontal(char **names, int count, int maxlen)
{
    int width = terminal_width();
    int col_width = maxlen + 2;
    int pos = 0;      /* current horizontal position on the line */
    int prev_len = 0; /* length of the previous name */

    for (int i = 0; i < count; i++)
    {
        int len = strlen(names[i]);

        if (i > 0)
        {
            int pad = col_width - prev_len;
            if (pos + pad + maxlen > width) /* next column slot does not fit: new line */
            {
                putchar('\n');
                pos = 0;
            }
            else
            {
                for (int k = 0; k < pad; k++)
                    putchar(' ');
                pos += pad;
            }
        }

        printf("%s", names[i]);
        pos += len;
        prev_len = len;
    }
    if (count > 0)
        putchar('\n');
}

/* ---------- long listing ---------- */

static void compute_widths(const char *dir, char **names, int count,
                           widths_t *w, long long *total)
{
    memset(w, 0, sizeof(*w));
    *total = 0;

    for (int i = 0; i < count; i++)
    {
        char path[PATH_MAX];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (lstat(path, &st) == -1)
            continue;

        char ub[16], gb[16];
        int l;

        l = num_digits(st.st_nlink);
        if (l > w->nlink) w->nlink = l;
        l = strlen(user_name(st.st_uid, ub, sizeof(ub)));
        if (l > w->user) w->user = l;
        l = strlen(group_name(st.st_gid, gb, sizeof(gb)));
        if (l > w->group) w->group = l;
        l = num_digits(st.st_size);
        if (l > w->size) w->size = l;

        *total += st.st_blocks; /* 512-byte units */
    }
}

static void display_long(const char *dir, char **names, int count)
{
    widths_t w;
    long long total;

    compute_widths(dir, names, count, &w, &total);
    printf("total %lld\n", total / 2); /* ls shows 1024-byte blocks */

    for (int i = 0; i < count; i++)
        print_long(dir, names[i], &w);
}

void do_ls(const char *dir, display_mode_t mode)
{
    char **names;
    int count, maxlen;

    if (read_names(dir, &names, &count, &maxlen) == -1)
        return;

    if (mode == MODE_LONG)
        display_long(dir, names, count);
    else if (mode == MODE_HORIZONTAL)
        display_horizontal(names, count, maxlen);
    else
        display_columns(names, count, maxlen);

    free_names(names, count);
}

/* Convert st_mode into a string like "drwxr-xr-x" */
void mode_to_string(mode_t m, char *s)
{
    if (S_ISREG(m))       s[0] = '-';
    else if (S_ISDIR(m))  s[0] = 'd';
    else if (S_ISLNK(m))  s[0] = 'l';
    else if (S_ISCHR(m))  s[0] = 'c';
    else if (S_ISBLK(m))  s[0] = 'b';
    else if (S_ISFIFO(m)) s[0] = 'p';
    else if (S_ISSOCK(m)) s[0] = 's';
    else                  s[0] = '?';

    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_IXUSR) ? 'x' : '-';
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_IXGRP) ? 'x' : '-';
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_IXOTH) ? 'x' : '-';

    if (m & S_ISUID) s[3] = (m & S_IXUSR) ? 's' : 'S';
    if (m & S_ISGID) s[6] = (m & S_IXGRP) ? 's' : 'S';
    if (m & S_ISVTX) s[9] = (m & S_IXOTH) ? 't' : 'T';

    s[10] = '\0';
}

void print_long(const char *dir, const char *name, const widths_t *w)
{
    char path[PATH_MAX];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &st) == -1)
    {
        perror(path);
        return;
    }

    char mode[11];
    mode_to_string(st.st_mode, mode);

    char ub[16], gb[16];
    const char *uname = user_name(st.st_uid, ub, sizeof(ub));
    const char *gname = group_name(st.st_gid, gb, sizeof(gb));

    /* ls shows the time for recent files, and the year for old ones */
    char *t = ctime(&st.st_mtime);
    char timebuf[32];
    time_t now = time(NULL);
    double age = difftime(now, st.st_mtime);
    if (age > 182 * 24 * 3600 || age < 0)
        snprintf(timebuf, sizeof(timebuf), "%.6s  %.4s", t + 4, t + 20);
    else
        snprintf(timebuf, sizeof(timebuf), "%.12s", t + 4);

    printf("%s %*lu %-*s %-*s %*lld %s %s",
           mode,
           w->nlink, (unsigned long)st.st_nlink,
           w->user, uname,
           w->group, gname,
           w->size, (long long)st.st_size,
           timebuf, name);

    if (S_ISLNK(st.st_mode))
    {
        char target[PATH_MAX];
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n != -1)
        {
            target[n] = '\0';
            printf(" -> %s", target);
        }
    }
    printf("\n");
}
