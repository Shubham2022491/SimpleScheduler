#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>



typedef struct {
    int pid;
    int burst_time; // Time needed to complete the process
    int remaining_time; // Remaining time to complete the process
    int state; // Process state (e.g., READY, RUNNING, TERMINATED)
    char *executable;
} Process;


// Define a circular queue to store progcesses
Process ready_queue[MAX_QUEUE_SIZE];
int front = 0;
int rear = -1;
int itemCount = 0;



// Attach to the shared memory segment (shm_id is the shared memory ID)
void* shared_memory = shmat(shm_id, NULL, 0);

struct {
    pid_t pid;
    char executable[256]; // Adjust the buffer size as needed
    int NCPU;
    int TSLICE;
    int scheduler_pid; 
    //int signal_start; // Signal to start a process
    //int signal_stop;  // Signal to stop a process                                                                                 //  dbt what will be the data type 
} *shared_data = (struct shared_data*)shared_memory;


int status_pipe[2]; // Pipe for sending process status to the scheduler


// Function to add a process to the ready queue
void enqueue(Process process) {
    if (itemCount < MAX_QUEUE_SIZE) {
        if (rear == MAX_QUEUE_SIZE - 1)
            rear = -1;
        ready_queue[++rear] = process;
        itemCount++;
    }
}

// Function to remove and return a process from the front of the queue
Process dequeue() {
    Process data = ready_queue[front++];
    if (front == MAX_QUEUE_SIZE)
        front = 0;
    itemCount--;
    return data;
}

// Function to send a custom signal to the SimpleScheduler to add a process to the ready queue
void add_to_ready_queue(Process newProcess ,int NCPU ,int TSLICE) {
    // Define the custom signal to be used for communication
    int custom_signal = SIGUSR1;
    shared_data->pid = newProcess.pid; // The PID of the new process
    strcpy(shared_data->executable, newProcess.executable);
    shared_data->NCPU = NCPU;
    shared_data->TSLICE = TSLICE;

    // Send the custom signal to the SimpleScheduler process (using the stored scheduler_pid)
    if (kill(shared_data->scheduler_pid, custom_signal) == -1) {
        perror("kill");
    }
}

void start_execution(int signo) {
    if (signo == SIGUSR2) {
        // Create a pipe for communication
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        pid_t child_pid = fork();

        if (child_pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (child_pid == 0) {
            // This code runs in the child process
            close(pipefd[0]);  // Close the read end of the pipe

            // Execute the user program here
            execl(executable, executable, NULL);
            perror("execl");
            exit(EXIT_FAILURE);
        } else {
            // This code runs in the parent process
            close(pipefd[1]);  // Close the write end of the pipe

            int status;
            // Wait for the child process to complete and get its status
            waitpid(child_pid, &status, 0);

            // You can write the status to the pipe so that the parent can read it
            
            close(status_pipe[1]);

                // Write the status to the pipe
            write(status_pipe[0], &status, sizeof(status));
        }
    }
}


int custom_signal = SIGUSR2;

int main(int argc, char* argv[]) {
    if (kill(shared_data->scheduler_pid, custom_signal) == -1) {
        perror("kill");
        // Handle any errors, if necessary
    }

     while (1) {
        char command[100];
        printf("SimpleShell$ ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0'; // Remove the trailing newline

        // When a user submits a program using the "submit" command, send a request to add
        // the program to the ready queue in the SimpleScheduler
        if (strncmp(command, "submit ", 7) == 0) {
            char* executable = command + 7; // Extract the executable name

            // Create a new process and add it to the process table
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                return 1;
            } else if (pid == 0) {
                // This code runs in the child process
                signal(SIGUSR2, start_execution);

                // Wait for a signal from the SimpleScheduler to start execution
                pause();

                // Execute the user program here
                
            } else {
                // This code runs in the parent process

                // Create a new process entry in the process table
                Process newProcess;
                newProcess.pid = pid;
                newProcess.executable = strdup(executable); // Duplicate the executable string
                newProcess.state = 0; // Set the state to READY

                // Add the process to the ready queue
                enqueue(newProcess);

                // Send a signal to the SimpleScheduler to add the process to the ready queue
                add_to_ready_queue(newProcess,argv[0],argv[1]);
            }
        }

        // Add code for other shell commands and functionalities here
    }
    // Detach from shared memory
    shmdt(shared_memory);

    return 0;
}
