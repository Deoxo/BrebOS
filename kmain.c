#include "fb.h"
#include "gdt.h"
#include "interrupts.h"
#include "multiboot.h"
#include "memory.h"
#include "system.h"
#include "process.h"

gdt_entry_t gdt[GDT_ENTRIES];
gdt_descriptor_t gdt_descriptor;

idt_entry_t idt[256];
idt_descriptor_t idt_descriptor;

#include "clib/stdio.h"

//Todo: Investigate about whether page 184 (VGA buffer start address is mapped twice)
//Todo: same for page pde 771 pte11 at the end of run_module
int kmain([[maybe_unused]] unsigned int ebx)
{
	disable_interrupts();

	// Initialize framebuffer
	fb_clear_screen();


	printf("Setting up GDT\n");
	gdt_init(&gdt_descriptor, gdt);
	fb_ok();

	printf("Remapping PIC\n");
	pic_init();
	fb_ok();
	
	printf("Setting up IDT\n");
	idt_init(&idt_descriptor, idt);
	fb_ok();
	
	printf("Enabling interrupts\n");
	enable_interrupts();
	fb_ok();
	
	printf("Initialize memory\n");
	init_mem((multiboot_info_t*) (ebx + 0xC0000000));
	fb_ok();

	printf("Setting up process handling\n");
	processes_init();
	fb_ok();

	// Do stuff
	const unsigned int N = 100000;
	int* a = malloc(N * sizeof (int));
	for (unsigned int i = 0; i < N; i++)
		a[i] = 2;
	int* b = malloc(sizeof (int));
	*b = 2;

	free(b);
	free(a);

	start_module(0,  0);
	schedule();
	//fb_clear_screen();

	return shutdown();
}
