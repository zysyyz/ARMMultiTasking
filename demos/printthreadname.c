#include "print.h"
#include "thread.h"
#include <stdint.h>

void work(void) {
}

void setup(void) {
  config.log_scheduler = false;

  // Check we can escape %
  printf("%% Print Demo %%\n");

  char buf[100];
  sprintf(buf, "Sprintf hex: 0x%X 0x%" PRIX64 "\n",
    0xABAB, (uint64_t)0xCAFEF00DDEADBEEF);
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
