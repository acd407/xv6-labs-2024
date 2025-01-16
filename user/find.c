#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY (x)
#define assert(cond)                                                           \
    do {                                                                       \
        if (! (cond)) {                                                        \
            const char *msg = "Assertion failed: " #cond " at " __FILE__       \
                              ":" TOSTRING (__LINE__) "\n";                    \
            write (2, msg, strlen (msg));                                      \
            exit (255);                                                        \
        }                                                                      \
    } while (0)

char *basename (char *path) {
    char *p;
    for (p = path + strlen (path); p >= path && *p != '/'; p--)
        ;
    p++;
    return p;
}

// find 返回 bool，代表子调用是否找到了 target
// ，如果找到，则需要打印出顶层目录。
void find (char *path, char *target) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open (path, O_RDONLY)) < 0) {
        fprintf (2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat (fd, &st) < 0) {
        fprintf (2, "find: cannot stat %s\n", path);
        close (fd);
        return;
    }

    if (st.type == T_DIR) {
        if (strlen (path) + 1 + DIRSIZ + 1 > sizeof buf) {
            printf ("find: path too long\n");
            return;
        }
        strcpy (buf, path);
        p = buf + strlen (buf);
        *p++ = '/';
        while (read (fd, &de, sizeof (de)) == sizeof (de)) {
            if (de.inum == 0)
                continue;
            memmove (p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (! strcmp (p, "."))
                continue;
            if (! strcmp (p, ".."))
                continue;
            find (buf, target);
        }
    } else {
        if (0 == strcmp (basename (path), target))
            printf ("%s\n", path);
    }
    close (fd);
}

int main (int argc, char *argv[]) {
    assert (argc == 3);
    find (argv[1], argv[2]);
    exit (0);
}
