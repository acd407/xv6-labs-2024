#include "kernel/stat.h"
#include "user/user.h"

int main (int argc, char *argv[]) {
    int fdf2c[2] = {0};
    int fdc2f[2] = {0};
    char s[5] = {'\0'};
    pipe (fdf2c);
    pipe (fdc2f);
    if (fork()) {
        write (fdf2c[1], "ping", 4);
        read (fdc2f[0], s, 4);
        wait(0);
        printf ("%d: received %s\n", getpid(), s);
    } else {
        write (fdc2f[1], "pong", 4);
        read (fdf2c[0], s, 4);
        printf ("%d: received %s\n", getpid(), s);
    }
}
