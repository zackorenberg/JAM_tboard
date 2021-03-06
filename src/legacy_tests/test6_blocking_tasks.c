/**
 * Test spawn tasks that generates several blocking tasks and prints return value after
 * blocking tasks has ceased.
 * 
 * Test terminates once all of these blocking tasks terminate and calling task prints return values.
 * 
 * Test also creates a never-terminated always-yielding blocking task to test behavior of killing task board
 * with blocking task still running.
 */
#include "legacy_tests.h"
#ifdef LTEST_6

#include "../tboard.h"
#include "../dummy_MQTT.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

struct b_function_t {
    double (*fn)(double, double);
    const char *fn_name;
};

#define B_FUNC(func) (struct b_function_t){.fn = &func, .fn_name = #func}

struct b_data_t {
    double a;
    double b;
    double resp;
    struct b_function_t op;
};


tboard_t *tboard = NULL;
pthread_t completion;

#define NUM_TASKS 100
#define SECONDARY_EXECUTORS 2

int completion_count = 0;
double yield_count = 0;
pthread_mutex_t count_mutex;
struct b_data_t blocking_data[NUM_TASKS];

const struct timespec completion_sleep = {
    .tv_sec = 0,
	.tv_nsec = 500000L,
};

void increment_completion_count();
int read_completion_count();

double fadd(double, double);
double fsub(double, double);
double fmul(double, double);
double fdiv(double, double);
double pow(double, double);
double fmod(double, double);
double atan2(double, double);

double rand_double(double min, double max);
void generate_data(struct b_data_t *data);

void blocking_task(context_t ctx);
void create_blocking_task(context_t ctx);

void never_ending_blocking_task(context_t ctx);
void create_never_ending_blocking_task(context_t ctx);

void *check_completion(void *args);

int main()
{
    tboard = tboard_create(SECONDARY_EXECUTORS);
	tboard_start(tboard);

    pthread_create(&completion, NULL, check_completion, tboard);
    
    task_create(tboard, TBOARD_FUNC(create_never_ending_blocking_task), SECONDARY_EXEC, NULL, 0);
    for (int i=0; i<NUM_TASKS; i++) {
        task_create(tboard, TBOARD_FUNC(create_blocking_task), PRIMARY_EXEC, &blocking_data[i], 0);
    }

    tboard_destroy(tboard);
    pthread_join(completion, NULL);

    printf("Tasks completed. Checking values:\n");
    for (int i=0; i<NUM_TASKS; i++){
        if(blocking_data[i].resp != blocking_data[i].op.fn(blocking_data[i].a,blocking_data[i].b)){
            printf("Discrepency found in task %d\n",i);
        }
    }
    tboard_exit();
}

//////////// Arithmetic Functions ////////////

double fadd(double x, double y)
{
    return x + y;
}
double fsub(double x, double y)
{
    return x - y;
}
double fmul(double x, double y)
{
    return x * y;
}
double fdiv(double x, double y)
{
    return x / y;
}

//////////// Data gen functions /////////////
/** defined in tests/tests.c
double rand_double(double min, double max)
{
    double scale = (double)(rand()) / (double)RAND_MAX;
    return min + scale*max;
}*/

void generate_data(struct b_data_t *data)
{
    int funct = rand() % 7;

    data->a = rand_double(1.0, 10.0);
    data->b = rand_double(1.0, 10.0);
    data->resp = NAN;
    switch (funct) {
        case 0:
            ; data->op = B_FUNC(fadd);
            break;
        case 1:
            ; data->op = B_FUNC(fsub);
            break;
        case 2:
            ; data->op = B_FUNC(fmul);
            break;
        case 3:
            ; data->op = B_FUNC(fdiv);
            break;
        case 4:
            ; data->op = B_FUNC(pow);
            break;
        case 5:
            ; data->op = B_FUNC(fmod);
            break;
        case 6:
            ; data->op = B_FUNC(atan2);
            break;
    }
}
//////////////// Task Functions /////////////////
void create_blocking_task(context_t ctx)
{
    (void)ctx;
    struct b_data_t *data = (struct b_data_t *)task_get_args();
    generate_data(data);
    bool res = blocking_task_create(tboard, TBOARD_FUNC(blocking_task), SECONDARY_EXEC, data, 0);
    if (res == true)
        printf("Blocked to compute %s(%f, %f) = %f\n",data->op.fn_name, data->a, data->b, data->resp);
    else
        printf("Error creating blocking task\n");
    increment_completion_count();
}

void blocking_task(context_t ctx)
{
    (void)ctx;
    struct b_data_t *data = (struct b_data_t *)task_get_args();
    data->resp = (data->op.fn)(data->a, data->b);
    return;
}


void create_never_ending_blocking_task(context_t ctx)
{
    (void)ctx;
    bool res = blocking_task_create(tboard, TBOARD_FUNC(never_ending_blocking_task), SECONDARY_EXEC, NULL, 0);
    if (res)
        tboard_err("Never ending blocking task ended?\n");
}

void never_ending_blocking_task(context_t ctx)
{
    (void)ctx;
    while(true){
        task_yield();
    }
}



//////////////// Thread Functions ///////////////

void *check_completion(void *args){
    (void)args;
    while(true){
        if(completion_count >= NUM_TASKS){
            pthread_mutex_lock(&(tboard->tmutex));
			tboard_log("Completed %d secondary tasks with %e yields.\n",completion_count, yield_count);

			int cond_wait_time = clock();
			tboard_kill(tboard);
			cond_wait_time = clock() - cond_wait_time;
			int unfinished_tasks = tboard->task_count;
            history_print_records(tboard, stdout);
            pthread_mutex_unlock(&(tboard->tmutex));
            tboard_log("Found %d unfinished tasks, waited %d CPU cycles for condition signal.\n", unfinished_tasks, cond_wait_time);
            break;
        } else {
            nanosleep(&completion_sleep, NULL);
        }
    }
    return NULL;
}

///////////////// Helper Functions ////////////////

void increment_completion_count()
{
    pthread_mutex_lock(&count_mutex);
	completion_count++;
	pthread_mutex_unlock(&count_mutex);
}

int read_completion_count()
{
	pthread_mutex_lock(&count_mutex);
	int ret = completion_count;
	pthread_mutex_unlock(&count_mutex);
    return ret;
}

#endif