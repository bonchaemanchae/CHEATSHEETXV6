/*
file processing utility, opens 1 or more files, reads char by char, 
extracts sequences of numbers and prints them only if divisble by 5 or 6
*/
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "kernel/fcntl.h"
// #include "user/user.h"

//string of characters that breaks a number
static char *separator = " -\r\t\n./,"; 

//uses strchr to see if char exists within defined separator string,
//if strchr returns pointer and not a '0', meaning c is a separator
static inline int is_separator(char c){
  return strchr(separator, c) != 0;
}

int main(int argc, char *argv[]){
  if (argc < 2) {
    fprintf(2, "Unable to process file\n");
    exit(1);
  }

  char c; // stores per char from txt file
  char numbuf[64]; // size 64, can store 63 actual digits,
  int idx; // position in numbuf

  for (int i = 1; i < argc; i++) { //iterates though every filename passed in cmd
    int fd = open(argv[i], 0); //open in read-only mode, upon success read, fd will be positive int
    if (fd < 0) {
        fprintf(2, "Error in opening %s\n", argv[i]);
        continue;
    }

    idx = 0; //resets buffer index for new file
    while (read(fd, &c, 1) == 1) { //reading one byte at a time from opened file, &c refers to user space buffer
        if (c >= '0' && c <= '9') { //reading per byte char, checking if digit, numbuf will ever only have digits
          if (idx < sizeof(numbuf) - 1) //prevents buffer overflow, if idx less than 63, can add character
              numbuf[idx++] = c; //numbuf contains ['c1','c2',..]
          continue;
        }
        if (idx > 0 && is_separator(c)) {
          numbuf[idx] = 0; //setting next available slot to \0
          int n = atoi(numbuf);
          if (n % 5 == 0 || n % 6 == 0)
              printf("%d\n", n);
        }
        idx = 0;
    }

    // EOF
    /*
    if file ends with digit, and no separator at the end
    when loop finishes, last number will still be in numbuf and this
    code processes that final number
    */
    if (idx > 0) {
        numbuf[idx] = 0;
        int n = atoi(numbuf);
        if (n % 5 == 0 || n % 6 == 0)
        printf("%d\n", n);
    }
    close(fd);
  }
  exit(0);
}

/* difference between file read and pipe read
- only need to pipe when its inter communication between parent and child
file read refers to reading from actual file on disk
pipe read fd refers to memory buffer inside kernel, if buffer empty, read sleeps process
*/