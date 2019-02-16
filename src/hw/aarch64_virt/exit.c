void qemu_exit() {
  asm volatile (
    "mov x1, #0x26\n\t"        // 0x20026 == ADP_Stopped_ApplicationExit
    "movk x1, #2, lsl #16\n\t"
    "str x1, [sp,#0]\n\t"
    "mov x0, #0\n\t"           // Exit status code 0
    "str x0, [sp,#8]\n\t"
    "mov x1, sp\n\t"           // address of parameter block (unused here)
    "mov w0, #0x18\n\t"        // SYS_EXIT
    "hlt 0xf000\n\t"           // do semihosting call
  );
}