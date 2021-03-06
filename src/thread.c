#ifdef linux
#define _GNU_SOURCE
#include <pthread.h>
#endif
#include "print.h"
#include "thread.h"
#include "util.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef linux
#define THREAD_STACK_SIZE 1024 * STACK_SIZE
// 2 registers on AArch64
#define MONITOR_STACK_SIZE 2 * 8
#define STACK_CANARY       0xcafebeefdeadf00d
#endif

#define THREAD_NAME_SIZE      12
#define THREAD_MSG_QUEUE_SIZE 5

typedef struct {
  int src;
  int content;
} Message;

typedef struct {
#ifdef linux
  pthread_t self;
#else
  uint8_t* stack_ptr;
#endif
  // Not an enum directly because we need to know its size
  size_t state;
  int id;
  const char* name;
  // Deliberately not (void)
  void (*work)();
  ThreadArgs args;
  Message messages[THREAD_MSG_QUEUE_SIZE];
  Message* next_msg;
  Message* end_msgs;
  bool msgs_full;
#ifndef linux
  uint64_t bottom_canary;
  uint8_t stack[THREAD_STACK_SIZE];
  uint64_t top_canary;
#endif
} Thread;

__attribute__((section(".thread_structs"))) Thread all_threads[MAX_THREADS];
// Volatile is here for the pthread implementation
__attribute__((section(".thread_vars"))) Thread* volatile next_thread;
__attribute__((section(".scheduler_thread"))) Thread scheduler_thread;

__attribute__((section(".thread_vars")))
MonitorConfig config = {.destroy_on_stack_err = false,
                        .exit_when_no_threads = true,
                        .log_scheduler = true};

bool is_valid_thread(int tid) {
  return (tid >= 0) && (tid < MAX_THREADS) && all_threads[tid].id != -1;
}

static bool can_schedule_thread(int tid) {
  return is_valid_thread(tid) && (all_threads[tid].state == suspended ||
                                  all_threads[tid].state == init);
}

Thread* current_thread(void);

const char* get_thread_name(void) {
  return current_thread()->name;
}

int get_thread_id(void) {
  return current_thread()->id;
}

extern void setup(void);
void start_scheduler(void);
__attribute__((noreturn)) void entry(void) {
  // Invalidate all threads in the pool
  for (size_t idx = 0; idx < MAX_THREADS; ++idx) {
    all_threads[idx].id = -1;
  }

  // Call user setup
  setup();
  start_scheduler();

  __builtin_unreachable();
}

static void inc_msg_pointer(Thread* thr, Message** ptr) {
  ++(*ptr);
  // Wrap around from the end to the start
  if (*ptr == &(thr->messages[THREAD_MSG_QUEUE_SIZE])) {
    *ptr = &(thr->messages[0]);
  }
}

bool get_msg(int* sender, int* message) {
  // If message box is not empty, or it is full
  if (current_thread()->next_msg != current_thread()->end_msgs ||
      current_thread()->msgs_full) {
    *sender = current_thread()->next_msg->src;
    *message = current_thread()->next_msg->content;

    inc_msg_pointer(current_thread(), &current_thread()->next_msg);
    current_thread()->msgs_full = false;

    return true;
  }

  return false;
}

bool send_msg(int destination, int message) {
  if (
      // Invalid destination
      destination >= MAX_THREADS || destination < 0 ||
      all_threads[destination].id == -1 ||
      // Buffer is full
      all_threads[destination].msgs_full) {
    return false;
  }

  Thread* dest = &all_threads[destination];
  Message* our_msg = dest->end_msgs;
  our_msg->src = get_thread_id();
  our_msg->content = message;
  inc_msg_pointer(dest, &(dest->end_msgs));
  dest->msgs_full = dest->next_msg == dest->end_msgs;

  return true;
}

#ifdef linux
void thread_switch(void);
#else
extern void thread_switch(void);
void check_stack(void);
#endif

void thread_yield(Thread* next) {
  bool log = get_thread_id() != -1 || config.log_scheduler;

#ifndef linux
  check_stack();
#endif

  if (log) {
    log_event("yielding");
  }
  next_thread = next;
  thread_switch();
  if (log) {
    log_event("resuming");
  }
}

void yield(void) {
  // To be called in user threads
  thread_yield(&scheduler_thread);
}

void format_thread_name(char* out) {
  // fill with spaces (no +1 as we'll terminate it later)
  for (size_t idx = 0; idx < THREAD_NAME_SIZE; ++idx) {
    out[idx] = ' ';
  }

  const char* name = current_thread()->name;
  if (name == NULL) {
    int tid = get_thread_id();

    if (tid == -1) {
      const char* hidden = "<HIDDEN>";
      size_t h_len = strlen(hidden);
      size_t padding = THREAD_NAME_SIZE - h_len;
      strncpy(&out[padding], hidden, h_len);
    } else {
      // Just show the ID number (assume max 999 threads)
      char idstr[4];
      int len = sprintf(idstr, "%u", tid);
      strcpy(&out[THREAD_NAME_SIZE - len], idstr);
    }
  } else {
    size_t name_len = strlen(name);

    // cut off long names
    if (name_len > THREAD_NAME_SIZE) {
      name_len = THREAD_NAME_SIZE;
    }

    size_t padding = THREAD_NAME_SIZE - name_len;
    strncpy(&out[padding], name, name_len);
  }

  out[THREAD_NAME_SIZE] = '\0';
}

void log_event(const char* event) {
  char thread_name[THREAD_NAME_SIZE + 1];
  format_thread_name(thread_name);
  printf("Thread %s: %s\n", thread_name, event);
}

__attribute__((noreturn)) void do_scheduler(void) {
  while (1) {
    bool live_threads = false;

    for (size_t idx = 0; idx < MAX_THREADS; ++idx) {
      if (!can_schedule_thread(idx)) {
        continue;
      }

      if (all_threads[idx].id != idx) {
        log_event("thread ID and position inconsistent!");
        exit(1);
      }

      if (config.log_scheduler) {
        log_event("scheduling new thread");
      }

      live_threads = true;
      thread_yield(&all_threads[idx]);
      if (config.log_scheduler) {
        log_event("thread yielded");
      }
    }

    if (!live_threads && config.exit_when_no_threads) {
      if (config.log_scheduler) {
        log_event("all threads finished");
      }
      exit(0);
    }
  }
}

static bool set_thread_state(int tid, ThreadState state) {
  if (is_valid_thread(tid)) {
    all_threads[tid].state = state;
    return true;
  }
  return false;
}

bool thread_wake(int tid) {
  return set_thread_state(tid, suspended);
}

bool thread_cancel(int tid) {
  return set_thread_state(tid, cancelled);
}

bool yield_to(int tid) {
  if (!can_schedule_thread(tid)) {
    return false;
  }

  Thread* candidate = &all_threads[tid];
  thread_yield(candidate);
  return true;
}

bool yield_next(void) {
  // Yield to next valid thread, wrapping around the list
  int id = get_thread_id();

  // Check every other thread than this one
  size_t limit = id + MAX_THREADS;
  for (size_t idx = id + 1; idx < limit; ++idx) {
    size_t idx_in_range = idx % MAX_THREADS;
    if (can_schedule_thread(idx_in_range)) {
      thread_yield(&all_threads[idx_in_range]);
      return true;
    }
  }

  // Don't switch just continue to run current thread
  return false;
}

int add_thread(void (*worker)(void)) {
  return add_named_thread(worker, NULL);
}

int add_named_thread(void (*worker)(), const char* name) {
  ThreadArgs args = {0, 0, 0, 0};
  return add_named_thread_with_args(worker, name, args);
}

void init_thread(Thread* thread, int tid, const char* name,
                 void (*do_work)(void), ThreadArgs args) {
  // thread_start will jump to this
  thread->work = do_work;
  thread->state = init;

  thread->id = tid;
  thread->name = name;
  thread->args = args;

  // Start message buffer empty
  thread->next_msg = &(thread->messages[0]);
  thread->end_msgs = thread->next_msg;
  thread->msgs_full = false;

#ifndef linux
  thread->bottom_canary = STACK_CANARY;
  thread->top_canary = STACK_CANARY;
  // Top of stack
  size_t stack_ptr = (size_t)(&(thread->stack[THREAD_STACK_SIZE]));
  // Mask to align to 16 bytes for AArch64
  thread->stack_ptr = (uint8_t*)(stack_ptr & ~0xF);
#endif
}

#ifdef linux
void* thread_entry();
#endif
int add_named_thread_with_args(void (*worker)(), const char* name,
                               ThreadArgs args) {
  for (size_t idx = 0; idx < MAX_THREADS; ++idx) {
    if (all_threads[idx].id == -1) {
      init_thread(&all_threads[idx], idx, name, worker, args);

#ifdef linux
      pthread_create(&all_threads[idx].self, NULL, thread_entry, NULL);
#endif

      return idx;
    }
  }
  return -1;
}

void thread_wait(void) {
  current_thread()->state = waiting;
  // Call thread_switch directly to keep state intact
  next_thread = &scheduler_thread;
  thread_switch();
}

bool thread_join(int tid, ThreadState* state) {
  while (1) {
    // Initial ID is invalid, or it was destroyed due to stack err
    if (!is_valid_thread(tid)) {
      return false;
    }

    ThreadState ts = all_threads[tid].state;
    if (ts == finished || ts == cancelled) {
      if (state) {
        *state = ts;
      }
      return true;
    } else {
      yield();
    }
  }
}

#ifdef linux

void thread_switch_alrm() {
  next_thread = &scheduler_thread;
  thread_switch();
}

Thread* current_thread(void) {
  pthread_t self = pthread_self();
  for (int i = 0; i < MAX_THREADS; ++i) {
    if (all_threads[i].self == self) {
      return &all_threads[i];
    }
  }
  return &scheduler_thread;
}

void* thread_entry() {

  while (next_thread != current_thread()) {
    pthread_yield();
  }

  current_thread()->work(current_thread()->args.a1, current_thread()->args.a2,
                         current_thread()->args.a3, current_thread()->args.a4);

  // Yield back to the scheduler
  log_event("exiting");

  // Make sure we're not scheduled again
  current_thread()->state = finished;

  // Go back to scheduler
  next_thread = &scheduler_thread;

  return NULL; //!OCLINT
}

void thread_switch(void) {
  while (next_thread != current_thread()) {
    pthread_yield();
  }
}

void start_scheduler(void) {

  ThreadArgs args = {0, 0, 0, 0};
  init_thread(&scheduler_thread, -1, NULL, do_scheduler /*redundant*/, args);

  // Hack around us not having a dummy thread here
  // Start with an empty name to get <HIDDEN>
  log_event("starting scheduler");

  // Then properly name it
  scheduler_thread.name = "<scheduler>";

  pthread_create(&scheduler_thread.self, NULL, (void* (*)(void*))do_scheduler,
                 NULL);

  while (1) { //!OCLINT
  }
}

#else

// Don't care if this gets corrupted, we'll just reset it's stack anyway
__attribute__((section(".thread_structs"))) static Thread dummy_thread;

// Use these struct names to ensure that these are
// placed *after* the thread structs to prevent
// stack overflow corrupting them.
__attribute__((section(".thread_vars"))) Thread* _current_thread;
__attribute__((section(".thread_vars"))) size_t thread_stack_offset =
    offsetof(Thread, stack);

// Known good stack to save registers to while we check stack extent
// In a seperate section so we can garauntee it's alignement for AArch64
__attribute__((section(".monitor_vars")))
uint8_t monitor_stack[MONITOR_STACK_SIZE];
__attribute__((section(".thread_vars"))) uint8_t* monitor_stack_top =
    &monitor_stack[MONITOR_STACK_SIZE];

extern void thread_switch_initial(void);

Thread* current_thread(void) {
  return _current_thread;
}

void stack_extent_failed(void) {
  // current_thread is likely still valid here
  log_event("Not enough stack to save context!");
  exit(1);
}

void check_stack(void) {
  bool underflow = current_thread()->bottom_canary != STACK_CANARY;
  bool overflow = current_thread()->top_canary != STACK_CANARY;

  if (underflow || overflow) {
    // Don't schedule this again, or rely on its ID
    current_thread()->id = -1;
    current_thread()->name = NULL;

    if (underflow) {
      log_event("Stack underflow!");
    }
    if (overflow) {
      log_event("Stack overflow!");
    }

    if (config.destroy_on_stack_err) {
      /* Use the dummy thread to yield back to the scheduler
         without doing any more damage. */
      _current_thread = &dummy_thread;

      /* Reset dummy's stack ptr so repeated exits here doesn't
         corrupt *that* stack. */
      dummy_thread.stack_ptr = &dummy_thread.stack[THREAD_STACK_SIZE];

      next_thread = &scheduler_thread;
      /* Setting -1 here, instead of state=finished is fine,
         because A: the thread didn't actually finish
                 B: the thread struct is actually invalid */
      current_thread()->id = -1;
      thread_switch_initial();
    } else {
      exit(1);
    }
  }
}

__attribute__((noreturn)) void thread_start(void) {
  // Every thread starts by entering this function

  // Call thread's actual function
  current_thread()->work(current_thread()->args.a1, current_thread()->args.a2,
                         current_thread()->args.a3, current_thread()->args.a4);

  // Yield back to the scheduler
  log_event("exiting");

  // Make sure we're not scheduled again
  current_thread()->state = finished;

  /* You might think this is a timing issue.
     What if we're interrupted here?

     Well, we'd go to thread_switch, next_thread
     is set to the scheduler automatically.
     Since our state is finished, it won't be updated
     to suspended. Meaning, we'll never come back here.

     Which is just fine, since we were going to switch
     away anyway.
  */

  next_thread = &scheduler_thread;
  // Calling thread_switch directly so we don't print 'yielding'
  // TODO: we save state here that we don't need to
  thread_switch();

  __builtin_unreachable();
}

__attribute__((noreturn)) void start_scheduler(void) {
  ThreadArgs args = {0, 0, 0, 0};

  // Hidden so that the scheduler doesn't run itself somehow
  init_thread(&scheduler_thread, -1, "<scheduler>", do_scheduler, args);

  // Need a dummy thread here otherwise we'll try to write to address 0
  init_thread(&dummy_thread, -1, NULL, (void (*)(void))(0), args);

  // Actual current thread here, not the getter
  _current_thread = &dummy_thread;
  next_thread = &scheduler_thread;
  log_event("starting scheduler");
  thread_switch_initial();

  __builtin_unreachable();
}

#endif // ifndef linux
