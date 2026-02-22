// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: monitor mask command [args...]\n");
    exit(1);
  }

  int mask = atoi(argv[1]); // take integer mask
  monitor(mask);

  // exec the command starting at argv[2]
  exec(argv[2], &argv[2]);

  // if exec returns, it failed
  fprintf(2, "monitor: exec %s failed\n", argv[2]);
  exit(1);
}
