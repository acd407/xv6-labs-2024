#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"
#define CKSZ 20

int main (int argc, char *argv[]) {
    // your code here.  you should write the secret to fd 2 using write
    // (e.g., write(2, secret, 8)
    char *end = sbrk (PGSIZE * CKSZ);
    for (int i = 0; i < CKSZ; i++) {
        char *ck = end + PGSIZE * i;
        if (ck[18] == 's' && ck[19] == 'e')
            write (2, ck + 32, 8);
    }
    exit (0);
}
