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
/*TODO: implement LRU
 *
 */

const int PAGESIZEBITS = 12;			// page size = 4Kbytes
const int VIRTUALADDRBITS = 32;			// virtual address space size = 4Gbytes

#define LINE printf("%d\n", __LINE__)
#define TRACE_TWICE

typedef struct pageTableEntry {
	int level;				// page table level (1 or 2)
	char valid;
	struct pageTableEntry *sndLvPageTable;	// valid if this entry is for the first level page table (level = 1)
	int frameNumber;								// valid if this entry is for the second level page table (level = 2)
} *Page;

typedef struct framePage {
	uint32_t number;			// frame number
	int pid;			// Process id that owns the frame
	uint32_t vpn;			// virtual page number using the frame
	struct framePage *lruLeft;	// for LRU circular doubly linked list
	struct framePage *lruRight; // for LRU circular doubly linked list
} *Frame;

typedef struct invertedPageTableEntry {
	int pid;					// process id
	uint32_t vpn;		// virtual page number
	int frameNumber;			// frame number allocated
	struct invertedPageTableEntry *next;
} *Ipage;

typedef struct procEntry {
	char* traceName;			// the memory trace name
	int pid;					// process (trace) id
	int ntraces;				// the number of memory traces
	int num2ndLvPageTable;	// The 2nd level page created(allocated);
	int numIHTConflictAccess; 	// The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;		// The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;		// The number of Non Empty Inverted Hash Table Accesses
	int numPageFault;			// The number of page faults
	int numPageHit;				// The number of page hits
	struct pageTableEntry *firstLvPageTable;
	FILE* tracefp;
}*Proc;


Frame oldestFrame; // the oldest frame pointer


int firstLevelBits, phyMemSizeBits, numProcess;

void initPhyMem(Frame phyMem, int nFrame) {
	int i;
	for (i = 0; i < nFrame; i++) {
		phyMem[i].number = i;
		phyMem[i].pid = -1;
		phyMem[i].vpn = -1;
		phyMem[i].lruLeft = &phyMem[(i - 1 + nFrame) % nFrame];
		phyMem[i].lruRight = &phyMem[(i + 1 + nFrame) % nFrame]; /* +nFrame ensures that the result of mod operation is bigger than 0 */
	}

	oldestFrame = phyMem;
}

void updateLRU(Frame recent)
{
	if (oldestFrame == recent) {
		oldestFrame = oldestFrame->lruRight;
	}
	else {
		Frame prev = recent->lruLeft,
			next = recent->lruRight;
		//pop recent frame
		prev->lruRight = next;
		next->lruLeft = prev;
		//recent frame : last link
		//  first, make a link between so-far last node and recent node
		oldestFrame->lruLeft->lruRight = recent;
		recent->lruLeft = oldestFrame->lruLeft;
		//  second, make a link between recent node and first node
		oldestFrame->lruLeft = recent;
		recent->lruRight = oldestFrame;
	}
}


uint32_t firstPageIndex(const uint32_t page)
{
	uint32_t sndLvBits = VIRTUALADDRBITS - firstLevelBits - PAGESIZEBITS,
		mask = ((1ULL << firstLevelBits) - 1) << (PAGESIZEBITS + sndLvBits);
	uint32_t ret = (page & mask) >> (PAGESIZEBITS + sndLvBits);
	assert(ret < (1u << firstLevelBits));
	return ret;
}

uint32_t secondPageIndex(const uint32_t page)
{
	uint32_t sndLvBits = VIRTUALADDRBITS - firstLevelBits - PAGESIZEBITS;
	uint32_t mask = ((1u << sndLvBits) - 1) << PAGESIZEBITS;
	uint32_t ret = (page & mask) >> PAGESIZEBITS;
	assert(ret < (1u << (VIRTUALADDRBITS - firstLevelBits - PAGESIZEBITS)));
	return ret;
}

uint32_t getVPN(uint32_t virt_addr);

void handlePageFault(const Proc procTable, const Proc proc, const uint32_t virt_addr)
{
	const uint32_t sndLvBits = VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits,
		secondPageSize = 1u << sndLvBits;

	Page firPage = proc->firstLvPageTable + firstPageIndex(virt_addr);
	assert(firPage);

	if (!firPage->valid) /* first level page: make second level page */
	{
		firPage->sndLvPageTable = (Page)calloc(secondPageSize, sizeof(struct pageTableEntry));
		assert(firPage->sndLvPageTable);
		uint32_t i = 0;
		for (i = 0; i < secondPageSize; ++i)
			firPage->sndLvPageTable[i].level = 2;
		firPage->valid = 1;

		proc->num2ndLvPageTable++;
	}

	Page sndPage = firPage->sndLvPageTable + secondPageIndex(virt_addr);
	assert(sndPage);
	if (!sndPage->valid) /* second level page: get frame */
	{
		if (oldestFrame->pid != -1) /* used frame */
		{
			uint32_t firPageNum = firstPageIndex(oldestFrame->vpn << PAGESIZEBITS),
				sndPageNum = secondPageIndex(oldestFrame->vpn << PAGESIZEBITS);

			procTable[oldestFrame->pid].firstLvPageTable[firPageNum].sndLvPageTable[sndPageNum].valid = 0;
			procTable[oldestFrame->pid].firstLvPageTable[firPageNum].sndLvPageTable[sndPageNum].frameNumber = -1;
		}

		oldestFrame->pid = proc->pid;
		oldestFrame->vpn = getVPN(virt_addr);


		sndPage->frameNumber = oldestFrame->number;
		sndPage->valid = 1;

	}
}

void secondLevelVMSim(Proc procTable, Frame phyMemFrames)
{
	int i = 0;
	//calculate page bits and sizes;
	const uint32_t firstPageSize = 1u << firstLevelBits;

	//create a first-level page
	for (i = 0; i < numProcess; ++i)
	{
		procTable[i].firstLvPageTable = (Page)calloc(firstPageSize, sizeof(struct pageTableEntry));
		uint32_t j = 0;
		for (j = 0; j < firstPageSize; ++j)
			procTable[i].firstLvPageTable[j].level = 1;
	}

	//begin simulating 2-level page system
	//one cycle is given for each process
	int memoryReads = 1000000; 	//one million
	while (memoryReads--)
	{
		//for each process
		for (i = 0; i < numProcess; i++)
		{
			Proc proc = procTable + i;
			//access memory once
			uint32_t virt_addr = 0;
			char action; 	//maybe unused
			fscanf(proc->tracefp, "%x %c", &virt_addr, &action);
			++proc->ntraces;


			//check whether page fault arises or not
			uint32_t firPageNum = firstPageIndex(virt_addr),
				sndPageNum = secondPageIndex(virt_addr),
				frameNum;
			Page firPage = proc->firstLvPageTable + firPageNum;

			if (firPage->valid && firPage->sndLvPageTable[sndPageNum].valid)
			{
				++proc->numPageHit;
				frameNum = firPage->sndLvPageTable[sndPageNum].frameNumber;
			}
			else /*page fault*/
			{
				proc->numPageFault++;
				frameNum = oldestFrame->number;
				handlePageFault(procTable, proc, virt_addr);
			}
			updateLRU(phyMemFrames + frameNum);

			//You can get physical memory!
			//do whatever you want!
		}
	}

	//display results
	for (i = 0; i < numProcess; i++) {
		printf("**** %s *****\n", procTable[i].traceName);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n", i, procTable[i].num2ndLvPageTable);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}

	//release page table
	for (i = 0; i < numProcess; ++i)
	{
		unsigned int j;
		for (j = 0; j < firstPageSize; ++j)
			free(procTable[i].firstLvPageTable[j].sndLvPageTable);
		free(procTable[i].firstLvPageTable);
	}
}

uint32_t getVPN(uint32_t virt_addr)
{
	uint32_t ret = virt_addr >> PAGESIZEBITS;
	return ret;
}

uint32_t hash(uint32_t pid, uint32_t vpn/* virtual page number */)
{
	const int nFrame = (1 << (phyMemSizeBits - PAGESIZEBITS));
	return (vpn + pid) % nFrame;
}

// equivalent to hash[proc] or hash.at(proc);
// also, log its results.
int hashAt(Ipage hashTable, Proc proc, uint32_t vpn)
{
	uint32_t page_idx = hash(proc->pid, vpn);
	//cf) list.at(0) is a dummy.
	Ipage ptr = hashTable[page_idx].next;

	if (ptr)
		proc->numIHTNonNULLAcess++;
	else
		proc->numIHTNULLAccess++;

	for (; ptr; ptr = ptr->next)
	{
		proc->numIHTConflictAccess++;
		if (ptr->pid == proc->pid && ptr->vpn == vpn)
		{
			return ptr->frameNumber;
		}
	}
	return -1;
}

// map.at(pid, vpn) = value (frame #)
void hashInsert(Ipage hashTable,
	Proc proc,
	uint32_t vpn /* virtual page number */,
	uint32_t value /* frame # */)
{
	Ipage head = hashTable + hash(proc->pid, vpn);
	Ipage new_elem = (Ipage)calloc(1, sizeof(struct invertedPageTableEntry));

	//store unique key to avoid collision
	new_elem->pid = proc->pid;
	new_elem->vpn = vpn;

	//store value
	new_elem->frameNumber = value;

	//set list order
	new_elem->next = head->next;
	head->next = new_elem;
}

void hashRemoveAt(Ipage hashTable, Proc proc, uint32_t vpn)
{
	uint32_t page_idx = hash(proc->pid, vpn);
	//cf) list.at(0) is a dummy.
	Ipage prev = hashTable + page_idx,
		ptr;
	for (ptr = prev->next; ptr; ptr = ptr->next, prev = prev->next)
	{
		if (ptr->pid == proc->pid && ptr->vpn == vpn)
		{
			prev->next = ptr->next;
			free(ptr);
			return;
		}
	}
}


void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame)
{
	int i = 0;
	//make IPT
	Ipage hashTable = (Ipage)malloc(nFrame * sizeof(struct invertedPageTableEntry));
	for (i = 0; i < nFrame; ++i)
	{
		hashTable[i].frameNumber = i;
		hashTable[i].pid = -1;
		hashTable[i].next = NULL;
		hashTable[i].vpn = 0;
	}

	uint32_t memoryReads = 1000000; 	//one million
	while (memoryReads--)
	{
		for (i = 0; i < numProcess; ++i)
		{
			Proc proc = procTable + i;
			uint32_t virt_addr;
			char action; 	//maybe unused
			fscanf(proc->tracefp, "%x %c", &virt_addr, &action);
			uint32_t vpn = getVPN(virt_addr);
#ifdef TRACE_TWICE
			++proc->ntraces;
#endif

			int frameNum = hashAt(hashTable, proc, vpn);
			assert(frameNum <  nFrame);

			if (frameNum >= 0)
				/* page hit */
			{
#ifdef TRACE_TWICE
				proc->numPageHit++;
#endif
				//allocate
				phyMemFrames[frameNum].pid = proc->pid;
				phyMemFrames[frameNum].vpn = vpn;


			}
			else /* page fault */
			{
#ifdef TRACE_TWICE
				proc->numPageFault++;
#endif
				if (oldestFrame->pid != -1) /* already used */
				{
					hashRemoveAt(hashTable, procTable + oldestFrame->pid, oldestFrame->vpn);
				}

				//map new page into phyMem[frameNum]
				oldestFrame->pid = proc->pid;
				oldestFrame->vpn = vpn;

				hashInsert(hashTable, proc, vpn, oldestFrame->number);

				frameNum = oldestFrame->number;
			}
			//update LRU
			updateLRU(phyMemFrames + frameNum);
		}
	}

	//display results
	for (i = 0; i < numProcess; i++) {
		printf("**** %s *****\n", procTable[i].traceName);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n", i, procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}

	//release memory
	for (i = 0; i < nFrame; ++i)
	{
		Ipage ptr = hashTable[i].next;
		while (ptr)
		{
			Ipage tmp = ptr->next;
			free(ptr);
			ptr = tmp;
		}
	}
	free(hashTable);
}

int main(int argc, char *argv[]) {
	int i;

	if (argc < 4) {
		printf("Usage : %s firstLevelBits PhysicalMemorySizeBits TraceFileNames\n", argv[0]);
		exit(1);
	}

	sscanf(argv[2], "%d", &phyMemSizeBits);
	if (phyMemSizeBits < PAGESIZEBITS) {
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n", phyMemSizeBits, PAGESIZEBITS);
		exit(1);
	}

	sscanf(argv[1], "%d", &firstLevelBits);
	// secondLevelBits >= 0
	if (VIRTUALADDRBITS <= PAGESIZEBITS + firstLevelBits) {
		printf("firstLevelBits %d is too Big\n", firstLevelBits);
		exit(1);
	}

	// initialize procTable for two-level page table
	numProcess = argc - 3;
	Proc procTable = (Proc)calloc(numProcess, sizeof(struct procEntry));
	for (i = 0; i < numProcess; i++) {
		// opening a tracefile for the process
		printf("process %d opening %s\n", i, argv[i + 3]);
		procTable[i].traceName = argv[i + 3];
		procTable[i].pid = i;
		procTable[i].tracefp = fopen(procTable[i].traceName, "r");
	}

	//initialize frame(physical memory)
	int nFrame = (1 << (phyMemSizeBits - PAGESIZEBITS));
	assert(nFrame > 0);

	Frame phyMemFrames = malloc(nFrame * sizeof(struct framePage));
	initPhyMem(phyMemFrames, nFrame);

	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n", nFrame, (1L << phyMemSizeBits));

	printf("=============================================================\n");
	printf("The 2nd Level Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	secondLevelVMSim(procTable, phyMemFrames);

	// initialize procTable for the inverted Page Table
	initPhyMem(phyMemFrames, nFrame);
	for (i = 0; i < numProcess; i++) {
		// rewind tracefiles
#ifdef TRACE_TWICE
		procTable[i].numPageFault = 0;
		procTable[i].numPageHit = 0;
		procTable[i].ntraces = 0;
#endif
		fseek(procTable[i].tracefp, 0, SEEK_SET);
	}

	printf("=============================================================\n");
	printf("The Inverted Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	invertedPageVMSim(procTable, phyMemFrames, nFrame);

	//release memory and file
	for (i = 0; i < numProcess; ++i)
		fclose(procTable[i].tracefp);

	free(procTable);
	free(phyMemFrames);
	return 0;
}
