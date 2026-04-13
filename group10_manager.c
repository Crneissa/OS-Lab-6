/*
 * SOFE 3950: Operating Systems - Lab 6
 * Virtual Memory Manager
 *
 * Translates logical addresses to physical addresses using:
 *   - A TLB (Translation Lookaside Buffer) with 16 entries (FIFO replacement)
 *   - A page table with 256 entries
 *   - Demand paging from BACKING_STORE.bin on page faults
 *
 * Virtual address space: 2^16 = 65,536 bytes
 * Page size / Frame size: 256 bytes (2^8)
 * Number of frames: 256
 * Physical memory size: 65,536 bytes
 *
 * Address structure (16-bit):
 *   Bits 15-8 : page number (8 bits)
 *   Bits  7-0 : page offset (8 bits)
 *
 * Usage:
 *   ./group1_manager < addresses.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Constants ──────────────────────────────────────────────────────────── */
#define PAGE_SIZE        256      /* bytes per page / frame                  */
#define NUM_PAGES        256      /* entries in the page table               */
#define NUM_FRAMES       256      /* physical memory frames                  */
#define TLB_SIZE         16       /* entries in the TLB                      */
#define PHYSICAL_MEM     65536    /* total physical memory (bytes)           */
#define BACKING_STORE    "BACKING_STORE.bin"

/* ── Data structures ────────────────────────────────────────────────────── */

/*
 * Page table entry.
 * frame_number : which physical frame holds this page (-1 = not loaded)
 * valid        : 1 if the page is currently in physical memory, 0 otherwise
 */
typedef struct {
    int  frame_number;
    int  valid;
} PageTableEntry;

/*
 * TLB entry.
 * page_number  : logical page number (-1 = empty slot)
 * frame_number : corresponding physical frame number
 */
typedef struct {
    int page_number;
    int frame_number;
} TLBEntry;

/* ── Globals ────────────────────────────────────────────────────────────── */
static signed char      physical_memory[PHYSICAL_MEM];  /* simulated RAM    */
static PageTableEntry   page_table[NUM_PAGES];           /* page table       */
static TLBEntry         tlb[TLB_SIZE];                   /* TLB              */
static int              tlb_count    = 0;   /* number of valid TLB entries   */
static int              tlb_fifo_idx = 0;   /* FIFO replacement pointer      */
static int              next_frame   = 0;   /* next free physical frame      */

/* Statistics */
static int total_addresses = 0;
static int page_faults     = 0;
static int tlb_hits        = 0;

/* ── Initialisation ─────────────────────────────────────────────────────── */

/* Initialise all page-table entries as invalid. */
static void init_page_table(void)
{
    for (int i = 0; i < NUM_PAGES; i++) {
        page_table[i].frame_number = -1;
        page_table[i].valid        = 0;
    }
}

/* Mark every TLB slot as empty. */
static void init_tlb(void)
{
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].page_number  = -1;
        tlb[i].frame_number = -1;
    }
}

/* ── TLB operations ─────────────────────────────────────────────────────── */

/*
 * Look up page_number in the TLB.
 * Returns the frame number on a hit, or -1 on a miss.
 */
static int tlb_lookup(int page_number)
{
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].page_number == page_number) {
            return tlb[i].frame_number;   /* TLB hit */
        }
    }
    return -1;   /* TLB miss */
}

/*
 * Insert (page_number, frame_number) into the TLB.
 * Uses FIFO replacement when the TLB is full.
 */
static void tlb_insert(int page_number, int frame_number)
{
    /* If this page is already in the TLB, update it in place. */
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].page_number == page_number) {
            tlb[i].frame_number = frame_number;
            return;
        }
    }

    /* Use FIFO slot (wraps around once TLB is full). */
    tlb[tlb_fifo_idx].page_number  = page_number;
    tlb[tlb_fifo_idx].frame_number = frame_number;
    tlb_fifo_idx = (tlb_fifo_idx + 1) % TLB_SIZE;

    if (tlb_count < TLB_SIZE) {
        tlb_count++;
    }
}

/* ── Page-fault handler ─────────────────────────────────────────────────── */

/*
 * Load page `page_number` from BACKING_STORE.bin into physical memory.
 * Updates the page table.  Returns the frame number used.
 */
static int handle_page_fault(int page_number, FILE *backing_store)
{
    int frame = next_frame;          /* allocate the next free frame         */
    next_frame++;                    /* (no replacement needed: 256 = 256)   */

    /* Seek to the correct position in the backing store. */
    if (fseek(backing_store, (long)page_number * PAGE_SIZE, SEEK_SET) != 0) {
        fprintf(stderr, "Error: fseek failed for page %d\n", page_number);
        exit(EXIT_FAILURE);
    }

    /* Read 256 bytes into the chosen frame. */
    if (fread(&physical_memory[frame * PAGE_SIZE],
              sizeof(signed char), PAGE_SIZE, backing_store) != PAGE_SIZE) {
        fprintf(stderr, "Error: fread failed for page %d\n", page_number);
        exit(EXIT_FAILURE);
    }

    /* Update the page table. */
    page_table[page_number].frame_number = frame;
    page_table[page_number].valid        = 1;

    page_faults++;
    return frame;
}

/* ── Address translation ────────────────────────────────────────────────── */

/*
 * Translate a logical address to a physical address.
 * Returns the physical address and writes the byte value to *value.
 */
static int translate(int logical_address, signed char *value,
                     FILE *backing_store)
{
    /* Mask to 16 bits, then extract page number and offset. */
    int masked      = logical_address & 0xFFFF;
    int page_number = (masked >> 8) & 0xFF;
    int offset      = masked & 0xFF;

    total_addresses++;

    /* 1. Check the TLB. */
    int frame = tlb_lookup(page_number);

    if (frame != -1) {
        /* TLB hit */
        tlb_hits++;
    } else {
        /* TLB miss – consult the page table. */
        if (!page_table[page_number].valid) {
            /* Page fault: load from backing store. */
            frame = handle_page_fault(page_number, backing_store);
        } else {
            frame = page_table[page_number].frame_number;
        }
        /* Bring the mapping into the TLB for future references. */
        tlb_insert(page_number, frame);
    }

    /* Compute the physical address and read the stored byte. */
    int physical_address = frame * PAGE_SIZE + offset;
    *value = physical_memory[physical_address];

    return physical_address;
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Open BACKING_STORE.bin for random-access reading. */
    FILE *backing_store = fopen(BACKING_STORE, "rb");
    if (!backing_store) {
        fprintf(stderr, "Error: cannot open %s\n", BACKING_STORE);
        return EXIT_FAILURE;
    }

    /* Initialise data structures. */
    init_page_table();
    init_tlb();
    memset(physical_memory, 0, sizeof(physical_memory));

    /* Process each logical address from stdin. */
    int logical_address;
    while (scanf("%d", &logical_address) == 1) {
        signed char value;
        int physical_address = translate(logical_address, &value, backing_store);

        printf("Virtual address: %d Physical address: %d Value: %d\n",
               logical_address, physical_address, (int)value);
    }

    fclose(backing_store);

    /* Print statistics (rounded to three decimal places). */
    printf("Page Fault Rate = %.3f\n", (double)page_faults  / total_addresses);
    printf("TLB Hit Rate = %.3f\n",    (double)tlb_hits      / total_addresses);

    return EXIT_SUCCESS;
}
