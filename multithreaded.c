#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

// Global variables: the task_queue, search string, number of workers, the thread_statuses array and 
// a corresponding semaphore that acts as a lock when accessing thread_statuses
struct task_queue *queue;
char *search_string;
int num_workers;
int *thread_statuses;
sem_t thread_statuses_lock;

// struct for a node of the task queue
struct task_queue_node {
    char *associated_path;
    struct task_queue_node *next_node;
};

// struct for the task_queue itself
struct task_queue {
    sem_t head_lock;
    sem_t tail_lock;
    struct task_queue_node *head;
    struct task_queue_node *tail;
};

// function that initializes the task queue with an initial dummy head node;
void task_queue_initialize(struct task_queue *kyu) {
    // initial dummy node for task queue
    struct task_queue_node *placeholder = malloc(sizeof(struct task_queue_node));
    // This is allocated since it will be free()-d later on
    placeholder->associated_path = malloc(sizeof(char) * 300);
    placeholder->associated_path[0] = '\0';
    placeholder->next_node = NULL;
    kyu->head = placeholder;
    kyu->tail = placeholder;
    // Initialize semaphores to be used as locks (initial value of 1)
    sem_init(&kyu->head_lock, 0, 1);
    sem_init(&kyu->tail_lock, 0, 1);
}

// function that enqueues a new task
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

// function that dequeues a task from the task queue
// returns -1 if failed to dequeue (empty queue), 0 otherwise
int task_queue_dequeue(struct task_queue *kyu, char *dequeued_path) {
    // Critical section - note Lec 16 for why
    sem_wait(&kyu->head_lock);
    struct task_queue_node *out_node = kyu->head;
    struct task_queue_node *new_head = out_node->next_node;
    // Condtional check applicable for empty task_queue
    if (new_head == NULL) {
        sem_post(&kyu->head_lock);
        return -1;
    }
    strcpy(dequeued_path, new_head->associated_path);
    kyu->head = new_head;
    sem_post(&kyu->head_lock);
    free(out_node->associated_path);
    free(out_node);
    return 0;
}

// function that does the necessary freeing of the allocated space related to the queue
void task_queue_free(struct task_queue *kyu) {
    // At this point there should only be 1 node in the task_queue (last "dummy" node)
    // which head and tail both point to
    sem_destroy(&kyu->head_lock);
    sem_destroy(&kyu->tail_lock);
    free(kyu->head->associated_path);
    free(kyu->head);
    free(kyu);
}

// function that checks if all threads are not working
// returns -1 if there is a thread still working, 0 otherwise
int all_done_checker(){
    // Goes through thread_statuses to see if anyone is working
    // if anyone is, we're not done, return -1 indicating not done
    for (int i = 0; i < num_workers; i++){
        if (thread_statuses[i] == 1){
            return -1;
        }
    }
    // if all thread_status cells are 0, then no more content can be placed in the task_queue
    // return 0 indicating done
    return 0;
}

// Each thread will be running this function, defines the behavior of each worker
void worker_behavior(int *id){
    int workerID = *id;
    
    // Initializes a char pointer that keeps track of which directory path the thread is currently working on
    char *curr_task = malloc(sizeof(char) * 300);

    // Infinite loop, thread will repeat these set of instructions until no more tasks can be enqueued
    while(1){
        // dequeues from the task queue, sets value of curr_task
        // returned status indicates success or failure of dequeue operation
        int status = task_queue_dequeue(queue, curr_task);

        // If dequeue operation fails, then the queue is empty (Case 1)
        if (status < 0) {
            // acquires thread_statuses lock - ensure that thread_statuses is not modified while all_done_checker is active
            sem_wait(&thread_statuses_lock);
            if (all_done_checker() < 0){
                // if all_done_checker returns -1, at least 1 thread is still running and that means there is a possibility that
                // a new task will be enqueued
                sem_post(&thread_statuses_lock);
                continue;
            } else {
                // if all_done_checker returns 0, no threads are running anymore, meaning no more content can be enqueued into the task queue
                // thus the thread can now break out of the loop and terminate
                sem_post(&thread_statuses_lock);
                break;
            }
        }
        
        // If dequeue operation is successful, then a new task is obtained (Case 2)
        // Set thread status in thread_statuses to 1 indicating that it is currently working on a task
        sem_wait(&thread_statuses_lock);
        thread_statuses[workerID] = 1;
        sem_post(&thread_statuses_lock);
        
        // Print output indicating a directory is being worked on
        printf("[%d] DIR %s\n", workerID, curr_task);

        // Opens the directory and returns DIR pointer to directory
        DIR * curr_dir = opendir(curr_task);

        // Iterate through the entries in the directory
        while(1){
            // Invokes readdir, obtaining a new child objectww
            struct dirent *entry = readdir(curr_dir);
		    if (entry == NULL) break;
            
            // Building the relative path to be used for realpath, which obtains the absolute path of the file/directory
            // buffer for absolute path storing also initialized
            char *relative_path = malloc(sizeof(char) * 300);
            relative_path[0] = '\0';
            char *absolute_buffer = malloc(sizeof(char) * 300);
            absolute_buffer[0] = '\0';
            strcat(relative_path, curr_task);
            strcat(relative_path, "/");
            strcat(relative_path, entry->d_name);

            // Stores absolute path of file in aptly named pointer
            char *absolute_path = realpath(relative_path, absolute_buffer);

            // free built relative path string, no longer needed
            free(relative_path);

            // If child object is: (Cases 3.1 and 3.2)
            switch(entry->d_type){
                // If child object is DIRECTORY (Case 3.1)
                case DT_DIR:
                // Make sure that directory is not . or ..
                // strcmp returns 0 if equal, and a non-zero integer if non-equal
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    printf("[%d] ENQUEUE %s\n", workerID, absolute_path);
                    task_queue_enqueue(queue, absolute_path);
                } else {
                    // if directory name is . or .., ignore
                    free(absolute_path);
                }
                break;

                // If child object is a file (Case 3.2)
                case DT_REG:
                // Invoke grep - edit size of grep command based on size of search_string (might need to malloc this)

                // form full grep command
                char grep_command[500] = "grep ";
                strcat(grep_command, "\"");
                strcat(grep_command, search_string);
                strcat(grep_command, "\" ");
                strcat(grep_command, absolute_path);
                // redirect output to /dev/null
                strcat(grep_command, " > /dev/null");
                int grep_status = system(grep_command);
                
                // check return value of system function to see error code (exit status is 0 if string is found, 1 otherwise)
                if (grep_status == 0){
                    printf("[%d] PRESENT %s\n", workerID, absolute_path);
                } else {
                    printf("[%d] ABSENT %s\n", workerID, absolute_path);
                }
                // free absolute path, no longer needed if file
                free(absolute_path);
                break;
            }
        }
        // close the opened directory corresponding to DIR *
        closedir(curr_dir);

        // Update status to 0 in thread_statuses to indicate that thread is finished working (for now)
        sem_wait(&thread_statuses_lock);
        thread_statuses[workerID] = 0;
        sem_post(&thread_statuses_lock);
    }
    // free curr_task since thread is finished running
    free(curr_task);
}

int main(int argc, char *argv[]) {
    // Obtain arguments from argv, set global values
    num_workers = atoi(argv[1]);
    char *rootpath = argv[2];
    search_string = argv[3];

    // Initializes worker thread, workerID, and thread status arrays based on number of workers
    pthread_t workers[num_workers];
    int workerids[num_workers];
    int statuses[num_workers];


    thread_statuses = statuses;
    // initialize semaphore used as thread_statuses lock
    sem_init(&thread_statuses_lock, 0, 1);

    // Initialize task queue
    queue = malloc(sizeof(struct task_queue));
    task_queue_initialize(queue);

    // Enqueueing of rootpath into the task queue
    // Absolute paths will not exceed 250 characters -> Project 2 document
    char *rootpath_buffer = malloc(sizeof(char) * 300);
    rootpath_buffer[0] = '\0';
    char* absolute_rootpath = realpath(rootpath, rootpath_buffer);
    task_queue_enqueue(queue, absolute_rootpath);

    // create threads, set their worker IDs and initialize their statuses
    for (int i = 0; i < num_workers; i++){
        workerids[i] = i;
        statuses[i] = 0;
        pthread_create(&workers[i], NULL, (void *) worker_behavior, &workerids[i]);
    }

    // main thread waits for other threads to finish
    for (int i = 0; i < num_workers; i++){
        pthread_join(workers[i], NULL);
    }

    // destroy semaphore used as thread_statuses lock
    sem_destroy(&thread_statuses_lock);

    // free space allocated related to the task queue
    task_queue_free(queue);
    return 0;
}