/* MY LAB ANSWER */
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

int main(int argc, char* argv[]){
    int pid;
    int pfd[2]; //every pipe has exactly 2 ends, 0 for read, 1 for write
    int cfd[2];
    char byte = atoi(argv[1]);
    /*
    - argv[0] is handshake, argv[1] is 1 in handshake 1
    - atoi to convert string to int, setting to char casts atoi from 4 to 1 byte
    */
    char buffer[1]; //array created to hold 1 byte

    pipe(pfd); //each pipe creates fd0 and fd1
    pipe(cfd);
    
    pid = fork();

    if(pid==0){ //child
        read(pfd[0], buffer, 1); //wait till at least 1 byte is read and put into buffer (sleep state)
        printf("%d: received %d from parent\n", getpid(), buffer[0]);
        /* 
        - buffer would be passing memory address of box, not , hence buffer[0]
        - %s buffer print all chars in as string until hitting \0
        */
        write(cfd[1], buffer, 1); //write to child write
        close(pfd[0]); //close read from parent
        close(cfd[1]); //close write from child

    } else { //parent
        write(pfd[1], &byte, 1); //write to parent write
        /*
        - parent copies into kernel buffer (internal pipe buffer)
        - byte sits there till read() is called
        - &byte: address of variable stored in atoi results
        */
        read(cfd[0], buffer, 1); //read from child read
        printf("%d: received %d from child\n", getpid(), buffer[0]);
        close(cfd[0]); //close read from child
        close(pfd[1]); //close write from parent
    }
    exit(0);
}

/*IMPROVEMENT ON CODES*/
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

int main(int argc, char* argv[]){
    if(argc < 2){
        fprintf(2, "Usage: handshake <byte>\n");
        exit(1);
    }

    int pid;
    int pfd[2], cfd[2];
    char byte = atoi(argv[1]);
    char buffer[1];

    if(pipe(pfd) < 0 || pipe(cfd) < 0){
        fprintf(2, "Pipe failed\n");
        exit(1);
    }
    
    pid = fork();
    if(pid < 0){
        fprintf(2, "Fork failed\n");
        exit(1);
    }

    if(pid == 0){
        close(pfd[1]); 
        close(cfd[0]); 

        if(read(pfd[0], buffer, 1) != 1){
            fprintf(2, "Child read error\n");
            exit(1);
        }
        printf("%d: received %d from parent\n", getpid(), buffer[0]);
        
        write(cfd[1], buffer, 1);

        close(pfd[0]);
        close(cfd[1]);
    } else {
        close(pfd[0]); 
        close(cfd[1]); 

        write(pfd[1], &byte, 1);
        
        if(read(cfd[0], buffer, 1) != 1){
            fprintf(2, "Parent read error\n");
            exit(1);
        }
        printf("%d: received %d from child\n", getpid(), buffer[0]);

        close(cfd[0]);
        close(pfd[1]);
    }
    exit(0);
}