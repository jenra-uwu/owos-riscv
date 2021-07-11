#include "syscall.h"
#include "../drivers/console/console.h"

// user_syscall(unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long) -> void
// Does a syscall for a user mode process.
void user_syscall(
    unsigned long long syscall,
    unsigned long long a0,
    unsigned long long a1,
    unsigned long long a2,
    unsigned long long a3,
    unsigned long long a4,
    unsigned long long a5
) {
    console_printf("Called syscall #%llx with arguments:\n", syscall);
    console_printf("a0: %llx\n", a0);
    console_printf("a1: %llx\n", a1);
    console_printf("a2: %llx\n", a2);
    console_printf("a3: %llx\n", a3);
    console_printf("a4: %llx\n", a4);
    console_printf("a5: %llx\n", a5);

    switch (syscall) {
        case 0x00:
            console_puts("Called syscall 0! (Not factorial)\n");
            break;
        default:
            console_printf("Called unknown syscall %llx.\n", syscall);
            break;
    }
}

