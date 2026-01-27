/* MY LAB ANSWER */
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

int
main(int argc, char *argv[]){
    if(argc < 2){
        fprintf(2, "did not pass sleep argument\n");
        exit(1);
    }

    int ticks = atoi(argv[1]);//ASCII to INT
    pause(ticks);
    exit(0);
}

/*MODIFICATIONS*/
/*PAUSE REPEATEDLY UNTIL TICKS RUNS OUT*/
int
main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: sleep ticks\n");
        exit(1);
    }
    int ticks = atoi(argv[1]);
    if (ticks <= 0) {
        printf("invalid ticks\n");
        exit(1);
    }
    for (int i = 0; i < ticks; i++) {
        pause(1); //pause 1 second
    }
    exit(0);
}
