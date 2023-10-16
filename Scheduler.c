#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>

#define MAX_QUEUE_SIZE 100
#define MAX_EXECUTABLE_LENGTH 256

sem_t readyqueue_mutex;
sem_t runningqueue_mutex;
sem_t terminatedqueue_mutex;

int NCPU, TSLICE;

typedef struct {
    int pid;
    int remaining_time;
    int state;
    char executable[MAX_EXECUTABLE_LENGTH];
} Process;

typedef struct {
    Process queue[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int itemCount;
} SharedQueue;

SharedQueue *readyqueue;
SharedQueue *runningqueue;
SharedQueue *terminatedqueue;

void start_execution(int signo) {
    if (signo == SIGUSR2) {
        int front = readyqueue->front;
        char *s = readyqueue->queue[front].executable;
        execlp("sh", "sh", "-c", s, NULL);
    }
}

int fd_ready, fd_running, fd_terminated;

void terminate_shell(int signo) {
    printf("Processes in the terminated queue:\n");
    if (signo == SIGINT) {
        while (terminatedqueue->itemCount > 0) {
            Process terminatedProcess = terminatedqueue->queue[++terminatedqueue->front];
            printf("PID: %d, Executable: %s\n", terminatedProcess.pid, terminatedProcess.executable);
            terminatedqueue->itemCount--;
        }

        munmap(readyqueue, sizeof(SharedQueue));
        munmap(runningqueue, sizeof(SharedQueue));
        munmap(terminatedqueue, sizeof(SharedQueue));

        close(fd_ready);
        close(fd_running);
        close(fd_terminated);

        shm_unlink("/readyqueue");
        shm_unlink("/runningqueue");
        shm_unlink("/terminatedqueue");

        exit(EXIT_SUCCESS);
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);
    int pid;
    printf("Command line inputs stored: %d %d\n", NCPU, TSLICE);
    int custom_signal = SIGUSR2;

    int fd_ready = shm_open("/readyqueue", O_CREAT | O_RDWR, 0666);
    if (fd_ready == -1) {
        perror("shm_open readyqueue");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(fd_ready, sizeof(SharedQueue)) == -1) {
        perror("ftruncate readyqueue");
        exit(EXIT_FAILURE);
    }

    readyqueue = (SharedQueue *)mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd_ready, 0);
    if (readyqueue == MAP_FAILED) {
        perror("mmap readyqueue");
        exit(EXIT_FAILURE);
    }

    readyqueue->front = -1;
    readyqueue->rear = -1;
    readyqueue->itemCount = 0;

    int fd_running = shm_open("/runningqueue", O_CREAT | O_RDWR, 0666);
    if (fd_running == -1) {
        perror("shm_open runningqueue");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(fd_running, sizeof(SharedQueue)) == -1) {
        perror("ftruncate runningqueue");
        exit(EXIT_FAILURE);
    }

    runningqueue = (SharedQueue *)mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd_running, 0);
    if (runningqueue == MAP_FAILED) {
        perror("mmap runningqueue");
        exit(EXIT_FAILURE);
    }

    runningqueue->front = -1;
    runningqueue->rear = -1;
    runningqueue->itemCount = 0;

    int fd_terminated = shm_open("/terminatedqueue", O_CREAT | O_RDWR, 0666);
    if (fd_terminated == -1) {
        perror("shm_open terminatedqueue");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(fd_terminated, sizeof(SharedQueue)) == -1) {
        perror("ftruncate terminatedqueue");
        exit(EXIT_FAILURE);
    }

    terminatedqueue = (SharedQueue *)mmap(NULL, sizeof(SharedQueue), PROT_READ | PROT_WRITE, MAP_SHARED, fd_terminated, 0);
    if (terminatedqueue == MAP_FAILED) {
        perror("mmap terminatedqueue");
        exit(EXIT_FAILURE);
    }

    terminatedqueue->front = -1;
    terminatedqueue->rear = -1;
    terminatedqueue->itemCount = 0;

    sem_init(&readyqueue_mutex, 1, 1);
    sem_init(&runningqueue_mutex, 1, 1);
    sem_init(&terminatedqueue_mutex, 1, 1);

    signal(SIGINT, terminate_shell);

    pid = fork();
    if (pid < 0) {
        perror("fork failed!");
    } else if (pid == 0) {
        printf("Scheduler started\n");

        while (1) {
            if (readyqueue->itemCount != 0) {
                printf("Item count as seen by scheduler: %d\n",readyqueue->itemCount);
                int num_to_run = (readyqueue->itemCount < NCPU) ? readyqueue->itemCount : NCPU;
                printf("Scheduler sends signal to start\n");
                for (int i = 0; i < num_to_run; i++) {
                    printf("Check1\n");
                    Process process = readyqueue->queue[++readyqueue->front];
                    printf("Check2\n");
                    readyqueue->itemCount--;
                    printf("Check3\n");
                    process.state = 1;
                    printf("Check4\n");
                    runningqueue->queue[++runningqueue->rear] = process;
                    printf("Check5\n");
                    runningqueue->itemCount++;
                    kill(process.pid, SIGUSR2);
                    printf("SCHEDULER signal sent to start\n");
                }
                printf("Scheduler sleeps\n");
                sleep(TSLICE / 1000);
                printf("Scheduler sends signal to stop\n");
                for (int i = 0; i < num_to_run; i++) {
                    Process process = runningqueue->queue[++runningqueue->front];
                    runningqueue->itemCount--;
                    process.state = 0;
                    int status = 0;
                    printf("hi\n");
                    if (waitpid(process.pid, &status, WNOHANG) == 0) {
                        kill(process.pid, SIGSTOP);
                        sem_wait(&readyqueue_mutex);
                        readyqueue->queue[++readyqueue->rear] = process;
                        readyqueue->itemCount++;
                        sem_post(&readyqueue_mutex);
                        printf("Hey I'm going in readyqueue process: %s", process.executable);
                    } else {
                        terminatedqueue->queue[++terminatedqueue->rear] = process;
                        terminatedqueue->itemCount++;
                        printf("Hey I'm terminated process: %s\n", process.executable);
                    }
                    printf("status in scheduler: %d\n", status);
                    printf("SCHEDULER signal sent to stop\n");
                }
            }
        }
    } else {
        while (1) {
            char command[100];
            printf("SimpleShell$ ");
            fgets(command, sizeof(command), stdin);
            command[strcspn(command, "\n")] = '\0';

            if (strncmp(command, "submit ", 7) == 0) {
                char *executable = command + 7;
                int childs_pid = fork();
                if (childs_pid < 0) {
                    perror("fork");
                } else if (childs_pid == 0) {
                    signal(SIGUSR2, start_execution);
                    pause();
                } else {
                    Process newProcess;
                    newProcess.pid = childs_pid;
                    strncpy(newProcess.executable, executable, MAX_EXECUTABLE_LENGTH);
                    newProcess.state = 0;
                    sem_wait(&readyqueue_mutex);
                    readyqueue->queue[++readyqueue->rear] = newProcess;
                    readyqueue->itemCount++;
                    sem_post(&readyqueue_mutex);
                }
            }
        }
    }
    return 0;
}
