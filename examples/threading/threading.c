#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *data = (struct thread_data *) thread_param;
    
    int rc;

    DEBUG_LOG("Waiting to obtain mutex for %i ms", data->obtain_ms);
    usleep(data->obtain_ms * 1000);
    DEBUG_LOG("Obtaining mutex");
    rc = pthread_mutex_lock(data->mutex);
    if(rc != 0) {
	ERROR_LOG("Cannot lock mutex: %i\n", rc);
    } else {
	DEBUG_LOG("Waiting to release mutex");
	usleep(data->release_ms * 1000);
	data->thread_complete_success = true;
        DEBUG_LOG("Releasing mutex");	
	rc = pthread_mutex_unlock(data->mutex);
	if(rc != 0) {
		ERROR_LOG("Cannot unlock mutex: %i\n", rc);
	}
    }
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data * data = (struct thread_data *) malloc(sizeof(struct thread_data));

    if(data == NULL){
	    return false;
    }


    bool ret = false;
    int rc;
    do {

	    data->obtain_ms = wait_to_obtain_ms;
	    data->release_ms = wait_to_release_ms;
	    data->mutex = mutex;

	    DEBUG_LOG("Creating thread");
	    rc = pthread_create(thread, NULL, threadfunc, (void *)data);

	    if(ret == 0){
		    DEBUG_LOG("Thread %i created", rc);
		    ret = true;
	    } else {
		    ERROR_LOG("Failed to create thread: %i", rc);
	    }

    } while(0);

    return ret;
}

