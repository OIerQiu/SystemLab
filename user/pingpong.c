#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int pf2s[2], ps2f[2];
    char buf[1];

    if (pipe(pf2s) < 0 || pipe(ps2f) < 0)
    {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    int pid = fork();

    if (pid < 0)
    {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {

        close(pf2s[1]);
        close(ps2f[0]);

        if (read(pf2s[0], buf, 1) != 1)
        {
            fprintf(2, "read failed\n");
            exit(1);
        }

        printf("%d: received ping\n", getpid());

        if (write(ps2f[1], buf, 1) != 1)
        {
            fprintf(2, "write failed\n");
            exit(1);
        }

        exit(0);
    } else {

        close(pf2s[0]);
        close(ps2f[1]);

        if (write(pf2s[1], "x", 1) != 1)
        {
            fprintf(2, "write failed\n");
            exit(1);
        }

        if (read(ps2f[0], buf, 1) != 1)
        {
            fprintf(2, "read failed\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());
        exit(0);
    }
}