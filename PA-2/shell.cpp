#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include <ctime>

#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m"

using namespace std;

// Global variable to track previous directory for "cd -"
string prev_dir = "";

// Vector to store background process PIDs
vector<pid_t> bg_processes;

// Function to reap background processes (non-blocking)
void reapBackgroundProcesses() {
    for (auto it = bg_processes.begin(); it != bg_processes.end(); ) {
        int status;
        pid_t result = waitpid(*it, &status, WNOHANG);
        if (result > 0) {
            // Process has finished
            it = bg_processes.erase(it);
        } else {
            ++it;
        }
    }
}

// Function to handle cd command
bool handleCD(Command* cmd) {
    if (cmd->args.size() < 2) {
        // cd with no arguments goes to home
        const char* home = getenv("HOME");
        if (home) {
            char cwd[1024];
            getcwd(cwd, sizeof(cwd));
            prev_dir = string(cwd);
            chdir(home);
        }
        return true;
    }
    
    string target = cmd->args[1];
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    
    if (target == "-") {
        // cd - goes to previous directory
        if (prev_dir.empty()) {
            cerr << "cd: OLDPWD not set" << endl;
            return true;
        }
        string temp = string(cwd);
        if (chdir(prev_dir.c_str()) < 0) {
            perror("cd");
        } else {
            cout << prev_dir << endl;  // Print where we're going TO
            prev_dir = temp;
        }
    } else {
        // Regular cd
        prev_dir = string(cwd);
        if (chdir(target.c_str()) < 0) {
            perror("cd");
        }
    }
    return true;
}

// Function to convert vector<string> to char* array for execvp
char** vectorToCharArray(const vector<string>& vec) {
    char** arr = new char*[vec.size() + 1];
    for (size_t i = 0; i < vec.size(); i++) {
        arr[i] = (char*)vec[i].c_str();
    }
    arr[vec.size()] = nullptr;
    return arr;
}

// Function to execute a single command or pipeline
void executeCommands(Tokenizer& tknr, int stdin_copy) {
    // Check if background process
    bool is_background = tknr.commands.back()->isBackground();
    for (auto cmd : tknr.commands) {
        if (cmd->isBackground()) {
            is_background = true;
            break;
        }
    }
    
    // Single command (no pipes)
    if (tknr.commands.size() == 1) {
        Command* cmd = tknr.commands.at(0);
        
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(2);
        }
        
        if (pid == 0) {
            // Child process
            
            // Handle input redirection
            if (cmd->hasInput()) {
                int fd = open(cmd->in_file.c_str(), O_RDONLY);
                if (fd < 0) {
                    perror("open input file");
                    exit(2);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            // Handle output redirection
            if (cmd->hasOutput()) {
                int fd = open(cmd->out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("open output file");
                    exit(2);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            // Execute command
            char** args = vectorToCharArray(cmd->args);
            if (execvp(args[0], args) < 0) {
                perror("execvp");
                exit(2);
            }
        } else {
            // Parent process
            if (is_background) {
                // Add to background processes list
                bg_processes.push_back(pid);
            } else {
                // Wait for foreground process
                int status = 0;
                waitpid(pid, &status, 0);
            }
        }
    }
    // Multiple commands (pipes)
    else {
        vector<pid_t> pids;
        int pipe_fd[2];
        int prev_pipe_read = -1;
        
        for (size_t i = 0; i < tknr.commands.size(); i++) {
            Command* cmd = tknr.commands.at(i);
            bool is_last = (i == tknr.commands.size() - 1);
            
            // Create pipe for all but last command
            if (!is_last) {
                if (pipe(pipe_fd) < 0) {
                    perror("pipe");
                    exit(2);
                }
            }
            
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(2);
            }
            
            if (pid == 0) {
                // Child process
                
                // Handle input from previous pipe or file
                if (i == 0) {
                    // First command - check for input redirection
                    if (cmd->hasInput()) {
                        int fd = open(cmd->in_file.c_str(), O_RDONLY);
                        if (fd < 0) {
                            perror("open input file");
                            exit(2);
                        }
                        dup2(fd, STDIN_FILENO);
                        close(fd);
                    }
                } else {
                    // Not first command - input comes from previous pipe
                    dup2(prev_pipe_read, STDIN_FILENO);
                    close(prev_pipe_read);
                }
                
                // Handle output to next pipe or file
                if (is_last) {
                    // Last command - check for output redirection
                    if (cmd->hasOutput()) {
                        int fd = open(cmd->out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd < 0) {
                            perror("open output file");
                            exit(2);
                        }
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }
                } else {
                    // Not last command - output goes to pipe
                    dup2(pipe_fd[1], STDOUT_FILENO);
                    close(pipe_fd[0]);
                    close(pipe_fd[1]);
                }
                
                // Execute command
                char** args = vectorToCharArray(cmd->args);
                if (execvp(args[0], args) < 0) {
                    perror("execvp");
                    exit(2);
                }
            } else {
                // Parent process
                pids.push_back(pid);
                
                // Close previous pipe read end
                if (prev_pipe_read != -1) {
                    close(prev_pipe_read);
                }
                
                // Close write end and save read end for next iteration
                if (!is_last) {
                    close(pipe_fd[1]);
                    prev_pipe_read = pipe_fd[0];
                }
            }
        }
        
        // Restore stdin in parent
        dup2(stdin_copy, STDIN_FILENO);
        
        // Wait for all processes
        if (is_background) {
            // Add all pids to background list
            for (pid_t pid : pids) {
                bg_processes.push_back(pid);
            }
        } else {
            // Wait for all foreground processes
            for (pid_t pid : pids) {
                int status = 0;
                waitpid(pid, &status, 0);
            }
        }
    }
}

int main() {
    // Save original stdin for later restoration
    int stdin_copy = dup(STDIN_FILENO);
    
    for (;;) {
        // Reap any finished background processes
        reapBackgroundProcesses();
        
        // Get current time
        time_t now = time(nullptr);
        string time_str = ctime(&now);
        // Format: "Mon DD HH:MM:SS" from "Day Mon DD HH:MM:SS YYYY\n"
        string timestamp = time_str.substr(4, 15);
        
        // Get username
        char* user = getenv("USER");
        string username = user ? string(user) : "root";
        
        // Get current directory
        char cwd[1024];
        string current_dir;
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            perror("getcwd");
            current_dir = "unknown";
        } else {
            current_dir = string(cwd);
        }
        
        // Print custom prompt
        cout << timestamp << " " << username << ":" << current_dir << "$";
        
        // Get user inputted command
        string input;
        getline(cin, input);
        
        // Check for EOF or exit
        if (cin.eof()) {
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }
        
        // Skip empty input
        if (input.empty()) {
            continue;
        }
        
        // Handle command chaining with && and ;
        vector<string> commands;
        bool should_exit = false;
        
        // First split by semicolon, then each part by &&
        vector<string> semi_parts;
        string temp = input;
        size_t pos = 0;
        
        while ((pos = temp.find(";")) != string::npos) {
            string part = temp.substr(0, pos);
            if (!part.empty()) {
                semi_parts.push_back(part);
            }
            temp = temp.substr(pos + 1);
        }
        if (!temp.empty()) {
            semi_parts.push_back(temp);
        }
        
        // Now split each semicolon part by &&
        for (const string& semi_part : semi_parts) {
            temp = semi_part;
            pos = 0;
            while ((pos = temp.find("&&")) != string::npos) {
                string cmd = temp.substr(0, pos);
                if (!cmd.empty()) {
                    commands.push_back(cmd);
                }
                temp = temp.substr(pos + 2);
            }
            if (!temp.empty()) {
                commands.push_back(temp);
            }
        }
        
        // Execute each command
        for (const string& cmd : commands) {
            // Trim whitespace
            string trimmed = cmd;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            size_t end = trimmed.find_last_not_of(" \t\n\r");
            if (start != string::npos && end != string::npos) {
                trimmed = trimmed.substr(start, end - start + 1);
            }
            
            if (trimmed.empty()) continue;
            
            // Check for exit command
            if (trimmed == "exit") {
                should_exit = true;
                break;
            }
            
            // Get tokenized commands from user input
            Tokenizer tknr(trimmed);
            if (tknr.hasError()) {
                continue;
            }
            
            // Handle cd command (no fork needed)
            if (tknr.commands.at(0)->args.at(0) == "cd") {
                handleCD(tknr.commands.at(0));
                continue;
            }
            
            // Handle pwd command
            if (tknr.commands.at(0)->args.at(0) == "pwd") {
                char cwd_buf[1024];
                if (getcwd(cwd_buf, sizeof(cwd_buf))) {
                    cout << cwd_buf << endl;
                }
                continue;
            }
            
            // Execute commands
            executeCommands(tknr, stdin_copy);
        }
        
        // Check if we should exit after processing all commands
        if (should_exit) {
            cout << endl <<  RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }
    }
    
    // Clean up
    close(stdin_copy);
    return 0;
}
