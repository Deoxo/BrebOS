#ifndef INCLUDE_PROCESS_H
#define INCLUDE_PROCESS_H

#include "memory.h"
#include "interrupts.h"
#include "ELF.h"
#include "list.h"
#include "clib/stddef.h"

typedef unsigned int pid_t;

// Process is terminated but not freed
#define P_TERMINATED 1
// Process has been interrupted during a syscall
#define P_SYSCALL_INTERRUPTED 2
// Process is waiting for a key press
#define P_WAITING_KEY 4

class Scheduler;

class Process
{
	friend class Scheduler;

private:
	// Process page tables. Process can use all virtual addresses below the kernel virtual location at pde 768
	page_table_t page_tables[768];
	pdt_t pdt; // process page directory table

	unsigned int quantum, priority;

	unsigned int num_pages; // Num pages over which the process code spans
	unsigned int* pte; // Array of pte where the process code is loaded to

	pid_t pid; // PID
	pid_t ppid; // Parent PID

	unsigned int k_stack_top; // Top of syscall handlers' stack
	unsigned int flags; // Process state

	list* allocs; // list of memory blocks allocated by the process
public:
	cpu_state_t cpu_state; // Registers
	cpu_state_t k_cpu_state; // Syscall handler registers
	stack_state_t stack_state; // Execution context
	stack_state_t k_stack_state; // Syscall handler execution context

	ELF* elf;
private:

	/** Page aligned allocator **/
	void* operator new(size_t size);

	/** Page aligned allocator **/
	void* operator new[](size_t size);

	/** Page aligned free **/
	void operator delete(void* p);

	/** Page aligned free **/
	void operator delete[](void* p);

	/** Frees a terminated process */
	~Process();

	/**
	 * Sets up a dynamically linked process
	 * @param proc process
	 * @param grub_modules grub modules
	 * @return process set up, NULL if an error occurred
	 */
	bool dynamic_loading();

	/**
	* Load a lib in a process' address soace
	* @param p process to add libynlk to
	* @param lib_mod GRUB modules
	* @return libdynlk runtime entry point address
	*/
	bool load_lib(GRUB_module* lib_mod);

	/**
	 * Allocate space for libydnlk and add it to a process' address space
	 * @param elf32Ehdr ELF heder
	 * @param elf32Phdr program table header
	 * @param p process to add libdynlk to
	 * @return boolean indicating success state
	 */
	bool alloc_and_add_lib_pages_to_process(ELF& lib_elf);

	/**
	 * Allocate a process containing an ELF
	 * @return process struct, NULL if an error occurred
	 */
	static Process* allocate_proc_for_elf_module(GRUB_module* module);

public:
	/** Gets the process' PID */
	[[nodiscard]] pid_t get_pid() const;

	/**
	 * Terminates a process
	 */
	void terminate();

	/**
	 * Contiguous heap memory allocator
	 * @param n required memory quantity
	 * @return pointer to beginning of allocated heap memory, NULL if an error occurred
	 */
	[[nodiscard]] void* allocate_dyn_memory(uint n) const;

	/**
	 * Contiguous heap memory free
	 * @param ptr pointer to beginning of allocated heap memory
	 */
	void free_dyn_memory(void* ptr) const;

	/**
	 * Create a process to run an ELF executable
	 *
	 * @param module ELF Grub module id
	 * @return program's process
	 */
	static Process* from_ELF(GRUB_module* module);

	/**
	 * Sets a flag
	 * @param flag flag to set
	 */
	void set_flag(uint flag);

	/** Checks whether the program is terminated */
	[[nodiscard]] bool is_terminated() const;

	/** Checks whether the program is waiting for a key press */
	[[nodiscard]] bool is_waiting_key() const;

	/**
	 * Loads a flat binary in memory
	 *
	 * @param module Grub module id
	 * @param grub_modules grub modules
	 * @return program's process, NULL if an error occurred
	 */
	static Process* from_binary(unsigned int module, GRUB_module* grub_modules);
};

#endif //INCLUDE_PROCESS_H
