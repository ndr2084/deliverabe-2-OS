/*
 * alarm_cond.c
 *
 * This is an enhancement to the alarm_mutex.c program, which
 * used only a mutex to synchronize access to the shared alarm
 * list. This version adds a condition variable. The alarm
 * thread waits on this condition variable, with a timeout that
 * corresponds to the earliest timer request. If the main thread
 * enters an earlier timeout, it signals the condition variable
 * so that the alarm thread will wake up and process the earlier
 * timeout first, requeueing the later request.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <semaphore.h>

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    int                 id_alarm;
    int                 id_group;
    char                message[128];
    time_t              time;   /* seconds from EPOCH */


} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;
sem_t sem_start_alarm;
sem_t sem_display_threads;

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    pthread_t thread_id_main = pthread_self();
    int status;
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL:
     *
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while (next != NULL) {
        if (next->time >= alarm->time) {
            alarm->link = next;
            *last = alarm;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    /*
     * If we reached the end of the list, insert the new alarm
     * there.  ("next" is NULL, and "last" points to the link
     * field of the last item, or to the list header.)
     */
    if (next == NULL) {
        *last = alarm;
        alarm->link = NULL;
    }
    printf("Alarm(%d) Inserted by Main Thread %lu"
           " Into Alarm List at %ld: Group(%d) %d %s\n", alarm->id_alarm, thread_id_main, alarm->time, alarm->id_group, alarm->seconds, alarm->message);
#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%d(%d)[\"%s\"] ", next->time,
            next->time - time (NULL), next->message);
    printf ("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->time < current_alarm) {
        current_alarm = alarm->time;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0) err_abort (status, "Signal cond");
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_group_display_creation (void *arg)
{
    alarm_t *alarm;
    struct timespec cond_time;
    time_t now;
    int status, expired;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = pthread_mutex_lock (&alarm_mutex); //LOCK MUTEX
    if (status != 0)
        err_abort (status, "Lock mutex");
    while (1) {
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        current_alarm = 0;
        while (alarm_list == NULL) {
            status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
            if (status != 0) err_abort (status, "Wait on cond");
        }
        alarm = alarm_list;
        alarm_list = alarm->link;
        now = time (NULL);
        expired = 0;
        if (alarm->time > now) {
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                alarm->time - time (NULL), alarm->message);
#endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->time;
            while (current_alarm == alarm->time) {
                status = pthread_cond_timedwait (
                    &alarm_cond, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                alarm_insert (alarm);
        } else
            expired = 1;
        if (expired) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
}

void alarm_change (alarm_t *alarm){}

void alarm_cancel (alarm_t *alarm){}

void alarm_suspend (alarm_t *alarm){}

void alarm_reactivate (alarm_t *alarm){}

void alarm_view (void){}


void *alarm_group_display_removal (void *arg) {
}


int input_validator(const char *keyword_action, const char *keyword_group, int user_arg ) {
    //if input is valid, return flag that corresponds to the keyword
    if((strcmp(keyword_action, "Cancel_Alarm") == 0) && (user_arg == 2)) {
        return 1;
    }
    if((strcmp(keyword_action, "View_Alarms") == 0) && (user_arg == 1)) {
        return 2;
    }
    if((strcmp(keyword_action, "Start_Alarm") == 0) && strcmp(keyword_group, "Group") == 0 && (user_arg == 6)) {
        return 3;
    }
    if((strcmp(keyword_action, "Change_Alarm") == 0) && strcmp(keyword_group, "Group") == 0 && (user_arg == 6)) {
        return 4;
    }
    if((strcmp(keyword_action, "Suspend_Alarm") == 0) && (user_arg == 2)) {
        return 5;
    }
    if((strcmp(keyword_action, "Reactivate_Alarm") == 0) && (user_arg == 2)) {
        return 6;
    }

    err_abort (69, "command not found");
}

int main (int argc, char *argv[])
{
    int status;
    int action;
    char line[128];
    alarm_t *alarm;
    pthread_t thread_alarm_group_display_creation;
    pthread_t thread_alarm_group_display_removal;
    char keyword_action[128];
    char keyword_group[128];
    sem_init(&sem_display_threads, 0, 0);

    status = pthread_create (&thread_alarm_group_display_creation, NULL, alarm_group_display_creation, NULL);
    if (status != 0)
        err_abort (status, "alarm group display creation");

    status = pthread_create (&thread_alarm_group_display_removal, NULL, alarm_group_display_removal, NULL);
    if (status != 0)
        err_abort (status, "alarm group display removal");


    while (1) {
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL){errno_abort ("Allocate alarm");}

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
        int user_arg = (sscanf (line, "%[^(\n](%d): %[^(\n](%d)%d %128[^\n]", keyword_action, &alarm->id_alarm, keyword_group,
            &alarm->id_group, &alarm->seconds, alarm->message));

        if (user_arg == -1 == -1 || alarm->id_alarm < 1 || alarm->id_group < 1) {
            fprintf (stderr, "Bad command\n");
            free (alarm);

        }
        else {
            input_validator(keyword_action, keyword_group, user_arg);
            //CRITICAL BEGIN
            status = pthread_mutex_lock (&alarm_mutex); if (status != 0){err_abort (status, "Lock mutex");}

            alarm->time = time (NULL) + alarm->seconds;

            alarm_insert (alarm);
            //CRITICAL END
            status = pthread_mutex_unlock (&alarm_mutex); if (status != 0){err_abort (status, "Unlock mutex");}
            alarm_group_display_creation(alarm);
            sem_wait(&sem_display_threads);
        }
    }
}
