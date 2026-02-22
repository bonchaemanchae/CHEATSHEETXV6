#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    int pid;
    int pfd[2];
    int cfd[2];
    char byte = atoi(argv[1]);
    char buffer[1];

    pipe(pfd);
    pipe(cfd);
    
    pid = fork();

    if(pid==0){
        // close(pfd[1]); 
        // close(cfd[0]);
        read(pfd[0], buffer, 1);
        printf("%d: received %d from parent\n", getpid(), buffer[0]);
        write(cfd[1], buffer, 1);
        close(pfd[0]); //close read from parent
        close(cfd[1]); //close write from child

    } else {
        // close(cfd[1]); //close read from child
        // close(pfd[0]); //close write from parent
        write(pfd[1], &byte, 1);   
        read(cfd[0], buffer, 1);
        printf("%d: received %d from child\n", getpid(), buffer[0]);
        close(cfd[0]); 
        close(pfd[1]); 
    }
    exit(0);
}