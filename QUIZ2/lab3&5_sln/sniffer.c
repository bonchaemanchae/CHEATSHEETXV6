// #include "kernel/types.h"
// #include "kernel/fcntl.h"
// #include "user/user.h"
// #include "kernel/riscv.h"

int main(int argc, char *argv[]) {
  char *hint = "This may help.";
  int hint_len = strlen(hint);  // 14

  int size = 128 * 4096;
  char *mem = sbrk(size);
  
  if(mem == (char *)-1) {
    printf("sbrk failed\n");
    exit(1);
  }

  for(int i = 0; i < size - 20; i++) {
    // Look for the hint followed by its null terminator
    int match = 1;
    for(int j = 0; j < hint_len; j++) {
      if(mem[i + j] != hint[j]) {
        match = 0;
        break;
      }
    }
    // Make sure it's actually null-terminated (real copy, not partial)
    if(match && mem[i + hint_len] != '\0') {
      match = 0;
    }

    if(match) {
      // The secret is at offset 16 from i
      // But first skip any null bytes after the hint to find actual data
      char *secret = mem + i + 16;
      
      // Validate: must be printable and not be "(null)"
      int valid = 1;
      int slen = 0;
      for(int k = 0; k < 256; k++) {
        char ch = secret[k];
        if(ch == '\0') break;
        if(ch < 32 || ch > 126) { valid = 0; break; }
        slen++;
      }

      // Skip if empty or if it's literally the string "(null)"
      if(valid && slen > 0 && strcmp(secret, "(null)") != 0) {
        printf("%s\n", secret);
        exit(0);
      }
    }
  }

  printf("Secret not found\n");
  exit(0);
}