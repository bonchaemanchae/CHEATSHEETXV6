// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "kernel/param.h"
// #include "user/user.h"

#define MAX_LINE_LEN 512
#define MAXARG 32

int readline(char *buffer) {
  char *p = buffer;
  while (read(0, p, 1) == 1) {
    if (*p == '\n') {
      *p = 0;
      return 1;
    }
    p++;
  }
  return (p != buffer);
}

int main(int argc, char *argv[]) {
  char *cmd_args[MAXARG]; 
  char line[MAX_LINE_LEN];
  int n = -1;
  int cmd_start = 1;

  // -n parsing checker
  if (argc > 3 && strcmp(argv[1], "-n") == 0) {
    n = atoi(argv[2]);
    cmd_start = 3;
  }

  int i = 0;
  for (int j = cmd_start; j < argc; j++) {
    cmd_args[i++] = argv[j];
  }

  int cmd_idx = i;
  int count = 0;
  while (readline(line)) {
    char *p = line;
    while (*p) {
      while (*p == ' ') p++; 
      if (*p == 0) break;

      char *arg_start = p;
      while (*p && *p != ' ') p++;
      if (*p) { *p = 0; p++; }

      // malloc to store args, so not overwritten by newline
      cmd_args[i] = malloc(strlen(arg_start) + 1);
      strcpy(cmd_args[i], arg_start);
      i++;
      count++;

      // -n grouping
      if (n != -1 && count == n) {
        cmd_args[i] = 0;
        if (fork() == 0) {
          exec(cmd_args[0], cmd_args);
          exit(1);
        }
        wait(0);
        // clean up malloc'd strings, memory leaks
        for(int k = cmd_idx; k < i; k++) free(cmd_args[k]);
        i = cmd_idx;
        count = 0;
      }
    }
  }

  if (count > 0) {
    cmd_args[i] = 0;
    if (fork() == 0) {
      exec(cmd_args[0], cmd_args);
      exit(1);
    }
    wait(0);
  }
  exit(0);
}