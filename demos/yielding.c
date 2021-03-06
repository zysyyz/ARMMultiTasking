#include "thread.h"
#include "util.h"

__attribute__((noreturn)) void thread_worker_1() {
  while (1) {
    for (int i = 0;; ++i) {
      if (i == 2) {
        log_event("working");
        log_event("exiting");
        exit(0);
      }
      yield();
    }
  }
}

__attribute__((noreturn)) void thread_worker_0() {
  while (1) {
    log_event("working");
    yield();
  }
}

void setup(void) {
  add_thread(thread_worker_0);
  add_thread(thread_worker_1);
}
