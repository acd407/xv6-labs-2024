#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

void g (int rfd) {
    int target;
    if (sizeof (int) != read (rfd, &target, sizeof (int))) {
        close (rfd);
        return;
    }
    printf ("prime %d\n", target);
    int fd[2] = {0};
    pipe (fd);
    if (fork()) {
        close (fd[0]);
        while (1) {
            int rev = 0;
            if (sizeof (int) != read (rfd, &rev, sizeof (int))) {
                break;
            }
            if (rev % target)
                write (fd[1], &rev, sizeof (int));
        }
        close (rfd);
        close (fd[1]);
        wait (0);
    } else {
        close (rfd);
        close (fd[1]);
        g (fd[0]);
    }
}

int main (void) {
    int fd[2] = {0};
    pipe (fd);
    if (fork()) {
        close (fd[0]);
        for (int i = 2; i <= 280; i++) {
            write (fd[1], &i, sizeof (int));
        }
        close (fd[1]);
        wait (0);
    } else {
        close (fd[1]);
        g (fd[0]);
    }
}
