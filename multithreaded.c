#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

// Global variables: the task_queue and the search string
struct task_queue *queue;
char *search_string;


struct task_queue_node {
    char *associated_path;
    struct task_queue_node *next_node;
};

struct task_queue {
    sem_t head_lock;
    sem_t tail_lock;
    struct task_queue_node *head;
    struct task_queue_node *tail;
};

void task_queue_initialize(struct task_queue *kyu) {
    // initial dummy node for task queue
    struct task_queue_node *placeholder = malloc(sizeof(struct task_queue_node));
    placeholder->associated_path = malloc(sizeof(char) * 300);
    placeholder->associated_path[0] = '\0';
    placeholder->next_node = NULL;
    kyu->head = placeholder;
    kyu->tail = placeholder;
    // Initialize semaphores to be used as locks (initial value of 1)
    sem_init(&kyu->head_lock, 0, 1);
    sem_init(&kyu->tail_lock, 0, 1);
}

void task_queue_enqueue(struct task_queue *kyu, char *new_path) {
    struct task_queue_node *new_node = malloc(sizeof(struct task_queue_node));
    new_node->associated_path = new_path;
    new_node->next_node = NULL;

    //Critical section - note Lec 16 for why
    sem_wait(&kyu->tail_lock);
    kyu->tail->next_node = new_node;
    kyu->tail = new_node;
    sem_post(&kyu->tail_lock);
}

int task_queue_dequeue(struct task_queue *kyu, char **dequeued_path) {
    // Critical section - note Lec 16 for why
    sem_wait(&kyu->head_lock);
    struct task_queue_node *out_node = kyu->head;
    struct task_queue_node *new_head = out_node->next_node;
    // Condtional check applicable for empty task_queue
    if (new_head == NULL) {
        sem_post(&kyu->head_lock);
        return -1;
    }
    *dequeued_path = new_head->associated_path;
    kyu->head = new_head;
    sem_post(&kyu->head_lock);
    free(out_node->associated_path);
    free(out_node);
    return 0;
}

void task_queue_free(struct task_queue *kyu) {
    // at this point there should only be node in the task_queue (newest "dummy" node)
    // which head and tail both point to
    //printf("head pointer before freeing: %p\n", kyu->head);
    //printf("tail pointer before freeing: %p\n", kyu->head);
    sem_destroy(&kyu->head_lock);
    sem_destroy(&kyu->tail_lock);
    free(kyu->head->associated_path);
    free(kyu->head);
    free(kyu);
}

// Each thread will be running this function
void worker_behavior(int *id){
    int workerID = *id;
    //printf("Address of queue in thread %d: %p\n", workerID, kyu);
    // Infinite loop
    while(1){
        char *curr_task;
        int status = task_queue_dequeue(queue, &curr_task);
        // temporary breaking point - if queue is empty, supposed to wait until no more tasks can be enqueued
        // NEED SOME WAY TO CHECK WHEN NO MORE CONTENT CAN BE ENQUEUED INTO THE TASK QUEUE 
        // 1st -> task queue must be empty, 2nd -> no thread must be working on a directory 
        // (Possible solution: have a thread_status array that keeps track of when a thread is working on a directory, each array cell is a flag to a corresponding thread)
        if (status < 0) {break;}
        
        // Start of Case 2 in document (dequeued process successfully)
        printf("[%d] DIR %s\n", workerID, curr_task);
        // Opens the directory and returns DIR pointer to directory
        DIR * curr_dir = opendir(curr_task);

        // Going through the entries in the directory
        while(1){
            // Critical section? baka need din ng global na dirent din since its an iterator that keeps track of where it is na (dir_lock)
            struct dirent *entry = readdir(curr_dir);
		    if (entry == NULL) break;
            
            // Building the relative path to be used in realpath
            char *relative_path = malloc(sizeof(char) * 300);
            relative_path[0] = '\0';
            char *absolute_buffer = malloc(sizeof(char) * 300);
            absolute_buffer[0] = '\0';
            strcat(relative_path, curr_task);
            strcat(relative_path, "/");
            strcat(relative_path, entry->d_name);

            char *absolute_path = realpath(relative_path, absolute_buffer);
            free(relative_path);
            //printf("object_path value set: %s\n", absolute_path);

            switch(entry->d_type){
                // Directory case
                case DT_DIR:
                if (entry->d_name[0] != '.') {
                    task_queue_enqueue(queue, absolute_path);
                    printf("[%d] ENQUEUE %s\n", workerID, absolute_path);
                } else {
                    // if . or .. ang d_name
                    free(absolute_path);
                }
                break;

                // File case
                case DT_REG:
                // Invoke grep - edit size of grep command based on size of search_string
                // Might need to malloc talaga
                char grep_command[500] = "grep ";
                strcat(grep_command, "\"");
                strcat(grep_command, search_string);
                strcat(grep_command, "\" ");
                strcat(grep_command, absolute_path);
                strcat(grep_command, " > /dev/null");
                //printf("grep_command value: %s\n", grep_command);
                int grep_status = system(grep_command);
                // check return value of system function to see error code (exit status is 0 if may nahanap, 1 if wala)
                if (grep_status == 0){
                    printf("[%d] PRESENT %s\n", workerID, absolute_path);
                } else {
                    printf("[%d] ABSENT %s\n", workerID, absolute_path);
                }
                free(absolute_path);
                break;
            }
        }
        closedir(curr_dir);
    }
}

int main(int argc, char *argv[]) {
    // Obtain arguments from argv
    int num_workers = atoi(argv[1]);
    char *rootpath = argv[2];
    search_string = argv[3];

    // Initializes the array of threads (threads still have to be initialized)
    // using pthread_create
    pthread_t workers[num_workers];
    int workerids[num_workers];

    // Initialize task queue
    queue = malloc(sizeof(struct task_queue));
    task_queue_initialize(queue);

    // Enqueueing of rootpath into the task queue
    // Absolute paths will not exceed 250 characters -> Project 2 document
    char *rootpath_buffer = malloc(sizeof(char) * 300);
    rootpath_buffer[0] = '\0';
    char* absolute_rootpath = realpath(rootpath, rootpath_buffer);
    task_queue_enqueue(queue, absolute_rootpath);

    for (int i = 0; i < num_workers; i++){
        workerids[i] = i;
        pthread_create(&workers[i], NULL, (void *) worker_behavior, &workerids[i]);
    }

    for (int i = 0; i < num_workers; i++){
        pthread_join(workers[i], NULL);
    }

    task_queue_free(queue);
    return 0;
}