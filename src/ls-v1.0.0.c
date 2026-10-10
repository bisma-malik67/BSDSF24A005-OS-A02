/*
* Programming Assignment 02: ls v1.1.0
* Feature: -l (long listing) with aligned columns
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>

extern int errno;

/* maximum column widths, found in the first pass */
typedef struct
{
    int nlink;
    int user;
    int group;
    int size;
} widths_t;

void do_ls(const char *dir, int long_flag);
void print_long(const char *dir, const char *name, const widths_t *w);

int main(int argc, char *argv[])
{
    int opt;
    int long_flag = 0;

    while ((opt = getopt(argc, argv, "l")) != -1)
    {
        switch (opt)
        {
        case 'l':
            long_flag = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l] [dir...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind == argc)
    {
        do_ls(".", long_flag);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], long_flag);
            puts("");
        }
    }
    return 0;
}

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

/* user name, or the number if there is no such user */
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

/* First pass: find the widest value of each column and the block total */
static void compute_widths(const char *dir, widths_t *w, long long *total)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
        return;

    memset(w, 0, sizeof(*w));
    *total = 0;

    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char path[PATH_MAX];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
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

        *total += st.st_blocks; /* counted in 512-byte units */
    }
    closedir(dp);
}

void do_ls(const char *dir, int long_flag)
{
    struct dirent *entry;
    widths_t w;
    long long total = 0;

    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    if (long_flag)
    {
        compute_widths(dir, &w, &total);
        printf("total %lld\n", total / 2); /* ls shows 1024-byte blocks */
    }

    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (long_flag)
            print_long(dir, entry->d_name, &w);
        else
            printf("%s\n", entry->d_name);
    }

    if (errno != 0)
    {
        perror("readdir failed");
    }

    closedir(dp);
}

/* Convert st_mode into a string like "drwxr-xr-x" */
void mode_to_string(mode_t m, char *s)
{
    /* file type */
    if (S_ISREG(m))       s[0] = '-';
    else if (S_ISDIR(m))  s[0] = 'd';
    else if (S_ISLNK(m))  s[0] = 'l';
    else if (S_ISCHR(m))  s[0] = 'c';
    else if (S_ISBLK(m))  s[0] = 'b';
    else if (S_ISFIFO(m)) s[0] = 'p';
    else if (S_ISSOCK(m)) s[0] = 's';
    else                  s[0] = '?';

    /* owner */
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_IXUSR) ? 'x' : '-';
    /* group */
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_IXGRP) ? 'x' : '-';
    /* others */
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_IXOTH) ? 'x' : '-';

    /* special bits: setuid, setgid, sticky */
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

    /* ctime gives "Sun Oct 11 00:11:00 2026\n"
       ls shows the time for recent files, and the year for old ones */
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

    /* for symbolic links show where they point */
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
