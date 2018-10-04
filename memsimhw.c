//
// Virual Memory Simulator Homework
// Two-level page table system
// Inverted page table with a hashing system 
// Student Name: Moon Junoh
// Student Number: B611062
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#define PAGESIZEBITS 12			// page size = 4Kbytes
#define VIRTUALADDRBITS 32		// virtual address space size = 4Gbytes

#define LINE printf("%d\n", __LINE__)

typedef struct pageTableEntry {
	int level;				// page table level (1 or 2)
	char valid;
	struct pageTableEntry *secondLevelPageTable;	// valid if this entry is for the first level page table (level = 1)
	int frameNumber;								// valid if this entry is for the second level page table (level = 2)
} *Page;

typedef struct framePage {
	int number;			// frame number
	int pid;			// Process id that owns the frame
	int virtualPageNumber;			// virtual page number using the frame
	struct framePage *lruLeft;	// for LRU circular doubly linked list
	struct framePage *lruRight; // for LRU circular doubly linked list
} *Frame;

typedef struct invertedPageTableEntry {
	int pid;					// process id
	int virtualPageNumber;		// virtual page number
	int frameNumber;			// frame number allocated
	struct invertedPageTableEntry *next;
} *Ipage;

typedef struct procEntry {
	char* traceName;			// the memory trace name
	int pid;					// process (trace) id
	int ntraces;				// the number of memory traces
	int num2ndLevelPageTable;	// The 2nd level page created(allocated);
	int numIHTConflictAccess; 	// The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;		// The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;		// The number of Non Empty Inverted Hash Table Accesses
	int numPageFault;			// The number of page faults
	int numPageHit;				// The number of page hits
	struct pageTableEntry *firstLevelPageTable;
	FILE* tracefp;
}* Proc;


Frame oldestFrame; // the oldest frame pointer


int firstLevelBits, phyMemSizeBits, numProcess;

void initPhyMem(Frame phyMem, int nFrame) {
	int i;
	for(i = 0; i < nFrame; i++) {
		phyMem[i].number = i;
		phyMem[i].pid = -1;
		phyMem[i].virtualPageNumber = -1;
		phyMem[i].lruLeft = &phyMem[(i-1+nFrame) % nFrame];
		phyMem[i].lruRight = &phyMem[(i+1+nFrame) % nFrame];
	}

	oldestFrame = &phyMem[0];

}


uint32_t firstPageIndex(const uint32_t page)
{
	return page >> (VIRTUALADDRBITS - firstLevelBits);
}

uint32_t secondPageIndex(const uint32_t page)
{
	const uint32_t secondLevelBits = VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits;
	return (page << firstLevelBits) >> (firstLevelBits + PAGESIZEBITS);
}

void replacePage(const Proc procTable, const Proc proc, int level, const uint32_t addr)
{
	const uint32_t secondLevelBits = VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits,
		  secondPageSize = 1u << secondLevelBits;

	Page fir_page = proc->firstLevelPageTable + firstPageIndex(addr);

	if(level == 1) /* make second level page */
	{
		//make second level pages
		fir_page->secondLevelPageTable = calloc(secondPageSize, sizeof(struct pageTableEntry));
		int i=0;
		for(i=0; i < secondPageSize; ++i)
			fir_page->secondLevelPageTable[i].level = 2;
		fir_page->valid = 1;

		proc->num2ndLevelPageTable++;

	}
	else /* second level page: get frame */
	{
		//get frame
		if(oldestFrame->pid != -1) /* used frame */
		{
			uint32_t fir_idx = firstPageIndex(oldestFrame->virtualPageNumber),
					 snd_idx = secondPageIndex(oldestFrame->virtualPageNumber);
			procTable[oldestFrame->pid].firstLevelPageTable[fir_idx].secondLevelPageTable[snd_idx].valid = 0;
		}

		oldestFrame->pid = proc->pid;
		oldestFrame->virtualPageNumber = addr;
		

		Page sndPage = fir_page->secondLevelPageTable + secondPageIndex(addr);
		sndPage->frameNumber = oldestFrame->number;
		sndPage->valid = 1;

		oldestFrame = oldestFrame->lruRight;
	}
}

void secondLevelVMSim(struct procEntry* procTable, struct framePage* phyMemFrames) {
	int i = 0;
	//calculate page bits and sizes;
	const uint32_t firstPageSize = 1u << firstLevelBits,
		  secondLevelBits = VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits, 	/*virt_addr(32) - offset - 1st level bits*/
		  secondPageSize = 1u << secondLevelBits;

	//create a first-level page
	for(i = 0; i < numProcess; ++i)
	{
		procTable[i].firstLevelPageTable = calloc(firstPageSize, sizeof(struct pageTableEntry));
		int j=0;
		for(j=0; j < firstPageSize; ++j)
			procTable[i].firstLevelPageTable[j].level = 1;
	}

	//begin simulating 2-level page system
	//one cycle is given for each process
	int memoryReads = 1000000; 	//one million
	while(memoryReads--)
	{
		//for each process
		for(i=0; i < numProcess; i++) 
		{
			Proc proc = procTable + i;
			//access memory once
			uint32_t addr; 
			char action; 	//maybe unused
			fscanf(proc->tracefp, "%x %c", &addr, &action);
			++proc->ntraces;


			//check whether page fault arises or not
			uint32_t fir_idx = firstPageIndex(addr),
					 snd_idx = secondPageIndex(addr);
			Page firPage = proc->firstLevelPageTable + fir_idx;
			if (firPage->valid)
			{
				Page sndPage = firPage->secondLevelPageTable + snd_idx;
				if(sndPage->valid) /* page hit */
				{
					proc->numPageHit++;
				}
				else /* 2nd page fault */
				{
					proc->numPageFault++;
					replacePage(procTable, proc, 2, addr);
				}
			}
			else /*1st page fault*/
			{
				proc->numPageFault++;
				replacePage(procTable, proc, 1, addr);
				replacePage(procTable, proc, 2, addr);
			}

			//You can get physical memory!
			//do whatever you want!
		}
	}

	//display results
	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n",i,procTable[i].num2ndLevelPageTable);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}

	//release page table
	for(i=0; i < numProcess; ++i)
	{
		int j;
		for(j=0; j < firstPageSize; ++j)
			free(procTable[i].firstLevelPageTable[j].secondLevelPageTable);
		free(procTable[i].firstLevelPageTable);
	}
}

void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame) {
	int i=0;
	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n",i,procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}
}

int main(int argc, char *argv[]) {
	int i;

	if (argc < 4) {
	     printf("Usage : %s firstLevelBits PhysicalMemorySizeBits TraceFileNames\n",argv[0]); 
		 exit(1);
	}

	sscanf(argv[2], "%d", &phyMemSizeBits);
	if (phyMemSizeBits < PAGESIZEBITS) {
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n",phyMemSizeBits,PAGESIZEBITS); 
		exit(1);
	}

	sscanf(argv[1], "%d", &firstLevelBits);
	// secondLevelBits >= 0
	if (VIRTUALADDRBITS <= PAGESIZEBITS + firstLevelBits) {
		printf("firstLevelBits %d is too Big\n",firstLevelBits); 
		exit(1);
	}
	
	// initialize procTable for two-level page table
	numProcess = argc - 3;
	Proc procTable = calloc(numProcess, sizeof(struct procEntry));
	for(i = 0; i < numProcess; i++) {
		// opening a tracefile for the process
		printf("process %d opening %s\n",i,argv[i+3]);
		procTable[i].traceName = argv[i+3];
		procTable[i].pid = i;
		procTable[i].tracefp = fopen(procTable[i].traceName, "r");
	}

	//initialize frame(physical memory)
	int nFrame = (1<<(phyMemSizeBits-PAGESIZEBITS)); 
	assert(nFrame>0);

	Frame frames = malloc(nFrame * sizeof(struct framePage));
	initPhyMem(frames, nFrame);

	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n",nFrame, (1L<<phyMemSizeBits));
	
	printf("=============================================================\n");
	printf("The 2nd Level Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	secondLevelVMSim(procTable, frames);

	// initialize procTable for the inverted Page Table
	for(i = 0; i < numProcess; i++) {
		// rewind tracefiles
		//rewind(procTable[i].tracefp);
		procTable[i].numPageFault = 0;
		procTable[i].numPageHit = 0;
		fseek(procTable[i].tracefp, 0, SEEK_SET);
	}

	printf("=============================================================\n");
	printf("The Inverted Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");
	

	//release memory and file
	for(i=0; i<numProcess; ++i)
		fclose(procTable[i].tracefp);

	free(procTable);
	free(frames);
	return 0;
}
