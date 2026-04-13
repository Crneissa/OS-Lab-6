/*
 * SOFE 3950 - Lab 6
 * Virtual Memory Manager
 *
 * This program converts logical addresses to physical addresses.
 * It uses a TLB, page table, and reads from a backing store if needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* constants */
#define PAGE_SIZE        256
#define NUM_PAGES        256
#define NUM_FRAMES       256
#define TLB_SIZE         16
#define PHYSICAL_MEM     65536
#define BACKING_STORE    "BACKING_STORE.bin"

/* page table entry */
typedef struct {
    int frame_number;
    int valid;
} PageTableEntry;

/* TLB entry */
typedef struct {
    int page_number;
    int frame_number;
} TLBEntry;

/* global variables */
static signed char    physical_memory[PHYSICAL_MEM];
static PageTableEntry page_table[NUM_PAGES];
static TLBEntry       tlb[TLB_SIZE];

static int tlb_count    = 0;
static int tlb_fifo_idx = 0;
static int next_frame   = 0;

/* stats */
static int total_addresses = 0;
static int page_faults     = 0;
static int tlb_hits        = 0;

/* initialize page table */
static void init_page_table(void)
{
    // go through every page and mark it as empty
    for (int i = 0; i < NUM_PAGES; i++) {
        page_table[i].frame_number = -1;
        page_table[i].valid = 0;
    }
}

/* initialize TLB */
static void init_tlb(void)
{
    // loop through the TLB and clear all entries
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].page_number = -1;
        tlb[i].frame_number = -1;
    }
}

/* check TLB */
static int tlb_lookup(int page_number)
{
    // check each entry to see if the page exists
    for (int i = 0; i < TLB_SIZE; i++) {
        // if we find the page, return its frame
        if (tlb[i].page_number == page_number) {
            return tlb[i].frame_number;
        }
    }
    return -1;
}

/* insert into TLB */
static void tlb_insert(int page_number, int frame_number)
{
    // first check if the page is already in the TLB
    for (int i = 0; i < TLB_SIZE; i++) {
        // if it exists, just update it
        if (tlb[i].page_number == page_number) {
            tlb[i].frame_number = frame_number;
            return;
        }
    }

    // if it's not there, replace the next entry using FIFO
    tlb[tlb_fifo_idx].page_number = page_number;
    tlb[tlb_fifo_idx].frame_number = frame_number;
    tlb_fifo_idx = (tlb_fifo_idx + 1) % TLB_SIZE;

    // make sure we don't go over the TLB size
    if (tlb_count < TLB_SIZE) {
        tlb_count++;
    }
}

/* handle page fault */
static int handle_page_fault(int page_number, FILE *backing_store)
{
    int frame = next_frame;
    next_frame++;

    // move to the correct position in the file
    if (fseek(backing_store, page_number * PAGE_SIZE, SEEK_SET) != 0) {
        printf("Error seeking page %d\n", page_number);
        exit(1);
    }

    // read the page into memory
    if (fread(&physical_memory[frame * PAGE_SIZE],
              sizeof(signed char), PAGE_SIZE, backing_store) != PAGE_SIZE) {
        printf("Error reading page %d\n", page_number);
        exit(1);
    }

    // update page table with new info
    page_table[page_number].frame_number = frame;
    page_table[page_number].valid = 1;

    page_faults++;
    return frame;
}

/* translate address */
static int translate(int logical_address, signed char *value,
                     FILE *backing_store)
{
    // get page number and offset
    int masked = logical_address & 0xFFFF;
    int page_number = (masked >> 8) & 0xFF;
    int offset = masked & 0xFF;

    total_addresses++;

    // check TLB first
    int frame = tlb_lookup(page_number);

    // if found in TLB
    if (frame != -1) {
        tlb_hits++;
    } else {
        // if not in TLB, check page table

        // if page is not in memory, we have a page fault
        if (!page_table[page_number].valid) {
            frame = handle_page_fault(page_number, backing_store);
        } else {
            // otherwise just use the frame from page table
            frame = page_table[page_number].frame_number;
        }

        // add this mapping into the TLB
        tlb_insert(page_number, frame);
    }

    // calculate physical address
    int physical_address = frame * PAGE_SIZE + offset;
    *value = physical_memory[physical_address];

    return physical_address;
}

int main(void)
{
    FILE *backing_store = fopen(BACKING_STORE, "rb");

    // check if file opened correctly
    if (!backing_store) {
        printf("Error opening file\n");
        return 1;
    }

    // initialize everything
    init_page_table();
    init_tlb();
    memset(physical_memory, 0, sizeof(physical_memory));

    int logical_address;

    // keep reading addresses until input ends
    while (scanf("%d", &logical_address) == 1) {
        signed char value;
        int physical_address = translate(logical_address, &value, backing_store);

        printf("Virtual address: %d Physical address: %d Value: %d\n",
               logical_address, physical_address, (int)value);
    }

    fclose(backing_store);

    // print final stats
    printf("Page Fault Rate = %.3f\n", (double)page_faults / total_addresses);
    printf("TLB Hit Rate = %.3f\n", (double)tlb_hits / total_addresses);

    return 0;
}
