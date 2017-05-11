////////////////////////////////////////////////////////////////////////////
//
// File             : 473_mm.c
// Description      : Construct and compare two page replacement algorithms to manage the limited physical memory
//
// Author           : Sparsh Saxena and Shawn Varughese
// Last Modified    : 12/08/2016
//

// Headers
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include "473_mm.h"

// Macros
#define FIFO    1
#define CLOCK   2

// Structures
typedef struct pageNode pageNode_t;
struct pageNode {
	int pageNumber; // in memory or not
	void *start;
	int reference_bit;
	int dirtyBit;
	pageNode_t *next;
};

//Fuction Declaration
void mySigHandler(int sigNum, siginfo_t *st, void *unused);
void* signal_addr_to_page_addr(void* signal_addr);
int page_addr_to_page_num(void* page_addr);
pageNode_t* create_new_page(void* start_addr, int page_num);
void add_to_end_of_phy_mem(pageNode_t* new_page_addr);
int get_physical_mem_length();
void remove_first_element_of_phy_mem();
pageNode_t* search_in_phy_mem(void* sig_addr);
pageNode_t* get_tail();

pageNode_t* physical_mem = NULL;

struct sigaction sa; // using struct from signal.h

// Global Variables
int myType;
void *myVMStart; // start of memory
int myVMSize;
int myNumFrames;
int myPageSize; // bytes in a page
int myNumPages;

int numFaults = 0;
int numWriteBacks = 0;

unsigned long total_sigsevs = 0;
unsigned long total_npage_faults = 0;
int total_npage_evicts = 0;

int* page_evicts = NULL; //Array

// Functions to Implement
void mm_init(void *vm, int vm_size, int n_frames, int page_size , int policy)
{
	myVMStart = vm;
	myVMSize = vm_size;
	myNumFrames = n_frames;
	myPageSize = page_size;
	myType = policy;

	//##### Setup for page evicts function #####//
	int max_pages = myVMSize/myPageSize;
	page_evicts = malloc(max_pages*sizeof(int));

	int i;
	for(i = 0; i < max_pages; i++)
	{
		page_evicts[i] = 0;
	}
	//##########################################//

	//int mprotect(void *addr, size_t len, int prot);
	mprotect(vm, vm_size, PROT_NONE); // PROT_NONE for right

	sa.sa_flags = SA_SIGINFO; //To use sa_sigaction as handler 
	
	//int sigemptyset(sigset_t *set);
	sigset_t *set = &sa.sa_mask;
	sigemptyset(set);

	//now bind it with 'mySigHandler' function
	sa.sa_sigaction = &mySigHandler;

	//int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
	if (sigaction(SIGSEGV, &sa, NULL) == -1) // error check/register signals
	{
		printf("ERROR: sigaction in mm_init failed\n");
	}
}

// Functions to Implement
void mySigHandler(int sigNum, siginfo_t *st, void *unused)
{
	total_sigsevs++;

	if (myType == FIFO)
	{
		pageNode_t* page_addr_in_phy_mem = search_in_phy_mem(st->si_addr);
		if(page_addr_in_phy_mem != NULL)
		{
			//write
			page_addr_in_phy_mem->dirtyBit = 1; // set bit to one when write
			if (mprotect(page_addr_in_phy_mem -> start, myPageSize, PROT_WRITE) == -1)
				printf("ERROR: mprotect for PROT_WRITE in FIFO failed\n");
			return;
		}

		total_npage_faults++;

		if(get_physical_mem_length() < myNumFrames)
		{
			//add frame
			void* start_address = signal_addr_to_page_addr(st->si_addr);
			int page = page_addr_to_page_num(start_address);
			pageNode_t* new_page_addr = create_new_page(start_address, page);
			add_to_end_of_phy_mem(new_page_addr);

			if (mprotect(start_address, myPageSize, PROT_READ) == -1)
				printf("ERROR: mprotect for FIFO failed\n");
		}
		else
		{
			//evict 
			if (mprotect(physical_mem->start, myPageSize, PROT_NONE) == -1)
				printf("ERROR: mprotect, before evicting in fifo, failed\n");
			page_evicts[physical_mem->pageNumber]++;
			remove_first_element_of_phy_mem();

 			//add
			void* start_address = signal_addr_to_page_addr(st->si_addr);
			int page = page_addr_to_page_num(start_address);
			pageNode_t* new_page_addr = create_new_page(start_address, page);
			add_to_end_of_phy_mem(new_page_addr);

			if (mprotect(start_address, myPageSize, PROT_READ) == -1)
				printf("ERROR: mprotect for FIFO failed\n");
		}

	} 
	else if(myType == CLOCK) {

		pageNode_t* page_addr_in_phy_mem = search_in_phy_mem(st->si_addr);
		if(page_addr_in_phy_mem != NULL){ // already in memory
			//write
			page_addr_in_phy_mem->dirtyBit = 1; // set bit to one when write
			if (mprotect(page_addr_in_phy_mem -> start, myPageSize, PROT_WRITE) == -1)
				printf("ERROR: mprotect for PROT_WRITE in CLOCK failed\n");
			return;
		}
				// need to PROT_NONE, before reading page?
				// Once reference is changed to 1, set PROT to READ?
		total_npage_faults++;

		if(get_physical_mem_length() < myNumFrames) { // List is not full

			void* start_address = signal_addr_to_page_addr(st->si_addr);
			int page = page_addr_to_page_num(start_address);
			pageNode_t* new_page_addr = create_new_page(start_address, page);
			add_to_end_of_phy_mem(new_page_addr);

			if (mprotect(start_address, myPageSize, PROT_READ) == -1)
				printf("ERROR: mprotect for CLOCK failed\n");
		}
		else { // List is Full
			// evict and add using clock
			pageNode_t* page_to_evict;
			pageNode_t* tail = get_tail();
			pageNode_t* current = physical_mem;

			while (physical_mem->reference_bit != 0) {
				tail->next = physical_mem;
				physical_mem -> reference_bit = 0;
				tail = physical_mem;
				physical_mem = physical_mem -> next;
				tail->next = NULL;
			}
			if (mprotect(physical_mem->start, myPageSize, PROT_NONE) == -1)
				printf("ERROR: mprotect for CLOCK failed\n");
			page_evicts[physical_mem->pageNumber]++;
			remove_first_element_of_phy_mem();

			// add
			void* start_address = signal_addr_to_page_addr(st->si_addr);
			int page = page_addr_to_page_num(start_address);
			pageNode_t* new_page_addr = create_new_page(start_address, page);
			add_to_end_of_phy_mem(new_page_addr);

			if (mprotect(start_address, myPageSize, PROT_READ) == -1)
				printf("ERROR: mprotect for CLOCK failed\n");
		}
	}
	else
	{
		printf("ERROR: Policy is neither FIFO nor CLOCK\n");
	}
}

pageNode_t* create_new_page(void* start_addr, int page_num) { 	

    	pageNode_t* newPage = malloc(sizeof(pageNode_t));

    	newPage->start = start_addr;
    	newPage->pageNumber = page_num;
    	newPage->reference_bit = 1;
	newPage->dirtyBit = 0;
    	newPage->next = NULL;

    	return newPage;
}

unsigned long mm_nsigsegvs()
{
	return total_sigsevs;
}

int mm_report_npage_evicts(int page_num)
{
	return page_evicts[page_num];
}

unsigned long mm_report_npage_faults()
{
	return total_npage_faults;
}

unsigned long mm_report_nwrite_backs()
{
	return numWriteBacks;
}

int mm_report_nframe_evicts(int i)
{
	/* Do Not Have to Implement */
	return -1;
}

void* signal_addr_to_page_addr(void* signal_addr) {
	int page_num = 0;
	void* page_addr;

	page_num = ((int)signal_addr - (int)myVMStart )/myPageSize;
	page_addr = myVMStart + (page_num * myPageSize);

	return page_addr;
}

int page_addr_to_page_num(void* page_addr) {
	return (page_addr - myVMStart)/myPageSize;
}

void add_to_end_of_phy_mem(pageNode_t* new_page_addr) {
	pageNode_t* current = NULL;
	current = physical_mem;

	if(physical_mem == NULL) {
		physical_mem = new_page_addr;
	} 
	else {
		while(current -> next != NULL)
		{
			current = current -> next;
		}
    	current->next = new_page_addr;
	}
}

int get_physical_mem_length(){

	int count = 0;  // Initialize count
	pageNode_t* current = physical_mem;  // Initialize current
	while (current != NULL)
	{
		count++;
		current = current->next;
	}
	return count;
}

void remove_first_element_of_phy_mem() {
	if (physical_mem->dirtyBit == 1)
	{
		numWriteBacks++; // if page modified, then write_back
	}
	pageNode_t* current = physical_mem;
	physical_mem = physical_mem -> next;
	free(current);
}

pageNode_t* search_in_phy_mem(void* sig_addr) {
	void* page_start_address = signal_addr_to_page_addr(sig_addr);
	pageNode_t* current = NULL;
	current = physical_mem;

	if(physical_mem == NULL){
		return NULL;
	}
	else {
		while(current != NULL){
			if(current->start == page_start_address){
				return current;
			}
			else {
				current = current->next;
			}
		}
		return NULL;
	}
}

pageNode_t* get_tail() {
	pageNode_t* current = physical_mem;

	while(current->next != NULL){
		current = current->next;
	}
	return current;
}
