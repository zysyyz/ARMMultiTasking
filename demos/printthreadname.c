#include "print.h"
#include "thread.h"

void work(void) {
}

void setup(void) {
  config.log_scheduler = false;

  // Check we can escape %
  printf("%% Print Demo %%\n");

  char buf[100];
  const char* hex = "hex";
  sprintf(buf, "Sprintf %s: 0x%X\n", hex, 0xABAB);
  printf("%s", buf);

  // Use up some IDs
  unsigned padding = 8;
  for (int i = 0; i < padding; ++i) {
    add_thread(work);
    thread_cancel(i);
  }
  printf("Added then cancelled %u threads.\n", padding);

  // ID 7
  add_named_thread(work, "name_that_gets_cut_off");
  // The rest
  int tid = -1;
  do {
    tid = add_thread(work);
  } while (tid != -1);
}
