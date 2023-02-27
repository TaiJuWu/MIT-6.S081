#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{
    int p2c[2];
    int c2p[2];

    char ping[] = "ping";
    char pong[] = "pong";

    if(pipe(p2c) < 0){
      printf("pipe() failed\n");
      exit(1);
    }

    if(pipe(c2p) < 0){
      close(p2c[0]);
      close(p2c[1]);
      printf("pipe() failed\n");
      exit(1);
    }

    int pid = fork();
    char buf[64];

    if(pid > 0){ // parent
        int my_pid = getpid();

        write(p2c[1], ping, strlen(ping));
        read(c2p[0], buf, 5);
        printf("%d: received %s\n", my_pid, buf);        
    }
    else if(pid == 0){ // child
        int my_pid = getpid();

        read(p2c[0], buf, 5); // wait util parent send data to child
        printf("%d: received %s\n", my_pid, buf);
        write(c2p[1], pong, strlen(pong));
    }
    else{
        printf("fork execute error\n");
        exit(1);
    }

    close(p2c[0]);
    close(p2c[1]);
    close(c2p[0]);
    close(c2p[1]);

    exit(0);
}