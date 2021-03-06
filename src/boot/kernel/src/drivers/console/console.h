#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include "../filesystems/generic_file.h"

extern generic_filesystem_t console_fs;

// console_generic_file_write(generic_file_t*) -> int
// Reads a character from the console.
int console_generic_file_read(generic_file_t* _);

// console_generic_file_write(generic_file_t*, int) -> int
// Writes a character to the console.
int console_generic_file_write(generic_file_t* _, int c);

// console_puts(char*) -> void
// Puts a string onto the console.
void console_puts(char*);

// console_getc(void) -> char
// Gets a character from the console and echos it back.
char console_getc();

// console_getc_noecho() -> char
// Gets a character from the console without echoing it back.
char console_getc_noecho();

// console_printf(char*, ...) -> void
// Writes its arguments to the console according to the format string.
__attribute__((format(printf, 1, 2))) void console_printf(char* format, ...);

// console_put_hexdump(void*, unsigned long long)
// Dumps a hexdump onto the console.
void console_put_hexdump(void* data, unsigned long long size);

// console_readline(char*) -> char*
// Reads a line from console input.
char* console_readline(char* prompt);

#endif /* KERNEL_CONSOLE_H */

