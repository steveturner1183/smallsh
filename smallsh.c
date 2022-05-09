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

// Struct used for storing data related to a user command
struct command {
    char *arguments[MAX_ARGS];
    size_t totalArgs;
    int backgroundPIDs[MAX_BG_PROCESS];
    int bgIndex;
    char *inputFile;
    char *outputFile;
    bool foreground;
	bool foregroundOnly;
    struct sigaction SIGINT_action;
    struct sigaction SIGTSTP_action;
    char exitMessage[30];
    int exitValue; 
};

volatile sig_atomic_t signalStatus;


/*****************************************************************************************
*	Function: handle_SIGSTP
*   
*	- Handle SIGSTP signal (CTRL-Z); use signal to switch to foreground-only mode on/off.
*	
*	@param signo	signal number
*
*****************************************************************************************/

void handle_SIGTSTP(int signo) {

	// Switch to foreground-only
	if (signalStatus == 0) {
		char *message =  "Entering foreground-only mode (& is now ignored)\n:";
		write(STDOUT_FILENO, message, 50);
		signalStatus = 1;

	// Switch off foregroun-only
	} else if (signalStatus == 1) {
		char *message = "Exiting foreground-only mode\n:";
		write(STDOUT_FILENO, message, 30);
		signalStatus = 0;
	}
}


/*****************************************************************************************
*	Function: expandPid
*
*	- Parse given string and expands any instances of $$ to the current PID
*
*	@param word		string to parse
*	@param userCmd	pointer to structure holding command information
*
*****************************************************************************************/

void expandPid(char* word, struct command *userCmd) {

	// Convert pid to string
	// Code taken from OSU CS344 Ed discussion post #355
	pid_t pid = getpid();
	char *pidstr;

	int n = snprintf(NULL, 0, "%jd", pid);
	pidstr = malloc((n + 1) * sizeof *pidstr);
	sprintf(pidstr, "%jd", pid);

	char *temp = calloc(strlen(word), sizeof(char));

	int tempIndex = 0;

	// Search for $$ and replace with PID if found
	for (size_t i =0; i< strlen(word); i++) {
		if (word[i] == '$') {
				
				if (i+1 < strlen(word) && word[i+1] == '$') { // $$ found -> expand
					strcat(temp, pidstr);
					i++;
					tempIndex += strlen(pidstr);
			
				} else {  // Single $, store char
					temp[tempIndex] = word[i];
					tempIndex++;
			}
		 
		} else { // Non $, Store char
		temp[tempIndex] = word[i];
		tempIndex ++;
	}

}
	// Store input for later processing
	char* expandedWord = calloc(tempIndex, sizeof(char));
	strncpy(expandedWord, temp, tempIndex);
	free(temp);
	userCmd->arguments[userCmd->totalArgs] = expandedWord;

}


/***************************************************************************************
*	Function: processInput
*            
*	- Prompt user for command line input
*	- Validate input (Max chars, max arguments, empty command, comment)
*	- Store command, arguments, i/o files, & (background), as applicable
*
*	@param userCmd	pointer to structure holding command information
*
*****************************************************************************************/

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
		if (signalStatus == 0) {  // foreground-only OFF
			userCmd->foregroundOnly = false;
		} else if (signalStatus == 1) { // foreground-only ON
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
					if (strcmp(input, "<") == 0) { // Input redirection
						input = strtok(NULL, " \n");
						userCmd->inputFile = input;
						input = strtok(NULL, " \n");
						
					} else if ((strcmp(input, ">") == 0)) {  // Output redirection
						input = strtok(NULL, " \n");
						userCmd->outputFile = input;
						input = strtok(NULL, " \n");
						
					} else if ((strcmp(input, "&") == 0)) {  // Background process requested
						input = strtok(NULL, " \n");
						if (input == NULL && userCmd->foregroundOnly != true) {
							userCmd->foreground = false; // ignore &; foreground-only
						}

					} else {  // store command or arument                  
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

/*****************************************************************************************
*	Function: exit_cmd
*            
*	- Kill background processes
*	- exit shell
*
* 	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/

void exit_cmd(struct command* userCmd) {
	
	// kill unfinished / zombie background processes
	for (int pid=0; pid < userCmd->bgIndex; pid++) {
		kill(userCmd->backgroundPIDs[pid], SIGTERM);
	}

	exit(0);
}


/*****************************************************************************************
*	Function: cd_cmd
*            
*	- Change current working directory
*	- If no arguments were given, change directory to home
*
*	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/

void cd_cmd (struct command* userCmd) {

	// No argument given, change to home directory
	if (userCmd->arguments[1] == 0) {
		int dirc = chdir(getenv("HOME"));
		if (dirc < 0) {
			perror(getenv("HOME"));
		}
	
	// Change directory to given argument
	} else {
		int dirc = chdir(basename(userCmd->arguments[1]));
		if (dirc < 0) {
			perror(basename(userCmd->arguments[1]));
		}
	}
}


/*****************************************************************************************
*	Function: status_cmd
*            
*	- Display exit status of last run foregruond process
*	- Status set after completion of each foregruond process
*
*	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/

void status_cmd(struct command* userCmd) {
    printf("%s %d\n", userCmd->exitMessage,userCmd->exitValue);
}

/*****************************************************************************************
*	Function ioRedirect
*   
*	- Redirect i/o for background process to /dev/null or specified file (if given)
*	- Redirect i/o for foregruond process to specified file (if given)
*
*	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/

void ioRedirect(struct command* userCmd) {
	
	/**** INPUT ****/
	
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
	
	/**** OUTPUT ****/

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

/*****************************************************************************************
*	Function: nonBuitIn
*   
*	- Handle all commands except cd, exit, and status
*	- Command is executed through forked child and execv
*
*	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/
void nonBuiltIn(struct command* userCmd) {

	int childStatus;
	pid_t (spawnPid) = fork();

	switch(spawnPid) {

		case -1: // fork error
			perror("fork()\n");
			exit(1);
			break;

		case 0:  // child process
			// Terminate foregound process with SIGSTP (CTRL-C)
			if (userCmd->foreground == true) {
				userCmd->SIGINT_action.sa_handler = SIG_DFL;
				sigaction(SIGINT, &userCmd->SIGINT_action, NULL);
			}

			ioRedirect(userCmd); // Set redirection for child process
			
			execvp(userCmd->arguments[0], userCmd->arguments);
			
			// execvp fail - command is not in /bin
			printf("Invalid Command\n");
			fflush(stdout);
			exit(2);
			break;

		default: // parent process

			// Run command as foreground process
			if (userCmd->foreground == true) {

				spawnPid = waitpid(spawnPid, &childStatus, 0);
		
				memset(userCmd->exitMessage, 0, 30);
				
				// Child process exited successfully	
				if (WIFEXITED(childStatus)) {
					char *tempMess = "exit value";
					strcpy(userCmd->exitMessage, tempMess);			
					userCmd->exitValue = WEXITSTATUS(childStatus);
					fflush(stdout);	
				
				// Child process terminated by signal
				} else {
					char *tempMess = "terminated by signal";
					strcpy(userCmd->exitMessage, tempMess);
					userCmd->exitValue = WTERMSIG(childStatus);
					printf("terminated by signal %d\n", WTERMSIG(childStatus));
					fflush(stdout);						
				}

				break;

			// Run command as backgruond process
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

/*****************************************************************************************
*	Function finishedBackground
*           
*	- Check for finished background processes prior to prompting user for next input
*
*	@param userCmd		pointer to structure holding command information
*
*****************************************************************************************/

void finishedBackground(struct command* userCmd) {

	int temp[MAX_BG_PROCESS] = {0};
	int tempIndex = 0;

	// Check all outstanding background processes
	for (int pid = 0; pid < userCmd->bgIndex; pid++) {
		
		int childStatus;
		int childPid = waitpid(userCmd->backgroundPIDs[pid], &childStatus, WNOHANG);
	
		if (childPid > 0) {
			// Child process exited successfully	
			if (WIFEXITED(childStatus)) {
				printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(childStatus));
				fflush(stdout);

			// Background process terminated by signal
			} else {
				printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(childStatus));
				fflush(stdout);
			}
		} else { 
			temp[tempIndex] = userCmd->backgroundPIDs[pid];
			tempIndex += 1;
		}
	}

	// Store ongoing processes and  update count
	memset(userCmd->backgroundPIDs, 0, MAX_BG_PROCESS);
	userCmd->bgIndex = tempIndex;
	
	for (int i; i < tempIndex; i++) {
		userCmd->backgroundPIDs[i] = temp[i];
	}
	
}

int main (int argc, char *argv[]) {

	// Initialize struct that while hold all smallsh commands
	struct command userCmd;
	userCmd.exitValue = 0;
	char *tempMess = "exit value";
	strcpy(userCmd.exitMessage, tempMess);	

	userCmd.bgIndex = 0;
	userCmd.foregroundOnly = false;
	memset(userCmd.backgroundPIDs, 0, MAX_BG_PROCESS);

	signalStatus = 0;

	// Initizlize SIGINT to be ignored, only set in non built in command foregruond process
	userCmd.SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&userCmd.SIGINT_action.sa_mask);
	userCmd.SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &userCmd.SIGINT_action, NULL);

	// Initialize SIGTSTP signal handler to switch foreground-only mode on/off
	userCmd.SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&userCmd.SIGTSTP_action.sa_mask);
	userCmd.SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &userCmd.SIGTSTP_action, NULL);
	
    while(1) {
		// Set / reset SIGINT action
		userCmd.SIGINT_action.sa_handler = SIG_IGN;
		sigaction(SIGINT, &userCmd.SIGINT_action, NULL);

		// Reap completed background processes
		//finishedBackground(&userCmd);
			
		// Gather user input and store in command struct
		processInput(&userCmd);

		// Override to foreground only process if foreground-only mode set
		if(userCmd.foregroundOnly) {
			userCmd.foreground = true;
		}

		// Run commands

		// cd command
		if (strcmp(userCmd.arguments[0], "cd") == 0){
				cd_cmd(&userCmd);

		// exit command
		} else if (strcmp(userCmd.arguments[0], "exit") == 0) {
				exit_cmd(&userCmd);
		
		// status command
		} else if (strcmp(userCmd.arguments[0], "status") == 0) {
				status_cmd(&userCmd);

		// All other valid commands
		} else {
				nonBuiltIn(&userCmd);
		}
    }
    return 0;
}

