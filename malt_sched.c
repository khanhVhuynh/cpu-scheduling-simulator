/* This is the only file you will be editing.
 * - malt_sched.c (Malt Scheduler Library Code)
 * - Copyright of Starter Code: Prof. Kevin Andrea, George Mason University. All Rights Reserved
 * - Copyright of Student Code: You!  
 * - Restrictions on Student Code: Do not post your code on any public site (eg. Github).
 * -- Feel free to post your code on a PRIVATE Github and give interviewers access to it.
 * -- You are liable for the protection of your code from others.
 * - Date: Aug 2026
 */

/* CS367 Project 1, Fall Semester, 2026
 * Fill in your Name, GNumber, and Section Numbers in the following comment fields
 * Name:  
 * GNumber:  G
 * Section Number:    CS367-0__             (Replace the __ with your lecture section number)
 * Recitation Number: CS367-3__             (Replace the __ with your recitation section number)
 */

/* malt CPU Scheduling Library */
 
/* Standard Library Includes */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
/* Unix System Includes */
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>

/* DO NOT CHANGE THE FOLLOWING INCLUDES - Local Includes 
 * If you change these, it will not build on Zeus with the Makefile
 * If you change these, it will not run in the grader
 */
  #include "malt_sched.h"
  #include "strawhat_scheduler.h"
  #include "strawhat_support.h"
  #include "strawhat_process.h"
/* DO NOT CHANGE ABOVE INCLUDES - Local Includes */


/* Edit the file below this line....
 * Feel free to create any definitions or constants you like!
 * Feel free to create any helper functions you like! 
 */

/*** Helper Functions ***/

/* Change only the four state bits (R/U/S/T), preserving all other bits. */
static void set_process_state(Malt_process_s *process, int bit_position) {
  unsigned short state_mask = (unsigned short)((1U << BIT_READY) |
                                                (1U << BIT_RUNNING) |
                                                (1U << BIT_SUSPENDED) |
                                                (1U << BIT_TERMINATED));
  process->state &= (unsigned short)(~state_mask);
  process->state |= (unsigned short)(1U << bit_position);
}

/* Replace only the low 8 exit-code bits. */
static void set_exit_code(Malt_process_s *process, int exit_code) {
  unsigned short exit_mask = (unsigned short)((1U << BITS_EXIT_CODE) - 1U);
  process->state &= (unsigned short)(~exit_mask);
  process->state |= (unsigned short)(exit_code & exit_mask);
}

/* Append an existing node to the end of a queue. */
static void append_process(Malt_queue_s *queue, Malt_process_s *process) {
  Malt_process_s *current;

  process->next = NULL;

  if (queue->head == NULL) {
    queue->head = process;
    queue->tail = process;
  }
  else {
    current = queue->head;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = process;
    queue->tail = process;
  }

  queue->count++;
}

/* Remove and return a node by PID.  A PID of 0 removes the head node. */
static Malt_process_s *remove_process(Malt_queue_s *queue, pid_t pid) {
  Malt_process_s *current;
  Malt_process_s *previous;

  if (queue->head == NULL) {
    return NULL;
  }

  current = queue->head;
  previous = NULL;

  if (pid != 0) {
    while (current != NULL && current->pid != pid) {
      previous = current;
      current = current->next;
    }
  }

  if (current == NULL) {
    return NULL;
  }

  if (previous == NULL) {
    queue->head = current->next;
  }
  else {
    previous->next = current->next;
  }

  if (current == queue->tail) {
    queue->tail = previous;
  }
  if (queue->head == NULL) {
    queue->tail = NULL;
  }

  queue->count--;
  current->next = NULL;
  return current;
}

/* Remove and return a specific node pointer from a queue. */
static Malt_process_s *remove_selected_process(Malt_queue_s *queue,
                                                Malt_process_s *selected) {
  Malt_process_s *current;
  Malt_process_s *previous;

  current = queue->head;
  previous = NULL;

  while (current != NULL && current != selected) {
    previous = current;
    current = current->next;
  }

  if (current == NULL) {
    return NULL;
  }

  if (previous == NULL) {
    queue->head = current->next;
  }
  else {
    previous->next = current->next;
  }

  if (current == queue->tail) {
    queue->tail = previous;
  }
  if (queue->head == NULL) {
    queue->tail = NULL;
  }

  queue->count--;
  current->next = NULL;
  return current;
}

/* Free every process in a queue, including each dynamically allocated cmd. */
static void free_queue_processes(Malt_queue_s *queue) {
  Malt_process_s *current;
  Malt_process_s *next;

  current = queue->head;
  while (current != NULL) {
    next = current->next;
    free(current->cmd);
    free(current);
    current = next;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->count = 0;
}

/*** Malt Library API Functions to Complete ***/

/* Initializes the Malt_schedule_s Struct and all of the Malt_queue_s Structs
 * Follow the project documentation for this function.
 * Returns a pointer to the new Malt_schedule_s or NULL on any error.
 * - Hint: What does malloc return on an error?
 */
Malt_schedule_s *malt_initialize() {
  Malt_schedule_s *schedule = calloc(1, sizeof(Malt_schedule_s));

  if (schedule == NULL) {
    return NULL;
  }

  schedule->ready_queue = calloc(1, sizeof(Malt_queue_s));
  if (schedule->ready_queue == NULL) {
    free(schedule);
    return NULL;
  }

  schedule->suspended_queue = calloc(1, sizeof(Malt_queue_s));
  if (schedule->suspended_queue == NULL) {
    free(schedule->ready_queue);
    free(schedule);
    return NULL;
  }

  schedule->terminated_queue = calloc(1, sizeof(Malt_queue_s));
  if (schedule->terminated_queue == NULL) {
    free(schedule->suspended_queue);
    free(schedule->ready_queue);
    free(schedule);
    return NULL;
  }

  return schedule;
}

/* Allocates and Initializes a new Malt_process_s with the given information.
 * - Malloc and copy the command string, don't just assign it!
 * Follow the project documentation for this function.
 * - You may assume all members within data are Legal and Correct for this Function Only
 * Returns a pointer to the new Malt_process_s on success or a NULL on any error.
 */
Malt_process_s *malt_create(Malt_create_data_s *data) {
  Malt_process_s *process;
  size_t cmd_length;

  if (data == NULL) {
    return NULL;
  }

  process = malloc(sizeof(Malt_process_s));
  if (process == NULL) {
    return NULL;
  }

  cmd_length = strlen(data->original_cmd);
  process->cmd = malloc(cmd_length + 1);
  if (process->cmd == NULL) {
    free(process);
    return NULL;
  }

  strncpy(process->cmd, data->original_cmd, cmd_length + 1);

  process->pid = data->pid;
  process->state = 0;
  set_process_state(process, BIT_READY);
  if (data->is_critical) {
    process->state |= (unsigned short)(1U << BIT_CRITICAL);
  }
  process->priority = data->priority_level;
  process->age = 0;
  process->next = NULL;

  return process;
}

/* Add a process into the end of Ready Queue (singly linked list).
 * Follow the project documentation for this function.
 * - Do not create a new process to insert, insert the SAME process passed in.
 * Returns a 0 on success or a -1 on any error.
 */
int malt_add(Malt_schedule_s *schedule, Malt_process_s *process) {
  if (schedule == NULL || schedule->ready_queue == NULL || process == NULL) {
    return -1;
  }

  set_process_state(process, BIT_READY);
  append_process(schedule->ready_queue, process);
  return 0;
}

/* Returns the number of items in a given Malt Queue (singly linked list).
 * Follow the project documentation for this function.
 * Returns the number of processes in the list or -1 on any errors.
 */
int malt_count(Malt_queue_s *queue) {
  if (queue == NULL) {
    return -1;
  }
  return queue->count;
}

/* Selects the best process to run from the Ready Queue (singly linked list).
 * Follow the project documentation for this function.
 * Returns a pointer to the process selected or NULL if none available or on any errors.
 * - Do not create a new process to return, return a pointer to the SAME process selected.
 * - Return NULL if the ready queue was empty OR if there were any errors.
 */
Malt_process_s *malt_select(Malt_schedule_s *schedule) {
  Malt_queue_s *queue;
  Malt_process_s *current;
  Malt_process_s *selected;

  if (schedule == NULL || schedule->ready_queue == NULL) {
    return NULL;
  }

  queue = schedule->ready_queue;
  if (queue->head == NULL) {
    return NULL;
  }

  selected = NULL;

  /* First choice: the first Critical process. */
  current = queue->head;
  while (current != NULL) {
    if ((current->state & (unsigned short)(1U << BIT_CRITICAL)) != 0) {
      selected = current;
      break;
    }
    current = current->next;
  }

  /* Second choice: the first Starving process. */
  if (selected == NULL) {
    current = queue->head;
    while (current != NULL) {
      if (current->age >= STARVING_AGE) {
        selected = current;
        break;
      }
      current = current->next;
    }
  }

  /* Third choice: numerically lowest priority value, first node on ties. */
  if (selected == NULL) {
    selected = queue->head;
    current = queue->head->next;
    while (current != NULL) {
      if (current->priority < selected->priority) {
        selected = current;
      }
      current = current->next;
    }
  }

  selected = remove_selected_process(queue, selected);
  if (selected == NULL) {
    return NULL;
  }

  selected->age = 0;
  set_process_state(selected, BIT_RUNNING);
  selected->next = NULL;

  current = queue->head;
  while (current != NULL) {
    current->age++;
    current = current->next;
  }

  return selected;
}

/* Moves the process with matching pid from Ready to Suspended Queue.
 * Follow the specification for this function.
 * Returns a 0 on success or a -1 on any error (such as process not found).
 */
int malt_suspend(Malt_schedule_s *schedule, pid_t pid) {
  Malt_process_s *process;

  if (schedule == NULL || schedule->ready_queue == NULL ||
      schedule->suspended_queue == NULL) {
    return -1;
  }

  process = remove_process(schedule->ready_queue, pid);
  if (process == NULL) {
    return -1;
  }

  set_process_state(process, BIT_SUSPENDED);
  process->next = NULL;
  append_process(schedule->suspended_queue, process);
  return 0;
}

/* Moves the process with matching pid from Suspended to Ready Queue.
 * Follow the specification for this function.
 * Returns a 0 on success or a -1 on any error (such as process not found).
 */
int malt_resume(Malt_schedule_s *schedule, pid_t pid) {
  Malt_process_s *process;

  if (schedule == NULL || schedule->suspended_queue == NULL ||
      schedule->ready_queue == NULL) {
    return -1;
  }

  process = remove_process(schedule->suspended_queue, pid);
  if (process == NULL) {
    return -1;
  }

  set_process_state(process, BIT_READY);
  process->next = NULL;
  append_process(schedule->ready_queue, process);
  return 0;
}

/* This is called when a process exits normally that was just Running.
 * Puts the given node into the Terminated Queue and sets the Exit Code
 * - Do not create a new process to insert, insert the SAME process passed in.
 * Follow the project documentation for this function.
 * Returns a 0 on success or a -1 on any error.
 */
int malt_exited(Malt_schedule_s *schedule, Malt_process_s *process, int exit_code) {
  if (schedule == NULL || schedule->terminated_queue == NULL || process == NULL) {
    return -1;
  }

  set_process_state(process, BIT_TERMINATED);
  set_exit_code(process, exit_code);
  process->next = NULL;
  append_process(schedule->terminated_queue, process);
  return 0;
}

/* This is called when the OS terminates a process early.
 * - The matching process will either be in your Ready Queue or Suspended Queue.
 * - The difference with malt_exited is that this process is in one of your Queues already.
 * Removes the process with matching pid from either of these Queues and adds the Exit Code to it.
 * - You have to check both since it could be in either queue, if it exists at all.
 * Follow the project documentation for this function.
 * Returns a 0 on success or a -1 on any error.
 */
int malt_killed(Malt_schedule_s *schedule, pid_t pid, int exit_code) {
  Malt_process_s *process;

  if (schedule == NULL || schedule->ready_queue == NULL ||
      schedule->suspended_queue == NULL || schedule->terminated_queue == NULL ||
      pid <= 0) {
    return -1;
  }

  process = remove_process(schedule->ready_queue, pid);
  if (process == NULL) {
    process = remove_process(schedule->suspended_queue, pid);
  }
  if (process == NULL) {
    return -1;
  }

  set_process_state(process, BIT_TERMINATED);
  set_exit_code(process, exit_code);
  process->next = NULL;
  append_process(schedule->terminated_queue, process);
  return 0;
}

/* This is called when StrawHat reaps a Terminated (Defunct) Process.  (reap command).
 * Removes and frees the process with matching pid from the Termainated Queue.
 * Follow the specification for this function.
 * Returns the exit_code on success or a -1 on any error (such as process not found).
 */
int malt_reap(Malt_schedule_s *schedule, pid_t pid) {
  Malt_process_s *process;
  int exit_code;

  if (schedule == NULL || schedule->terminated_queue == NULL) {
    return -1;
  }

  process = remove_process(schedule->terminated_queue, pid);
  if (process == NULL) {
    return -1;
  }

  exit_code = (int)(process->state & (unsigned short)((1U << BITS_EXIT_CODE) - 1U));
  free(process->cmd);
  free(process);
  return exit_code;
}

/* Returns the exit code if the process is terminated.
 * (All Linux exit codes are between 0 and 255)
 * Follow the project documentation for this function.
 * If the process is not terminated, return -1.
 */
int malt_get_ec(Malt_process_s *process) {
  if (process == NULL) {
    return -1;
  }

  if ((process->state & (unsigned short)(1U << BIT_TERMINATED)) == 0) {
    return -1;
  }

  return (int)(process->state & (unsigned short)((1U << BITS_EXIT_CODE) - 1U));
}

/* Frees all allocated memory in the Malt_schedule_s, all of the Queues, and all of their Nodes.
 * Follow the project documentation for this function.
 * Returns void.
 */
void malt_deallocate(Malt_schedule_s *schedule) {
  if (schedule == NULL) {
    return;
  }

  if (schedule->ready_queue != NULL) {
    free_queue_processes(schedule->ready_queue);
    free(schedule->ready_queue);
  }

  if (schedule->suspended_queue != NULL) {
    free_queue_processes(schedule->suspended_queue);
    free(schedule->suspended_queue);
  }

  if (schedule->terminated_queue != NULL) {
    free_queue_processes(schedule->terminated_queue);
    free(schedule->terminated_queue);
  }

  free(schedule);
}
