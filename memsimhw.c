//
// Virual Memory Simulator Homework
// Two-level page table system
// Inverted page table with a hashing system 
// Student Name:	Moon Junoh
// Student Number:	B611062
//
#define NDEBUG
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

enum {
	PAGE_SZ_BITS = 12,		// page size = 4Kbytes
	VIRT_ADDR_BITS = 32		// virtual address space size = 4Gbytes
};
int firstLvBits, phy_mem_sz_bits, numProcess;
_Bool doTrace = false;

#define FIRST_PAGE_SZ (1u << firstLvBits)
#define SND_LV_BITS (VIRT_ADDR_BITS - firstLvBits - PAGE_SZ_BITS)
#define SND_PAGE_SZ (1u << SND_LV_BITS)
#define N_FRAME  (1u << (phy_mem_sz_bits - PAGE_SZ_BITS))


typedef struct PTE {
	int level;				// page table level (1 or 2)
	char valid;
	struct PTE *sndLvPageTable;	// valid if this entry is for the first level page table (level = 1)
	int frame_num;				// valid if this entry is for the second level page table (level = 2)
} *Page;

typedef struct framePage {
	uint32_t number;			// frame number
	int pid;					// Process id that owns the frame
	uint32_t vpn;				// virtual page number using the frame
	struct framePage *lru_left;	// for LRU circular doubly linked list
	struct framePage *lru_right;// for LRU circular doubly linked list
} *Frame;

typedef struct IPTE {
	int pid;			// process id
	uint32_t vpn;		// virtual page number
	int frame_num;		// frame number allocated
	struct IPTE *next;
} *Ipage;

typedef struct procEntry {
	char* trace_name;			// the memory trace name
	int pid;					// process (trace) id
	int ntraces;				// the number of memory traces
	int num2ndLvPageTable;		// The 2nd level page created(allocated);
	int numIHTConflictAccess; 	// The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;		// The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;		// The number of Non Empty Inverted Hash Table Accesses
	int num_page_fault;			// The number of page faults
	int num_page_hit;			// The number of page hits
	struct PTE *firstLvPageTable;
	FILE* fp;
}*Proc;


Frame oldest_frame; // the oldest frame pointer


void initPhyMem(Frame phy_mem, int nFrame) {
	int i;
	for (i = 0; i < nFrame; i++) {
		phy_mem[i].number = i;
		phy_mem[i].pid = -1;
		phy_mem[i].vpn = -1;
		phy_mem[i].lru_left = &phy_mem[(i - 1 + nFrame) % nFrame];
		/* '+nFrame' ensures that the result of mod operation is bigger than 0 */
		phy_mem[i].lru_right = &phy_mem[(i + 1 + nFrame) % nFrame];
	}

	oldest_frame = phy_mem;
}

void updateLRU(Frame recent)
{
	if (oldest_frame == recent) {
		oldest_frame = oldest_frame->lru_right;
	} else {
		Frame prev = recent->lru_left,
			next = recent->lru_right;
		//pop recent frame
		prev->lru_right = next;
		next->lru_left = prev;
		//recent frame : last link
		//  first, make a link between so-far last node and recent node
		oldest_frame->lru_left->lru_right = recent;
		recent->lru_left = oldest_frame->lru_left;
		//  second, make a link between recent node and first node
		oldest_frame->lru_left = recent;
		recent->lru_right = oldest_frame;
	}
}


#define FIR_PAGE_MASK (\
	((1ULL << firstLvBits) - 1) << (PAGE_SZ_BITS + SND_LV_BITS) \
)

#define SND_PAGE_MASK (\
	( ( (1u << SND_LV_BITS) - 1) << PAGE_SZ_BITS) \
)

#define OFFSET_MASK (\
	( (1u << PAGE_SZ_BITS) - 1 ) \
)

#define FIR_PAGE_IDX(page) (\
	( ( (page) & FIR_PAGE_MASK) >> (PAGE_SZ_BITS + SND_LV_BITS) ) \
)

#define SND_PAGE_IDX(page) (\
	( (page) &  SND_PAGE_MASK) >> PAGE_SZ_BITS \
)

#define GET_VPN(virt_addr) ( \
	(virt_addr) >> PAGE_SZ_BITS \
)

#define GET_OFFSET(addr) ( \
	(addr) & OFFSET_MASK\
)

void handlePageFault(const Proc procTable, const Proc proc, const uint32_t virt_addr)
{
	Page fir_page = proc->firstLvPageTable + FIR_PAGE_IDX(virt_addr);
	assert(fir_page);

	if (!fir_page->valid) /* first level page: make second level page */
	{
		fir_page->sndLvPageTable = (Page)calloc(SND_PAGE_SZ, sizeof(struct PTE));
		assert(fir_page->sndLvPageTable);
		uint32_t i = 0;
		for (i = 0; i < SND_PAGE_SZ; ++i)
			fir_page->sndLvPageTable[i].level = 2;
		fir_page->valid = 1;

		proc->num2ndLvPageTable++;
	}

	Page sndPage = fir_page->sndLvPageTable + SND_PAGE_IDX(virt_addr);
	assert(sndPage);
	if (!sndPage->valid) /* second level page: get frame */
	{
		if (oldest_frame->pid != -1) /* a frame in use */
		{
			uint32_t fir_page_num = FIR_PAGE_IDX(oldest_frame->vpn << PAGE_SZ_BITS),
				snd_page_num = SND_PAGE_IDX(oldest_frame->vpn << PAGE_SZ_BITS);

			procTable[oldest_frame->pid].firstLvPageTable[fir_page_num].sndLvPageTable[snd_page_num].valid = 0;
			procTable[oldest_frame->pid].firstLvPageTable[fir_page_num].sndLvPageTable[snd_page_num].frame_num = -1;
		}

		oldest_frame->pid = proc->pid;
		oldest_frame->vpn = GET_VPN(virt_addr);


		sndPage->frame_num = oldest_frame->number;
		sndPage->valid = 1;

	}
}

void secondLevelVMSim(Proc procTable, Frame phy_mem_frames)
{
	int i = 0;
	//calculate page bits and sizes;

	//create a first-level page
	for (i = 0; i < numProcess; ++i)
	{
		procTable[i].firstLvPageTable = (Page)calloc(FIRST_PAGE_SZ, sizeof(struct PTE));
		uint32_t j = 0;
		for (j = 0; j < FIRST_PAGE_SZ; ++j)
			procTable[i].firstLvPageTable[j].level = 1;
	}

	//begin simulating 2-level page system
	//one cycle is given for each process
	int mem_read = 1000000; 	//one million
	while (mem_read--)
	{
		//for each process
		for (i = 0; i < numProcess; i++)
		{
			Proc proc = procTable + i;
			//access memory once
			uint32_t virt_addr = 0;
			char action; 	//maybe unused
			fscanf(proc->fp, "%x %c", &virt_addr, &action);
			++proc->ntraces;


			//check whether page fault arises or not
			uint32_t fir_page_num = FIR_PAGE_IDX(virt_addr),
				snd_page_num = SND_PAGE_IDX(virt_addr),
				frame_num;
			Page fir_page = proc->firstLvPageTable + fir_page_num;

			if (fir_page->valid && fir_page->sndLvPageTable[snd_page_num].valid)
				/* page hit */
			{
				++proc->num_page_hit;
				frame_num = fir_page->sndLvPageTable[snd_page_num].frame_num;
			}
			else /*page fault*/ {
				proc->num_page_fault++;
				frame_num = oldest_frame->number;
				handlePageFault(procTable, proc, virt_addr);
			}
			updateLRU(phy_mem_frames + frame_num);
			if (doTrace)
			{
				uint32_t phy_addr = (phy_mem_frames[frame_num].number << PAGE_SZ_BITS) | GET_OFFSET(virt_addr);
				printf("2Level procID %d traceNumber %d virtual addr %x pysical addr %x\n", i, proc->ntraces, virt_addr, phy_addr);
			}
		}
	}

	//display results
	for (i = 0; i < numProcess; i++) {
		printf("**** %s *****\n", procTable[i].trace_name);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n", i, procTable[i].num2ndLvPageTable);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].num_page_fault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].num_page_hit);
		assert(procTable[i].num_page_hit + procTable[i].num_page_fault == procTable[i].ntraces);
	}

	//release page table
	for (i = 0; i < numProcess; ++i)
	{
		unsigned int j;
		for (j = 0; j < FIRST_PAGE_SZ; ++j)
			free(procTable[i].firstLvPageTable[j].sndLvPageTable);
		free(procTable[i].firstLvPageTable);
	}
}


#define HASH(pid, vpn) (\
	( ( (vpn) + (pid) ) % N_FRAME) \
)

// equivalent to hash[proc] or hash.at(proc);
// also, log its results.
int hashAt(Ipage hash_table, Proc proc, uint32_t vpn)
{
	uint32_t page_idx = HASH(proc->pid, vpn);
	//cf) list.at(0) is a dummy.
	Ipage ptr = hash_table[page_idx].next;

	if (ptr)
		proc->numIHTNonNULLAcess++;
	else
		proc->numIHTNULLAccess++;

	for (; ptr; ptr = ptr->next)
	{
		proc->numIHTConflictAccess++;
		if (ptr->pid == proc->pid && ptr->vpn == vpn)
		{
			return ptr->frame_num;
		}
	}
	return -1;
}

// map.at(pid, vpn) = value (frame #)
void hashInsert(Ipage hash_table, Proc proc, uint32_t vpn /* virtual page number */, uint32_t value /* frame # */)
{
	Ipage head = hash_table + HASH(proc->pid, vpn);
	Ipage new_elem = (Ipage)calloc(1, sizeof(struct IPTE));

	//store unique key to avoid collision
	new_elem->pid = proc->pid;
	new_elem->vpn = vpn;

	//store value
	new_elem->frame_num = value;

	//set list order
	new_elem->next = head->next;
	head->next = new_elem;
}

void hashRemoveAt(Ipage hash_table, Proc proc, uint32_t vpn)
{
	uint32_t page_idx = HASH(proc->pid, vpn);
	//cf) list.at(0) is a dummy.
	Ipage prev = hash_table + page_idx,
		ptr = NULL;
	for (ptr = prev->next; ptr; ptr = ptr->next, prev = prev->next)
	{
		if (ptr->pid == proc->pid && ptr->vpn == vpn)
		{
			prev->next = ptr->next;
			free(ptr);
			break;
		}
	}
}


void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame)
{
	int i = 0;
	//make IPT
	Ipage hash_table = (Ipage)malloc(nFrame * sizeof(struct IPTE));
	for (i = 0; i < nFrame; ++i)
	{
		hash_table[i].frame_num = i;
		hash_table[i].pid = -1;
		hash_table[i].next = NULL;
		hash_table[i].vpn = 0;
	}

	uint32_t mem_read = 1000000; 	//one million
	while (mem_read--)
	{
		for (i = 0; i < numProcess; ++i)
		{
			Proc proc = procTable + i;
			uint32_t virt_addr;
			char action;	/* unused */
			fscanf(proc->fp, "%x %c", &virt_addr, &action);
			uint32_t vpn = GET_VPN(virt_addr);
			++proc->ntraces;

			int frame_num = hashAt(hash_table, proc, vpn);
			assert(frame_num < nFrame);

			if (frame_num >= 0)
				/* page hit */
			{
				proc->num_page_hit++;
				//allocate
				phyMemFrames[frame_num].pid = proc->pid;
				phyMemFrames[frame_num].vpn = vpn;


			}
			else /* page fault */
			{
				proc->num_page_fault++;
				if (oldest_frame->pid != -1) /* already used */
				{
					hashRemoveAt(hash_table, procTable + oldest_frame->pid, oldest_frame->vpn);
				}

				//map new page into phyMem[frameNum]
				oldest_frame->pid = proc->pid;
				oldest_frame->vpn = vpn;

				hashInsert(hash_table, proc, vpn, oldest_frame->number);

				frame_num = oldest_frame->number;
			}
			//update LRU
			updateLRU(phyMemFrames + frame_num);
			if (doTrace)
			{
				uint32_t phy_addr = (phyMemFrames[frame_num].number << PAGE_SZ_BITS) | GET_OFFSET(virt_addr);
				printf("IHT procID %d traceNumber %d virtual addr %x pysical addr %x\n", i, proc->ntraces, virt_addr, phy_addr);
			}
		}
	}

	//display results
	for (i = 0; i < numProcess; i++) {
		printf("**** %s *****\n", procTable[i].trace_name);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n", i, procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].num_page_fault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].num_page_hit);
		assert(procTable[i].num_page_hit + procTable[i].num_page_fault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}

	//release memory
	for (i = 0; i < nFrame; ++i)
	{
		Ipage ptr = hash_table[i].next;
		while (ptr)
		{
			Ipage tmp = ptr->next;
			free(ptr);
			ptr = tmp;
		}
	}
	free(hash_table);
}

_Bool shouldTrace(int* argc, char** argv)
{
	int i;
	for (i = 0; i < *argc; i++)
	{
		if (strcmp(argv[i], "-s") == 0)
		{
			int j;
			for (j = i + 1; j < *argc; j++)
			{
				argv[j - 1] = argv[j];
			}
			--*argc;
			return false;
		}
	}
	return true;
}

int main(int argc, char *argv[]) {
	int i;


	//check for -s option
	doTrace = shouldTrace(&argc, argv);

	if (argc < 4) {
		printf("Usage : %s firstLevelBits PhysicalMemorySizeBits TraceFileNames\n", argv[0]);
		exit(1);
	}

	sscanf(argv[2], "%d", &phy_mem_sz_bits);
	if (phy_mem_sz_bits < PAGE_SZ_BITS) {
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n", phy_mem_sz_bits, PAGE_SZ_BITS);
		exit(1);
	}

	sscanf(argv[1], "%d", &firstLvBits);
	// secondLevelBits >= 0
	if (VIRT_ADDR_BITS <= PAGE_SZ_BITS + firstLvBits) {
		printf("firstLevelBits %d is too Big\n", firstLvBits);
		exit(1);
	}

	// initialize procTable for two-level page table
	numProcess = argc - 3;
	Proc proc_table = (Proc)calloc(numProcess, sizeof(struct procEntry));
	for (i = 0; i < numProcess; i++) {
		// opening a tracefile for the process
		printf("process %d opening %s\n", i, argv[i + 3]);
		proc_table[i].trace_name = argv[i + 3];
		proc_table[i].pid = i;
		proc_table[i].fp = fopen(proc_table[i].trace_name, "r");
	}

	//initialize frame(physical memory)
	int nFrame = (1 << (phy_mem_sz_bits - PAGE_SZ_BITS));
	assert(nFrame > 0);

	Frame phy_mem_frames = malloc(nFrame * sizeof(struct framePage));
	initPhyMem(phy_mem_frames, nFrame);

	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n", nFrame, (1L << phy_mem_sz_bits));

	printf("=============================================================\n");
	printf("The 2nd Level Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	secondLevelVMSim(proc_table, phy_mem_frames);

	// initialize procTable for the inverted Page Table
	initPhyMem(phy_mem_frames, nFrame);
	for (i = 0; i < numProcess; i++) {
		// rewind tracefiles
		proc_table[i].num_page_fault = 0;
		proc_table[i].num_page_hit = 0;
		proc_table[i].ntraces = 0;
		fseek(proc_table[i].fp, 0, SEEK_SET);
	}

	printf("=============================================================\n");
	printf("The Inverted Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	invertedPageVMSim(proc_table, phy_mem_frames, nFrame);

	//release memory and file
	for (i = 0; i < numProcess; ++i)
		fclose(proc_table[i].fp);

	free(proc_table);
	free(phy_mem_frames);
	return 0;
}
