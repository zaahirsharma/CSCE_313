/****************
LE2: Introduction to Unnamed Pipes
****************/
#include <unistd.h> // pipe, fork, dup2, execvp, close
#include <iostream>
using namespace std;

int main () {
    // lists all the files in the root directory in the long format
    char* cmd1[] = {(char*) "ls", (char*) "-al", (char*) "/", nullptr};
    // translates all input from lowercase to uppercase
    char* cmd2[] = {(char*) "tr", (char*) "a-z", (char*) "A-Z", nullptr};

    // TODO: add functionality
    // Create pipe
    int pipefd[2];
    pipe(pipefd);

    //Store original input and output file descriptors of parent
    int original_stdin = dup(STDIN_FILENO);
    int original_stdout = dup(STDOUT_FILENO);



    // Create child to run first command
    pid_t pid1 = fork();

    if (pid1 < 0) {
    	// Fork failed
	return 1;
    }
    // In child, redirect output to write end of pipe
    if (pid1 == 0){
	dup2(pipefd[1], STDOUT_FILENO);
	// Close the read end of the pipe on the child side.
	close(pipefd[0]);
    	// In child, execute the command
	execvp(cmd1[0], cmd1);
    }

    // Create another child to run second command
    pid_t pid2 = fork();

    if (pid2 < 0) {
	// Fork failed
	return 1;
    }

    // In child, redirect input to the read end of the pipe
    if (pid2 == 0) {
	dup2(pipefd[0], STDIN_FILENO);
    // Close the write end of the pipe on the child side.
	close(pipefd[1]);
    // Execute the second command.
	execvp(cmd2[0], cmd2);
    }
    // Reset the input and output file descriptors of the parent.
    dup2(original_stdin, STDIN_FILENO);
    dup2(original_stdout, STDOUT_FILENO);
}
