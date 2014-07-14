/*
   This file is part of Sigil, a tool for call graph profiling programs.
 
   Copyright (C) 2012, Siddharth Nilakantan, Drexel University
  
   This tool is derived from and contains code from Callgrind
   Copyright (C) 2002-2011, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)
 
   This tool is also derived from and contains code from Cachegrind
   Copyright (C) 2002-2011 Nicholas Nethercote (njn@valgrind.org)
 
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
 
   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.
 
   The GNU General Public License is contained in the file COPYING.
*/

#include "global.h"

/* DEFINITIONS ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */
#define DRWINFOCHUNK_SIZE 10000
#define NUM_FUNCTIONS 10000
#define SELF 20000
#define NO_PROD 30000
#define STARTUP_FUNC 30001
#define NUM_CALLEES 1000
#define HASH_SIZE 20
#define DEPENDENCE_LIST_CHUNK_SIZE 100
#define NUM_CACHE_ENTRIES 10000
#define CACHE_ENTRY_SIZE 20
#define CACHE_LOOKUP_PART_SIZE 10
#define ADDRCHUNK_ARRAY_SIZE 10
#define MAX_THREADS 512
#define EVENTCHUNK_SIZE 10000
#define MAX_EVENTLIST_CHUNKS_PER_THREAD 3
#define MAX_EVENTLIST_CHUNKS 1400
#define MAX_EVENTINFO_SIZE 6 //In Giga bytes. 
#define MAX_EVENTADDRCHUNK_SIZE 1 //In Giga bytes. 
#define MAX_RECURSE_DEPTH 10000
#define DOLLAR_ON 1
#define LWC_PM_SIZE 1048576 //1MB
#define LWC_SM_SIZE 262144 //256K
#define MAX_PRIMARY_ADDRESS (Addr)((((Addr)LWC_SM_SIZE) * LWC_PM_SIZE)-1)

#define FUNCINSTLIST_CHUNK_SIZE 5
#define MAX_NUM_SYSCALLS      256   // Highest seen in vg_syscalls.c: 237
#define INVALID_TEMPREG 999999999
#define SYSCALL_ADDRCHUNK_SIZE 10000
#define HISTOGRAM_NUM_BINS 10000
#define HISTOGRAM_BIN_SIZE 1000
#define HISTOGRAM_REUSECOUNT_BIN_SIZE 10
#define MAX_MEMORY_USAGE 20000 //In Mb. This could be a command line option later
/* Done with DEFINITIONS - inserted by Sid */

/*------------------------------------------------------------*/
/*--- Structure declarations                               ---*/
/*------------------------------------------------------------*/

/* STRUCTURES ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

typedef struct _addrchunk addrchunk;
typedef struct _funcinst funcinst;
typedef struct _addrchunknode addrchunknode;
typedef struct _evt_addrchunknode evt_addrchunknode;
typedef struct _drwevent drwevent;
typedef struct _funcinfo funcinfo;
typedef struct _dependencelist dependencelist;
typedef struct _dependencelist_elemt dependencelist_elemt;
typedef struct _hist_list hist_list;
typedef struct _hist_elemt hist_elemt;
typedef struct _hist_list_elemt hist_list_elemt;

struct _evt_addrchunknode {
  Addr rangefirst, rangelast;
  int shared_bytes, shared_read_tid;
  ULong shared_read_event_num;
  evt_addrchunknode *prev, *next;
};

struct _addrchunknode {
  Addr rangefirst, rangelast;
  addrchunknode *prev, *next;
  ULong count_unique; //Holds the count of the number of times this chunk of addresses was accessed uniquely
  ULong count; //Holds the count of the number of times this was accessed, either for a read or write
};

struct _addrchunk {
  Addr first, last; // Holds the first hash and last hash 
  addrchunknode* original; //Original points to the first addrchunknode whose  hash of address range is in between first and last hash
};

/* Structure to list all the functions that have been consumed from.
   This will be used by funcinfo to hold addresses from various consumers.
 */
struct _dependencelist_elemt { //8bytes + consumerfuncinfostuff
  int fn_number;
  int funcinst_number;
  int tid;

  //Copied over from consumerfuncinfo
  funcinst *vert_parent_fn;
  funcinst *consumed_fn;
  dependencelist_elemt *next_horiz, *prev_horiz;
  addrchunk* consumedlist; //Pointer to a linked list of address chunks. These addresses have been consumed from by the funcinfo function and have been produced by this function.
  ULong count;
  ULong count_unique;
};

struct _dependencelist { //If a pointer is 8 bytes, then 16bytes * DEPENDENCE_LIST_CHUNK_SIZE + 32bytes + 4bytes. With default values, size = 15.6kB
  dependencelist *next, *prev;
  dependencelist_elemt list_chunk[DEPENDENCE_LIST_CHUNK_SIZE]; //note that this is not a list of pointers to structures, it is a list of structures!
  int size; //Should be initialized to zero
};

struct _hist_elemt {
  UInt rangefirst, rangelast;
  UInt count;
};

struct _hist_list_elemt {
  ULong rangefirst, rangelast;
  UInt count;
  hist_list_elemt *next, *prev;
};

/* struct _reusecount_elemt { */
/*   ULong rangefirst, rangelast; */
/*   UInt count; */
/*   hist_list_elemt *next, *prev; */
/* }; */

struct _hist_list {
  hist_list *next, *prev;
  hist_elemt list[DEPENDENCE_LIST_CHUNK_SIZE];
  UInt size;
};

typedef struct _funccontext funccontext;
struct _funccontext {
  int fn_number;
  int funcinst_number;
  funcinst *funcinst_ptr;
};

/* Array to store central function info. Also has global link to the 
 * structures of functions who consume from this function.
 * Chunks are allocated on demand, and deallocated at program termination.
 * This can also act as a linked list. This is needed because 
 * contiguous locations in the funcarray are not necessarily used. There may
 * be holes in between. Thus we need to track only the locations which are 
 * used from this list. 
 * Alternatively, an array could be used, but needs to be sized statically
 */

struct _funcinfo {
  fn_node* function; //From here we can access name with function->name
  int fn_number;
  funcinfo *next, *prev; //To quickly access the list of the functions present in the program
  int number_of_funcinsts;

  //Cache entire contexts to accelerate the common case lookup
  funccontext *context;
  int context_size;
};

/* Array to store data for a function instance. This is separated from other structs
 * to support a dynamic number of functioninfos for a function info item.
 * Chunks are allocated on demand, and deallocated at program termination.
 * This can also act as a linked list. This is needed because 
 * contiguous locations in the funcarray are not necessarily used. There may
 * be holes in between. Thus we need to track only the locations which are 
 * used from this list. 
 * Alternatively, an array could be used, but needs to be sized statically
 */
struct __attribute__ ((__packed__)) _funcinst {
  funcinfo* function_info;
  int fn_number;
  int tid;

  dependencelist *consumedlist; //Pointer to a linked list of functions consumed from. Should point to the first element in that list
  dependencelist_elemt *consumerlist; //Pointer to a linked list of functions that consume from this function. Should point to the first element in that list. This corresponds to the horizontal linking of the consumerfuncinfo lists
  funcinst *caller; //Points to the single caller in this context. (There will be a different context for each unique caller to this function and different such structures for this function under different contexts)
  funcinst **callees; //Pointer to the pointers for the different callees of this function. NUM_CALLEES will determine the number of callees.
  int num_callees;
  UInt callee_prnt_idx; //Callee print index
  ULong ip_comm_unique;
  //ULong op_comm_unique; //Output communication can be inferred.
  ULong ip_comm;
  //ULong op_comm;
  ULong instrs, flops, iops;
  ULong current_call_instr_num;
  int num_calls;

  //For assigning unique numbers for funcinsts and finding them quickly.
  int funcinst_number; //Each funcinst for a particular function will have a unique number.
  ULong num_events;

  SysRes res;
  int fd;
  
  //To keep track of size
  ULong num_dependencelists;
  ULong num_addrchunks;
  ULong num_addrchunknodes;

  hist_list_elemt *input_histogram;
  hist_list_elemt *local_histogram;
  hist_list_elemt *input_reuse_counts;
  hist_list_elemt *local_reuse_counts;
};

typedef struct _drwbbinfo drwbbinfo;
struct _drwbbinfo {
  int previous_bb_jmpindex;
  BB* previous_bb;
  BBCC* previous_bbcc;
  BB* current_bb;
  ClgJumpKind expected_jmpkind;
};

struct _drwevent {
  int type; //type = 0 -> operations, 1 -> communication
  funcinst* producer; //This is used for type 0 and type 1 events
  funcinst* consumer; //This is 0 for type 0 events and has the consumer to type 1 events
  UInt producer_call_num, consumer_call_num;
  //drwevent* producer_event; //This is 0 for type 0 events and has the consumer to type 1 events
  ULong bytes; //0 for type 0, count for type 1
  ULong bytes_unique; //0 for type 0, count for type 1
  ULong iops; //count for type 0, 0 for type 1
  ULong flops; //count for type 0, 0 for type 1
  //  int tid;

  //These are to capture statistics for local memory operations during a type0 event
  ULong unique_local, non_unique_local; //Unique will give the number of cold misses and non-unique can be used to calculate hits
  ULong total_mem_reads;
  ULong total_mem_writes;

  //Shared stuff
  ULong shared_bytes_written;
  ULong event_num;

  //Pointers just to keep necessary timing information when a producedlist chunk is updated/overwritten
  evt_addrchunknode *list;
  fn_node* debug_node1;
  fn_node* debug_node2;
  
};

typedef struct _drweventlist drweventlist;
struct _drweventlist {
  drweventlist *next, *prev;
  drwevent list_chunk[EVENTCHUNK_SIZE]; //note that this is not a list of pointers to structures, it is a list of structures!
  int size; //Should be initialized to zero
};

typedef struct _drwglobvars drwglobvars;
struct _drwglobvars {
  funcinfo* funcarray[NUM_FUNCTIONS];
  funcinfo* funcinfo_first;
  funcinst* funcinst_first;
  funcinst* previous_funcinst;
  drwbbinfo current_drwbbinfo;
  int tid;
};

//We can encapsulate these in structures as well making it slightly more efficient
typedef struct __attribute__ ((__packed__)) { // Secondary Map: covers 64KB
  funcinst *last_writer[LWC_SM_SIZE]; // 64K entries for the last writer of the location
  funcinst *last_reader[LWC_SM_SIZE]; // 64K entries for the last writer of the location
} SM;
//SM Size = (8bytes + 8bytes) * LWC_SM_SIZE. E.g. with LWC_SM_SIZE = 64K, SM Size = 1MB
//with LWC_SM_SIZE = 256K, SM Size = 4MB

typedef struct __attribute__ ((__packed__)) { // Secondary Map: covers 64KB
  funcinst *last_writer[LWC_SM_SIZE]; // 64K entries for the last writer of the location
  funcinst *last_reader[LWC_SM_SIZE]; // 64K entries for the last reader of the location
  UInt call_number[LWC_SM_SIZE]; //Call number of LAST READER
  ULong reuse_length_start[LWC_SM_SIZE]; //This is the reuse length in instructions. Zeroed out when the call number's don't match, else
  ULong reuse_length_end[LWC_SM_SIZE]; //This is the reuse length in instructions. Zeroed out when the call number's don't match, else
  UInt reuse_count[LWC_SM_SIZE];
} SM_datareuse;

typedef struct __attribute__ ((__packed__)) { // Secondary Map: covers 64KB
  funcinst *last_writer[LWC_SM_SIZE]; // 64K entries for the last writer of the location
  drwevent *last_writer_event[LWC_SM_SIZE];
  funcinst *last_reader[LWC_SM_SIZE];
  drwevent *last_reader_event[LWC_SM_SIZE];
  int last_writer_event_dumpnum[LWC_SM_SIZE];
} SM_event;

void* PM[LWC_PM_SIZE]; // Primary Map: covers 32GB
UInt PM_list[LWC_PM_SIZE]; // Primary Map list just keeps track of the FIFO order in which SMs are created, so that we can free them if need be

typedef struct {
      Char*  name;
      UInt   argc;
      Char** argv;            // names of args
      UInt   min_mem_reads;   // min number of memory reads done
      UInt   max_mem_reads;   // max number of memory reads done
} SyscallInfo;

/* Done with STRUCTURES - inserted by Sid */

/*------------------------------------------------------------*/
/*--- Functions                                            ---*/
/*------------------------------------------------------------*/

/* FUNCTIONS AND GLOBAL VARIABLES ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

//New stuff for changed format
extern drwglobvars* CLG_(thread_globvars)[MAX_THREADS];
extern ULong CLG_(total_data_reads);
extern ULong CLG_(total_data_writes);
extern ULong CLG_(total_instrs);
//extern int CLG_(previous_bb_jmpindex);
extern drwbbinfo CLG_(current_drwbbinfo);
extern void* CLG_(DSM);
extern SysRes CLG_(drw_res);
extern int CLG_(drw_fd);
extern ULong CLG_(num_events);
//Variable for FUNCTION CALLS TO INSTRUMENT THIGNS DURING SYSTEM CALLS
extern SyscallInfo CLG_(syscall_info)[MAX_NUM_SYSCALLS];
extern Int  CLG_(current_syscall);
extern Int  CLG_(current_syscall_tid);
extern Addr CLG_(last_mem_write_addr);
extern UInt CLG_(last_mem_write_len);
extern addrchunk* CLG_(syscall_addrchunks);
extern Int CLG_(syscall_addrchunk_idx);

void CLG_(init_funcarray)(void);
void CLG_(free_funcarray)(void);
void CLG_(print_to_file)(void);
void CLG_(storeIDRWcontext) (InstrInfo* inode, int datasize, Addr ea, Bool WR, int opsflag);
void CLG_(put_in_last_write_cache) (Int datasize, Addr ea, funcinst* writer, int tid);
void CLG_(read_last_write_cache) (Int datasize, Addr ea, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid);
void CLG_(drwinit_thread)(int tid);

//FUNCTION CALLS TO INSTRUMENT THIGNS DURING SYSTEM CALLS
void CLG_(pre_mem_read_asciiz)(CorePart part, ThreadId tid, Char* s, Addr a);
void CLG_(pre_mem_read)(CorePart part, ThreadId tid, Char* s, Addr a, SizeT len);
void CLG_(pre_mem_write)(CorePart part, ThreadId tid, Char* s, Addr a, SizeT len);
void CLG_(post_mem_write)(CorePart part, ThreadId tid, Addr a, SizeT len);
void CLG_(new_mem_brk) ( Addr a, SizeT len, ThreadId tid);
void CLG_(new_mem_mmap) ( Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle );
void CLG_(new_mem_startup) ( Addr start_a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle );

//End new stuff for changed format

/* Done with FUNCTIONS AND GLOBAL VARIABLES - inserted by Sid */


/*COPIED FROM HELGRIND.H so that Sigil could be standalone*/
