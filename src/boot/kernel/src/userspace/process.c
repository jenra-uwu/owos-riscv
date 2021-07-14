#include "../lib/memory.h"
#include "process.h"

pid_t MAX_PID = 10000;
pid_t current_pid = 1;
process_t* process_table;

unsigned long long JOB_QUEUE_SIZE = 4096;
unsigned long long job_queue_pos = 0;
pid_t* job_queue;

// init_process_table() -> void
// Initialises the process table.
void init_process_table() {
    unsigned long long page_num = (MAX_PID * sizeof(process_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    process_table = alloc_page(page_num);
    MAX_PID = page_num * PAGE_SIZE / sizeof(process_t);

    page_num = (JOB_QUEUE_SIZE * sizeof(pid_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    job_queue = alloc_page(page_num);
    JOB_QUEUE_SIZE = page_num * PAGE_SIZE / sizeof(pid_t);
}

// spawn_process(pid_t) -> pid_t
// Spawns a process given its parent process. Returns 0 if unsuccessful.
pid_t spawn_process(pid_t parent_pid) {
    if (current_pid < MAX_PID) {
        process_table[current_pid] = (process_t) {
            .pid = current_pid,
            .parent_pid = parent_pid,
            .state = PROCESS_STATE_WAIT,
            .mmu_data = (void*) 0,
            .pc = 0,
            .xs = { 0 },
            .fs = { 0.0 }
        };
        pid_t pid = current_pid;
        current_pid++;
        return pid;
    }

    for (pid_t i = 1; i < MAX_PID; i++) {
        if (process_table[i].state == PROCESS_STATE_DEAD) {
            process_table[i] = (process_t) {
                .pid = i,
                .parent_pid = parent_pid,
                .state = PROCESS_STATE_WAIT,
                .mmu_data = (void*) 0,
                .pc = 0,
                .xs = { 0 },
                .fs = { 0.0 }
            };
            return i;
        }
    }

    return 0;
}

// fetch_process(pid_t) -> process_t*
// Fetches a process from the process table.
process_t* fetch_process(pid_t pid) {
    return &process_table[pid];
}

// load_elf_as_process(pid_t, elf_t*) -> pid_t
// Uses an elf file as a process.
pid_t load_elf_as_process(pid_t parent_pid, elf_t* elf, unsigned int stack_page_count) {
    pid_t pid = spawn_process(parent_pid);
    if (pid == 0)
        return pid;

    process_t* process = fetch_process(pid);
    process->file_descriptors = alloc_page(2);
    process->mmu_data = create_mmu_top();
    map_mmu(process->mmu_data, process->file_descriptors, process->file_descriptors, MMU_FLAG_READ | MMU_FLAG_WRITE);

    void* last_pointer = 0;
    for (int i = 0; i < elf->header.program_header_num; i++) {
        unsigned long long j;
        void* ptr = (void*) elf->program_headers[i].virtual_address;
        for (j = 0; j < elf->program_headers[i].file_size; j += MMU_PAGE_SIZE) {
            void* page = alloc_page_mmu(process->mmu_data, ptr, MMU_FLAG_EXEC | MMU_FLAG_READ | MMU_FLAG_USER | MMU_FLAG_WRITE);
            unsigned long long size = elf->program_headers[i].file_size - j;
            memcpy(page, elf->data[i] + j, size < MMU_PAGE_SIZE ? size : MMU_PAGE_SIZE);
            ptr += MMU_PAGE_SIZE;
        }

        if (last_pointer < ptr)
            last_pointer = ptr;
    }

    for (unsigned int i = 0; i < stack_page_count; i++) {
        alloc_page_mmu(process->mmu_data, last_pointer, MMU_FLAG_READ | MMU_FLAG_WRITE | MMU_FLAG_USER);
        last_pointer += MMU_PAGE_SIZE;
    }

    process->pc = elf->header.entry;
    process->xs[PROCESS_REGISTER_SP] = (unsigned long long) last_pointer;
    process->xs[PROCESS_REGISTER_FP] = (unsigned long long) last_pointer;

    return pid;
}

// process_init_kernel_mmu(pid_t) -> void
// Initialises a process's mmu by setting up the kernel part of hte mmu.
void process_init_kernel_mmu(pid_t pid) {
    process_t* process = fetch_process(pid);
    unsigned long long mmu;
    mmu_level_1_t* top = (void*) 0;
    asm volatile("csrr %0, satp" : "=r" (mmu));
    top = (void*) ((mmu & 0x00000fffffffffff) << 12);
    copy_mmu_globals(process->mmu_data, top);
}

// add_process_to_queue(pid_t) -> int
// Adds a process to the jobs queue. Returns true if added to the queue.
int add_process_to_queue(pid_t pid) {
    process_t* process = fetch_process(pid);
    for (unsigned long long i = 0; i < JOB_QUEUE_SIZE; i++) {
        if (job_queue[i] == 0) {
            job_queue[i] = pid;
            return 1;
        }
    }

    return 0;
}

// next_process_in_queue() -> pid_t
// Returns the next process in the queue, or 0 if no such process exists.
pid_t next_process_in_queue() {
    unsigned long long last_in_queue = job_queue_pos++;
        if (last_in_queue >= JOB_QUEUE_SIZE)
            last_in_queue %= JOB_QUEUE_SIZE;

    while (last_in_queue != job_queue_pos) {
        if (job_queue[job_queue_pos] != 0)
            return job_queue[job_queue_pos];
        job_queue_pos++;
        if (job_queue_pos >= JOB_QUEUE_SIZE)
            job_queue_pos %= JOB_QUEUE_SIZE;
    }

    return job_queue[last_in_queue];
}

// jump_to_process(pid_t) -> void
// Jumps to a given process.
void jump_to_process(pid_t pid) {
    // Get process info
    process_t* process = fetch_process(pid);
    process->state = PROCESS_STATE_RUNNING;

    // Set up page table
    unsigned long long mmu = (((unsigned long long) process->mmu_data) >> 12) | 0x8000000000000000;
    asm volatile("csrw satp, %0" : "=r" (mmu));
    mmu = 0;
    asm volatile("sfence.vma zero, %0" : "=r" (mmu));

    // process_switch_context(process_t*) -> void
    // Switches the current context to a process and continues execution of that process.
    void process_switch_context(process_t* process);

    process_switch_context(process);
}

