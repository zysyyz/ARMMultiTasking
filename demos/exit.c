#include "thread.h"
#include "util.h"
#include <stddef.h>

void work(int num) {
  for (int i = 0; i < num; ++i) {
    yield();
  }
}

void counter() {
  int our_id = get_thread_id();

  // Since we're the last thread added, we're the upper bound on ID
  for (int i = 0; i < our_id; ++i) {
    ThreadState state;
    thread_join(i, &state);
    assert(state == finished);
    log_event("a thread exited");
  }
}

void setup(void) {
  config.log_scheduler = false;

  ThreadArgs ta1 = make_args(2, 0, 0, 0);
  add_named_thread_with_args(work, NULL, ta1);

  ThreadArgs ta2 = make_args(4, 0, 0, 0);
  add_named_thread_with_args(work, NULL, ta2);

  add_thread(counter);
}
