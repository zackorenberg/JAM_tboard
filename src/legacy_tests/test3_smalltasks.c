#include "legacy_tests.h"
#ifdef LTEST_3

#include <stdio.h>

#include "../tboard.h"
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>


#define ITERATIONS 100000 //10000000
#define SECONDARY_EXECUTORS 5


tboard_t *tboard = NULL;
int n = 0;
int NUM_TASKS = ITERATIONS;
int completion_count = 0;
int task_count = 0;
double yield_count = 0;
int priority_count = 0;
bool print_priority = true;
bool primary_task_complete = false;
int max_tasks_reached = 0;
pthread_t killer_thread, priority_creator_thread,pcompletion;
pthread_mutex_t count_mutex;

struct timespec ts = {
	.tv_sec = 0,
	.tv_nsec = 300000
};

void increment_completion_count(){
	pthread_mutex_lock(&count_mutex);
	completion_count++;
	pthread_mutex_unlock(&count_mutex);
}

int read_completion_count(){
	pthread_mutex_lock(&count_mutex);
	int ret = completion_count;
	pthread_mutex_unlock(&count_mutex);
    return ret;
}

void priority_task(context_t ctx)
{
	(void)ctx;
	int priority_count = (intptr_t)task_get_args();
	if(print_priority)
		tboard_log("priority: priority task %d executed at CPU time %d.\n", priority_count, clock());
}


void secondary_task(context_t ctx);

void *check_completion(void *args){
	(void)args;
	while(true){
		if(primary_task_complete && completion_count >= task_count){
			pthread_mutex_lock(&(tboard->tmutex));
			tboard_log("Completed %d secondary tasks with %e yields.\n",task_count, yield_count);
			tboard_log("Max tasks reached %d times. There were %d priority tasks executed.\n", max_tasks_reached, priority_count);
			pthread_cancel(killer_thread);
			pthread_cancel(priority_creator_thread);
			
			int cond_wait_time = clock();
			tboard_kill(tboard);
			cond_wait_time = clock() - cond_wait_time;
			int unfinished_tasks = tboard->task_count;
            history_print_records(tboard, stdout);
			//for(int i=0; i<MAX_TASKS; i++){
			//	if (tboard->task_list[i].status != 0)
			//		unfinished_tasks++;
			//}
			tboard_log("Found %d unfinished tasks, waited %d CPU cycles for condition signal.\n", unfinished_tasks, cond_wait_time);
			
			
			pthread_mutex_unlock(&(tboard->tmutex));
			break;
		}
		nanosleep(&ts, NULL);
		//task_yield(); yield_count++;
	}
	return NULL;
}

void primary_task(context_t ctx)
{
	(void)ctx;
	int i = 0;
    int *n;
    primary_task_complete = false;
	tboard_log("primary: Creating %d small tasks\n", NUM_TASKS);
	for (; i<NUM_TASKS; i++) {
        int unable_to_create_task_count = 0; // bad name i know
        n = calloc(1, sizeof(int));
        *n = i;
		while(false == task_create(tboard, TBOARD_FUNC(secondary_task), SECONDARY_EXEC, n, sizeof(int))){
            if(unable_to_create_task_count > 30){
                tboard_log("primary: Was unable to create the same task after 30 attempts. Ending at %d tasks created.\n",i);
                primary_task_complete = true;
                return;
            }
			max_tasks_reached++;
			nanosleep(&ts, NULL);
			task_yield(); yield_count++;
            unable_to_create_task_count++;
		}
		task_count++;
		task_yield(); yield_count++;
	}
	tboard_log("primary: Created %d small tasks.\n", i);
	
	task_yield(); yield_count++;
    primary_task_complete = true;
}
void secondary_task(context_t ctx)
{
	(void)ctx;
	int *xptr = ((int *)(task_get_args()));
    int x = *xptr;
    task_yield(); yield_count++;
    x /= 2;
    increment_completion_count();
    return;

    int i = 0;
    if (x <= 1) {
        if (x >= 0) increment_completion_count();
        else        tboard_err("secondary: Invalid value of x encountered in secondary task: %d\n", x);
        return;

    }
	while (x != 1) {
		if(x % 2 == 0)  x /= 2;
		else 			x = 3*x+1;
		i++;
		task_yield(); yield_count++;
	}
    increment_completion_count();
}


void *tboard_killer(void *args){
	(void)args;
    int last_completion = -1;
    sleep(1);
    while(true){
        int cc = read_completion_count();
        if(cc != last_completion){
            last_completion = cc;
            tboard_log("Completed %d/%d/%d tasks.\n",last_completion, task_count, NUM_TASKS);
        }else{
            tboard_log("Error: Has not finished a task in 10 seconds, killing taskboard with %d completions.\n", completion_count);
            break;
        }
        sleep(10);
    }

	pthread_cancel(*((pthread_t *)args));
    pthread_cancel(pcompletion);
	pthread_mutex_lock(&(tboard->tmutex));
	tboard_kill(tboard);
	pthread_cond_wait(&(tboard->tcond), &(tboard->tmutex));
	history_print_records(tboard, stdout);
	pthread_mutex_unlock(&(tboard->tmutex));
	tboard_log("Confirmed conjecture for %d of %d values with %e yields.\n", completion_count, task_count, yield_count);
	tboard_log("Max tasks reached %d times. There were %d priority tasks executed.\n", max_tasks_reached, priority_count);
	return NULL;
}

void *priority_task_creator(void *args){
	(void)args;
	priority_count = 0;
	while(true){
		sleep(rand() % 20);
		if(print_priority)
			tboard_log("priority: issued priority task at CPU time %d\n",clock());
		bool res = task_create(tboard, TBOARD_FUNC(priority_task), PRIORITY_EXEC, (void *)(intptr_t)priority_count, 0);
		if(res)
			priority_count++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	(void)argv;
	if(argc > 1) print_priority = false;
	pthread_mutex_init(&count_mutex, NULL);

	tboard = tboard_create(SECONDARY_EXECUTORS);
	tboard_start(tboard);

	
	pthread_create(&priority_creator_thread, NULL, priority_task_creator, NULL);
	pthread_create(&killer_thread, NULL, tboard_killer, &priority_creator_thread);
	pthread_create(&pcompletion, NULL, check_completion, NULL);

	task_create(tboard, TBOARD_FUNC(primary_task), PRIMARY_EXEC, NULL, 0);
	
	pthread_join(priority_creator_thread, NULL);
    tboard_destroy(tboard);
    pthread_join(killer_thread, NULL);
	pthread_join(pcompletion, NULL);

	pthread_mutex_destroy(&count_mutex);

	tboard_exit();

	return (0);
}

#endif
// */