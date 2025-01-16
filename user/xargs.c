#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"
#include <assert.h>
#define BUF_SIZE 128

int main (int argc, char *argv[]) {
    char buf[BUF_SIZE];
    char **args = (char **) malloc ((argc + 1) * sizeof (char *));
    for (int i = 1; i < argc; i++)
        args[i - 1] = argv[i];
    args[argc - 1] = buf;
    args[argc] = 0;
    while (gets (buf, BUF_SIZE)) {
        if (strlen (buf) < 2)
            break;
        buf[strlen (buf) - 1] = '\0';
        if (fork()) {
            wait (0);
        } else {
            exec (argv[1], args);
            return 0;
        }
    }
    free (args);
    return 0;
}
