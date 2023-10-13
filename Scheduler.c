// Modify the SimpleShell code
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

int NCPU , TSLICE;

typedef struct {
    int pid;
    int burst_time; // Time needed to complete the process
    int remaining_time; // Remaining time to complete the process
    int state; // Process state (e.g., READY, RUNNING, TERMINATED)
    char *executable;
} Process;

// Define a circular queue to store processes
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

void signal_start_execution(int signo) {
    if (signo == SIGSUR2) {
        while(1){
            // Start executing the processes
            if (itemCount != 0){
                int num_to_run = (itemCount < NCPU) ? itemCount : NCPU;
                for (int i = 0; i < num_to_run; i++) {
                    Process process = dequeue();
                    process.state = 1; // Set the state to RUNNING
                    // Notify SimpleShell to start the process
                    kill(process.pid, SIGUSR2);
                }
                // Simulate the time slice (TSLICE) by sleeping
                sleep(TSLICE);
                // Notify SimpleShell to stop the processes
                for (int i = 0; i < num_to_run; i++) {
                    Process process = dequeue();
                    process.state = 0; // Set the state to READY
                    // Notify SimpleShell to stop the process
                    kill(process.pid, SIGUSR2);
                }
            }
        }    
    }
}

// Signal handler to catch the signal sent by the SimpleShell
void signal_handler(int signo) {
    if (signo == SIGUSR1) {
        // Example:
        Process newProcess;
        // Extract the PID and executable
        NCPU = shared_data->NCPU;
        TSLICE = shared_data->TSLICE;
        newProcess.pid = shared_data->pid;
        strcpy(newProcess.executable, shared_data->executable);
        newProcess.state = 0; // Set the state to READY

        // Add the process to the ready queue
        enqueue(newProcess);
    }
}

int main(int argc, char* argv[]) {
    // ...


    // Set up signal handlers to catch signals from SimpleShell
    signal(SIGUSR1, signal_handler);
    signal(SIGUSR2, signal_start_execution);

    // Start the SimpleScheduler daemon as a separate process
    pid_t scheduler_pid = fork();

    if (scheduler_pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (scheduler_pid == 0) {
        // This code runs in the SimpleScheduler daemon
//
    //    // Execute the SimpleScheduler code here
        execl("./simplescheduler", "simplescheduler", NULL); // Use the correct path to the SimpleScheduler executable
        perror("execl");
        exit(EXIT_FAILURE);
    } else {
        shared_data->scheduler_pid = scheduler_pid;
    //    // This code runs in the SimpleShell
//          
    //    // Store the process ID of the SimpleScheduler daemon
    //    // You can use this ID to send signals to the SimpleScheduler
    //    // ...
//
    //    // Continue with the SimpleShell logic
    }

    // ...
    // Detach from shared memory
    shmdt(shared_memory);

    return 0;
}
