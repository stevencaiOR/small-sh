#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

//-- Global constants
#define ARGCOUNT 512    //-- Maximum supported number of arguments
#define CMDLENGTH 2048  //-- Maximum character length of command

//-- Struct to hold information regarding command
struct cmd{
    char * args[ARGCOUNT];          //-- Last argument is NULL
    int background;                 //-- 0 - foreground process, 1 - background process
    char inputFile[CMDLENGTH];
    char outputFile[CMDLENGTH];
};

//-- Global vars for SIGTSTP handler
volatile int SIGTSTP_count = 0;     //-- Tracks how many CTRL + Z's there were
volatile int fgRunning = 0;         //-- Boolean if foreground is running or not
volatile int SIGTSTP_sent = 0;      //-- Tracks if SIGTSTP was sent in middle of foreground process

/*
 * Function: processInput
 * ----------------------------------------------------------
 * Prompts user for input, performs comment detection and
 * variable expansion, then parses it into an arguments array.
 * 
 * cmd: cmd struct to be modified
 * 
 * returns: N/A
 */
void processInput(char * input, size_t inputSize, struct cmd * cmd){
    //-- Prompts user for input
    int currPidNum = getpid();
    char currPid[32];
    sprintf(currPid, "%d", currPidNum);
    printf(":");
    fflush(stdout);
    getline(&input, &inputSize, stdin);

    //-- Strips the newline character from input
    input[strcspn(input, "\n")] = 0;

    //-- Variable expansion - $$ -> current process id
    char * var = strstr(input, "$$");
    while(var){
        char * suffix = (char *) strdup(var + 2);
        char * prefix = strcpy(var, currPid);
        strcat(prefix, suffix);
        free(suffix);
        var = strstr(input, "$$");
    }
    
    //-- Command parsing
    char * arg = strtok(input, " ");
    int i = 0;
    while(arg != NULL && i < ARGCOUNT){
        if(strcmp(arg, "<") == 0){
            //-- Redirect to input file

            arg = strtok(NULL, " ");
            if(arg != NULL){
                strcpy(cmd->inputFile, arg);
                arg = strtok(NULL, " ");
            }
        } else if(strcmp(arg, ">") == 0){
            //-- Redirect to input file

            arg = strtok(NULL, " ");
            if(arg != NULL){
                strcpy(cmd->outputFile, arg);
                arg = strtok(NULL, " ");
            }
        } else if(strcmp(arg, "&") == 0){
            //-- Check if its last character

            arg = strtok(NULL, " "); //-- Sets arg to the next argument in the command
            if(arg == NULL && SIGTSTP_count % 2 == 0){
                cmd->background = 1;
            }
        } else if(strcmp(arg, "|") == 0 || strcmp(arg, "!") == 0){
            //-- Ignore these shell-specific operators and move on

            arg = strtok(NULL, " ");
        } else {
            cmd->args[i] = arg;
            arg = strtok(NULL, " ");
            i++;
        }
    }
}

/*
 * Function: displayStatus
 * ----------------------------------------------------------
 * Decodes the exit method of a process and prints it to the
 * screen.
 * 
 * exitMethod: encoded exit method of specified process
 * 
 * returns: N/A
 */
void displayStatus(int exitMethod){
    if(WIFEXITED(exitMethod)){
        //-- Foreground process exited normally

        printf("exit value %d\n", WIFEXITED(exitMethod));
        fflush(stdout);
    } else if(WIFSIGNALED(exitMethod)){
        //-- Foreground process killed by signal

        printf("terminated by signal %d\n", exitMethod);
        fflush(stdout);
    }
}

/*
 * Function: handleSIGTSTP
 * ----------------------------------------------------------
 * Handles the SIGTSTP signal (CTRL + Z) sent by user
 * 
 * returns: N/A
 */
void handleSIGTSTP(int signal){
    //-- Increment counter for SIGTSTP signals sent
    SIGTSTP_count++;

    if(fgRunning){
        //-- If foreground process is running
        
        SIGTSTP_sent = 1;   //-- Toggle on SIGTSTP sent
        return;             //-- Exit the function to handle this stuff later
    }

    //-- Print necessary signal message
    if(SIGTSTP_count % 2 == 1){
        write(STDOUT_FILENO, "Entering foreground-only mode (& is now ignored)\n:", 50);
    } else {
        write(STDOUT_FILENO, "Exiting foreground-only mode\n:", 30);
    }
    return;
}

/*
 * Function: main
 * ----------------------------------------------------------
 * Runs small shell
 * 
 * returns: 0
 */
int main(){
    //-- Foreground vars
    //-- This section contains information of the latest foreground process
    int fgExited = 0;           //-- indicates if any foreground process has ran and exited.
    int fgExitMethod;           //-- stores exit method of latest foreground process.

    //-- Background var
    int bgProcesses[256] = {};  //-- stores PID's of background processes

    //-- Signals
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};
    
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    SIGTSTP_action.sa_handler = handleSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;       //-- avoid nasty forever while loops (?)
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //-- Small shell implementation
    int run = 1; //-- probably not necessary, but feels wrong to do while(1)
    while(run){
        //-- Check for background processes
        for(int i = 0; i < 256; i++){
            int bgExitMethod;
            if(bgProcesses[i] > 0 && waitpid(bgProcesses[i], &bgExitMethod, WNOHANG) > 0){
                //-- If background process exists AND has completed

                printf("background pid %d is done: ", bgProcesses[i]);
                fflush(stdout);

                displayStatus(bgExitMethod);                            //-- Display status info on background process
                bgProcesses[i] = 0;                                     //-- Background process is completed, reset array element to 0
            }
        }
        
        //-- Input handling
        size_t inputSize = CMDLENGTH;
        char * input = (char *) malloc(inputSize + 1);
        struct cmd * cmd = malloc(sizeof(struct cmd));
        processInput(input, inputSize, cmd);
        
        //-- Process commands
        if(cmd->args[0] == NULL || cmd->args[0][0] == '#'){
            continue;
        } else if(strcmp(cmd->args[0], "exit") == 0){
            //-- Built-in command: exit

            pid_t currGpid = getpgrp(); //-- gets group ID of parent (also group ID of children by default)
            killpg(currGpid, SIGKILL);
            run = 0;
            return 0; //-- Deadchecks the shell
        } else if(strcmp(cmd->args[0], "status") == 0){
            //-- Built-in command: status

            if(fgExited){
                displayStatus(fgExitMethod);
            } else {
                printf("exit value 0\n");
                fflush(stdout);
            }
        } else if(strcmp(cmd->args[0], "cd") == 0){
            //-- Built-in command: cd

            if(cmd->args[1] == NULL){
                //-- No path -> use HOME environment

                char * homeEnv = getenv("HOME");
                chdir(homeEnv);
            } else {
                chdir(cmd->args[1]);
            }
        } else {
            //-- Custom commands

            pid_t pid = fork();
            switch(pid){
                case 0:
                    {
                        //-- Input file redirection
                        int sourceFD;
                        if(strcmp(cmd->inputFile, "")){
                            //-- Redirect stdin to read from input file
                            sourceFD = open(cmd->inputFile, O_RDONLY);
                            if(sourceFD == -1){ perror("source open()"); exit(1);}
                            int result = dup2(sourceFD, 0);
                            if(result == -1){ perror("source dup2()"); exit(2);}
                        } else if(cmd->background){
                            //-- Redirect stdin to read from /dev/null if it's a background process           
                            sourceFD = open("/dev/null", O_RDONLY);
                            if(sourceFD == -1){ perror("source open()"); exit(1);}
                            int result = dup2(sourceFD, 0);
                            if(result == -1){ perror("source dup2()"); exit(2);}
                        }
                        
                        //-- Output file redirection
                        int targetFD;
                        if(strcmp(cmd->outputFile, "")){
                            //-- Redirect stdout to output file
                            targetFD = open(cmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if(targetFD == -1){ perror("target open()"); exit(1);}
                            int result = dup2(targetFD, 1);
                            if(result == -1){ perror("target dup2()"); exit(2);}
                        } else if(cmd->background){
                            //-- Redirect stdout to /dev/null if it's a background process
                            targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if(targetFD == -1){ perror("target open()"); exit(1);}
                            int result = dup2(targetFD, 1);
                            if(result == -1){ perror("target dup2()"); exit(2);}
                        }
                        
                        //-- Signal handling
                        if(!cmd->background){
                            SIGINT_action.sa_handler = SIG_DFL;
                            sigaction(SIGINT, &SIGINT_action, NULL);
                        }
                        SIGTSTP_action.sa_handler = SIG_IGN;
                        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

                        //-- Custom command exectuion
                        execvp(cmd->args[0], cmd->args);
                        printf("bash: %s: command not found\n", cmd->args[0]);
                        fflush(stdout);
                        exit(1);
                        break;
                    }
                case -1:
                    perror("pid not found\n");
                    exit(-1);
                    break;
            }

            
            if(!cmd->background){
                //-- Foreground process

                fgRunning = 1;                  //-- Toggles on foreground process running 
                waitpid(pid, &fgExitMethod, 0); //-- Immediately updates fgExitMethod upon finishing
                fgRunning = 0;                  //-- Toggles off foreground process running
                
                if(WTERMSIG(fgExitMethod) == SIGINT){
                    printf("terminated by %d\n", WTERMSIG(fgExitMethod));
                    fflush(stdout);
                }
                if(SIGTSTP_sent){
                    //-- If SIGTSTP was sent in middle of foreground process

                    //-- Toggle off SIGTSTP sent
                    SIGTSTP_sent = 0;
                    
                    //-- Print necessary signal message
                    if(SIGTSTP_count % 2 == 1){
                        printf("Entering foreground-only mode (& is now ignored)\n");
                        fflush(stdout);
                    } else {
                        printf("Exiting foreground-only mode\n");
                        fflush(stdout);
                    }
                }
                fgExited = 1;
            } else {
                //-- Background process

                //-- Display background process PID
                printf("background pid is %d\n", pid);
                fflush(stdout);

                //-- Store background process PID in array
                for(int i = 0; i < 256; i++){
                    if(bgProcesses[i] == 0){
                        bgProcesses[i] = pid;
                        break;
                    }
                }
            }

            //-- End of command processing block
        }
        
        //-- End of giant while loop
    }

    return 0;
}