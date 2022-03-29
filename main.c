#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//0 = false, 1 = true
// Used to check for foreground only mode via cntrl-z
int foregroundOnlymode = 0;

void handle__SIGTSTP(int signo)
/* SIGSTSP Signal Handler
*  Handles the foreground only mode requirement
*  Toggles Global Variable foregroundOnlymode
*/
{
    if(foregroundOnlymode == 0)
    {
        foregroundOnlymode = 1;
        char *message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message, 53);
    }else
    {
        foregroundOnlymode = 0;
        char *message = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, 33);
    }
}

struct process
/**************************************************************************
*   Description -
*       DLL Node used to store a process PID
*       Used to kill zombie processes
*   -----------------------------------------------------------------------
*    int pid                - Stores the process pid
*    struct process *next   - Node Next Process
*    struct process *prev   - Node Previous Process
*
***************************************************************************/
{
    int pid;
    struct process *next;
    struct process *prev;
};

struct command
/**************************************************************************
*   Description -
*       User inputted command used to store information to be assessed in 
*       program
*       
*       command [arg1 arg2 ...] [< input_file] [> output_file] [&]
*
*   -----------------------------------------------------------------------
*       char *commandType       - command
*       char *arguements[512]   - [arg1 arg2 ...]
*       char *inputFile         - [< input_file]
*       char *outputFile        - [> output_file]
*       bool backGround         - [&]
*
***************************************************************************/
{
    char *commandType;
    char *arguements[512];
    char *inputFile;
    char *outputFile;
    bool backGround;
};

char *dollaDollaParse(char *string)
/***************************************************************************
*   Description -
*       Takes a buffer and converts all '$$' into process pid
*
*   -----------------------------------------------------------------------
*   Param - 
*       char *string            - String pointer [buffer] 
*
*   -----------------------------------------------------------------------
*   Returns
*      char *string             - Converted string
****************************************************************************/
{   
    // Atoi conversion to get string length
    int pid = getpid();
    int count = 0;
    do {
        pid /=10;
        ++count;
    } while (pid != 0);

    // Initialize and set pidString to current pid
    char pidString[count+1];           
    sprintf(pidString, "%d", getpid());

    // Counts number of $$ expansions required to set return string length
    int countDol = 0;
    for(int x = 0; x < strlen(string); x++)
    {
        if(string[x] == '$' && string[x+1] == '$')
        {
            countDol ++;
        }
    }

    // No conversion required
    if (countDol == 0)
    {
        return string;
    }

    // Initalize and set newString 
    int length = (countDol * (strlen(pidString))) + strlen(string);
    char newString[length];
    newString[length] = '\0';
    int stringPointer = 0;
    int newStringPointer = 0;

    // Iterate across string and copy all values over to newString
    // Exapnds all $$ to pid number
    while (newStringPointer != length)
    {
        if(string[stringPointer] == '$' && string[stringPointer+1] == '$')
        {
            for(int x = 0; x < strlen(pidString); x++)
            {
                newString[newStringPointer] = pidString[x];
                newStringPointer ++;
            }
            stringPointer ++;
            stringPointer ++;
        }
        else
        {
            newString[newStringPointer] = string[stringPointer];
            newStringPointer ++;
            stringPointer ++;
        }
    }

    // Transfer back to string to return to buffer 
    strcpy(string, newString);
    return string;
    
}

struct command *parseBuffer(char *buffer)
/***************************************************************************
*   Description -
*       Parses a user command and creates a command structre based off 
*       of user inputs 
*
*   -----------------------------------------------------------------------
*   Param - 
*       char *buffer            - Pointer to 'buffer' in main() function
*
*   -----------------------------------------------------------------------
*   Returns
*      command structre         - currCommand
****************************************************************************/
{
    //If buffer is blank
    if (strlen(buffer) == 0)
    {
        struct command *currCommand = malloc(sizeof(struct command));
        currCommand->commandType = calloc(5 + 1, sizeof(char));
        strcpy(currCommand->commandType, "Blank");
        return currCommand;
    }

    // Token intialization -> Used to break apart the user command
    struct command *currCommand = malloc(sizeof(struct command));
    char *saveptr;
    char *token = strtok_r(buffer, " ", &saveptr);

    // If buffer is blank or a comment
    if (token == NULL || token[0] == '#' || token[0] == '\0')
    {
        struct command *currCommand = malloc(sizeof(struct command));
        currCommand->commandType = calloc(5 + 1, sizeof(char));
        strcpy(currCommand->commandType, "Blank");
        return currCommand;
    }

    // Set commandType to first token (always exist, will always be a command)
    currCommand->commandType = calloc(strlen(token) + 1, sizeof(char));
    strcpy(currCommand->commandType, token);

    // Initialize loop set up 
    bool bArguements = true;    //Arguments set to true meaning we will look for these first
    bool inputFile = false;     // Input and output files set to false, will turn true if > or < is parsed
    bool outputFile = false;
    int i = 0;                  // Used for arguement count

    // Parse rest of the command until our saveptr is empty or null
    while((int)strlen(saveptr) != 0)
    {
        // set token to equal next word in command
        char *token = strtok_r(NULL, " ", &saveptr);
        // Break -> Avoid Segmentation Fault
        if(token == NULL)
        {
            break;
        }
        // Switch 'flags' to change next token assessment (such that we place input in Input and output to Output)
        if(strcmp(token, ">") == 0 || strcmp(token,"<") == 0 || (strcmp(token,"&") == 0 && (int)strlen(saveptr) == 0))
        {
            if(strcmp(token, "<") == 0)
            {
                inputFile = true;
                bArguements = false;
                continue;
            }
            else if(strcmp(token, ">") == 0)
            {
                outputFile = true;
                bArguements = false;
                continue;
            }
            else
            {
                // In the case that & is not last, we will not toggle background process
                currCommand->backGround = true;
                break;
            }
        }

        // Argument assessment
        if(inputFile == false && outputFile == false && bArguements == true && i < 512)
        {
            currCommand->arguements[i] = calloc(strlen(token)+1, sizeof(char));
            currCommand->arguements[i] = token;
            i ++;                                                                             
        }

        else if(inputFile == true)
        {
            currCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));   
            strcpy(currCommand->inputFile, token);
            inputFile = false;
        }

        else if(outputFile == true)
        {
            currCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->outputFile, token);
            outputFile = false;
        }
    }
    return currCommand;
}

void exitProcess(struct process *pointer)
/***************************************************************************
*   Description -
*       Exits our shell. It takes no arguments. When this command is run, 
*       our shell will kill any other processes or jobs that our shell has 
*       started before it terminates itself.
*
*   -----------------------------------------------------------------------
*   Param - 
*       struct process *pointer - MainProcess and linked list of processes
*
*   -----------------------------------------------------------------------
*   Returns
*      Breaks from loop to free() structures and EXITS program
****************************************************************************/
    {
        // Point to process after main
        pointer = pointer->next;
        // Iterates across processes and kills them
        while(pointer != NULL)
        {
            int pid = pointer->pid;
            int count = 0;
            do {
                pid /=10;
                ++count;
            } while (pid != 0);
            char pidString[count];
            sprintf(pidString, "%d", pointer->pid);
            kill(pointer->pid, SIGUSR1);
            pointer->prev->next = pointer->next;
            if(pointer->next != NULL)
            {
                pointer->next->prev = pointer->prev;    
            }
            pointer = pointer->next;
        }
    }

void cdProcess(struct command *ourCommand)
/***************************************************************************
*   Description -
*       The cd command changes the working directory of smallsh.
*
*       By itself - with no arguments - it changes to the directory specified 
*       in the HOME environment variable
*       This command can also take one argument: the path of a directory to 
*       change to. Your cd command should support both absolute and relative 
*       paths.
*
*   -----------------------------------------------------------------------
*   Param - 
*       struct command *ourCommand      - Carries our arguements
*
*   -----------------------------------------------------------------------
*   Returns
*      None
****************************************************************************/
{
    // No arguments == go home
    if(ourCommand->arguements[0] == NULL)
    {   
        chdir(getenv("HOME"));
        return;
    }

    // Initilize path
    char *buffer[2048];
    *buffer = getcwd(*buffer, sizeof(buffer));
    strcat(*buffer, "/");
    strcat(*buffer, ourCommand->arguements[0]);
    //Relative
    if (chdir(*buffer) == 0)
    {}
    //Absolute
    else if (chdir(ourCommand->arguements[0]) ==0 )
    {}
    else
    {
        printf("Attempt %s  or  %s\n", ourCommand->arguements[0], *buffer);
        printf("No such file or directory\n");
        fflush(stdout);
    }
}

void statusProcess(int* FGS)
/***************************************************************************
*   Description -
*   The status command prints out either the exit status or the terminating 
*   signal of the last foreground process ran by your shell.
*   
*    If this command is run before any foreground command is run, it'll 
*    return the exit status 0.
*    The three built-in shell commands do not count as foreground processes 
*
*   -----------------------------------------------------------------------
*   Param - 
*       int* FGS        - Last exit status of a foreground process
*
*   -----------------------------------------------------------------------
*   Returns
*       None
****************************************************************************/
    {
        printf("exit value %d\n", *FGS);
        fflush(stdout);
    }

void otherProcess(struct command *ourCommand, struct process *process, int* FGS, struct sigaction SIGINT_action, struct sigaction SIGTSTP_action)
/***************************************************************************
*   Description -
*   Whenever a non-built in command is received, the parent (i.e., smallsh) 
*   will fork off a child.
*   
*   The child will use a function from the exec() family of functions to 
*   run the command.
*   
*   Your shell should use the PATH variable to look for non-built in commands, 
*   and it should allow shell scripts to be executed
*   
*   If a command fails because the shell could not find the command to run, 
*   then the shell will print an error message and set the exit status to 1
*   
*   A child process must terminate after running a command 
*   (whether the command is successful or it fails). 
*
*   -----------------------------------------------------------------------
*   Param - 
*       struct command *ourCommand      - Command structre
*       struct process *process         - Process LL
*       int* FGS                        - Foreground exit status
*       struct sigaction SIGINT_action  - SIGINT handler
*       struct sigaction SIGTSTP_action - SIGTSTP handler
*
*   -----------------------------------------------------------------------
*   Returns
*       None
****************************************************************************/
{
    // Initialize arguements
    char *amper = "&";
    int size = 0;
    // Count arguements to set **arguement array size
    for(int x = 0; x < sizeof(ourCommand->arguements)/sizeof(ourCommand->arguements[0]); x++)
    {                                                      
        if(ourCommand->arguements[x] == NULL)
        {
            break;
        }
        size ++;
    }
    size = size + 2;
    char **arguement[size];

    // set our first arguement to the command
    arguement[0] = &ourCommand->commandType;

    // Iterate across our arguements in ourCommand and add to arguement
    for(int x = 1; x < size-1; x ++)
    {
        arguement[x] = &ourCommand->arguements[x-1];
    }

    // Include amper for background commands
    if(ourCommand->backGround == true && foregroundOnlymode == 0)
    {
        arguement[size-1] = &amper;
    }
    else
    {
    arguement[size-1] = NULL;
    }

    // Due to server crashes, program reattempts fork
    int attempts = 0;
    int childStatus;
    fflush(stdin);

    // Fork off to child process
    pid_t spawnPid = fork();

    // Due to server crashes, program reattempts fork
    while(spawnPid == -1 && attempts < 45)
    {
        sleep(1);
        spawnPid = fork();
        attempts ++;
        printf("bash: fork: retry: Resource temporarily unavailable - Attempting to reconnect\n");
        fflush(stdout);
    }

    // If fork fails
    if(spawnPid == -1)
    {

        perror("fork() failed!\n");
        fflush(stdout);
    }

    // If fork is sucessful
    else if(spawnPid == 0)
    {
        // Turn off all signal inputs for TSTP for child
        SIGTSTP_action.sa_handler = SIG_IGN;
        sigfillset(&SIGTSTP_action.sa_mask);
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

        // file input and output
        int sourceFD;
        int targetFD;
        if(ourCommand->inputFile != NULL)
        {
            sourceFD = open(ourCommand->inputFile, O_RDONLY);
            if (sourceFD == -1) 
            { 
                printf("cannot open %s for input\n", ourCommand->inputFile);
                exit(1); 
            }

            // Redirect stdin to source file
            int result = dup2(sourceFD, 0);
            if (result == -1) 
            { 
                perror("source dup2()"); 
                exit(2); 
            }
            
        }

        if(ourCommand->outputFile != NULL)
        {
            targetFD = open(ourCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (targetFD == -1) 
            { 
                printf("cannot open %s for output\n", ourCommand->outputFile); 
                exit(1); 
            }
        
            // Redirect stdout to target file
            int result = dup2(targetFD, 1);
            if (result == -1) 
            { 
                perror("target dup2()"); 
                exit(1); 
            }
        }

        // Check background status
        if(ourCommand->backGround == false)
        {
            SIGINT_action.sa_handler = SIG_DFL;
            sigfillset(&SIGINT_action.sa_mask);
            sigaction(SIGINT, &SIGINT_action, NULL);
        }

        // Execute command
        execvp(*arguement[0], *arguement);
        printf("no such file or directory\n");
        exit(1);
    }

    // Parent
    else
    {   
        //Background process, continue as normal
        if (ourCommand->backGround == true && foregroundOnlymode == 0)
        {
            // Add background process to LL
            while(process->next != NULL)
            {
                process = process->next;
            }
            struct process *newProcess = malloc(sizeof(struct process));
            newProcess->pid = spawnPid;
            newProcess->next = NULL;
            newProcess->prev = process;
            process->next = newProcess;
            printf("background pid is %d\n", newProcess->pid);
            fflush(stdout);

            spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);

        }
        // Foreground process, wait on execution
        else
        {
            spawnPid = waitpid(spawnPid, &childStatus, 0);
            if(WIFEXITED(childStatus) != 1)
            {
                printf("terminated by signal %d\n",  WTERMSIG(childStatus));
                fflush(stdout);
            }
            *FGS = WEXITSTATUS(childStatus);
        }

    }
}


int main(void){ 
/***************************************************************************
*   Description -
*       Init    
*           Set sigaction for SIGINT
*           Set sigaction for SIGTSTP
*           Create buffer for user command
*           Create process structure node and add main process to front
*           Init foreground (exit) status
*       Loop
*           Clear buffer
*           Get User input
*           Set SIGTSTP to ignore
*           Set up buffer expansion and send to dollaDollaParse
*           Create command structre with user input
*           Clear all zombie processes
*           Execute command depending on type (blank/comment, built in, other)
*           Set SIGTSTP to handler
*           Set SIGINT to ignore
*           Free pointer
*       On Exit
*           free processMain
*           free buffer
*
*   -----------------------------------------------------------------------
*   Param - 
*       None
*   -----------------------------------------------------------------------
*   Returns
*       None
****************************************************************************/

    //Set sigaction for SIGINT
    struct sigaction SIGINT_action = {{0}};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    //Set sigaction for SIGTSTP
    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = handle__SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //Create buffer for user command
    size_t len = 2048;
    char *buffer = malloc(len * sizeof(char));
    size_t input;

    //Create process structure node and add main process to front
    struct process *processMain = malloc(sizeof(struct process));
    processMain->pid = getpid();
    processMain->next = NULL;

    // Init foreground (exit) status
    int FGS = 0;
    
    do
    {   
        //Clear buffer
        for (int x = 0; x < 2048; x++)
        {
            buffer[x] = '\0';
        }

        // Get User input
        printf(": ");
        fflush(stdout);
        input = getline(&buffer, &len, stdin);

        // Set up buffer expansion and send to dollaDollaParse
        int size = (int) input;
        if (size == 1)
        {
           continue;
        }
        buffer[size] = '\0';
        buffer[strcspn(buffer, "\n")] = 0;
        buffer = dollaDollaParse(buffer);

        // Create command structre with user input
        struct command *ourCommand = parseBuffer(buffer);
        fflush(stdin);

        // Clear all zombie processes
        struct process *pointer = malloc(sizeof(struct process));
        pointer = processMain->next;
        while(pointer != NULL)
        {
            int childStatus;
            int PID = waitpid(pointer->pid, &childStatus, WNOHANG);
            if(PID == pointer->pid)
            {
                if(WIFEXITED(childStatus))
                {
                    printf("Child %d exited normally with status %d\n", pointer->pid, WEXITSTATUS(childStatus));
                } else
                {
                    printf("Child %d exited abnormally due to signal %d\n", pointer->pid, WTERMSIG(childStatus));
                }
                fflush(stdout);
                pointer->prev->next = pointer->next;
                if(pointer->next != NULL)
                {
                    pointer->next->prev = pointer->prev;    
                }
            }
            pointer = pointer->next;
        }
        
        // Execute command depending on type
        if(strcmp(ourCommand->commandType,"Blank") == 0)
        {
            continue;
        }
        // Built in
        else if (strcmp(ourCommand->commandType,"exit") == 0)
        {
            exitProcess(processMain);
            break;
        }
        // Built in
        else if (strcmp(ourCommand->commandType,"cd") == 0)
        {
            cdProcess(ourCommand);
        }
        // Built in
        else if (strcmp(ourCommand->commandType,"status") == 0)
        {
            statusProcess(&FGS);
        }
        // All other cases
        else
        {
            otherProcess(ourCommand, processMain, &FGS, SIGINT_action, SIGTSTP_action);
        }

        // Set SIGINT to ignore
        SIGINT_action.sa_handler = SIG_IGN;
        sigfillset(&SIGINT_action.sa_mask);
        sigaction(SIGINT, &SIGINT_action, NULL);

        free(pointer);
        fflush(stdout);
    } while (true);

    free(processMain);
    free(buffer);
    return 0;
}
