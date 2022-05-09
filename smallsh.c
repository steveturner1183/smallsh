#define _GNU_SOURCE
#define _POSIX_C_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CHARS 2048
#define MAX_ARGS 512
#define MAX_BG_PROCESS 50

struct command {
    char *arguments[MAX_ARGS];
    size_t totalArgs;
    int backgroundPIDs[MAX_BG_PROCESS];
    int bgIndex;
    char *inputFile;
    char *outputFile;
    bool foreground;
    struct sigaction SIGINT_action;
    bool foregroundOnly;
    struct sigaction SIGTSTP_action;
    char exitMessage[30];
    int exitValue; 
};

//sigset_t base_mask, waiting_mask;
volatile sig_atomic_t signalStatus;

/*****************************************************
*
*            Signaling
*
*
*
*
*
*****************************************************/
/*
void handle_SIGINT(int signo) {
	pid_t pid = getpid();
	char* message = "terminated by signal 2";
	write(STDOUT_FILENO, message, 23);
	//write(STDOUT_FILENO, &signo, 2);
	char *newline = "\n";
	write(STDOUT_FILENO, newline, 1);
	fflush(stdout);	
	
}*/

void handle_SIGTSTP(int signo) {
	if (signalStatus == 0) {
		char *message =  "Entering foreground-only mode (& is now ignored)\n:";
		write(STDOUT_FILENO, message, 50);
		signalStatus = 1;
	} else if (signalStatus == 1) {
		char *message = "Exiting foreground-only mode\n:";
		write(STDOUT_FILENO, message, 30);
		signalStatus = 0;
	}
}





/*****************************************************
*
*            expand $$
*
*
*
*
*
*****************************************************/
void expandPid(char* word, struct command *userCmd) {
	pid_t pid = getpid();
    	char *pidstr;
    
    	int n = snprintf(NULL, 0, "%jd", pid);
    	pidstr = malloc((n + 1) * sizeof *pidstr);
    	sprintf(pidstr, "%jd", pid);

    	char *temp = calloc(strlen(word), sizeof(char));
    	int tempIndex = 0;

    	for (size_t i =0; i< strlen(word); i++) {
        	if (word[i] == '$') {
        	    	if (i+1 < strlen(word) && word[i+1] == '$') {
                		strcat(temp, pidstr);
                		i++;
                		tempIndex += strlen(pidstr);
            		} else {
            			temp[tempIndex] = word[i];
            			tempIndex++;
        		}		
    		} else {
			temp[tempIndex] = word[i];
			tempIndex ++;
		}

	}
    	char* expandedWord = calloc(tempIndex, sizeof(char));
    	strncpy(expandedWord, temp, tempIndex);
    
    	free(temp);

    	userCmd->arguments[userCmd->totalArgs] = expandedWord;

}
/***************************************************
*
*            PROCESS INPUT
*
*
*
*
*
*****************************************************/

void processInput(struct command* userCmd) {
    
    bool inputCheck = false;
    
    while (inputCheck == false) {
        // Set / Reset initial values for command stuct
	memset(userCmd->arguments, 0, MAX_ARGS);
        userCmd->arguments[0] = NULL;
        userCmd->foreground = true;
	userCmd->totalArgs = 0;
        userCmd->inputFile = "/dev/null";
        userCmd->outputFile = "/dev/null";
 
	// Get user input
        char* inputBuf;
        size_t buf = 0;
        ssize_t args; 
        
        printf(":");
        fflush(stdout);
       	args = getline(&inputBuf, &buf, stdin);
 
	// Set foreground only if ctrl-Z was entered during prompt
	if (signalStatus == 0) {
		userCmd->foregroundOnly = false;
	} else if (signalStatus == 1) {
		userCmd->foregroundOnly = true;
	}       

        // Check chars entered vs max input
        if (args > MAX_CHARS) {
        	printf("\nToo many characters entered\n");
        	fflush(stdout);
        	free(inputBuf);
        } else {
		int count = 0;
        	char* input = strtok(inputBuf, " \n");		
		
            	if (input == NULL || input[0] == '#') { // Skip comments and blank lines
			free(inputBuf);
		} else {               
                    while (input != NULL && count <= MAX_ARGS) {
        	        if (strcmp(input, "<") == 0) {
                	input = strtok(NULL, " \n");
                	userCmd->inputFile = input;
                        input = strtok(NULL, " \n");
                        
                    } else if ((strcmp(input, ">") == 0)) {
                        input = strtok(NULL, " \n");
                        userCmd->outputFile = input;
                        input = strtok(NULL, " \n");
                        
                    } else if ((strcmp(input, "&") == 0)) {
                        input = strtok(NULL, " \n");
                        if (input == NULL && userCmd->foregroundOnly != true) {
                            userCmd->foreground = false;
                        }
                    } else {                      
                        expandPid(input, userCmd);
                        userCmd->totalArgs += 1;
                        input = strtok(NULL, " \n");
                    }
                    count += 1;
                }
                if (count < MAX_ARGS) {
                    	userCmd->arguments[userCmd->totalArgs] = NULL;
			inputCheck = true;
                } else {
                    printf("max args exceeded");
                    fflush(stdout);
                    free(inputBuf);
                }
		
            }
        } 
    }

}

/*****************************************************
*
*            EXIT
*
*
*
*
*
*****************************************************/
void exit_cmd(struct command* userCmd) {
	
	for (int pid=0; pid < userCmd->bgIndex; pid++) {
		kill(userCmd->backgroundPIDs[pid], SIGTERM);
	}

	exit(0);


}
// exit
    // kill all processes or jobs that have started, then exit shell
    // do not set exit status
    // Built-in commands occur in the shell, not in a child process, so an ampersand is meaningless to them.


// Built-in commands occur in the shell, not in a child process, so an ampersand is meaningless to them.

/*****************************************************
*
*            CD
*
*
*
*
*
*****************************************************/
void cd_cmd (struct command* userCmd) {
	if (userCmd->arguments[1] == 0) {
		int dirc = chdir(getenv("HOME"));
		if (dirc < 0) {
			perror(getenv("HOME"));
		}
		
	} else {
		int dirc = chdir(basename(userCmd->arguments[1]));
		if (dirc < 0) {
			perror(basename(userCmd->arguments[1]));
		}
	}
}

/*****************************************************
*
*            Status
*
*
*
*
*
*****************************************************/

void status_cmd(struct command* userCmd) {
    printf("%s %d\n", userCmd->exitMessage,userCmd->exitValue);
}

/*****************************************************
*
*            IO redirection
*
*
*
*
*
*****************************************************/
void ioRedirect(struct command* userCmd) {
	
	// Redirect input for all background processes, and foreground processes that specify an input file
	if (userCmd->foreground == false || (userCmd->foreground == true && strcmp(userCmd->inputFile, "/dev/null") != 0)) { 
		// Redirect stdin to /dev/null or inpt file, if specified
		int sourceFD = open(userCmd->inputFile, O_RDONLY);
		if (sourceFD == -1) {
			perror("input file");
			exit(1);
		}
	
		int result = dup2(sourceFD, 0);
		if (result == -1) {
			perror("dup2()");
			exit(2);
		}
	}
	

	// Redirect output for all background processes, and foreground processes that specify an output file
	if (userCmd->foreground == false || (userCmd->foreground == true && strcmp(userCmd->outputFile, "/dev/null") != 0)) { 
		// Redirect stdin to /dev/null or inpt file, if specified
		int targetFD = open(userCmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (targetFD == -1) {
			perror("output file");
			exit(1);
		}
	
		int result = dup2(targetFD, 1);
		if (result == -1) {
			perror("dup2()");
			exit(2);
		}
	}
}

/*****************************************************
*
*            Non Built in commands
*
*
*
*
*
*****************************************************/
void non_built_in_cmd(struct command* userCmd) {

	//char* newargv[] = {userCmd->command, userCmd->arguments};
	
	int childStatus;

	pid_t (spawnPid) = fork();

	switch(spawnPid) {
		case -1: // fork error
			perror("fork()\n");
			exit(1);
			break;
		case 0:  // child process	
			userCmd->SIGINT_action.sa_handler = SIG_DFL;
			sigaction(SIGINT, &userCmd->SIGINT_action, NULL);

			//userCmd->SIGTSTP_action.sa_handler = SIG_IGN;
			//sigaction(SIGTSTP, &userCmd->SIGTSTP_action, NULL);
			
			ioRedirect(userCmd);						
			
			execvp(userCmd->arguments[0], userCmd->arguments);
			printf("Invalid Command\n");
			fflush(stdout);
			exit(2);
			break;
		default: // parent process
			if (userCmd->foreground == true) {
				//userCmd->SIGINT_action.sa_handler = handle_SIGINT;
				//sigaction(SIGINT, &userCmd->SIGINT_action, NULL);

				spawnPid = waitpid(spawnPid, &childStatus, 0);
		
				if ((strcmp(userCmd->arguments[0], "cd") != 0) && (strcmp(userCmd->arguments[0], "exit") != 0) && (strcmp(userCmd->arguments[0], "status") != 0)) {
					memset(userCmd->exitMessage, 0, 30);
				
					// Child process exited successfully	
					if (WIFEXITED(childStatus)) {
						char *tempMess = "exit value";
						strcpy(userCmd->exitMessage, tempMess);			
						userCmd->exitValue = WEXITSTATUS(childStatus);
						
					} else {
						char *tempMess = "terminated by signal";
						strcpy(userCmd->exitMessage, tempMess);
						userCmd->exitValue = WTERMSIG(childStatus);
						printf("terminated by signal %d\n", WTERMSIG(childStatus));
						
					}
				}

				break;
			} else {		
				userCmd->backgroundPIDs[userCmd->bgIndex] = spawnPid; 
				userCmd->bgIndex += 1;
				printf("background pid is %d\n", spawnPid);
				fflush(stdout);			
		
				spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);	
			
				break;
			}
			exit(0);
	}

}

/*****************************************************
*
*            Reap background processes
*
*
*
*
*
*****************************************************/

void finishedBackground(struct command* userCmd) {

	int temp[MAX_BG_PROCESS] = {0};
	int tempIndex = 0;

	for (int pid = 0; pid < userCmd->bgIndex; pid++) {
		
		int childStatus;
		int childPid = waitpid(userCmd->backgroundPIDs[pid], &childStatus, WNOHANG);
	
		if (childPid > 0) {
			// Child process exited successfully	
			if (WIFEXITED(childStatus)) {
				printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(childStatus));
				fflush(stdout);
			} else {
				printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(childStatus));
				fflush(stdout);
			}
		} else { // Store ongoing processes
			temp[tempIndex] = userCmd->backgroundPIDs[pid];
			tempIndex += 1;
 		}		
	}

	memset(userCmd->backgroundPIDs, 0, MAX_BG_PROCESS);
	userCmd->bgIndex = tempIndex;
	
	for (int i; i < tempIndex; i++) {
		userCmd->backgroundPIDs[i] = temp[i];
	}
	
}

/*****************************************************
*
*            MAIN
*
*
*
*
*
*****************************************************/
int main (int argc, char *argv[]) {
	struct command userCmd;
	signalStatus = 0;
	//sigset_t base_mask, waiting_mask;
	//sigemptyset(&base_mask);
	//sigaddset(&base_mask, SIGTSTP);
	//sigprocmask(SIG_SETMASK, &base_mask, NULL);

	userCmd.exitValue = 0;
//	userCmd.SIGINT_action = {0};
	userCmd.SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&userCmd.SIGINT_action.sa_mask);
	userCmd.SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &userCmd.SIGINT_action, NULL);

	userCmd.SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&userCmd.SIGTSTP_action.sa_mask);
	userCmd.SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &userCmd.SIGTSTP_action, NULL);

	userCmd.bgIndex = 0;
 	memset(userCmd.backgroundPIDs, 0, MAX_BG_PROCESS);
	userCmd.foregroundOnly = false;
	
    	while(1) {
    	
	//sigpending(&waiting_mask);
	//if(sigismember (&waiting_mask, SIGTSTP)) {
	//	printf("Switched to foreground only");
	//	userCmd.foreground = true;
	//	
	//}

	userCmd.SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &userCmd.SIGINT_action, NULL);

	//userCmd.SIGTSTP_action.sa_handler = handle_SIGTSTP;
	//sigaction(SIGTSTP, &userCmd.SIGTSTP_action, NULL);

	// Reap background processes
        finishedBackground(&userCmd);
        
        // Gather user input and store in struct
        processInput(&userCmd); /* TO DO - expand $$ to process id*/

	
	if(userCmd.foregroundOnly) {
		userCmd.foreground = true;
	}

	if (strcmp(userCmd.arguments[0], "cd") == 0){
			cd_cmd(&userCmd);

	} else if (strcmp(userCmd.arguments[0], "exit") == 0) {
            exit_cmd(&userCmd);
            	
	} else if (strcmp(userCmd.arguments[0], "status") == 0) {
            status_cmd(&userCmd);

        } else {
            non_built_in_cmd(&userCmd);
        }
 
        // Restart loop
    }
    return 0;
}

