int
main(int argc, char *argv[])
{
    int rc = fork();
    if (rc < 0) {
        // fork failed; exit
        fprintf(2, "fork failed\n");
        exit(1);
    } else if (rc == 0) {
	// child: redirect standard output to a file
	close(1); 
	open("./p4.output", O_CREATE|O_WRONLY|O_TRUNC);

	// now exec "wc"...
        char *myargs[3];
        myargs[0] = "wc";   // program: "wc" (word count)
        myargs[1] = "README"; // argument: file to count
        myargs[2] = 0;           // marks end of array
        exec(myargs[0], myargs);  // runs word count
        // myargs[0] is the program to exec? wc is loads word count
    } else {
        // parent goes down this path (original process)
        wait(0);
    }
    exit(0);
}