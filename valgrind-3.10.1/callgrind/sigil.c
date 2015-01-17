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

#include "pub_tool_basics.h"
#include "pub_tool_redir.h"
#include "valgrind.h"
#include "pthread.h"
#include "config.h"
#include "global.h"
#include "sigil.h"

#include "pub_tool_threadstate.h"
#include "pub_tool_libcfile.h"

/* GLOBAL VARIABLES ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

#include "pub_tool_mallocfree.h"

/* FUNCTIONS ADDED TO PUT ALL DATA ACCESSES FOR EVERY ADDRESS IN A LINKED LIST - Sid */

drwglobvars* CLG_(thread_globvars)[MAX_THREADS];
ULong CLG_(total_data_reads) = 0;
ULong CLG_(total_data_writes) = 0;
ULong CLG_(total_instrs) = 0;
ULong CLG_(total_flops) = 0;
ULong CLG_(total_iops) = 0;
ULong CLG_(num_eventlist_chunks) = 0;
ULong CLG_(num_eventaddrchunk_nodes) = 0;
ULong CLG_(tot_eventinfo_size) = 0;
int CLG_(num_threads) = 0;
int CLG_(num_globvars) = 0;
drwevent* CLG_(drwevent_latest_event) = 0;
drwevent* CLG_(drw_eventlist_start) = 0;
drwevent* CLG_(drw_eventlist_end) = 0;
ULong CLG_(shared_reads_missed_pointer) = 0;
ULong CLG_(shared_reads_missed_dump) = 0;
int CLG_(num_eventdumps) = 0; //Keeps track of the number of times the events were dumped to file
int CLG_(max_search_depth) = 0;
int CLG_(min_search_depth) = 0;
ULong CLG_(tot_search_depth) = 0;
ULong CLG_(num_searches) = 0;
ULong CLG_(num_funcinsts) = 0;
ULong CLG_(num_callee_array_elements) = 0;
ULong CLG_(num_dependencelists) = 0;
ULong CLG_(num_histlists) = 0;
ULong CLG_(num_funcinfos) = 0;
ULong CLG_(num_funccontexts) = 0;
ULong CLG_(num_addrchunks) = 0;
ULong CLG_(num_addrchunknodes) = 0;
ULong CLG_(num_sms) = 0;
ULong CLG_(num_written_bytes) = 0;
ULong CLG_(tot_memory_usage) = 0;
ULong CLG_(event_info_size) = 0;
ULong CLG_(current_call_instr_num) = 0;
int SM_bits = -1; //Set it up to fail if not initialized
int PM_bits = -1;

//HACK
ULong CLG_(debug_count_events) = 0;
int CLG_(L2_line_size) = 8; //64 Bytes according to cat /sys/devices/system/cpu/cpu0/cache/index2/coherency_line_size. This method obtained from: http://stackoverflow.com/questions/794632/programmatically-get-the-cache-line-size. I have used 8 because I don't expect that loads and stores will be bigger than that. This is also written in the shadow memory paper.
/*--------------------------------------------------------------------*/
/* Sparsely filled array;  index == syscallno */
SyscallInfo CLG_(syscall_info)[MAX_NUM_SYSCALLS];
// Remember some things about the syscall that we don't use until
// SK_(post_syscall) is called...

#define MAX_READ_MEMS  10

// Most distinct mem blocks read by a syscall is 5, I think;  10 should be safe
// This array is null-terminated.
Int  CLG_(current_syscall) = -1;
Int  CLG_(current_syscall_tid) = -1;
Addr CLG_(last_mem_write_addr) = INVALID_TEMPREG;
UInt CLG_(last_mem_write_len)  = INVALID_TEMPREG;
addrchunk* CLG_(syscall_addrchunks) = 0;
Int CLG_(syscall_addrchunk_idx) = 0;
drwglobvars* CLG_(syscall_globvars) = 0;

//Used when running in serial with no threads and events on
SysRes CLG_(drw_funcserial_res);
int CLG_(drw_funcserial_fd) = 0;
ULong CLG_(num_events) = 0;
//Used when running in serial with no threads and events on
SysRes CLG_(drw_datareuse_res);
int CLG_(drw_datareuse_fd) = 0;
//To keep track of Primary Map list
UInt PM_list_idx1 = 0, PM_list_idx2 = 0;

void* CLG_(DSM) = 0;
void* CLG_(Dummy_SM) = 0;

/***NON-GLOBAL PROTOTYPE DECLARATIONS***/
static __inline__
void insert_to_consumedlist (funcinst *current_funcinst_ptr, funcinst *consumed_fn, drwevent *consumed_event, Addr ea, Int datasize, ULong count_unique, ULong count_unique_event);
static __inline__
void insert_to_consumedlist_datareuse (funcinst *current_funcinst_ptr, funcinst *consumed_fn, Addr ea, Int datasize, ULong count_unique, ULong reuse_length, ULong reuse_length_old, UInt reuse_count, UInt reuse_count_old);
static __inline__
void insert_to_drweventlist( int type, int optype , funcinst *producer, funcinst *consumer, ULong count, ULong count_unique );
static int insert_to_evtaddrchunklist(evt_addrchunknode** chunkoriginal, Addr range_first, Addr range_last, ULong* refarg, int shared_read_tid, ULong shared_read_event_num);
static int search_evtaddrchunklist(evt_addrchunknode** chunkoriginal, Addr range_first, Addr range_last, ULong* refarg, int* address_array);
static void traverse_and_remove_in_dependencelist (dependencelist_elemt** chunkoriginal, Addr ea, Int datasize);
static void handle_memory_overflow(void);
static void my_fwrite(Int fd, Char* buf, Int len);
static Char* filename = 0;
void dump_eventlist_to_file_serialfunc(void);
static __inline__
void dump_eventlist_to_file(void);

static __inline__ void drw_free( void* ptr, ULong* num_structs ){
  VG_(free) ( ptr );
  (*num_structs)--;
}

static
void file_err(void)
{
   VG_(message)(Vg_UserMsg,
                "Error: can not open cache simulation output file `%s'\n",
                filename );
   VG_(exit)(1);
}

void CLG_(init_funcarray)()
{
  int i;
  SM* sm_temp = 0;
  SM_event* sm_event_temp = 0;
  SM_datareuse* sm_datareuse_temp = 0;
  funcinst* funcinsttemp = 0;
  funcinfo* funcinfotemp = 0;
  
  for(i = 0; i < MAX_THREADS; i++){
    CLG_(thread_globvars)[i] = 0;
  }
  //DONE ADDITION BY SID
  
  //Initiallize structures for system calls here
  CLG_(syscall_addrchunks) = (addrchunk*) CLG_MALLOC("cl.init_funcarray.sys.1",sizeof(addrchunk) * SYSCALL_ADDRCHUNK_SIZE);
  CLG_(num_addrchunks) += SYSCALL_ADDRCHUNK_SIZE;
  CLG_(syscall_addrchunk_idx) = 0;
  CLG_(syscall_globvars) = (drwglobvars*) CLG_MALLOC("cl.init_funcarray.sys.2",sizeof(drwglobvars));
  CLG_(num_globvars)++;
  funcinfotemp = CLG_(syscall_globvars)->funcinfo_first = (funcinfo*) CLG_MALLOC("cl.init_funcarray.sys.3",sizeof(funcinfo));
  CLG_(syscall_globvars)->funcinst_first  = (funcinst*) CLG_MALLOC("cl.init_funcarray.sys.4",sizeof(funcinst));
  funcinsttemp = CLG_(syscall_globvars)->funcinst_first;
  CLG_(syscall_globvars)->tid = STARTUP_FUNC;
  CLG_(syscall_globvars)->funcinfo_first->fn_number = 0;
  CLG_(syscall_globvars)->funcarray[0] = funcinfotemp;
  funcinsttemp->fn_number = 0; funcinsttemp->ip_comm_unique = funcinsttemp->ip_comm = funcinsttemp->instrs = funcinsttemp->flops = funcinsttemp->iops = 0;
  funcinsttemp->consumerlist = 0; funcinsttemp->consumedlist = 0; 
  funcinsttemp->funcinst_number = 0;
  funcinsttemp->tid = STARTUP_FUNC;
  funcinsttemp->function_info = CLG_(syscall_globvars)->funcinfo_first; //Store pointer to central info store of function
  funcinsttemp->num_callees = funcinsttemp->funcinst_number = funcinsttemp->callee_prnt_idx = 0;
  funcinsttemp->callees = 0;
  funcinsttemp->num_events = 0;

  /* Inspired by Valgrind's Memcheck, each Primary Map entry in the Shadow Memory, initially points to
     the same Secondary Map initialized with all variables at zero (the DSM).
     This is done to ensure that any lookups see inavlid variables inside the secondary map.
     Also, by memcpy'ing the DSM, there is never a need to initialize a valid Secondary Map.
   */
  if(CLG_(clo).drw_events){
    CLG_(DSM) = (void*) CLG_MALLOC("cl.init_funcarray.sm.1",sizeof(SM_event));
    sm_event_temp = (SM_event*) CLG_(DSM);
  }
  else if (CLG_(clo).drw_datareuse) {
    CLG_(DSM) = (void*) CLG_MALLOC("cl.init_funcarray.sm.1",sizeof(SM_datareuse));
    sm_datareuse_temp = (SM_datareuse*) CLG_(DSM);
  }
  else{
    CLG_(DSM) = (void*) CLG_MALLOC("cl.init_funcarray.sm.1",sizeof(SM));
    sm_temp = (SM*) CLG_(DSM);
  }
  CLG_(num_sms)++;
  if(CLG_(clo).drw_calcmem){
    CLG_(Dummy_SM) = (void*) CLG_MALLOC("cl.init_funcarray.sm.1",sizeof(SM));
  }
  for(i = 0; i < LWC_PM_SIZE; i++){
    if( i < LWC_SM_SIZE){
      if(CLG_(clo).drw_events){
	sm_event_temp->last_writer[i] = 0;
	sm_event_temp->last_writer_event[i] = 0;
	sm_event_temp->last_writer_event_dumpnum[i] = 0;
	sm_event_temp->last_reader[i] = 0;
	sm_event_temp->last_reader_event[i] = 0;
      }
      else if(CLG_(clo).drw_datareuse) {
	sm_datareuse_temp->last_writer[i] = 0;
	sm_datareuse_temp->last_reader[i] = 0;
	sm_datareuse_temp->call_number[i] = 0;
	sm_datareuse_temp->reuse_length_start[i] = 0;
	sm_datareuse_temp->reuse_length_end[i] = 0;
	sm_datareuse_temp->reuse_count[i] = 0;
      }
      else{
	sm_temp->last_writer[i] = 0;
	sm_temp->last_reader[i] = 0;
      }
    }
    PM[i] = CLG_(DSM);
    PM_list[i] = 0;
  }
  SM_bits = VG_(log2)( LWC_SM_SIZE );
  PM_bits = VG_(log2)( LWC_PM_SIZE );
  //VG_(printf)(" Number of bits used for addressing within SM: %d\n", SM_bits);

  /*If events are enabled, initialize structures and malloc a large buffer in which to store events*/
  if(CLG_(clo).drw_events){
    CLG_(event_info_size) = (MAX_EVENTINFO_SIZE * 1024 * 1024)/sizeof(drwevent);
    CLG_(drw_eventlist_start) = (drwevent*) CLG_MALLOC("cl.init_funcarray.drwevent.1",sizeof(drwevent) * CLG_(event_info_size) * 1024);
    if(!CLG_(drw_eventlist_start)) handle_memory_overflow();
    //Although drw_eventlist_end should point to the last valid entry + 1, since here we have no valid entries, it points location zero.
    CLG_(drw_eventlist_end) = CLG_(drw_eventlist_start);//CLG_(event_info_size) - 1 is the size of the array.
    CLG_(drwevent_latest_event) = CLG_(drw_eventlist_start);
  }
}

void CLG_(drwinit_thread)(int tid)
{
  drwglobvars *thread_globvar;
  int j;
  char drw_filename[50], buf[8192]; SysRes res;
  
  thread_globvar = (drwglobvars*) CLG_MALLOC("cl.drwinit_thread.gc.1",sizeof(drwglobvars));
  CLG_(thread_globvars)[tid] = thread_globvar;
  CLG_(num_threads)++;
  CLG_(num_globvars)++;
  thread_globvar->funcinfo_first = 0;
  thread_globvar->funcinst_first = 0;
  thread_globvar->previous_funcinst = 0;
  thread_globvar->current_drwbbinfo.previous_bb_jmpindex = -1;
  thread_globvar->current_drwbbinfo.current_bb = 0;
  thread_globvar->current_drwbbinfo.previous_bb = 0;
  thread_globvar->current_drwbbinfo.previous_bbcc = 0;
  thread_globvar->current_drwbbinfo.expected_jmpkind = 0;
  thread_globvar->tid = tid;
  for (j = 0; j < NUM_FUNCTIONS; j++){
    thread_globvar->funcarray[j] = 0; //NULL might be better, but we'll just stick with 0 as GNU C Null pointer constant uses 0
  }

  if(CLG_(clo).drw_thread_or_func){
    CLG_(num_events) = 0;
    /***1. CREATE THE NECESSARY FILE***/
    if(CLG_(clo).drw_events){
      VG_(sprintf)(drw_filename, "sigil.events.out-%d",tid);
      res = VG_(open)(drw_filename, VKI_O_WRONLY|VKI_O_TRUNC, 0);
      
      if (sr_isError(res)) {
	res = VG_(open)(drw_filename, VKI_O_CREAT|VKI_O_WRONLY,
			VKI_S_IRUSR|VKI_S_IWUSR);
	if (sr_isError(res)) {
	  file_err(); // If can't open file, then create one and then open. If still erroring, Valgrind will die using this call - Sid
	}
      }
      
      CLG_(drw_funcserial_res) = res;
      CLG_(drw_funcserial_fd) = (Int) sr_Res(res);
      VG_(sprintf)(buf, "%s,%s,%s,%s,%s,%s,%s,%s,%s\nOR\n", "EVENT_NUM", "CONSUMER-FUNCTION NUMBER", "CONSUMER-FUNCINST NUMBER", "CALL NUM", "PRODUCER-FUNCTION NUMBER", "PRODUCER-FUNCINST NUMBER", "CALL NUM", "BYTES", "BYTES_UNIQ" );
      my_fwrite(CLG_(drw_funcserial_fd), (void*)buf, VG_(strlen)(buf));
      VG_(sprintf)(buf, "%s,%s,%s,%s,%s,%s,%s,%s\n\n", "EVENT_NUM", "FUNCTION NUMBER", "FUNC_INST NUM", "CALL NUM", "IOPS", "FLOPS", "LOC", "LOC_UNIQ" );
      my_fwrite(CLG_(drw_funcserial_fd), (void*)buf, VG_(strlen)(buf));
    }
    /***1. DONE CREATION***/
  }
}

static void record_search_depth(int search_depth){
  if(search_depth > CLG_(max_search_depth))
    CLG_(max_search_depth) = search_depth;
  else if(search_depth < CLG_(min_search_depth))
    CLG_(min_search_depth) = search_depth;
  CLG_(tot_search_depth) += search_depth;
  CLG_(num_searches)++;
}

/* Functions to free structures after Sigil has run to completion */
static void free_recurse(funcinst *funcinstpointer){

  int i;
  for(i = 0; i < funcinstpointer->num_callees; i++){
    free_recurse(funcinstpointer->callees[i]);
    drw_free (funcinstpointer->callees[i], &CLG_(num_funcinsts));
  }
}

void CLG_(free_funcarray)()
{
  funcinfo *funcinfopointer, *funcinfopointer_next;
  funcinst *funcinstpointer;
  drwglobvars *thread_globvar;
  int i;
  VG_(printf)("Freeing Sigil's data structures now\n");
  for(i = 0; i < MAX_THREADS; i++){
    thread_globvar = CLG_(thread_globvars)[i];
    if (!thread_globvar) continue;
    funcinfopointer = thread_globvar->funcinfo_first;
    funcinstpointer = thread_globvar->funcinst_first;
    while(funcinfopointer){
      funcinfopointer_next = funcinfopointer->next;
      drw_free (funcinfopointer, &CLG_(num_funcinfos));
      funcinfopointer = funcinfopointer_next;
    }
    //Traverse the call tree that we have generated and free all of the data structures
    free_recurse(funcinstpointer);
    drw_free (funcinstpointer, &CLG_(num_funcinfos));
  }
  VG_(printf)("Done freeing Sigil's data structures now\n");
}


/*The following two functions are to calculate and print the memory footprint of all data structures used by Sigil*/
static void calculate_debuginfo(void){
  UInt free_idx_temp;
  //Calculate memory footprint for all data structures used by Sigil
  CLG_(tot_memory_usage) = sizeof(PM) + sizeof(PM_list) + CLG_(num_funcinsts) * (sizeof(funcinst) + sizeof(funcinst*) * NUM_CALLEES) + CLG_(num_dependencelists) * sizeof(dependencelist) + CLG_(num_addrchunks) * sizeof(addrchunk) + CLG_(num_addrchunknodes)  * sizeof(addrchunknode) + CLG_(num_funcinfos) * (sizeof(funcinfo) + sizeof(funccontext)*CLG_(clo).separate_callers) + CLG_(num_histlists) * sizeof(hist_list_elemt) + CLG_(num_globvars)*sizeof(drwglobvars); //Num funccontexts needn't be used as each funcinfo has separate_callers number of funccontexts
  if(CLG_(clo).drw_events)
    CLG_(tot_memory_usage) += CLG_(num_sms) * sizeof(SM_event);
  else if(CLG_(clo).drw_datareuse)
    CLG_(tot_memory_usage) += CLG_(num_sms) * sizeof(SM_datareuse);
  else
    CLG_(tot_memory_usage) += CLG_(num_sms) * sizeof(SM);
  /* NOTE: When the Sigil memory usage exceeds a certain threshold, space is reclaimed from the Shadow Memory, assuming it is the bottleneck
     If the memory usage goes beyond smlimit which defaults to 30G, then we must start sacrificing accuracy.
     This simple implementation just removes the first few SMs declared until overall memory usage falls below the specified limit
     This does not help when Shadow Memory is not the biggest consumer of memory. 
     On a few applications with a large number of functions called from unique instances, such as gcc and xalancbmk from SPEC2006, 
     data structures related to the function instances occupy much more of the address space than the Shadow Memory.
     However, in these cases it is usually found that the bottleneck is within the Callgrind structures.
     Currently, there is no workaround for these cases. Eventually, space will need to be reclaimed from other structures as well
  */
  while(1){
    if(((CLG_(tot_memory_usage)/1024/1024) > MAX_MEMORY_USAGE) || ((CLG_(tot_memory_usage)/1024/1024) > CLG_(clo).drw_smlimit)){
      free_idx_temp = PM_list[PM_list_idx1 % LWC_PM_SIZE];
      drw_free(PM[free_idx_temp], &CLG_(num_sms)); //Free the SMs
      PM[free_idx_temp] = CLG_(DSM); //Update the pointer in the PM back to the DSM
      PM_list[PM_list_idx1 % LWC_PM_SIZE] = 0;//Update the value in the PM_list to point to index 0 which is usually not used
      PM_list_idx1++;
      if(PM_list_idx2 % LWC_PM_SIZE == PM_list_idx1 % LWC_PM_SIZE){
	VG_(printf)("PM list indexes are overlapping1: idx1: %d idx2: %d LWC_PM_SIZE:%d !\n",PM_list_idx1,PM_list_idx2,LWC_PM_SIZE);
	tl_assert(0);
      }
      if(CLG_(clo).drw_events)
	CLG_(tot_memory_usage) -= sizeof(SM_event);
      else if(CLG_(clo).drw_datareuse)
	CLG_(tot_memory_usage) -= sizeof(SM_datareuse);
      else
	CLG_(tot_memory_usage) -= sizeof(SM);
    }
    else {
      break;
    }
  }
}

static void print_debuginfo(void){
  //Print memory footprint information
  VG_(printf)("Print for %llu: \n\n",CLG_(total_instrs));
  VG_(printf)("Num SMs: %-10llu Num funcinsts: %-10llu Num Callee array elements(funcinst): %-10llu Num dependencelists(funcinst): %-10llu Num addrchunks(funcinst): %-10llu Num addrchunknodes(funcinst): %-10llu Num funcinfos: %-10llu Num funccontexts: %-10llu Num histelements: %-10llu\n",CLG_(num_sms), CLG_(num_funcinsts), CLG_(num_callee_array_elements), CLG_(num_dependencelists), CLG_(num_addrchunks), CLG_(num_addrchunknodes), CLG_(num_funcinfos), CLG_(num_funccontexts), CLG_(num_histlists));
  VG_(printf)("Size of PM: %-10zu = %zu(Mb) Size of SM: %-10zu = %zu(Kb) Size of SM_event: %-10zu  = %zu(Kb) Size of SM_datareuse: %-10zu  = %zu(Kb) Size of funcinst: %-10zu = %zu(Kb) Size of dependencelist: %-10zu = %zu(Kb) Size of addrchunk(funcinst): %-10zu Size of addrchunknode(funcinst): %-10zu Size of funcinfo: %-10zu = %zu(Kb) Size of histelement: %-10zu\n",sizeof(PM), sizeof(PM)/1024/1024, sizeof(SM), sizeof(SM)/1024, sizeof(SM_event), sizeof(SM_event)/1024, sizeof(SM_datareuse), sizeof(SM_datareuse)/1024, (sizeof(funcinst) + sizeof(funcinst*) * NUM_CALLEES), (sizeof(funcinst) + sizeof(funcinst*) * NUM_CALLEES)/1024, sizeof(dependencelist), sizeof(dependencelist)/1024, sizeof(addrchunk), sizeof(addrchunknode), (sizeof(funcinfo) + sizeof(funccontext)*CLG_(clo).separate_callers), (sizeof(funcinfo) + sizeof(funccontext)*CLG_(clo).separate_callers)/1024, sizeof(hist_list_elemt));

  VG_(printf)("Memory for SM(bytes): %-10llu = %llu(Mb) Memory for SM_event(bytes): %-10llu = %llu(Mb) Memory for SM_datareuse(bytes): %-10llu = %llu(Mb) Memory for funcinsts(bytes): %-10llu = %llu(Mb)\n", CLG_(num_sms) * sizeof(SM), CLG_(num_sms) * sizeof(SM)/1024/1024, CLG_(num_sms) * sizeof(SM_event), CLG_(num_sms) * sizeof(SM_event)/1024/1024, CLG_(num_sms) * sizeof(SM_datareuse), CLG_(num_sms) * sizeof(SM_datareuse)/1024/1024, CLG_(num_funcinsts) * (sizeof(funcinst) + sizeof(funcinst*) * NUM_CALLEES), CLG_(num_funcinsts) * (sizeof(funcinst) + sizeof(funcinst*) * NUM_CALLEES)/1024/1024);
  VG_(printf)("  Memory for dependencelist(bytes): %-10llu = %llu(Mb) Memory for addrchunks(bytes): %-10llu = %llu(Mb) Memory for addrchunknodes(bytes): %-10llu = %llu(Mb) Memory for funcinfos(bytes): %-10llu = %llu(Mb) Memory for datareuse(bytes): %-10llu = %llu(Mb)\n", CLG_(num_dependencelists) * sizeof(dependencelist), CLG_(num_dependencelists) * sizeof(dependencelist)/1024/1024, CLG_(num_addrchunks) * sizeof(addrchunk), CLG_(num_addrchunks) * sizeof(addrchunk)/1024/1024, CLG_(num_addrchunknodes)  * sizeof(addrchunknode), CLG_(num_addrchunknodes) * sizeof(addrchunknode)/1024/1024, CLG_(num_funcinfos) * (sizeof(funcinfo) + sizeof(funccontext)*CLG_(clo).separate_callers), CLG_(num_funcinfos) * (sizeof(funcinfo) + sizeof(funccontext)*CLG_(clo).separate_callers)/1024/1024, CLG_(num_histlists) * sizeof(hist_list_elemt), CLG_(num_histlists) * sizeof(hist_list_elemt)/1024/1024);
  VG_(printf)("Total Memory size(bytes): %-10llu = %llu(Mb)\n\n", CLG_(tot_memory_usage), CLG_(tot_memory_usage)/1024/1024);
}

static void handle_memory_overflow(void){
  VG_(printf)("Null pointer was returned for malloc! Dumping contents to file\n");
  CLG_(print_to_file)();
  tl_assert(0);
}

//The following function marks an address range within an event as being 
//The address range here must represent the range of addresses actually written into. Thus shared_unique will reflect rangelast - rangefirst + 1
static int mark_event_shared(drwevent* producer_event, ULong shared_unique, Addr rangefirst, Addr rangelast){
  ULong dummy;
  if(CLG_(clo).drw_thread_or_func)
    return 1;
  if(DOLLAR_ON && CLG_(drwevent_latest_event)->producer->tid != STARTUP_FUNC)
    //If the producing event should be annotated with the address that was read.
    insert_to_evtaddrchunklist(&producer_event->list, rangefirst, rangelast, &dummy, CLG_(drwevent_latest_event)->consumer->tid, CLG_(drwevent_latest_event)->event_num);
  /*We need to write a pointer to the appropriate communication event as well, so that we can print it out
    We need to add something in the latest communication event to say that this address range from the computation 
    event corresponds to a certain producer entity and certain event number.*/
  insert_to_evtaddrchunklist(&CLG_(drwevent_latest_event)->list, rangefirst, rangelast, &dummy, CLG_(drwevent_latest_event)->producer->tid, producer_event->event_num);
  return 1;
}

static void checkAddrValid( Addr ea )
{
  if( ea > MAX_PRIMARY_ADDRESS ){
    VG_(printf)("Address greater than 38-bit encountered! Quitting now\n");
    VG_(exit)(1);
  }
}

/* Helper functions to retrieve secondary map entries given the address. 
   When writing to the address, a secondary map may need to be created
   if one does not already exist. See init_funcarray for an explanation
   of how exactly this is done. The 'copy_for_writing' function achieves this functionality. */
static void* get_SM_for_reading(Addr ea)
{
  checkAddrValid( ea );  
  return PM[ea >> SM_bits]; // use bits [31..SM_bits] of 'a'
}

static void* copy_for_writing ( void* dist_sm )
{
  void* new_sm;
  if(CLG_(clo).drw_events){
    new_sm = CLG_MALLOC("cl.copy_for_writing.sm.1",sizeof(SM_event));
  }
  else if(CLG_(clo).drw_datareuse){
    new_sm = CLG_MALLOC("cl.copy_for_writing.sm.1",sizeof(SM_datareuse));
  }
  else{
    new_sm = CLG_MALLOC("cl.copy_for_writing.sm.1",sizeof(SM));
  }
  if (new_sm == NULL){
    VG_(printf)("Unable to allocate SM in copy_for_writing. Aborting!\n");
    tl_assert(0);
  }
  CLG_(num_sms)++;
  if(CLG_(clo).drw_events)
    VG_(memcpy)(new_sm, dist_sm, sizeof(SM_event));
  else if(CLG_(clo).drw_datareuse)
    VG_(memcpy)(new_sm, dist_sm, sizeof(SM_datareuse));
  else
    VG_(memcpy)(new_sm, dist_sm, sizeof(SM));
  return new_sm;
}

static void* get_SM_for_writing(Addr ea)
{
  checkAddrValid( ea );
  void** sm_p = &PM[ea >> SM_bits]; // bits [31..SM_bits]
  if ((*sm_p) == ((void*)CLG_(DSM))){
    *sm_p = (void*) copy_for_writing(*sm_p); // copy-on-write
    //Also store in the Primary map list for freeing later
    PM_list[PM_list_idx2 % LWC_PM_SIZE] = ea >> SM_bits;
    PM_list_idx2++;
    if(PM_list_idx2 % LWC_PM_SIZE == PM_list_idx1 % LWC_PM_SIZE){
      VG_(printf)("PM list indexes are overlapping2: idx1: %d idx2: %d LWC_PM_SIZE:%d !\n",PM_list_idx1,PM_list_idx2,LWC_PM_SIZE);
      tl_assert(0);
    }
  }
  return *sm_p;
}

/* When a set of byte addresses are read (Load instruction), we want to establish a producer-consumer relationship by looking up the entity (potentially, entities) that wrote the addresses. (The entities are currently functions)
   These helper functions determine the last writer for a particular address, by looking up the entry in the Shadow Memory.
   The helper functions handle all possible cases. For example, when the range of addresses fall across multiple Secondary maps, a slightly slower function is used that retrieves the secondary map for each address in the range. 
   There are separate functions for when events are turned on, and re-use analysis is turned on. 

   For every producer function encountered by a function A who is reading produced data, function A's data structures need to be updated with the number of bytes read from that producer.
   If there are different producers for different addresses in an address range for a request, the reading function's data structures are updated for each of every unique producer encountered.
   This makes the logic employed by the helpers quite complicated. This is an area for improvement. Perhaps a two-pass approach might work? */
static void get_last_writer_event_singlesm(Addr ea, void* sm_temp, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){
  SM_event *sm = (SM_event*) sm_temp;
  funcinst *temp, *candidate;
  drwevent *temp_event, *candidate_event;
  int temp_event_dumpnum, candidate_event_dumpnum;
  int i;
  ULong temp_rangefirst, temp_rangelast;
  ULong count_unique = 0, count_unique_event = 0;

  //Unroll the loop once and use as a seed for the loop
  candidate_event = sm->last_writer_event[ea & 0xffff];
  candidate_event_dumpnum = sm->last_writer_event_dumpnum[ea & 0xffff];
  candidate = sm->last_writer[ea & 0xffff];
  //If previous reader was not current funcinst ptr update the unique counts and change the current entity
  if(sm->last_reader[ea & 0xffff] != current_funcinst_ptr){
    count_unique++;
    sm->last_reader[ea & 0xffff] = current_funcinst_ptr;    
  }
  if(sm->last_reader_event[ea & 0xffff] != CLG_(drwevent_latest_event)){
    count_unique_event++;
    sm->last_reader_event[ea & 0xffff] = CLG_(drwevent_latest_event);
  }

  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  // For entire address range, check the producer for every byte read.
  // For every unique producer, call insert_to_consumedlist and update consuming function's data structures
  for(i = 1; i < datasize; i++){
    temp_event = sm->last_writer_event[(ea + i) & 0xffff];
    temp_event_dumpnum = sm->last_writer_event_dumpnum[(ea + i) & 0xffff];
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp_event != candidate_event || temp_event_dumpnum != candidate_event_dumpnum){ //Handles an extremely rare case because usually if the dumpnumbers are not equal it should mean that the events will not be the same either
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      if(candidate_event_dumpnum != CLG_(num_eventdumps))
	insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      else
	insert_to_consumedlist (current_funcinst_ptr, candidate, candidate_event, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      candidate = temp;
      candidate_event = temp_event;
      candidate_event_dumpnum = temp_event_dumpnum;
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
      count_unique = count_unique_event = 0;
    }
    else
      temp_rangelast++;
    //The above code takes care of whether previous ranges and unique have to be saved and reset with a call to insert_to_consumedlist
    if(sm->last_reader[(ea + i) & 0xffff] != current_funcinst_ptr){
      count_unique++;
      sm->last_reader[(ea + i) & 0xffff] = current_funcinst_ptr;
    }
    if(sm->last_reader_event[(ea + i) & 0xffff] != CLG_(drwevent_latest_event)){
      count_unique_event++;
      sm->last_reader_event[(ea + i) & 0xffff] = CLG_(drwevent_latest_event);
    }
  }
  if(candidate_event_dumpnum != CLG_(num_eventdumps))
    insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
  else
    insert_to_consumedlist (current_funcinst_ptr, candidate, candidate_event, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
}

static void get_last_writer_event(Addr ea, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){
  SM_event *sm = (SM_event*) get_SM_for_reading(ea);
  funcinst *temp, *candidate;
  drwevent *temp_event, *candidate_event;
  int temp_event_dumpnum, candidate_event_dumpnum;
  int i;
  ULong temp_rangefirst, temp_rangelast;
  ULong count_unique = 0, count_unique_event = 0;
  candidate_event = sm->last_writer_event[ea & 0xffff];
  candidate_event_dumpnum = sm->last_writer_event_dumpnum[ea & 0xffff];
  candidate = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(sm->last_reader[ea & 0xffff] != current_funcinst_ptr){
    count_unique++;
    sm->last_reader[ea & 0xffff] = current_funcinst_ptr;    
  }
  if(sm->last_reader_event[ea & 0xffff] != CLG_(drwevent_latest_event)){
    count_unique_event++;
    sm->last_reader_event[ea & 0xffff] = CLG_(drwevent_latest_event);
  }
  for(i = 1; i < datasize; i++){
    sm = (SM_event*) get_SM_for_reading(ea + i);
    temp_event = sm->last_writer_event[(ea + i) & 0xffff];
    temp_event_dumpnum = sm->last_writer_event_dumpnum[(ea + i) & 0xffff];
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp_event != candidate_event || temp_event_dumpnum != candidate_event_dumpnum){ //Handles an extremely rare case because usually if the dumpnumbers are not equal it should mean that the events will not be the same either
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      if(candidate_event_dumpnum != CLG_(num_eventdumps))
	insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      else
	insert_to_consumedlist (current_funcinst_ptr, candidate, candidate_event, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      candidate = temp;
      candidate_event = temp_event;
      candidate_event_dumpnum = temp_event_dumpnum;
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
      count_unique = count_unique_event = 0;
    }
    else
      temp_rangelast++;
    //The above code takes care of whether previous ranges and unique have to be saved and reset with a call to insert_to_consumedlist
    if(sm->last_reader[(ea + i) & 0xffff] != current_funcinst_ptr){
      count_unique++;
      sm->last_reader[(ea + i) & 0xffff] = current_funcinst_ptr;
    }
    if(sm->last_reader_event[(ea + i) & 0xffff] != CLG_(drwevent_latest_event)){
      count_unique_event++;
      sm->last_reader_event[(ea + i) & 0xffff] = CLG_(drwevent_latest_event);
    }
  }
  if(candidate_event_dumpnum != CLG_(num_eventdumps))
    insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
  else
    insert_to_consumedlist (current_funcinst_ptr, candidate, candidate_event, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
}

static void get_last_writer(Addr ea, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){
  SM *sm = (SM*) get_SM_for_reading(ea);
  funcinst *temp, *candidate;
  int i;
  ULong temp_rangefirst, temp_rangelast;
  ULong count_unique = 0, count_unique_event = 0;
  candidate = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(sm->last_reader[ea & 0xffff] != current_funcinst_ptr){
    count_unique++;
    sm->last_reader[ea & 0xffff] = current_funcinst_ptr;    
  }
  for(i = 1; i < datasize; i++){
    sm = (SM*) get_SM_for_reading(ea + i);
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != candidate){
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      candidate = temp;
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
      count_unique = count_unique_event = 0;
    }
    else
      temp_rangelast++;
    //The above code takes care of whether previous ranges and unique have to be saved and reset with a call to insert_to_consumedlist
    if(sm->last_reader[(ea + i) & 0xffff] != current_funcinst_ptr){
      count_unique++;
      sm->last_reader[(ea + i) & 0xffff] = current_funcinst_ptr;
    }
  }
  insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
}

static void get_last_writer_singlesm(Addr ea, void* sm_temp, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){
  SM *sm = (SM*) sm_temp;
  funcinst *temp, *candidate;
  int i;
  ULong temp_rangefirst, temp_rangelast;
  ULong count_unique = 0, count_unique_event = 0;
  candidate = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(sm->last_reader[ea & 0xffff] != current_funcinst_ptr){
    count_unique++;
    sm->last_reader[ea & 0xffff] = current_funcinst_ptr;    
  }
  for(i = 1; i < datasize; i++){
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != candidate){
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
      candidate = temp;
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
      count_unique = count_unique_event = 0;
    }
    else
      temp_rangelast++;
    //The above code takes care of whether previous ranges and unique have to be saved and reset with a call to insert_to_consumedlist
    if(sm->last_reader[(ea + i) & 0xffff] != current_funcinst_ptr){
      count_unique++;
      sm->last_reader[(ea + i) & 0xffff] = current_funcinst_ptr;
    }
  }
  insert_to_consumedlist (current_funcinst_ptr, candidate, 0, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, count_unique_event);
}

static void get_last_writer_datareuse(Addr ea, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){
  SM_datareuse *sm = (SM_datareuse*) get_SM_for_reading(ea);
  funcinst *temp, *candidate;
  int i;
  ULong temp_rangefirst, temp_rangelast;
  ULong count_unique = 0;
  ULong reuse_length = 0, reuse_length_temp = 0, reuse_length_old = 0, reuse_length_old_temp = 0, last_reader_test_flag = 0;
  UInt reuse_count = 0, reuse_count_old = 0, reuse_count_temp = 0, reuse_count_old_temp = 0;
  candidate = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  //Check if the last reader was the same as the current function call. If yes, compute reuse length. If not, re-initialize this SM element. 
  //if(sm->last_reader[ea & 0xffff] == current_funcinst_ptr && sm->call_number[ea & 0xffff] == current_funcinst_ptr->num_calls){
  if(sm->last_reader[ea & 0xffff] == current_funcinst_ptr && sm->call_number[ea & 0xffff] == current_funcinst_ptr->num_calls){ 
    if(sm->call_number[ea & 0xffff] == current_funcinst_ptr->num_calls){
      if(sm->reuse_length_start[ea & 0xffff] > sm->reuse_length_end[ea & 0xffff]){
	VG_(printf)("Error!\n");
      }
      reuse_length_old = reuse_length_old_temp = sm->reuse_length_end[ea & 0xffff] - sm->reuse_length_start[ea & 0xffff];
      sm->reuse_length_end[ea & 0xffff] = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num); //This will keep increasing for multiple reads to the same location.
      reuse_length = reuse_length_temp = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num) - sm->reuse_length_start[ea & 0xffff];
      reuse_count_old = reuse_count_old_temp = sm->reuse_count[ea & 0xffff];
      sm->reuse_count[ea & 0xffff]++;
      reuse_count = reuse_count_temp = sm->reuse_count[ea & 0xffff];
    }
  }
  else{
    count_unique++;
    sm->last_reader[ea & 0xffff] = current_funcinst_ptr;
    sm->call_number[ea & 0xffff] = current_funcinst_ptr->num_calls;
    sm->reuse_length_start[ea & 0xffff] = sm->reuse_length_end[ea & 0xffff] = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num);
    sm->reuse_count[ea & 0xffff] = 0;
  }
  for(i = 1; i < datasize; i++){
    sm = (SM_datareuse*) get_SM_for_reading(ea + i);
    temp = sm->last_writer[(ea + i) & 0xffff];
    last_reader_test_flag = 0;
    //Calculate the reuse lengths and if they don't match with the running reuse length we need to insert_to_consumedlist as well. 
    //if(sm->last_reader[(ea + i) & 0xffff] == current_funcinst_ptr && sm->call_number[(ea + i) & 0xffff] == current_funcinst_ptr->num_calls){
    if(sm->last_reader[(ea + i) & 0xffff] == current_funcinst_ptr && sm->call_number[(ea + i) & 0xffff] == current_funcinst_ptr->num_calls){
      if(sm->call_number[(ea + i) & 0xffff] == current_funcinst_ptr->num_calls){
	if(sm->reuse_length_start[(ea + i) & 0xffff] > sm->reuse_length_end[(ea + i) & 0xffff]){
	  VG_(printf)("Error!\n");
	}
	reuse_length_old_temp = sm->reuse_length_end[(ea + i) & 0xffff] - sm->reuse_length_start[(ea + i) & 0xffff];
	sm->reuse_length_end[(ea + i) & 0xffff] = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num); //This will keep increasing for multiple reads to the same location.
	reuse_length_temp = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num) - sm->reuse_length_start[(ea + i) & 0xffff];
	reuse_count_old_temp = sm->reuse_count[(ea + i) & 0xffff];
	sm->reuse_count[(ea + i) & 0xffff]++;
	reuse_count_temp = sm->reuse_count[(ea + i) & 0xffff];
	last_reader_test_flag = 1;
      }
    }
    else{ //We have encountered a unique so the following if condition must fail and we must save off previous contents
      reuse_length_old_temp = reuse_length_temp = 0; //This is to ensure the if condition fails. We need to save and restart counting, every time any of these variables change.
      reuse_count_old_temp = reuse_count_temp = 0; //This is to ensure the if condition fails. We need to save and restart counting, every time any of these variables change.      
    }
    //At this point, we are guaranteed to have valid temp values and valid values of the "current true values" for each of these variables so far
    if(temp != candidate || reuse_length != reuse_length_temp || reuse_length_old != reuse_length_old_temp || reuse_count != reuse_count_temp || reuse_count_old != reuse_count_old_temp){
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      insert_to_consumedlist_datareuse(current_funcinst_ptr, candidate, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, reuse_length, reuse_length_old, reuse_count, reuse_count_old);
      candidate = temp;
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
      count_unique = 0;
      reuse_length = reuse_length_temp;
      reuse_length_old = reuse_length_old_temp;
      reuse_count = reuse_count_temp;
      reuse_count_old = reuse_count_old_temp;
    }
    else
      temp_rangelast++;
    //The above code takes care of whether previous ranges and unique have to be saved and reset with a call to insert_to_consumedlist
    if(!last_reader_test_flag){
      count_unique++;
      sm->last_reader[(ea + i) & 0xffff] = current_funcinst_ptr;
      sm->call_number[(ea + i) & 0xffff] = current_funcinst_ptr->num_calls;
      sm->reuse_length_start[(ea + i) & 0xffff] = sm->reuse_length_end[(ea + i) & 0xffff] = (CLG_(total_instrs) - current_funcinst_ptr->current_call_instr_num);
      sm->reuse_count[(ea + i) & 0xffff] = 0;
    }
  }
  insert_to_consumedlist_datareuse (current_funcinst_ptr, candidate, temp_rangefirst, temp_rangelast - temp_rangefirst + 1, count_unique, reuse_length, reuse_length_old, reuse_count, reuse_count_old);
}

static void check_align_and_get_last_writer(Addr ea, int datasize, funcinfo *current_funcinfo_ptr, funcinst *current_funcinst_ptr, int tid){

  //If we only want to calculate memory footprint of the shadow memory for debugging purposes
  //The use of this option can potentially be avoided in favor of turning on drw-debug
  if(CLG_(clo).drw_calcmem){
    return;
  }

  void *sm_start = get_SM_for_writing(ea);
  void *sm_end = get_SM_for_writing(ea + datasize - 1);
  if(!sm_start || !sm_end){
    if(CLG_(clo).drw_events)
      insert_to_consumedlist(current_funcinst_ptr, 0, 0, ea, datasize, datasize, 0);
    else if(CLG_(clo).drw_datareuse)
      insert_to_consumedlist_datareuse(current_funcinst_ptr, 0, ea, datasize, datasize, 0, 0, 0, 0);
    else
      insert_to_consumedlist(current_funcinst_ptr, 0, 0, ea, datasize, datasize, 0);
    return;
  }
  if(sm_start != sm_end){
    if(CLG_(clo).drw_events)
      get_last_writer_event(ea, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
    else if(CLG_(clo).drw_datareuse)
      get_last_writer_datareuse(ea, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
    else
      get_last_writer(ea, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
  }
  else{
    if(CLG_(clo).drw_events)
      get_last_writer_event_singlesm(ea, sm_start, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
    else if(CLG_(clo).drw_datareuse)
      get_last_writer_datareuse(ea, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
    else
      get_last_writer_singlesm(ea, sm_start, datasize, current_funcinfo_ptr, current_funcinst_ptr, tid);
  }
}

/* Helper functions to update the producer of an address/address range in the shadow memory.
   There are separate functions for when event collection is turned on, and for when re-use analysis is activated as well
   
   We have commented out the 'traverse_and_remove_in_dependencelist', which traverses and remove the addresses in the 
   current function's consumerlist. This call was intended to reset any record of data being consumed from this function through this address 
   (while still maintaining total bytes consumed), as the address will get fresh data from this function.
   As we discovered, this is a very expensive function call, done every time, for a rare case. The cose explodes when performing it on all individual addresses in the range.
   Commenting out the call might skew the ratio of unique to non-unique as a consumer may be reading fresh data and believing its old data.
   TODO: To improve accuracy, we might be better off storing the last X readers of a produced value in the shadow memory and checking only those.
   TODO: Another design might be to maintain a linked list of consumers for a particular data element. This might also become prohibhitively expensive to store. 
   TODO: Final option, store a list of producer-consumer transactions and list them out. Post-process them to figure out unique and non-unique
*/
static void put_writer_event(Addr ea, int datasize, funcinst *current_funcinst_ptr, int tid){
  SM_event *sm;
  int i = 0;
  funcinst *current_previous_writer, *temp;
  ULong temp_rangefirst, temp_rangelast;
  /* traverse_and_remove_in_dependencelist(&current_funcinst_ptr->consumerlist, ea, datasize); */

  sm = (SM_event*) get_SM_for_writing(ea);
  current_previous_writer = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(current_previous_writer != current_funcinst_ptr && sm->last_reader[ea & 0xffff]){
    sm->last_writer[ea & 0xffff] = current_funcinst_ptr;
    sm->last_writer_event[ea & 0xffff] = CLG_(drwevent_latest_event);
    sm->last_writer_event_dumpnum[ea & 0xffff] = CLG_(num_eventdumps);
    sm->last_reader[ea & 0xffff] = 0;
    sm->last_reader_event[ea & 0xffff] = 0;
  }
  for(i = 1; i < datasize; i++){
    sm = (SM_event*) get_SM_for_writing(ea + i);
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != current_previous_writer){
/*       if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/* 	traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */

      //Make sure current_previous_writer is not zero and not local because we have already traversed and removed current_funcinst_ptr
      current_previous_writer = temp;
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
    }
    else
      temp_rangelast++;
    if(current_previous_writer != current_funcinst_ptr && sm->last_reader[(ea + i) & 0xffff]){
      sm->last_writer[(ea + i) & 0xffff] = current_funcinst_ptr;
      sm->last_writer_event[(ea + i) & 0xffff] = CLG_(drwevent_latest_event);
      sm->last_writer_event_dumpnum[(ea + i) & 0xffff] = CLG_(num_eventdumps);
      sm->last_reader[(ea + i) & 0xffff] = 0;
      sm->last_reader_event[(ea + i) & 0xffff] = 0;
    }
  }
/*   if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/*     traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */
}

static void put_writer_event_singlesm(Addr ea, void *sm_temp, int datasize, funcinst *current_funcinst_ptr, int tid){
  SM_event* sm = (SM_event*) sm_temp;
  int i = 0;
  funcinst *current_previous_writer, *temp;
  ULong temp_rangefirst, temp_rangelast;
/*   traverse_and_remove_in_dependencelist(&current_funcinst_ptr->consumerlist, ea, datasize); */

  current_previous_writer = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(current_previous_writer != current_funcinst_ptr && sm->last_reader[ea & 0xffff]){
    sm->last_writer[ea & 0xffff] = current_funcinst_ptr;
    sm->last_writer_event[ea & 0xffff] = CLG_(drwevent_latest_event);
    sm->last_writer_event_dumpnum[ea & 0xffff] = CLG_(num_eventdumps);
    sm->last_reader[ea & 0xffff] = 0;
    sm->last_reader_event[ea & 0xffff] = 0;
  }
  for(i = 1; i < datasize; i++){
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != current_previous_writer){
/*       if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/* 	traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */

      //Make sure current_previous_writer is not zero and not local because we have already traversed and removed current_funcinst_ptr
      current_previous_writer = temp;
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
    }
    else
      temp_rangelast++;
    if(current_previous_writer != current_funcinst_ptr && sm->last_reader[(ea + i) & 0xffff]){
      sm->last_writer[(ea + i) & 0xffff] = current_funcinst_ptr;
      sm->last_writer_event[(ea + i) & 0xffff] = CLG_(drwevent_latest_event);
      sm->last_writer_event_dumpnum[(ea + i) & 0xffff] = CLG_(num_eventdumps);
      sm->last_reader[(ea + i) & 0xffff] = 0;
      sm->last_reader_event[(ea + i) & 0xffff] = 0;
    }
  }
/*   if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/*     traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */
}

static void put_writer(Addr ea, int datasize, funcinst *current_funcinst_ptr, int tid){
  SM *sm;
  int i = 0;
  funcinst *current_previous_writer, *temp;
  ULong temp_rangefirst, temp_rangelast;
/*   traverse_and_remove_in_dependencelist(&current_funcinst_ptr->consumerlist, ea, datasize); */

  sm = (SM*) get_SM_for_writing(ea);
  current_previous_writer = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(current_previous_writer != current_funcinst_ptr && sm->last_reader[ea & 0xffff]){
    sm->last_writer[ea & 0xffff] = current_funcinst_ptr;
    sm->last_reader[ea & 0xffff] = 0;
  }
  for(i = 1; i < datasize; i++){
    sm = (SM*) get_SM_for_writing(ea + i);
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != current_previous_writer){
/*       if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/* 	traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */

      //Make sure current_previous_writer is not zero and not local because we have already traversed and removed current_funcinst_ptr
      current_previous_writer = temp;
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
    }
    else
      temp_rangelast++;
    if(current_previous_writer != current_funcinst_ptr && sm->last_reader[(ea + i) & 0xffff]){
      sm->last_writer[(ea + i) & 0xffff] = current_funcinst_ptr;
      sm->last_reader[(ea + i) & 0xffff] = 0;
    }
  }
/*   if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/*     traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */
}

static void put_writer_singlesm(Addr ea, void *sm_temp, int datasize, funcinst *current_funcinst_ptr, int tid){
  SM* sm = (SM*) sm_temp;
  int i = 0;
  funcinst *current_previous_writer, *temp;
  ULong temp_rangefirst, temp_rangelast;
/*   traverse_and_remove_in_dependencelist(&current_funcinst_ptr->consumerlist, ea, datasize); */

  current_previous_writer = sm->last_writer[ea & 0xffff];
  temp_rangefirst = ea;
  temp_rangelast = temp_rangefirst;
  if(current_previous_writer != current_funcinst_ptr && sm->last_reader[ea & 0xffff]){
    sm->last_writer[ea & 0xffff] = current_funcinst_ptr;
    sm->last_reader[ea & 0xffff] = 0;
  }
  for(i = 1; i < datasize; i++){
    temp = sm->last_writer[(ea + i) & 0xffff];
    if(temp != current_previous_writer){
/*       if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/* 	traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */

      //Make sure current_previous_writer is not zero and not local because we have already traversed and removed current_funcinst_ptr
      current_previous_writer = temp;
      //process whatever you have so far and reset temp_rangefirst and temp_rangelast
      temp_rangefirst = ea + i;
      temp_rangelast = temp_rangefirst;
    }
    else
      temp_rangelast++;
    if(current_previous_writer != current_funcinst_ptr && sm->last_reader[(ea + i) & 0xffff]){
      sm->last_writer[(ea + i) & 0xffff] = current_funcinst_ptr;
      sm->last_reader[(ea + i) & 0xffff] = 0;
    }
  }
/*   if(current_previous_writer && current_previous_writer != current_funcinst_ptr) */
/*     traverse_and_remove_in_dependencelist(&current_previous_writer->consumerlist, temp_rangefirst, temp_rangelast - temp_rangefirst + 1); */
}

static void put_writer_datareuse(Addr ea, int datasize, funcinst *current_funcinst_ptr, int tid){
  SM_datareuse *sm;
  int i = 0;

  //We may not be able to distinguish writes to an address from a prior call to the same function. However it will be counted as unique and the histogram should be fine
  for(i = 0; i < datasize; i++){
    sm = (SM_datareuse*) get_SM_for_writing(ea + i);
    sm->last_writer[(ea + i) & 0xffff] = current_funcinst_ptr;
    sm->last_reader[(ea + i) & 0xffff] = 0;
    sm->call_number[(ea + i) & 0xffff] = 0;
    sm->reuse_length_start[(ea + i) & 0xffff] = 0;
    sm->reuse_length_end[(ea + i) & 0xffff] = 0;
  }
}

static void check_align_and_put_writer(Addr ea, int datasize, funcinst *current_funcinst_ptr, int tid){

  //If we only want to calculate memory footprint of the Shadow Memory but not actually declare structures and evaluate the logic
  //The use of this option can potentially be avoided in favor of turning on drw-debug
  if(CLG_(clo).drw_calcmem){
    checkAddrValid( ea );
    void **sm_temp, **sm_start = &PM[ea >> SM_bits], **sm_end = &PM[(ea + datasize - 1) >> SM_bits]; // bits [31..SM_bits]
    sm_temp = sm_start;
    sm_end++;
    do{
      if ((*sm_temp) == ((void*)CLG_(DSM))){
	*sm_temp = (void*) CLG_(Dummy_SM);
	CLG_(num_sms)++;
      }
      sm_temp++;
    } while(sm_temp != sm_end);
    return;
  }

  void *sm_start = get_SM_for_writing(ea);
  void *sm_end = get_SM_for_writing(ea + datasize - 1);

  if(sm_start != sm_end){
    if(CLG_(clo).drw_events)
      put_writer_event(ea, datasize, current_funcinst_ptr, tid);
    else if(CLG_(clo).drw_datareuse)
      put_writer_datareuse(ea, datasize, current_funcinst_ptr, tid);
    else
      put_writer(ea, datasize, current_funcinst_ptr, tid);
  }
  else{
    if(CLG_(clo).drw_events)
      put_writer_event_singlesm(ea, sm_start, datasize, current_funcinst_ptr, tid);
    else if(CLG_(clo).drw_datareuse)
      put_writer_datareuse(ea, datasize, current_funcinst_ptr, tid);
    else
      put_writer_singlesm(ea, sm_start, datasize, current_funcinst_ptr, tid);
  }
}

/* Helper functions to handle creation and manipulation of data structures to 
	hold information for client functions called from every unique context */
static funcinst* create_funcinstlist (funcinst* caller, int fn_num, int tid){

  int k; funcinst *funcinsttemp;
  funcinfo *funcinfotemp;
  drwglobvars *thread_globvar = CLG_(thread_globvars)[tid];
  char drw_filename[50]; SysRes res;

  //We are guaranteed that a funcinfo exists for this by now.
  funcinfotemp = thread_globvar->funcarray[fn_num];
  funcinsttemp  = (funcinst*) CLG_MALLOC("cl.funcinst.gc.1",sizeof(funcinst));
  CLG_(num_funcinsts)++;
  if(funcinsttemp == 0)
    handle_memory_overflow();
  //1b. Similar to constructor, initialize all member variables for funcinst right here
  funcinsttemp->fn_number = fn_num;
  funcinsttemp->tid = tid;
  funcinsttemp->function_info = funcinfotemp; //Store pointer to central info store of function
  funcinsttemp->ip_comm_unique = 0;
  //funcinsttemp->op_comm_unique = 0;
  funcinsttemp->ip_comm = 0;
  //funcinsttemp->op_comm = 0;
  funcinsttemp->num_calls = 0;
  funcinsttemp->instrs = 0;
  funcinsttemp->iops = 0;
  funcinsttemp->flops = 0;
  //  funcinsttemp->consumedlist = 0; //Again, declare when needed and initialize. We might want to add at least the self function
  funcinsttemp->consumedlist = (dependencelist*) CLG_MALLOC("cl.deplist.gc.1",sizeof(dependencelist));
  CLG_(num_dependencelists)++;
  funcinsttemp->num_dependencelists = 1;
  funcinsttemp->num_addrchunks = 0;
  funcinsttemp->num_addrchunknodes = 0;
  funcinsttemp->consumedlist->prev = 0;
  funcinsttemp->consumedlist->next = 0;
  funcinsttemp->consumedlist->size = 0;
  funcinsttemp->consumerlist = 0;
  funcinsttemp->caller = caller; //If caller is defined, then go ahead and put that in. (Otherwise it might error out when doing the first ever function)
  funcinsttemp->callees = (funcinst**) CLG_MALLOC("cl.numcallees.gc.1",sizeof(funcinst*) * NUM_CALLEES);
  //Initialize histogram stuff
  funcinsttemp->input_histogram = 0;
  funcinsttemp->local_histogram = 0;
  funcinsttemp->input_reuse_counts = 0;
  funcinsttemp->local_reuse_counts = 0;

  for(k = 0; k < NUM_CALLEES; k++)
    funcinsttemp->callees[k] = 0;
  funcinsttemp->num_callees = 0;
  funcinsttemp->callee_prnt_idx = 0;
  CLG_(num_callee_array_elements) += NUM_CALLEES;
  if(funcinfotemp != 0){
    funcinsttemp->funcinst_number = funcinfotemp->number_of_funcinsts;
    funcinfotemp->number_of_funcinsts++;
  }
  else{
    funcinsttemp->funcinst_number = -1; //This indicates it is not instrumented
    //    funcinsttemp->funcinst_list_next = 0;
    //    funcinsttemp->funcinst_list_prev = 0;
  }

  funcinsttemp->num_events = 0;
  //Only do this if we are running threads and events. If we run function and events, do not do this
  if(!CLG_(clo).drw_thread_or_func){
    /***1. CREATE THE NECESSARY FILE***/
    if(CLG_(clo).drw_events){
      VG_(sprintf)(drw_filename, "sigil.events.out-%d",tid);
      res = VG_(open)(drw_filename, VKI_O_WRONLY|VKI_O_TRUNC, 0);
      
      if (sr_isError(res)) {
	      res = VG_(open)(drw_filename, VKI_O_CREAT|VKI_O_WRONLY,
			    VKI_S_IRUSR|VKI_S_IWUSR);
	      if (sr_isError(res)) {
	        file_err(); // If can't open file, then create one and then open. If still erroring, Valgrind will die using this call - Sid
	      }
      }
      funcinsttemp->res = res;
      funcinsttemp->fd = (Int) sr_Res(res);
    }
    /***1. DONE CREATION***/
  }

  return funcinsttemp;
}

/***
 * Search the list to see if the context is present in the list
 * If it is present, returns 1, otherwise return 0
 */
static int search_funcinstlist (funcinst** funcinstoriginal, Context* func_cxt, int cxt_size, funcinst** refarg, int tid){

  //funcinst *funcinstpointer = *funcinstoriginal; //funcinstpointer is not used since we have an easy way of indexing the functioninsts via a fixed size array
  int i, j, ilimit, temp_cxt_num, foundflag = 0;
  funcinst *current_funcinst_ptr;
  drwglobvars *thread_globvar = CLG_(thread_globvars)[tid];

  if(thread_globvar->funcinst_first == 0){
    //cxt_size should be 1 in this case. Do an assert.
    if(cxt_size != 1){
      VG_(printf)("Context size is not 1, when first function has not yet been encountered. Aborting...\n");
      VG_(exit)(1);
    }
    current_funcinst_ptr = create_funcinstlist(0, func_cxt->fn[0]->number, tid);
    *refarg = current_funcinst_ptr;
    return 0;
  }
  //Here we need to check if the context specified is in full or not.
  if(func_cxt->fn[cxt_size - 1]->number == ((*funcinstoriginal)->fn_number)){ //Check if the top of the context is also the topmost function in the funcinst list. If its not, then the list is not full.
    current_funcinst_ptr = *funcinstoriginal;
    ilimit = cxt_size - 2;
  }
  else{ //Not handled at the moment
    //For implementation hints see same section in insert_to_funcinstlist
    VG_(printf)("\nPartial context not yet supported. Please increase the number of --separate-callers option when invoking callgrind\n");
    VG_(exit)(1);
    //tl_assert(0);
  }

  for(i = ilimit; i >= 0; i--){ //Traverse the list of the context of the current function, knowing that it starts from the first ever function.
    temp_cxt_num = func_cxt->fn[i-1]->number;
    for(j = 0; j < current_funcinst_ptr->num_callees; j++){ //Traverse all the callees of the currentfuncinst
      if(current_funcinst_ptr->callees[j]->fn_number == temp_cxt_num){ //If the callee matches what is seen in the context, then update the currentfuncinst_ptr
	current_funcinst_ptr = current_funcinst_ptr->callees[j];
	foundflag = 1;
	break;
      }
    }
    if(foundflag != 1)
      return 0;
    else
      foundflag = 0;
  }

  *refarg = current_funcinst_ptr;
  return 1;
}

/***
 * Sigil requires full calling contexts to function correctly and identify which function instance node to update.
 * Search the list to see if the context is present in the list
 * If it is present, returns 1, otherwise insert and return 0
 * Currently we are not using the return value, though original versions used the return value under certain conditions
 */
static int insert_to_funcinstlist (funcinst** funcinstoriginal, Context* func_cxt, int cxt_size, funcinst** refarg, int tid, Bool full_context){
  funcinst *funcinsttemp, *current_funcinst_ptr; //funcinstpointer is not used since we have an easy way of indexing the functioninsts via a fixed size array
  int i, j, ilimit, temp_cxt_num, entire_context_found = 1, foundflag = 0, contextcache_missflag = 0;
  drwglobvars *thread_globvar = CLG_(thread_globvars)[tid];
  funcinfo *current_funcinfo_ptr = thread_globvar->funcarray[func_cxt->fn[0]->number];

  //CLG_(current_cxt) = CLG_(current_state)->bbcc->bb->jmpkind; //Save the context, for quick checking when storeDRWcontext is invoked in the same context.
  if(thread_globvar->funcinst_first == 0){
    //cxt_size should be 1 in this case. Do an assert. //Found out later that this need not always necessarily be the case. This can be put back in when both storeDRWcontext and storeIcontext are "activated"
    if(func_cxt->size != 1){
      VG_(printf)("Context size is not 1, when first function has not yet been encountered. Aborting...\n");
      VG_(exit)(1);
    }
    current_funcinst_ptr = create_funcinstlist(0, func_cxt->fn[0]->number, tid);
    *refarg = current_funcinst_ptr;
    thread_globvar->funcinst_first = current_funcinst_ptr;
    return 0;
  }

  //The cache should be used only when checking full contexts
  if(!full_context)
    contextcache_missflag = 1;

  /*Here we need to check if the context specified is in full or not. 
	Currently this limitation in the use of Sigil can cause benchmarks with 
	very large number of functions called from different contexts to have issues.*/
  if(func_cxt->fn[cxt_size - 1]->number == ((*funcinstoriginal)->fn_number)){ //Check if the top of the context is also the topmost function in the funcinst list. If its not, then the list is not full.
    current_funcinst_ptr = *funcinstoriginal;
    ilimit = cxt_size - 1;
    //Before going over the entire context first do the topmost function.
    ilimit--;
    // Check if cache has the appropriate first entry. 
	// We have a redundant check for cxt_size here as the sizes can be never be equal if the fn_numbers corresponding cxt_sizes are not equal
    if(full_context)
      if((cxt_size != current_funcinfo_ptr->context_size) || (current_funcinfo_ptr->context[cxt_size - 1].fn_number != func_cxt->fn[cxt_size - 1]->number)){//Cache context size does not match. Set the flag and initialize
	current_funcinfo_ptr->context[cxt_size - 1].fn_number = func_cxt->fn[cxt_size - 1]->number;
	current_funcinfo_ptr->context[cxt_size - 1].funcinst_ptr = current_funcinst_ptr;
	current_funcinfo_ptr->context_size = cxt_size;
	contextcache_missflag = 1;
      }
  }
  else{ //Not handled at the moment
    VG_(printf)("\nPartial context not yet supported. Please increase the number of --separate-callers option when invoking callgrind\n");
    VG_(exit)(1);
    //tl_assert(0);
	// Beginnings of an alternate approach given below
	//func_num1 = func_cxt->fn[cxt_size - 1]->number;
    //func_num2 = thread_globvar->funcinst_first;
    //Traverse from funcnum2 down and find the first instance of func_num1
    //If found, set the current_funcinst_ptr to that guy and ilimit to cxt_size - 2.
    //If not found, set the current_funcinst_ptr to funcinst_first and ilimit to cxt_size - 2.
  }

  for(i = ilimit; i >= 0; i--){ //Traverse the list of the context of the current function, knowing that it starts from the first ever function.
    temp_cxt_num = func_cxt->fn[i]->number;
    //First keep checking the cache and move forward with the appropriate funcinst pointers, if the miss flag has not been set
    if(full_context)
      if(!contextcache_missflag && current_funcinfo_ptr->context[i].fn_number == temp_cxt_num){
	current_funcinst_ptr = current_funcinfo_ptr->context[i].funcinst_ptr;
	continue;
      }
    //Cache miss, so mark the miss and re-cache the context in the function's data structure
    if(full_context){
      contextcache_missflag = 1;
      current_funcinfo_ptr->context[i].fn_number = temp_cxt_num;
    }
    for(j = 0; j < current_funcinst_ptr->num_callees; j++){ //Traverse all the callees of the currentfuncinst
      if(current_funcinst_ptr->callees[j]->fn_number == temp_cxt_num){ //If the callee matches what is seen in the context, then update the currentfuncinst_ptr
	current_funcinst_ptr = current_funcinst_ptr->callees[j];
	foundflag = 1;
	break;
      }
    }
    if(foundflag != 1){
      entire_context_found = 0;
      //Create a new funcinst and initialize it
      funcinsttemp = create_funcinstlist(current_funcinst_ptr, temp_cxt_num, tid);
      current_funcinst_ptr->callees[current_funcinst_ptr->num_callees++] = funcinsttemp;
      current_funcinst_ptr = funcinsttemp; //For the next iteration
    }
    //Since we are in this part of the code, we are re-caching, so update the cache
    if(full_context)
      current_funcinfo_ptr->context[i].funcinst_ptr = current_funcinst_ptr;
    foundflag = 0;
  }
  *refarg = current_funcinst_ptr;
  return entire_context_found;
}

/***
 * Search the list to see if the function is present in the list
 * If it is present, returns 1, otherwise return 0
 */
static int search_funcnodelist (funcinfo** funcinfooriginal, int func_num, funcinfo** refarg, int tid){
  int funcarrayindex = func_num;
  drwglobvars *thread_globvar = CLG_(thread_globvars)[tid];

  if(thread_globvar->funcarray[funcarrayindex] != 0){ //If there is already an entry, just return positive
    *refarg = thread_globvar->funcarray[funcarrayindex];
    return 1;
  }
  *refarg = 0;
  return 0;
}

/***
 * Search the list to see if the function is present in the list
 * If it is present, returns 1, otherwise insert and return 0
 */
static int insert_to_funcnodelist (funcinfo **funcinfooriginal, int func_num, funcinfo **refarg, fn_node* function, int tid){
  funcinfo *funcinfotemp; //funcinfopointer is not used since we have an easy way of indexing the functioninfos via a fixed size array
  int funcarrayindex = func_num, i = 0;
  drwglobvars *thread_globvar = CLG_(thread_globvars)[tid];

  if(thread_globvar->funcarray[funcarrayindex] != 0){ //If there is already an entry, just return positive
    *refarg = thread_globvar->funcarray[funcarrayindex];
    return 1;
  }

  funcinfotemp  = (funcinfo*) CLG_MALLOC("cl.funcinfo.gc.1",sizeof(funcinfo));
  CLG_(num_funcinfos)++;
  if(funcinfotemp == 0)
    handle_memory_overflow();
  //1b. Similar to constructor, lets put all the initialization info for funcinfo right here
  funcinfotemp->fn_number = funcarrayindex;
  funcinfotemp->function = function; //Store off the name indirectly
  //funcinfotemp->producedlist = 0; //Declare producer address chunks when an address is seen. That way the range can be allocated then as well.
  //funcinfotemp->consumerlist = 0; //Again, declare when needed and initialize. We might want to add at least the self function
  thread_globvar->funcarray[funcarrayindex] = funcinfotemp;

  funcinfotemp->next = *funcinfooriginal; //Insert new function at the head of the list. This will also work if the list is empty
  funcinfotemp->prev = 0;
  funcinfotemp->number_of_funcinsts = 0;
  //  funcinfotemp->funcinst_list = 0;
  funcinfotemp->context = (funccontext*) CLG_MALLOC("cl.funccontext.gc.1",sizeof(funccontext)*CLG_(clo).separate_callers);
  CLG_(num_funccontexts) += CLG_(clo).separate_callers;
  funcinfotemp->context_size = 0;
  for(i = 0; i < CLG_(clo).separate_callers; i++){
    funcinfotemp->context[i].fn_number = -1; //-1 indicates uninitialized context
    funcinfotemp->context[i].funcinst_ptr = 0;
  }
  //funcinfopointer->prev = funcinfotemp;
  if(*funcinfooriginal != 0)
    (*funcinfooriginal)->prev = funcinfotemp;
  *funcinfooriginal = funcinfotemp;
  *refarg = funcinfotemp;
  return 0;
}

/*  Sigil used to maintain a linked list of address ranges that were produced/consumed for each function instance.
    This was later changed to a technique where chunks of arrays are allocated on demand and linked together. 
	Thus, the following two insert and remove functions are no longer in use. */
static int insert_to_addrchunklist(addrchunk** chunkoriginal, Addr ea, Int datasize, ULong* refarg, int produced_list_flag, funcinst *current_funcinst_ptr) {
  addrchunk *chunk = *chunkoriginal; // chunk points to the first element of addrchunk array.
  addrchunknode *chunknodetemp = 0, *chunknodecurr = 0, *next_chunk = 0;
  Addr range_first = ea, range_last = ea+datasize-1, curr_range_last;
  ULong return_count = datasize, firsthash, lasthash, chunk_arr_idx, curr_count,arr_lasthash,curr_count_unique,firsthash_new;
  int i, partially_found =0, return_value2=0;

  // If the addrchunk array pointer is '0', indicating that there are no addresses inserted into the given addrchunk array,
  // allocate the array and point it to the chunk. Every element of this new array is initialized with first hash, last hash and an original
  // pointer that points to nothing
  if(chunk == 0) {
    chunk = (addrchunk*) CLG_MALLOC("cl.addrchunk.gc.1", (ADDRCHUNK_ARRAY_SIZE)*sizeof(addrchunk)); // check if its right
    CLG_(num_addrchunks) += ADDRCHUNK_ARRAY_SIZE;
    current_funcinst_ptr->num_addrchunks += ADDRCHUNK_ARRAY_SIZE;
    *chunkoriginal = chunk;
    for(i=0; i<ADDRCHUNK_ARRAY_SIZE; i++) {
      (chunk+i)->first = (HASH_SIZE)*i;
      (chunk+i)->last = (HASH_SIZE*i) + (HASH_SIZE -1 );
      (chunk+i)->original = 0;
    }
  }
  /*Total addrchunk array elements = ADDRCHUNK_ARRAY_SIZE
    Each array element has a hash range of HASH_SIZE.
    i.e., hash range of element i is i*HASH_SIZE to ((i+1)*HASH_SIZE)-1
    Therefore, total hash range covered is 0 to HASH_SIZE * ADDRCHUNK_ARRAY_SIZE
    
    In the below code, hash of the given address range is calculated to
    determine which element of the array should be searched. */
  
  firsthash = range_first % (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
  lasthash = range_last % (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
  firsthash_new = firsthash;
  // do while iterates over addrchunk array elements till hash of the last range passed
  // falls into an element's hash range.
  
  do {
    //index of the array element that needs to be searched for the given address
    //range is calculated below.
    firsthash = firsthash_new;
    chunk_arr_idx = firsthash/HASH_SIZE;
    arr_lasthash=(chunk+chunk_arr_idx)->last;
    
    //hash of the last range is calculated and compared against the current array
    //element's last hash to determine if the given address range should be split
    if(lasthash > arr_lasthash || lasthash < firsthash) {
      curr_range_last = range_first + (arr_lasthash-firsthash);
      firsthash_new = (arr_lasthash+1)% (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
    } else curr_range_last = range_last;
    
    //if the determined array element contains any address chunks, then those address
    //chunks are searched to determine if the address range being inserted is already present or should be inserted
    // if the original pointer points to nothing, then a new node is inserted
    
    if((chunk+chunk_arr_idx)->original == 0) {
      chunknodetemp = (addrchunknode*) CLG_MALLOC("c1.addrchunknode.gc.1",sizeof(addrchunknode));
      CLG_(num_addrchunknodes)++;
      current_funcinst_ptr->num_addrchunknodes++;
      chunknodetemp -> prev = 0;
      chunknodetemp -> next =0;
      chunknodetemp -> rangefirst = range_first;
      chunknodetemp -> rangelast = curr_range_last;
      chunknodetemp -> count = curr_range_last - range_first + 1;
      chunknodetemp -> count_unique = curr_range_last - range_first + 1;
      (chunk+chunk_arr_idx)->original = chunknodetemp;
    } // end of if that checks if original == 0
    else {
      chunknodecurr = (chunk+chunk_arr_idx)->original;
      curr_count = curr_range_last - range_first +1;
      
      //while iterates over addrchunknodes present in the given array element until it finds
      // the desired address or it reaches end of the list.
      while(1) {
	// if the address being inserted is less than current node's address, then a new node is inserted,
	// and further search is terminated as the node's are in increasing order of addresses
	if(curr_range_last < (chunknodecurr->rangefirst-1)) {
	  chunknodetemp = (addrchunknode*) CLG_MALLOC("c1.addrchunknode.gc.1",sizeof(addrchunknode));
	  CLG_(num_addrchunknodes)++;
	  current_funcinst_ptr->num_addrchunknodes++;
	  chunknodetemp -> prev = chunknodecurr->prev;
	  chunknodetemp -> next = chunknodecurr;
	  
	  if(chunknodecurr->prev!=0) chunknodecurr -> prev->next = chunknodetemp;
	  else (chunk+chunk_arr_idx)->original = chunknodetemp;
	  chunknodecurr -> prev = chunknodetemp;
	  chunknodetemp -> rangefirst = range_first;
	  chunknodetemp -> rangelast = curr_range_last;
	  chunknodetemp -> count_unique = curr_range_last - range_first + 1;
	  chunknodetemp -> count = curr_count;
	  break;
	}
	// if current node falls within the address range being inserted, then the current node is deleted and the
	// counts are adjusted appropriately
	else if((range_first <= (chunknodecurr->rangefirst-1)) && (curr_range_last >= (chunknodecurr->rangelast +1))) {
	  return_value2 =1;
	  return_count -= chunknodecurr->count_unique;
	  partially_found =1;
	  //We don't need to append in this case because the addresses we are writing will be noted as belonging to the current event and rightly so.
	  //If the previous event that had a part of the addresses should have already been read from by any other thread by now.
	  //Only bad code would do writes with no intervening read(in the case of threads it will be with locks of course)
	  //if(produced_list_flag)
	  //append_addr_to_event(chunknodecurr, range_first, curr_range_last);
	  if(chunknodecurr->next ==0 ){
	    chunknodecurr -> rangefirst = range_first;
	    chunknodecurr -> rangelast = curr_range_last;
	    chunknodecurr -> count_unique = curr_range_last - range_first + 1;
	    chunknodecurr -> count += curr_count;
	    break;
	  }
	  else {
	    if(chunknodecurr->prev!=0) chunknodecurr->prev->next = chunknodecurr->next;
	    else (chunk+chunk_arr_idx)->original = chunknodecurr->next;
	    
	    if(chunknodecurr->next!=0) chunknodecurr->next->prev = chunknodecurr->prev;
	    curr_count += chunknodecurr->count;
	    next_chunk = chunknodecurr ->next;
	    drw_free(chunknodecurr, &CLG_(num_addrchunknodes));
	    current_funcinst_ptr->num_addrchunknodes--;
	    chunknodecurr = next_chunk;
	  }
	}
	// if the address being inserted is more than current node's address range, search continues with the next
	// node
	else if(range_first > (chunknodecurr->rangelast +1)){
	  if(chunknodecurr->next != 0) chunknodecurr = chunknodecurr->next;
	  else {
	    
	    chunknodetemp = (addrchunknode*) CLG_MALLOC("c1.addrchunknode.gc.1",sizeof(addrchunknode));
	    CLG_(num_addrchunknodes)++;
	    current_funcinst_ptr->num_addrchunknodes++;
	    chunknodetemp -> prev = chunknodecurr;
	    chunknodetemp -> next = 0;
	    chunknodecurr -> next = chunknodetemp;
	    
	    chunknodetemp -> rangefirst = range_first;
	    chunknodetemp -> rangelast = curr_range_last;
	    chunknodetemp -> count_unique = curr_range_last - range_first + 1;
	    chunknodetemp -> count = curr_count;
	    break;
	  }
	}
	// if the address range being inserted falls within the current node's address range, count of current node is adjusted
	// appropriately, and the search is terminated
	else if((range_first >= (chunknodecurr->rangefirst))&&(curr_range_last<=(chunknodecurr->rangelast))) {
	  return_count -= (curr_range_last- range_first+1);
	  partially_found =1;
	  return_value2=1;
	  chunknodecurr->count += curr_count;
	  break;
	}
	
	else {
	  curr_count_unique = chunknodecurr -> count_unique;
	  
	  //if last address of the address range being inserted falls within current node, then range and count of current node
	  // are adjusted appropriately and search is terminated
	  if(range_first <= (chunknodecurr->rangefirst -1))
	    {
	      if(curr_range_last != (chunknodecurr->rangefirst -1)) return_value2 =1;
	      return_count -= (curr_range_last - (chunknodecurr->rangefirst)+1);
	      chunknodecurr -> rangefirst = range_first;
	      chunknodecurr ->count +=curr_count;
	      chunknodecurr -> count_unique = (chunknodecurr->rangelast) - (chunknodecurr->rangefirst) + 1;
	      break;
	    }
	  // if the last address of the address range being inserted is after the current node's address,
	  //and it is less than next node's start address, then current node's range is adjusted appropriately and the search terminates
	  //This implicitly takes care of the case where rangefirst is greater than chunknodecurr->rangefirst but less than chunknodecurr->rangelast. This is true by process of elimination above.
	  if((curr_range_last >= (chunknodecurr->rangelast +1)) &&((chunknodecurr->next ==0)||(curr_range_last < chunknodecurr->next->rangefirst -1)))  {
	    if(range_first != (chunknodecurr->rangelast+1)) return_value2 =1;
	    return_count -= ((chunknodecurr->rangelast)-range_first+1);
	    chunknodecurr -> rangelast = curr_range_last;
	    chunknodecurr -> count_unique = (chunknodecurr->rangelast) - (chunknodecurr->rangefirst) + 1;
	    chunknodecurr -> count += curr_count;
	    chunknodecurr = chunknodecurr->next;
	    break;
	  }
	  
	  // if the last address of the address range being inserted falls exactly at the edge or within the next node's range, then
	  //current node is deleted and the search is continued
	  else if((curr_range_last >= (chunknodecurr->rangelast +1)) &&(chunknodecurr->next !=0)&&(curr_range_last >= (chunknodecurr->next->rangefirst -1))) {
	    if(range_first != (chunknodecurr->rangelast+1)) return_value2 =1;
	    return_count -= ((chunknodecurr -> rangelast)- range_first +1);
	    range_first = chunknodecurr->rangefirst;
	    curr_count += chunknodecurr->count;
	    if(chunknodecurr->prev!=0) chunknodecurr->prev->next = chunknodecurr->next;
	    else (chunk+chunk_arr_idx)->original = chunknodecurr->next;
	    
	    if(chunknodecurr->next != 0) chunknodecurr->next->prev = chunknodecurr->prev;
	    next_chunk = chunknodecurr->next;
	    drw_free(chunknodecurr, &CLG_(num_addrchunknodes));
	    current_funcinst_ptr->num_addrchunknodes--;
	    chunknodecurr = next_chunk;
	  }
	}
	//break while loop if end of the addrchunknode list is reached
	if(chunknodecurr ==0) break;
      } // end of while (1)
    } // end of else corresponding to original ==0 check
      //Before moving to next array elements, range first is initialized to next element's range first

    //PRINT WARNINGS WHEN BAD COUNT/COUNT_UNIQUES ARE ENCOUNTERED
    if(chunknodecurr)
      if((chunknodecurr->rangelast - chunknodecurr->rangefirst + 1) != chunknodecurr->count_unique)
	VG_(printf)("Bad count_unique encountered\n");
    if(chunknodetemp)
      if((chunknodetemp->rangelast - chunknodetemp->rangefirst + 1) != chunknodetemp->count_unique)
	VG_(printf)("Bad count_unique encountered\n");
    
    range_first = curr_range_last+1;
    //while loop checks if the search should stop with the current array element, or it should continue.
    // if the hash of the last range of the address being searched lies within current element's hash range,
    // search stops. Second check takes care of the scenario where the address range being searched wraps around
    // addrchunk array
  } while (lasthash > arr_lasthash || lasthash<firsthash);
  //number of addresses not found are returned
  *refarg = return_count;
  // if the address range is fully found, return 2
  if (return_count ==0) partially_found =2;
  // if the address range is partially found return 1, else return 0.
  else if(return_value2 ==1) partially_found = 1;
  return partially_found;
}

static int remove_from_addrchunklist(addrchunk** chunkoriginal, Addr ea, Int datasize, ULong* refarg, int produced_list_flag, funcinst *vert_parent_fn) {

  addrchunk *chunk = *chunkoriginal; // chunk points to the first element of addrchunk array.
  addrchunknode *chunknodetemp, *chunknodecurr, *next_chunk;
  Addr range_first = ea, range_last = ea+datasize-1, curr_range_last;
  ULong return_count = datasize, firsthash, lasthash, chunk_arr_idx, curr_count,arr_lasthash, firsthash_new;
  int partially_found =0, return_value2=0;


  // If the addrchunk array pointer is '0', indicating that there are no addresses inserted into the given addrchunk array,
  // allocate the array and point it to the chunk. Every element of this new array is initialized with first hash, last hash and an original
  // pointer that points to nothing
  if(chunk == 0) {
    *refarg = datasize;
    return 0;
  }
  
  /*Total addrchunk array elements = ADDRCHUNK_ARRAY_SIZE
    Each array element has a hash range of HASH_SIZE.
    i.e., hash range of element i is i*HASH_SIZE to ((i+1)*HASH_SIZE)-1
    Therefore, total hash range covered is 0 to HASH_SIZE * ADDRCHUNK_ARRAY_SIZE
    
    In the below code, hash of the given address range is calculated to
    determine which element of the array should be searched. */
  
  firsthash = range_first % (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
  lasthash = range_last % (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
  firsthash_new = firsthash;
  
  // do while iterates over addrchunk array elements till hash of the last range passed
  // falls into an element's hash range.
  do {
    //index of the array element that needs to be searched for the given address
    //range is calculated below.
    firsthash = firsthash_new;
    chunk_arr_idx = firsthash/HASH_SIZE;
    arr_lasthash=(chunk+chunk_arr_idx)->last;
    
    //hash of the last range is calculated and compared against the current array
    //element's last hash to determine if the given address range should be split
    if(lasthash > arr_lasthash || lasthash < firsthash) {
      curr_range_last = range_first + (arr_lasthash-firsthash);
      firsthash_new = (arr_lasthash+1)% (HASH_SIZE * ADDRCHUNK_ARRAY_SIZE);
    } else curr_range_last = range_last;
    
    if((chunk+chunk_arr_idx)->original != 0) {
      chunknodecurr = (chunk+chunk_arr_idx)->original;
      curr_count = curr_range_last - range_first +1;
      
      //while iterates over addrchunknodes present in the given array element until it finds
      // the desired address or it reaches end of the list.
      while(1) {
	// if the address range being removed is less than current node's address, then search terminates,
	//as the node's are in increasing order of addresses
	if(curr_range_last < (chunknodecurr->rangefirst))  break;
	
	// if the current node falls within the address range being searched , then current node is removed
	// and search continues with the next addrchunknode
	else if((range_first <= (chunknodecurr->rangefirst)) && (curr_range_last >= (chunknodecurr->rangelast))) {
	  return_value2 =1;
	  return_count -= chunknodecurr->count_unique;
	  
	  if(chunknodecurr->prev!=0) chunknodecurr->prev->next = chunknodecurr->next;
	  else (chunk+chunk_arr_idx)->original =chunknodecurr->next;
	  
	  if(chunknodecurr->next!=0) chunknodecurr->next->prev = chunknodecurr->prev;
	  next_chunk = chunknodecurr ->next;
	  drw_free(chunknodecurr, &CLG_(num_addrchunknodes));
	  vert_parent_fn->num_addrchunknodes--;
	  chunknodecurr = next_chunk;
	}
	
	// if the address range being searched is after current node, then search continues with the next node
	else if(range_first > (chunknodecurr->rangelast)) chunknodecurr = chunknodecurr->next;
	
	// if the address being searched falls within current node's address range, then the portion of the
	// address being searched is removed and the current nodes address range is adjusted appropriately.
	
	else if((range_first >= chunknodecurr->rangefirst) && (curr_range_last <= chunknodecurr->rangelast)) {
	  chunknodecurr ->count -= curr_range_last - range_first + 1;//datasize;
	    chunknodecurr->count_unique -= curr_range_last - range_first + 1;//datasize;
	  return_count -= (curr_range_last - range_first +1);
	  return_value2=1;
	  if((range_first > chunknodecurr->rangefirst) && (curr_range_last < chunknodecurr->rangelast)){
	    chunknodetemp = (addrchunknode*) CLG_MALLOC("cl.addrchunknode.gc.1",sizeof(addrchunknode));
	    CLG_(num_addrchunknodes)++;
	    vert_parent_fn->num_addrchunknodes++;
	    if(chunknodecurr->prev == 0) 	{ (chunk+chunk_arr_idx)->original = chunknodetemp;
	    }
	    else   chunknodecurr->prev->next = chunknodetemp;
	    
	    chunknodetemp->prev = chunknodecurr->prev;
	    chunknodetemp->next = chunknodecurr;
	    chunknodecurr->prev = chunknodetemp;
	    
	    chunknodetemp->rangefirst = chunknodecurr->rangefirst;
	    chunknodetemp->rangelast = range_first-1;
	    chunknodetemp->count = chunknodetemp->rangelast - chunknodetemp->rangefirst +1;
	    chunknodetemp->count_unique = chunknodetemp->rangelast - chunknodetemp->rangefirst +1;
	    chunknodecurr->rangefirst = curr_range_last +1;
	    chunknodecurr->count -= chunknodetemp->count;
	    chunknodecurr->count_unique -= chunknodetemp->count_unique;
	    
	  }
	  else if(range_first == (chunknodecurr->rangefirst)) {
	    chunknodecurr->rangefirst = curr_range_last+1;
	  } else if(curr_range_last == (chunknodecurr->rangelast)) {
	    chunknodecurr->rangelast = range_first-1;
	  }
	  chunknodecurr=chunknodecurr->next;
	}
	
	// Following two cases handle the cases where address range being searched either starts within current node
	// or ends within current node. return_count and address_array are appropriately initialized, and then search
	// continues with the next node.
	else {
	  if(range_first >= (chunknodecurr->rangefirst)) {
	    return_count -= ((chunknodecurr -> rangelast)- range_first + 1);
	    chunknodecurr->count -= (chunknodecurr->rangelast - range_first +1);
	    chunknodecurr->count_unique -= (chunknodecurr->rangelast - range_first +1);
	    chunknodecurr -> rangelast = range_first-1;
	    return_value2 =1;
	  }
	  if(curr_range_last <= (chunknodecurr->rangelast)) {
	    return_count -= (curr_range_last - chunknodecurr->rangefirst + 1);
	    chunknodecurr->count -= (curr_range_last - chunknodecurr->rangefirst +1);
	    chunknodecurr->count_unique -= (curr_range_last - chunknodecurr->rangefirst +1);
	    chunknodecurr -> rangefirst = curr_range_last+1;
	    return_value2=1;
	  }
	  chunknodecurr = chunknodecurr->next;
	}
	//break while loop if end of the addrchunknode list is reached
	if(chunknodecurr ==0) break;
      } // end of while (1)
    } // end of else corresponding to original ==0 check
      //Before moving to next array elements, range first is initialized to next element's range first
    range_first = curr_range_last+1;
    //while loop checks if the search should stop with the current array element, or it should continue.
    // if the hash of the last range of the address being searched lies within current element's hash range,
    // search stops. Second check takes care of the scenario where the address range being searched wraps around
    // addrchunk array
  } while (lasthash > arr_lasthash || lasthash < firsthash);
  //number of addresses not found are returned
  *refarg = return_count;
  // if the address range is fully found, return 2
  if (return_count ==0) partially_found =2;
  // if the address range is partially found return 1, else return 0.
  else if(return_value2 ==1) partially_found = 1;
  return partially_found;
}

/***
 * Horizontally traverse the list and removes the address range
 * Returns nothing
 * Currently not in use as this search and update on every address is too expensive. 
 * A simpler solution with mild tradeoff in accuracy was made. See the "get_last_writer" functions.
 */
static void traverse_and_remove_in_dependencelist (dependencelist_elemt** chunkoriginal, Addr ea, Int datasize){
  dependencelist_elemt *chunk = *chunkoriginal;
  ULong leftcount;

  while(chunk){
    remove_from_addrchunklist(&chunk->consumedlist, ea, datasize, &leftcount, 0, chunk->vert_parent_fn);
    chunk = chunk->next_horiz;
  }
}

/***
 * Given a funcinst structure, this function will check if any part of the address range passed in, is produced by the funcinst structure
 * Returns void
 */
static __inline__
dependencelist_elemt* insert_to_dependencelist(funcinst *current_funcinst_ptr, funcinst *consumed_fn){
  dependencelist *temp_list; int temp_funcinst_number, temp_fn_number, temp_tid;
  int i, found_in_list_flag = 0; dependencelist_elemt *return_list_elemt = 0;
  funcinst *funcinsttemp = consumed_fn;

  if(consumed_fn){
    temp_funcinst_number = consumed_fn->funcinst_number;
    temp_fn_number = consumed_fn->fn_number;
    temp_tid = consumed_fn->tid;
  }
  else{
    temp_funcinst_number = NO_PROD;
    temp_fn_number = NO_PROD;
    temp_tid = NO_PROD;
  }
  //1. First search the list to see if you find this dependence.
  temp_list = current_funcinst_ptr->consumedlist;
  while(1){
    for(i = 0; i < temp_list->size; i++){
      if((temp_funcinst_number == temp_list->list_chunk[i].funcinst_number) &&
	 (temp_fn_number == temp_list->list_chunk[i].fn_number) && 
	 (temp_tid == temp_list->list_chunk[i].tid)){
	//2. If found, you can increment a count?
	return_list_elemt = &temp_list->list_chunk[i];
	found_in_list_flag = 1;
	break;
      }
    }
    if(!temp_list->next || found_in_list_flag) break;
    else temp_list = temp_list->next;  
  }
  
  //3. If not found, add
  if(!found_in_list_flag){
    if(temp_list->size == DEPENDENCE_LIST_CHUNK_SIZE){
      temp_list->next = (dependencelist*) CLG_MALLOC("cl.deplist.gc.1",sizeof(dependencelist));
      CLG_(num_dependencelists)++;
      current_funcinst_ptr->num_dependencelists++;
      temp_list->next->size = 0; temp_list->next->next = 0;
      temp_list->next->prev = temp_list;
      temp_list = temp_list->next;
    }
    return_list_elemt = &temp_list->list_chunk[temp_list->size];
    return_list_elemt->fn_number = temp_fn_number;
    return_list_elemt->funcinst_number = temp_funcinst_number;
    return_list_elemt->tid = temp_tid;
    return_list_elemt->vert_parent_fn = current_funcinst_ptr;
    return_list_elemt->consumed_fn = consumed_fn;
    return_list_elemt->consumedlist = 0;
    return_list_elemt->count = 0;
    return_list_elemt->count_unique = 0;
    if(consumed_fn == 0){
      return_list_elemt->next_horiz = 0;
      return_list_elemt->prev_horiz = 0;
    }
    else{
      funcinsttemp = consumed_fn;
      return_list_elemt->next_horiz = funcinsttemp->consumerlist;
      return_list_elemt->prev_horiz = 0;
      if(funcinsttemp->consumerlist != 0)
	funcinsttemp->consumerlist->prev_horiz = return_list_elemt;
      funcinsttemp->consumerlist = return_list_elemt;
    }
    
    temp_list->size++;
  }
  return return_list_elemt;
}

/***
 * The mext few functions help generate and store re-use data for the histograms printed at the output.
 * There is basic data collection for local and input histograms that have data for the local and input classes of commnication respectively.
 * Given a histlist and value, this function will check if the value exists in one of the static ranges and will then insert it
 * Returns void WE HAVE NOT YET IMPLEMENTED DEALLOCATION
 */
static __inline__
void insert_to_histlist(hist_list_elemt **histogram, ULong reuse_length, ULong reuse_length_old, Int datasize){
  hist_list_elemt *temp_list, *temp_list_next = 0, *temp_list_prev = 0; int found_in_list_flag = 0, found_old_in_list_flag = 0; hist_list_elemt *temp_list_elemt;
  ULong range_first_old = 0, range_last_old = 0, range_first = 0, range_last = 0;

  if(reuse_length_old == reuse_length){
    tl_assert(!(reuse_length_old | reuse_length)); //Making sure that they are equal only during zeros
    return;
  }
  if(reuse_length_old > reuse_length){
    VG_(printf)("Reuse length mismatch. reuse_length_old: %llu reuse_length: %llu\n",reuse_length_old, reuse_length);
    tl_assert(reuse_length_old <= reuse_length);
  }

  //0. Calculate range with bin size 1000
  range_first_old = reuse_length_old/HISTOGRAM_BIN_SIZE * HISTOGRAM_BIN_SIZE;
  range_last_old = range_first_old + HISTOGRAM_BIN_SIZE - 1;
  range_first = reuse_length/HISTOGRAM_BIN_SIZE * HISTOGRAM_BIN_SIZE;
  range_last = range_first + HISTOGRAM_BIN_SIZE - 1;
  //1. First search the list to see if you find this range.
  temp_list = *histogram;
  while(temp_list){
    //    VG_(printf)("I am in hist list, hello\n");
      if(reuse_length_old){
	if((range_first_old == temp_list->rangefirst) && 
	   (range_last_old == temp_list->rangelast)){
	  //2. If found, decrement
	  temp_list->count -= datasize;
	  found_old_in_list_flag = 1;
	}
      }
      if((range_first == temp_list->rangefirst) && 
	 (range_last == temp_list->rangelast)){
	//If found increment
	temp_list->count += datasize;
	found_in_list_flag = 1;
      }
      if(!temp_list->count) {
      //if(0) {
	if (temp_list->prev)
	  temp_list->prev->next = temp_list->next;
	else
	  *histogram = temp_list->next;
	if (temp_list->next)
	  temp_list->next->prev = temp_list->prev;
	temp_list_next = temp_list->next;
	temp_list_prev = temp_list->prev;
	drw_free(temp_list, &CLG_(num_histlists));
	//Check if there are more chunks left. If there are, continue. Otherwise update temp_list and break here
	if(!temp_list_next){ 
	  temp_list = temp_list_prev; //Should be zero here.
	  if(!temp_list_prev)
	    tl_assert(*histogram == 0); //If list is empty that can also be handled correctly outside by setting temp_list = 0
	  break;
	}
	else if(temp_list_next->rangefirst > range_last){
	  if(temp_list_prev) //If previous exists we need to be one behind the place to insert
	    temp_list = temp_list_prev;
	  else
	    temp_list = temp_list_next; //If there is no previous then this is the first element, so we can be at the first element
	  break;
	}
	else{ //Finally, if there is a next element and its range is not lesser than our rangelast, go ahead and continue
	  temp_list = temp_list_next;
	  continue; 
	}
      }
      if(!temp_list->next) break;
      //To put things in ascending order, quit if the next one is higher. We will also only insert in ascending order
      if(temp_list->next->rangefirst > range_last)
	break;
      else temp_list = temp_list->next;
      if(found_old_in_list_flag && found_in_list_flag)
	break;
  }
  
  if(reuse_length_old){
    if(!found_old_in_list_flag){
      VG_(printf)("Couldn't find old range in list! reuse_length_old: %llu, reuse_length: %llu\n",reuse_length_old, reuse_length);
      tl_assert(0);
    }
  }
  //3. If not found, add
  if(!found_in_list_flag){
    temp_list_elemt = (hist_list_elemt*) CLG_MALLOC("cl.histlist.gc.1",sizeof(hist_list_elemt));
    CLG_(num_histlists)++;
    temp_list_elemt->rangefirst = range_first;
    temp_list_elemt->rangelast = range_last;
    temp_list_elemt->count = datasize;

    if(!temp_list){ //If no elements present in list
      temp_list_elemt->prev = temp_list_elemt->next = 0;
      *histogram = temp_list_elemt;      
    }
    else if(temp_list == *histogram && (temp_list->rangefirst > range_last)){ //Special case: If its the first element and it has a greater range, then we need to insert before, not after
      *histogram = temp_list_elemt;
      temp_list_elemt->next = temp_list;
      temp_list_elemt->prev = 0;
      temp_list->prev = temp_list_elemt;
    }
    else{ //Otherwise we will need to insert after temp_list
      if (temp_list->next)
	temp_list->next->prev = temp_list_elemt;
      temp_list_elemt->next = temp_list->next;
      temp_list_elemt->prev = temp_list;
      temp_list->next = temp_list_elemt;
    }
  }
}

/***
 * Given a reusecountlist and value, this function will check if the value exists in one of the static ranges and will then insert it
 * Returns void WE HAVE NOT YET IMPLEMENTED DEALLOCATION
 */
static __inline__
void insert_to_reusecountlist(hist_list_elemt **histogram, UInt reuse_count, UInt reuse_count_old, Int datasize){
  hist_list_elemt *temp_list, *temp_list_next = 0, *temp_list_prev = 0; int found_in_list_flag = 0, found_old_in_list_flag = 0; hist_list_elemt *temp_list_elemt;
  ULong range_first_old = 0, range_last_old = 0, range_first = 0, range_last = 0;

  if(reuse_count_old == reuse_count){
    if (reuse_count_old | reuse_count)
      VG_(printf)("Reuse counts are both non-zero! reuse_count_old: %d reuse_count: %d\n",reuse_count_old,reuse_count);
    tl_assert(!(reuse_count_old | reuse_count)); //Making sure that they are equal only during zeros
    return;
  }
  if(reuse_count_old > reuse_count){
    VG_(printf)("Reuse length mismatch. reuse_count_old: %d reuse_count: %d\n",reuse_count_old,reuse_count);
    tl_assert(reuse_count_old <= reuse_count);
  }

  //0. Calculate range with bin size. This can be changed in the .h file
  range_first_old = reuse_count_old/HISTOGRAM_REUSECOUNT_BIN_SIZE * HISTOGRAM_REUSECOUNT_BIN_SIZE;
  range_last_old = range_first_old + HISTOGRAM_REUSECOUNT_BIN_SIZE - 1;
  range_first = reuse_count/HISTOGRAM_REUSECOUNT_BIN_SIZE * HISTOGRAM_REUSECOUNT_BIN_SIZE;
  range_last = range_first + HISTOGRAM_REUSECOUNT_BIN_SIZE - 1;
  //1. First search the list to see if you find this range.
  temp_list = *histogram;
  while(temp_list){
    //    VG_(printf)("I am in hist list, hello\n");
      if(reuse_count_old){
	if((range_first_old == temp_list->rangefirst) && 
	   (range_last_old == temp_list->rangelast)){
	  //2. If found, decrement
	  temp_list->count -= datasize;
	  found_old_in_list_flag = 1;
	}
      }
      if((range_first == temp_list->rangefirst) && 
	 (range_last == temp_list->rangelast)){
	//If found increment
	temp_list->count += datasize;
	found_in_list_flag = 1;
      }
      if(!temp_list->count) {
      //if(0) {
	if (temp_list->prev)
	  temp_list->prev->next = temp_list->next;
	else
	  *histogram = temp_list->next;
	if (temp_list->next)
	  temp_list->next->prev = temp_list->prev;
	temp_list_next = temp_list->next;
	temp_list_prev = temp_list->prev;
	drw_free(temp_list, &CLG_(num_histlists));
	//Check if there are more chunks left. If there are, continue. Otherwise update temp_list and break here
	if(!temp_list_next){ 
	  temp_list = temp_list_prev; //Should be zero here.
	  if(!temp_list_prev)
	    tl_assert(*histogram == 0); //If list is empty that can also be handled correctly outside by setting temp_list = 0
	  break;
	}
	else if(temp_list_next->rangefirst > range_last){
	  if(temp_list_prev) //If previous exists we need to be one behind the place to insert
	    temp_list = temp_list_prev;
	  else
	    temp_list = temp_list_next; //If there is no previous then this is the first element, so we can be at the first element
	  break;
	}
	else{ //Finally, if there is a next element and its range is not lesser than our rangelast, go ahead and continue
	  temp_list = temp_list_next;
	  continue; 
	}
      }
      if(!temp_list->next) break;
      //To put things in ascending order, quit if the next one is higher. We will also only insert in ascending order
      if(temp_list->next->rangefirst > range_last)
	break;
      else temp_list = temp_list->next;
      if(found_old_in_list_flag && found_in_list_flag)
	break;
  }
  
  if(reuse_count_old){
    if(!found_old_in_list_flag){
      VG_(printf)("Couldn't find old range in list! reuse_count_old: %u, reuse_count: %u\n",reuse_count_old, reuse_count);
      tl_assert(0);
    }
  }
  //3. If not found, add
  if(!found_in_list_flag){
    temp_list_elemt = (hist_list_elemt*) CLG_MALLOC("cl.histlist.gc.1",sizeof(hist_list_elemt));
    CLG_(num_histlists)++;
    temp_list_elemt->rangefirst = range_first;
    temp_list_elemt->rangelast = range_last;
    temp_list_elemt->count = datasize;

    if(!temp_list){ //If no elements present in list
      temp_list_elemt->prev = temp_list_elemt->next = 0;
      *histogram = temp_list_elemt;      
    }
    else if(temp_list == *histogram && (temp_list->rangefirst > range_last)){ //Special case: If its the first element and it has a greater range, then we need to insert before, not after
      *histogram = temp_list_elemt;
      temp_list_elemt->next = temp_list;
      temp_list_elemt->prev = 0;
      temp_list->prev = temp_list_elemt;
    }
    else{ //Otherwise we will need to insert after temp_list
      if (temp_list->next)
	temp_list->next->prev = temp_list_elemt;
      temp_list_elemt->next = temp_list->next;
      temp_list_elemt->prev = temp_list;
      temp_list->next = temp_list_elemt;
    }
  }
}

/***
 * Given a histlist, this function will simply loop over and print all the contents
 * Returns void 
 */
static __inline__
void print_histlist(hist_list_elemt *histogram){
  hist_list_elemt *temp_list, *temp_list_next;
  char buf[8192];

  //1. First search the list to see if you find this range.
  temp_list = histogram;
  while(temp_list){
    VG_(sprintf)(buf, "%llu-%llu,%u, ", temp_list->rangefirst, temp_list->rangelast, temp_list->count);
    my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
    temp_list_next = temp_list->next;
    drw_free(temp_list, &CLG_(num_histlists));
    if(!temp_list_next) break;
    else temp_list = temp_list_next;
  }
}

/***
 * This function is given the producer and consumer function of an address/range of addresses.
 * It is also given the number of bytes which were read uniquely. 
 * Previous versions of the function used to determine the number of unique bytes read, with the help of a search.
 * Since we started using last reader mechanisms in the Shadow Memory, the search has been eliminated.
 * It then determines whether the consuming function is listed as a consumer in the producer's data structures and how many bytes were consumed.
 * Returns void
 */
static __inline__
void insert_to_consumedlist (funcinst *current_funcinst_ptr, funcinst *consumed_fn, drwevent *consumed_event, Addr ea, Int datasize, ULong count_unique, ULong count_unique_event){
  dependencelist_elemt *consumedfunc;

  consumedfunc = insert_to_dependencelist(current_funcinst_ptr, consumed_fn);
  //Increment count in consumedfunclist and funcinfo
  consumedfunc->count += datasize;
  consumedfunc->count_unique += count_unique;
  if(consumed_fn == current_funcinst_ptr)
    insert_to_drweventlist(0, 1, consumed_fn, 0, datasize, count_unique);
  //SELF means consumed_fn == funcinstpointer. NO_PROD or NO_CONS means that consumed_fn == 0
  if((consumed_fn != current_funcinst_ptr) && (consumed_fn != 0)){
    current_funcinst_ptr->ip_comm += datasize;
    current_funcinst_ptr->ip_comm_unique += count_unique;

    if(CLG_(clo).drw_events){
      //insert_to_drweventlist(1, 0, consumed_fn, current_funcinst_ptr, datasize, leftcount);
      insert_to_drweventlist(1, 0, consumed_fn, current_funcinst_ptr, datasize, count_unique);
      if(consumed_event)
	mark_event_shared(consumed_event, datasize, ea, ea + datasize - 1); //This should be changed to consumed_fn->tid, fn_number and funcinst_number so that we can also capture events for functions
      else{
	//LOG THAT WE HAVE MISSED A SHARED READ
	CLG_(shared_reads_missed_dump)++;
	//VG_(printf)("Shared reads missed due to dumping of events\n");
      }
    }
  }
}

//This function is a bad way of getting the histogram. There will be too many accesses to the histogram thing for each funcinst
/* This function does the same as the above but is used when data-reuse is activated.  
 * Currently, data-reuse and event collection cannot be turned on at the same time as that would be too memory intensive, 
 * so there is no event related logic in this function at all
*/
static __inline__
void insert_to_consumedlist_datareuse (funcinst *current_funcinst_ptr, funcinst *consumed_fn, Addr ea, Int datasize, ULong count_unique, ULong reuse_length, ULong reuse_length_old, UInt reuse_count, UInt reuse_count_old){
  dependencelist_elemt *consumedfunc;

  if((reuse_length_old | reuse_length) && count_unique){
    VG_(printf)("Both reuse length and count_unique are non-zero!\n");
    tl_assert(0);
  }

  //found! Search the current function's consumedlist for the name/number of the function, create if necessary and add the address to that list
  consumedfunc = insert_to_dependencelist(current_funcinst_ptr, consumed_fn);
  //Increment count in consumedfunclist and funcinfo
  consumedfunc->count += datasize;
  consumedfunc->count_unique += count_unique;
  if(consumed_fn == current_funcinst_ptr){
      insert_to_histlist(&current_funcinst_ptr->local_histogram, reuse_length, reuse_length_old, datasize);
      insert_to_reusecountlist(&current_funcinst_ptr->local_reuse_counts, reuse_count, reuse_count_old, datasize);

  }
  //SELF means consumed_fn == funcinstpointer. NO_PROD or NO_CONS means that consumed_fn == 0
  else if((consumed_fn != current_funcinst_ptr) && (consumed_fn != 0)){
    //Update the central funcinst also
    current_funcinst_ptr->ip_comm += datasize;
    current_funcinst_ptr->ip_comm_unique += count_unique;
	insert_to_histlist(&current_funcinst_ptr->input_histogram, reuse_length, reuse_length_old, datasize);
	insert_to_reusecountlist(&current_funcinst_ptr->input_reuse_counts, reuse_count, reuse_count_old, datasize);
  }
}

/* This function was originally called every time it is detected that control has moved to a new function. It will marshall
*  all the dependences seen in the function that just completed. If there are fancy jumps in between function calls,
*  it is still counted as a new call. The function that just completed (fnA) maintains a list of dependencies (functions it read from)
*  that it encountered when executing during that call. This is checked against the global history of functions to see
*  if it read from any functions that were called after some prior call to fnA. fnA's structure can be obtained from
*  thread_globvar->previous_funcinst as it just completed and we have entered some new function 
*  Currently most of the logic to trace dependencies through individual function calls has been deprecated and replaced
*  in favor of capturing events and deal with dependences between individual function calls in post-processing
*/
static __inline__
void handle_callreturn(funcinst *current_funcinst_ptr, funcinst *previous_funcinst_ptr, int call_return, int tid){ //call_return = 0 indicates a call, 1 indicates a return, 2 indicates a call and a return or a jump. Basically 2 is passed into this function when we don't know what could have happened exactly

  //1. Take fnA and compare global history with the dependency list of the call that just completed (of fnA)
  //If previous call to A exists in the array, and still points to the old call, then we can start comparing from 
  //there with all the elements of the dependency list for the call that just completed
  //If the call that just completed to the previous funcinst was the first, then just check the whole list
  //DELETED OLD CODE

  if(!call_return || call_return == 2){
    current_funcinst_ptr->current_call_instr_num = CLG_(total_instrs);
    current_funcinst_ptr->num_calls++;
  }
}

/* The next two helper functions create a 'fake' context and function node for the root of the 
*  flattened trees captured by Sigil. 
*  It is used for the new_mem_startup system call, which is the first function call in the client program.
*  It is also used when function communication is not activated. ( which is the default setting: thread-level communication monitoring ).
*/
static __inline__ 
fn_node* create_fake_fn_node(int tid)
{
  Char fnname[512];
  fn_node* fn = (fn_node*) CLG_MALLOC("cl.drw.nfnnd.1",
				      sizeof(fn_node));
  VG_(sprintf)(fnname, "Thread-%d", tid);
  fn->name = VG_(strdup)("cl.drw.nfnnd.2", fnname);
  
  fn->number   = 0;
  fn->last_cxt = 0;
  fn->pure_cxt = 0;
  fn->file     = 0;
  fn->next     = 0;
  
  fn->dump_before  = False;
  fn->dump_after   = False;
  fn->zero_before  = False;
  fn->toggle_collect = False;
  fn->skip         = False;
  fn->pop_on_jump  = CLG_(clo).pop_on_jump;
  fn->is_malloc    = False;
  fn->is_realloc   = False;
  fn->is_free      = False;
  
  fn->group        = 0;
  fn->separate_callers    = CLG_(clo).separate_callers;
  fn->separate_recursions = CLG_(clo).separate_recursions;
  
#if CLG_ENABLE_DEBUG
  fn->verbosity    = -1;
#endif
  
  return fn;
}

static Context* create_fake_cxt(fn_node** fn)
{
    Context* cxt;
    CLG_ASSERT(fn);

    cxt = (Context*) CLG_MALLOC("cl.drw.nc.1", sizeof(Context));

    cxt->fn[0] = *fn;
    cxt->size        = 1;
    cxt->base_number = CLG_(stat).context_counter;
    cxt->hash        = 0;
    cxt->next = 0;
    return cxt;
}

void CLG_(storeIDRWcontext) (InstrInfo* inode, int datasize, Addr ea, Bool WR, int opsflag) 
//opsflag to indicate what we are recording with this call. opsflag = 0 for instruction/nothing, 1 for mem read, 2 for mem write, and 3 for iop and 4 for flop, 5 for new_mem_startup
{
  int funcarrayindex = 0, wasreturn_flag = 0, threadarrayindex = 0;
  funcinst *current_funcinst_ptr = 0;
  funcinfo *current_funcinfo_ptr = 0;
  funcinst *temp_caller_ptr, *temp_caller_ptr_2;
  drwglobvars *thread_globvar;
  fn_node* function;
  Context* cxt;
  int temp_num_splits = 1, temp_splits_rem = 0, temp_split_datasize = datasize, i = 0, temp_ea = ea;

  //Debug. Check if its break
/*   if(CLG_(current_state).cxt->fn[0]->number == 5 && opsflag == 1){ */
/*     VG_(printf)("=  storeidrwcontext brk read: %x..%x, %d, function: %s\n", ea, ea+datasize,datasize, CLG_(current_state).cxt->fn[0]->name); */
/*   } */
/*   else if(CLG_(current_state).cxt->fn[0]->number == 5 && opsflag == 2){ */
/*     VG_(printf)("=  storeidrwcontext brk write: %x..%x, %d, function: %s\n", ea, ea+datasize,datasize, CLG_(current_state).cxt->fn[0]->name); */
/*   } */

  //0. Figure out which thread first and pull in its last state if necessary
  threadarrayindex = CLG_(current_tid); //When setup_bbcc calls switch_thread this variable will be set to the correct current tid
  thread_globvar = CLG_(thread_globvars)[threadarrayindex];
  //As setup_bbcc has already made sure that whatever is accessed above is valid for the current thread we go ahead here

//DEBUGTRACE prints out every thread, function and request type/size for every LD/ST. 
//Is used for internal debugging purposes
  if(CLG_(clo).drw_debugtrace){
/*     //First check if this is a new function for this thread */
/*     if(thread_globvar->previous_funcinst->fn_number != CLG_(current_state).cxt->fn[0]->number){ */
/*       //Check if it is a call. If so, print it out */
/*       if((thread_globvar->current_drwbbinfo.previous_bb->jmp[thread_globvar->current_drwbbinfo.previous_bb_jmpindex].jmpkind == jk_Call) && (thread_globvar->previous_funcinst->fn_number == CLG_(current_state).cxt->fn[1]->number)){ //Then check conditions */
/* 	VG_(printf)("Call occurred to: %s\n", CLG_(current_state).cxt->fn[0]->name); */
/*       } */
/*     } */
    if(opsflag == 5)
      return;
    else if(!opsflag){
      CLG_(total_instrs) += datasize;
    }
    else if(opsflag == 1){
      VG_(printf)("Thread %d Read: %x.. %x, %d func: %s\n", CLG_(current_tid), (UInt) ea, (UInt) (ea+datasize), datasize, CLG_(current_state).cxt->fn[0]->name);
    }
    else if(opsflag == 2){
      VG_(printf)("Thread %d Write: %x.. %x, %d func: %s\n", CLG_(current_tid), (UInt) ea, (UInt) (ea+datasize), datasize, CLG_(current_state).cxt->fn[0]->name);
    }
    return;
  }
//DONE WITH ULTRA HACK

  //-1. if opsflag is 5 then do special stuff.
  if(opsflag == 5){
    if(CLG_(clo).drw_debugtrace)
      return;
    //Create context and do things in storeIDRWcontext. This is for the new_mem_startup
    CLG_(syscall_globvars)->funcinfo_first->function = create_fake_fn_node(STARTUP_FUNC); //Store off the name indirectly
    cxt = create_fake_cxt(&function);
    thread_globvar = CLG_(syscall_globvars); //Hack to ensure variables that are useless in this use case still have legal values.
    //Treat as write
    //insert_to_drweventlist(0, 2, current_funcinst_ptr, 0, datasize, 0);
    check_align_and_put_writer(ea, datasize, CLG_(syscall_globvars)->funcinst_first, STARTUP_FUNC);
    CLG_(total_data_writes) += datasize; // 1;
    return;
  }

  if(!CLG_(clo).drw_thread_or_func){   //If we are only instrumenting threads (and not worried about functions), then do the needful
    funcarrayindex = 0; //For thread-level transactions only, keep a single dummy function whose function_number = 0
    if(thread_globvar->funcinst_first == 0){
      function = create_fake_fn_node(threadarrayindex);
      cxt = create_fake_cxt(&function);
      if(!insert_to_funcnodelist(&thread_globvar->funcinfo_first, funcarrayindex, &current_funcinfo_ptr, function, threadarrayindex)){
	insert_to_funcinstlist(&thread_globvar->previous_funcinst, cxt, 1, &current_funcinst_ptr, threadarrayindex, 0); //Then search for the current function, inserting if necessary
      }
    }
    else{
      current_funcinst_ptr = thread_globvar->previous_funcinst;
    }
  }
  else{ //If we do care about functions
    funcarrayindex = CLG_(current_state).cxt->fn[0]->number;
    function = CLG_(current_state).cxt->fn[0];
    //1. Figure out if a function structure has been allocated already. If not, allocate it
    //The real issue here is that this is not necessarily so as simply checking in storeIcontext and storeDRWcontext is not enough to ensure that a BB was not missed in between.
    //It is entirely possible that a return could trigger another return in the next immediate BB, which means neither storeIcontext and storeDRWcontext will be invoked. This
    //would further mean that the current funcinst is not necessarily the caller/callee of the current function. Also, it might be also good to check funcinst numbers. TODO: So there
    //should be a way of checking the calltree without resorting to checking with the fullcontext from the top. For now, we just check from the top.
    if(!insert_to_funcnodelist(&thread_globvar->funcinfo_first, funcarrayindex, &current_funcinfo_ptr, function, threadarrayindex)){
      
      if((thread_globvar->current_drwbbinfo.previous_bb == 0) || (thread_globvar->previous_funcinst == 0) || CLG_(current_state).cxt->size < 2){ //First check if pointers are valid
	insert_to_funcinstlist(&thread_globvar->funcinst_first, CLG_(current_state).cxt, CLG_(current_state).cxt->size, &current_funcinst_ptr, threadarrayindex, 1);
      }
      else if((thread_globvar->current_drwbbinfo.previous_bb->jmp[thread_globvar->current_drwbbinfo.previous_bb_jmpindex].jmpkind == jk_Call) && (thread_globvar->previous_funcinst->fn_number == CLG_(current_state).cxt->fn[1]->number)){ //Then check conditions
	insert_to_funcinstlist(&thread_globvar->previous_funcinst, CLG_(current_state).cxt, 2, &current_funcinst_ptr, threadarrayindex, 0); //Then search for the current function, inserting if necessary
      }
      else { //Fall through is default
	insert_to_funcinstlist(&thread_globvar->funcinst_first, CLG_(current_state).cxt, CLG_(current_state).cxt->size, &current_funcinst_ptr, threadarrayindex, 1);
      }
      handle_callreturn(current_funcinst_ptr, thread_globvar->previous_funcinst, 0, threadarrayindex);//For a call or the first time a function is seen!
    }
    else if(thread_globvar->previous_funcinst->fn_number != funcarrayindex){
      // The first step here is to figure out if the change in function was caused by a call or return. 
	  // If it is caused by return or call, then we can easily just add or remove the funcinst.
      // If it is not a call or return which caused this, then we need to trace the entire function context just like in the if statement.
      
      if((thread_globvar->current_drwbbinfo.previous_bb->jmp[thread_globvar->current_drwbbinfo.previous_bb_jmpindex].jmpkind == jk_Call) && (thread_globvar->previous_funcinst->fn_number == CLG_(current_state).cxt->fn[1]->number)){
	insert_to_funcinstlist(&thread_globvar->previous_funcinst, CLG_(current_state).cxt, 2, &current_funcinst_ptr, threadarrayindex, 0); //Then search for the current function, inserting if necessary
	handle_callreturn(current_funcinst_ptr, thread_globvar->previous_funcinst, 0, threadarrayindex);
      }
      else if(thread_globvar->current_drwbbinfo.previous_bb->jmp[thread_globvar->current_drwbbinfo.previous_bb_jmpindex].jmpkind == jk_Return){
	temp_caller_ptr = thread_globvar->previous_funcinst->caller; //be careful, is there a condition where caller may not have been defined? I don't think so because a return would not have been recorded without previous funcinst having a caller.
	while(temp_caller_ptr){ //To handle the case where there were successive returns, but neither storeDRW or storeI functions were invoked. I am hoping this will not happen on successive calls as that case is not handled.
	  if(temp_caller_ptr->fn_number == CLG_(current_state).cxt->fn[0]->number){
	    wasreturn_flag = 1;
	    break;
	  }
	  temp_caller_ptr = temp_caller_ptr->caller;
	}
	
	if(wasreturn_flag){
	  current_funcinst_ptr = temp_caller_ptr;
	  temp_caller_ptr_2 = thread_globvar->previous_funcinst->caller;
	  
	  while(temp_caller_ptr_2){ //To handle the case where there were successive returns, but neither storeDRW or storeI functions were invoked. I am hoping this will not happen on successive calls as that case is not handled.
	    if(temp_caller_ptr_2 == temp_caller_ptr){
	      break;
	    }
	    handle_callreturn(current_funcinst_ptr, temp_caller_ptr_2, 1, threadarrayindex);
	    temp_caller_ptr_2 = temp_caller_ptr_2->caller;
	  }
	}
	else{ //Technically this should not be entered as it implies a return to a function that was never called! This could relate to the issue when all called functions are not stored off in my structure!
	  VG_(printf)("Warning: Return to a function with no matching call! \n");
	  insert_to_funcinstlist(&thread_globvar->funcinst_first, CLG_(current_state).cxt, CLG_(current_state).cxt->size, &current_funcinst_ptr, threadarrayindex, 1);
	  handle_callreturn(current_funcinst_ptr, thread_globvar->previous_funcinst, 2, threadarrayindex);
	}
      }
      else{ //This should also include 90112, when function changes according to that code number, it means execution just "walked in" as opposed to explicitly calling
	insert_to_funcinstlist(&thread_globvar->funcinst_first, CLG_(current_state).cxt, CLG_(current_state).cxt->size, &current_funcinst_ptr, threadarrayindex, 1);
	handle_callreturn(current_funcinst_ptr, thread_globvar->previous_funcinst, 2, threadarrayindex);
      }
    }
    else{
      current_funcinst_ptr = thread_globvar->previous_funcinst;
      //Do Nothing else
    }
    //1done. current_funcinfo_ptr and current_funcinst_ptr now point to the current function's structures
  }
  
  //Instrumentation or not:
  if(!CLG_(clo).drw_noinstr && !CLG_(clo).drw_debugtrace){
    //2. If its just to count instructions or even nothing. Just need to change the code below to do nothing
    if(!opsflag){
      CLG_(total_instrs) += datasize;
      current_funcinst_ptr->instrs += datasize;
      if(!(CLG_(total_instrs)%1000))
	calculate_debuginfo();
      if(CLG_(clo).drw_debug || CLG_(clo).drw_calcmem)
	if(!(CLG_(total_instrs)%100000))
	  print_debuginfo();	
    }
    //3. If this is a memory read
    else if(opsflag == 1){
      //CLG_(read_last_write_cache)(datasize, ea, current_funcinfo_ptr, current_funcinst_ptr, threadarrayindex); //not the way I wanted to implement it, but no time. so HAX
      check_align_and_get_last_writer(ea, datasize, current_funcinfo_ptr, current_funcinst_ptr, threadarrayindex);
      CLG_(total_data_reads) += datasize; // 1;
    }
    //3done.
    //4. If this is a memory write
    else if(opsflag == 2){
      //Take care of the cases when a write is too big in size. In such a case, split the write into multiple small writes
      if(datasize > CLG_(L2_line_size)){
	temp_num_splits = datasize/CLG_(L2_line_size);
	temp_splits_rem = datasize%CLG_(L2_line_size);
	temp_split_datasize = CLG_(L2_line_size);
      }
      for(i = 0; i < temp_num_splits; i++){
	insert_to_drweventlist(0, 2, current_funcinst_ptr, 0, temp_split_datasize, 0);
	temp_ea += temp_split_datasize;
      }
      if(temp_splits_rem)
	insert_to_drweventlist(0, 2, current_funcinst_ptr, 0, temp_splits_rem, 0);
      
      check_align_and_put_writer(ea, datasize, current_funcinst_ptr, threadarrayindex);
      //CLG_(put_in_last_write_cache)(datasize, ea, current_funcinst_ptr, threadarrayindex);
      CLG_(total_data_writes) += datasize; // 1;
    }
    //4done.
    //5. If its a integer operation
    else if (opsflag == 3){
      CLG_(total_iops)++;
      current_funcinst_ptr->iops++;
      insert_to_drweventlist(0, 3, current_funcinst_ptr, 0, 1, 0);
    }
    //6. If its a floating point operation
    else if (opsflag == 4){
      CLG_(total_flops)++;
      current_funcinst_ptr->flops++;
      insert_to_drweventlist(0, 4, current_funcinst_ptr, 0, 1, 0);
    }
  }
  thread_globvar->previous_funcinst = current_funcinst_ptr; //When storeDRWcontext is invoked again, this variable will contain the number of the function in which we were previously residing.
  return;

}

/* Done with FUNCTIONS - inserted by Sid */

/* FUNCTION CALLS ADDED TO PRINT ALL DATA ACCESSES FOR EVERY ADDRESS in LINKED LIST - Sid */

/*The following are helper functions to buffer data to be written to file. 
This will reduce the number of small writes to file invoked by VG_(write)
This functionality somewhat replicates libc's logic, since Valgrind bypasses most of libc*/
#define FWRITE_BUFSIZE 32000
#define FWRITE_THROUGH 10000
static Char fwrite_buf[FWRITE_BUFSIZE];
static Int fwrite_pos;
static Int fwrite_fd = -1;

static __inline__
void fwrite_flush(void)
{
    if ((fwrite_fd>=0) && (fwrite_pos>0))
	VG_(write)(fwrite_fd, (void*)fwrite_buf, fwrite_pos);
    fwrite_pos = 0;
}

static void my_fwrite(Int fd, Char* buf, Int len)
{
    if (fwrite_fd != fd) {
	fwrite_flush();
	fwrite_fd = fd;
    }
    if (len > FWRITE_THROUGH) {
	fwrite_flush();
	VG_(write)(fd, (void*)buf, len);
	return;
    }
    if (FWRITE_BUFSIZE - fwrite_pos <= len) fwrite_flush();
    VG_(strncpy)(fwrite_buf + fwrite_pos, buf, len);
    fwrite_pos += len;
}

/* Helper functions to print the output data*/
void dump_eventlist_to_file_serialfunc()
{
  char buf[4096];
  drwevent* event_list_temp;

  VG_(printf)("Printing events now\n");
  event_list_temp = CLG_(drw_eventlist_start);

  do{
    if(event_list_temp->type)
      VG_(sprintf)(buf, "c %llu,%d,%d,%d,%d,%d,%d,%llu,%llu\n", event_list_temp->event_num, event_list_temp->consumer->fn_number, event_list_temp->consumer->funcinst_number, event_list_temp->consumer_call_num, event_list_temp->producer->fn_number, event_list_temp->producer->funcinst_number, event_list_temp->producer_call_num, event_list_temp->bytes, event_list_temp->bytes_unique);
    else
      VG_(sprintf)(buf, "o %llu,%d,%d,%d,%llu,%llu,%llu,%llu\n", event_list_temp->event_num, event_list_temp->producer->fn_number, event_list_temp->producer->funcinst_number, event_list_temp->producer_call_num, event_list_temp->iops, event_list_temp->flops, event_list_temp->non_unique_local, event_list_temp->unique_local);

    my_fwrite(CLG_(drw_funcserial_fd), (void*)buf, VG_(strlen)(buf));
    event_list_temp++;
  } while(event_list_temp != CLG_(drw_eventlist_end)); //Since drw_eventlist_end points to the next entry which has not yet been written, we must process all entries upto but not including drw_eventlist_end. The moment event_list_temp gets incremented and points to 'end', the loop will terminate as expected.
}

/* This function prints events data for the case when function-level profiling is not enabled. 
*  Technically, this feature is not yet ready for prime-time, and should not really be used.
*/
static __inline__
void dump_eventlist_to_file()
{
  char buf[4096];
  drwevent* event_list_temp;
  evt_addrchunknode *addrchunk_temp, *addrchunk_next;
  drwglobvars *thread_globvar;

  VG_(printf)("Printing events now\n");
  event_list_temp = CLG_(drw_eventlist_start);

  do{
    //1. Print out the contents of the event
    if(!event_list_temp->type){
      VG_(sprintf)(buf, "%llu,%d,%llu,%llu,%llu,%llu,%llu,%llu", event_list_temp->event_num, event_list_temp->producer->tid, event_list_temp->iops, event_list_temp->flops, event_list_temp->non_unique_local, event_list_temp->unique_local, event_list_temp->total_mem_reads, event_list_temp->total_mem_writes);
      thread_globvar = CLG_(thread_globvars)[event_list_temp->producer->tid];
    }
    else{
      VG_(sprintf)(buf, "%llu,%d,%d,%llu,%llu", event_list_temp->event_num, event_list_temp->producer->tid, event_list_temp->consumer->tid, event_list_temp->bytes, event_list_temp->bytes_unique);
      thread_globvar = CLG_(thread_globvars)[event_list_temp->consumer->tid];
    }

    my_fwrite(thread_globvar->previous_funcinst->fd, (void*)buf, VG_(strlen)(buf));

    if(CLG_(clo).drw_debug){
      VG_(sprintf)(buf, " in %s and %s. ", event_list_temp->debug_node1->name, event_list_temp->debug_node2->name);
      if(!event_list_temp->type)
	thread_globvar = CLG_(thread_globvars)[event_list_temp->producer->tid];
      else
	thread_globvar = CLG_(thread_globvars)[event_list_temp->consumer->tid];
      my_fwrite(thread_globvar->previous_funcinst->fd, (void*)buf, VG_(strlen)(buf));
    }
    
    //2. Print out the contents of the list of computation and communication events
    addrchunk_temp = event_list_temp->list;
    while(addrchunk_temp){
      //Print out the dependences for this event in the same line
      if(event_list_temp->type)
	VG_(sprintf)(buf, " # %d %llu %lu %lu", addrchunk_temp->shared_read_tid, addrchunk_temp->shared_read_event_num, addrchunk_temp->rangefirst, addrchunk_temp->rangelast );
      else
	VG_(sprintf)(buf, " $ %d %llu %lu %lu", addrchunk_temp->shared_read_tid, addrchunk_temp->shared_read_event_num, addrchunk_temp->rangefirst, addrchunk_temp->rangelast );
      my_fwrite(thread_globvar->previous_funcinst->fd, (void*)buf, VG_(strlen)(buf));
      addrchunk_next = addrchunk_temp->next;
      drw_free(addrchunk_temp, &CLG_(num_eventaddrchunk_nodes));
      addrchunk_temp = addrchunk_next;
    }
    event_list_temp->list = 0;
    //Print out the dependences for this event in the same line
    VG_(sprintf)(buf, "\n" );
    my_fwrite(thread_globvar->previous_funcinst->fd, (void*)buf, VG_(strlen)(buf));
    event_list_temp++;
  } while(event_list_temp != CLG_(drw_eventlist_end)); //Since drw_eventlist_end points to the next entry which has not yet been written, we must process all entries upto but not including drw_eventlist_end. The moment event_list_temp gets incremented and points to 'end', the loop will terminate as expected.
  if(CLG_(num_eventaddrchunk_nodes))
    VG_(printf)("Encountered %llu un-freed nodes\n", CLG_(num_eventaddrchunk_nodes));
}

static __inline__
void insert_to_drweventlist( int type, // type 0-operation 1-communication
			     int optype , // optype - 0-nothing (used for communication events) 1-memread 2-memwrite 3-iops 4-flops
			     funcinst *producer, funcinst *consumer,
			     ULong count, ULong count_unique ){ // count - represents bytes for type 1 and numops for type 0
  int same_as_last_event_flag = 0;
  if(!CLG_(clo).drw_events){
    return;
  }
  //For function-level events do not collect computation events - for now. We should make this a command line option soon
  //if(CLG_(clo).drw_thread_or_func)
  //if(!type) return;
  //1. What kind of event? Operation or communication?
  //a. Is the kind of event same as the current event's kind?
  if(CLG_(drwevent_latest_event)){
    if(CLG_(drwevent_latest_event)->type == type){
      //aa. If yes, are the participants the same?
      if(CLG_(drwevent_latest_event)->producer == producer && CLG_(drwevent_latest_event)->consumer == consumer){
	//Adding a clause to check if we have exceeded 500 memory writes. If so, please create a new event
	if(CLG_(drwevent_latest_event)->total_mem_writes < CLG_(clo).drw_splitcomp)
	  //Set a flag to indicate that the existing latest event can be used
	  same_as_last_event_flag = 1;
      }
    }
  }
  //2.
  //1.b.,bb.,bbb. Create new event while checking if we have reached the event memory limits.
  if(!same_as_last_event_flag){
    if((CLG_(drw_eventlist_end) == &CLG_(drw_eventlist_start)[(CLG_(event_info_size) * 1024) - 1] + 1) ||
       ((CLG_(num_eventaddrchunk_nodes) * (sizeof(evt_addrchunknode)/1024) >= MAX_EVENTADDRCHUNK_SIZE * 1024 * 1024))){
      //Handle the dumping and re-use of files
      if(!CLG_(clo).drw_thread_or_func)
	dump_eventlist_to_file();
      else dump_eventlist_to_file_serialfunc();
      CLG_(drw_eventlist_end) = CLG_(drw_eventlist_start);
      CLG_(num_eventdumps)++;
      VG_(printf)("Printed to output. Dump no. %d\n", CLG_(num_eventdumps));
    }
    //b. Create a new event, initializing type etc. and counts
    //DRWDEBUG
    if(CLG_(drwevent_latest_event) && CLG_(clo).drw_debug)
      CLG_(drwevent_latest_event)->debug_node2 = CLG_(current_state).cxt->fn[0];
    CLG_(drwevent_latest_event) = CLG_(drw_eventlist_end); //Take one from the end and increment the end
    CLG_(drw_eventlist_end)++;
    //Initialize variables in the latest event
    CLG_(drwevent_latest_event)->type = type;
    CLG_(drwevent_latest_event)->producer = producer;
    CLG_(drwevent_latest_event)->producer_call_num = producer->num_calls;
    if(type){
      CLG_(drwevent_latest_event)->consumer = consumer;
      CLG_(drwevent_latest_event)->consumer_call_num = consumer->num_calls;
      //CLG_(drwevent_latest_event)->event_num = CLG_(thread_globvars)[consumer->tid]->num_events;
      //CLG_(thread_globvars)[consumer->tid]->num_events++;
      CLG_(drwevent_latest_event)->event_num = consumer->num_events;
      consumer->num_events++;
    }
    else{
      CLG_(drwevent_latest_event)->consumer = 0;
      CLG_(drwevent_latest_event)->consumer_call_num = 0;
      //CLG_(drwevent_latest_event)->event_num = CLG_(thread_globvars)[producer->tid]->num_events;
      //CLG_(thread_globvars)[producer->tid]->num_events++;
      CLG_(drwevent_latest_event)->event_num = producer->num_events;
      producer->num_events++;
    }
    CLG_(drwevent_latest_event)->bytes = CLG_(drwevent_latest_event)->bytes_unique = CLG_(drwevent_latest_event)->iops = CLG_(drwevent_latest_event)->flops = CLG_(drwevent_latest_event)->shared_bytes_written = 0;
    CLG_(drwevent_latest_event)->producer_call_num = CLG_(drwevent_latest_event)->consumer_call_num = 0;
    CLG_(drwevent_latest_event)->unique_local = CLG_(drwevent_latest_event)->non_unique_local = CLG_(drwevent_latest_event)->total_mem_reads = CLG_(drwevent_latest_event)->total_mem_writes = 0; //Unique will give the number of cold misses and non-unique can be used to calculate hits
    CLG_(drwevent_latest_event)->list = 0;
    if(CLG_(clo).drw_debug){
      CLG_(drwevent_latest_event)->debug_node1 = CLG_(current_state).cxt->fn[0]; CLG_(drwevent_latest_event)->debug_node2 = CLG_(current_state).cxt->fn[0];
    }
  }

  //3. Update the counts.
  //1.aaa. If yes, then increment counts for the current event, if necessary add addresses etc and return
  if(type && !optype){
    CLG_(drwevent_latest_event)->bytes += count;
    CLG_(drwevent_latest_event)->bytes_unique += count_unique;
  }
  else{
    switch(optype){
    case 1:
      CLG_(drwevent_latest_event)->non_unique_local += count;
      CLG_(drwevent_latest_event)->unique_local += count_unique;
      CLG_(drwevent_latest_event)->total_mem_reads++;
      break;
    case 2:
      CLG_(drwevent_latest_event)->total_mem_writes++;
      break;
    case 3:
      CLG_(drwevent_latest_event)->iops += count;
      break;
    case 4:
      CLG_(drwevent_latest_event)->flops += count;
      break;
    default:
      VG_(printf)("Incorrect optype specified when inserting to drweventlist");
      tl_assert(0);
    }
  }
}

static ULong aggregate_addrchunk_counts(addrchunk** chunkoriginal, ULong *refarg) {
  addrchunk *chunk = *chunkoriginal;
  ULong count=0, count_unique=0;
  addrchunknode  *chunknodecurr;
  int i;

  if(chunk == 0) return 0;
  for (i=0 ; i<ADDRCHUNK_ARRAY_SIZE; i++) {
    chunknodecurr = (chunk+i)->original;
    while (chunknodecurr !=0) {
      count += chunknodecurr->count;
      count_unique += chunknodecurr->count_unique;
      chunknodecurr = chunknodecurr->next;
    }
  }

  *refarg=count;
  return count_unique;
}

/* Helper functions for printing data */
static void print_for_funcinst (funcinst *funcinstpointer, int fd, char* caller, drwglobvars *thread_globvar){
  char drw_funcname[4096], buf[8192];
  int drw_funcnum, drw_funcinstnum, drw_tid;
  ULong drw_funcinstr;
  dependencelist_elemt *consumerfuncinfopointer;
  dependencelist *dependencelistpointer;
  ULong zero = 0; int i;

  /****3. ITERATE OVER PRODUCER LIST OF ADDRESS CHUNKS****/
  /****3. DONE ITERATION OVER PRODUCER LIST OF ADDRESS CHUNKS****/

  //Added because when we run with --toggle-collect=main, some funcinst instances have their "function_info" variables as empty (0x0), because I suppose instrumentation does not happen for them, but funcinsts get created for them as their children may get created. We should actually be allowed to skip this and return without printing anything
  if(funcinstpointer->function_info == 0)
    VG_(sprintf)(drw_funcname, "%s", "NOT COLLECTED");
  else
    VG_(sprintf)(drw_funcname, "%s", funcinstpointer->function_info->function->name);

  VG_(sprintf)(buf, "%-20d %-20d %-20d %50s %20s %-20llu %-20llu %-20llu %-20llu %-20llu %-20llu %-20llu\n", funcinstpointer->tid, funcinstpointer->fn_number, funcinstpointer->funcinst_number, /*funcinstpointer->function_info->function->name*/drw_funcname, caller, funcinstpointer->instrs, funcinstpointer->flops, funcinstpointer->iops, funcinstpointer->ip_comm_unique, /*funcinstpointer->op_comm_unique*/zero, funcinstpointer->ip_comm, /*funcinstpointer->op_comm*/zero);
  //VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
  my_fwrite(fd, (void*)buf, VG_(strlen)(buf));

  /****4. ITERATE OVER CONSUMED LIST****/
  dependencelistpointer = funcinstpointer->consumedlist;
  while(1){
    for(i = 0; i < dependencelistpointer->size; i++){
      consumerfuncinfopointer = &dependencelistpointer->list_chunk[i];
      
      if(consumerfuncinfopointer->vert_parent_fn == consumerfuncinfopointer->consumed_fn){
	VG_(sprintf)(drw_funcname, "%s", "SELF");
	drw_funcnum = SELF;
	drw_funcinstnum = 0;
	drw_funcinstr = 0;
	drw_tid = 0;
      }
      else if(consumerfuncinfopointer->consumed_fn == 0){
	VG_(sprintf)(drw_funcname, "%s", "NO PRODUCER");
	drw_funcnum = NO_PROD;
	drw_funcinstnum = 0;
	drw_funcinstr = 0;
	drw_tid = 0;
      }
      else{
	VG_(sprintf)(drw_funcname, "%s", consumerfuncinfopointer->consumed_fn->function_info->function->name);
	drw_funcnum = consumerfuncinfopointer->consumed_fn->fn_number;
	drw_funcinstnum = consumerfuncinfopointer->consumed_fn->funcinst_number;
	drw_funcinstr = consumerfuncinfopointer->consumed_fn->instrs;
	drw_tid = consumerfuncinfopointer->consumed_fn->tid;
      }
      
      VG_(sprintf)(buf, "%-20d %-20d %-20d %50s %20s %-20llu %20s %20s %-20llu %-20d %-20llu %-20d\n", drw_tid, drw_funcnum, drw_funcinstnum, drw_funcname, " ", drw_funcinstr, " ", " ", consumerfuncinfopointer->count_unique, 0, consumerfuncinfopointer->count, 0);
      //VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
      my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
    }
    if(!dependencelistpointer->next) break;
    else dependencelistpointer = dependencelistpointer->next;  
  }
  
  /****4. DONE ITERATION OVER CONSUMED LIST****/
  VG_(sprintf)(buf, "\n");
  //VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
  my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
  /****6. ITERATE OVER CONSUMER LIST****/
  consumerfuncinfopointer = funcinstpointer->consumerlist;
  i = 0;
  while(consumerfuncinfopointer){
    i++;
    if(consumerfuncinfopointer->vert_parent_fn == consumerfuncinfopointer->consumed_fn){
      consumerfuncinfopointer = consumerfuncinfopointer->next_horiz;
      continue;
    }
    else if(consumerfuncinfopointer->consumed_fn == 0){
      VG_(sprintf)(drw_funcname, "%s", "NO PRODUCER");
      drw_funcnum = NO_PROD;
      drw_funcinstnum = 0;
      drw_funcinstr = 0;
      drw_tid = 0;
    }
    else{
      VG_(sprintf)(drw_funcname, "%s", consumerfuncinfopointer->vert_parent_fn->function_info->function->name);
      drw_funcnum = consumerfuncinfopointer->vert_parent_fn->fn_number;
      drw_funcinstnum = consumerfuncinfopointer->vert_parent_fn->funcinst_number;
      drw_funcinstr = consumerfuncinfopointer->vert_parent_fn->instrs;
      drw_tid = consumerfuncinfopointer->vert_parent_fn->tid;
    }
    VG_(sprintf)(buf, "%-20d %-20d %-20d %50s %20s %-20llu %20s %20s %-20d %-20llu %-20d %-20llu\n", drw_tid, drw_funcnum, drw_funcinstnum, drw_funcname, " ", drw_funcinstr, " ", " ", 0, consumerfuncinfopointer->count_unique, 0, consumerfuncinfopointer->count);
    //VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
    consumerfuncinfopointer = consumerfuncinfopointer->next_horiz;
  }
  /****6. DONE ITERATION OVER CONSUMER LIST****/
  VG_(sprintf)(buf, "\n\n");
  //VG_(write)(fd, (void*)buf, VG_(strlen)(buf));
  my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
}

/*OLD way of printing the data through actual recursion*/
/* static void print_recurse_data (funcinst *funcinstpointer, int fd, int depth, drwglobvars *thread_globvar){ //Depth indicates how many recursions have been done.  */
/*   int j; */
/*   char caller[3]; */
/*   VG_(sprintf)(caller, "*"); */
/*   print_for_funcinst(funcinstpointer, fd, caller, thread_globvar); //Print the current function and the functions it has consumed from. ** denotes that all the *s that follow will be the immediate callees of this function. */
/*   if(depth == MAX_RECURSE_DEPTH) */
/*     return; */

/*   for(j = 0; j < funcinstpointer->num_callees; j++) //Traverse all the callees of the currentfuncinst */
/*     print_recurse_data(funcinstpointer->callees[j], fd, depth + 1, thread_globvar); */
/* } */

/*print data through iterative tree traversal*/
static void print_recurse_data(funcinst *p, int fd, int depth, drwglobvars *thread_globvar){
  char caller[3];
  //Special case, when p is the first and only node in the tree
  if (!p->caller && !p->num_callees){
    //Print some information for the node before leaving
    VG_(sprintf)(caller, "*");
    print_for_funcinst(p, fd, caller, thread_globvar); //Print the current function and the functions it has consumed from. ** denotes that all the *s that follow will be the immediate callees of this function.
    return;
  }
  do
  {
	while(p != 0)
	{
		//Print some information for the node before leaving
		VG_(sprintf)(caller, "*");
		print_for_funcinst(p, fd, caller, thread_globvar); //Print the current function and the functions it has consumed from. ** denotes that all the *s that follow will be the immediate callees of this function.

		//Go to the left-most child node. This means that the index has to be zero first
		if(!p->num_callees || p->callee_prnt_idx >= p->num_callees)
			break;
		else{
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
		}
	}
	do{ //Until a caller is valid, keep doing this. This loop goes back up the callstack (pops the callstack) until it encounters a node that has children as of yet untraversed.
		p = p->caller;
		if (p->callee_prnt_idx < p->num_callees){
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
			break;
		}
		p->callee_prnt_idx = 0; //Reset index for any re-traversals of tree
	} while(p->caller);
  } while(p->caller && p != 0); //Repeat while node is not null and stack is not empty
}

/* static void print_recurse_tree (funcinst *funcinstpointer, int fd, int depth, drwglobvars *thread_globvar){ //Prints by serializing the tree based on DWARF's debugging standard */
/*   int j; char buf[1024]; */
/*   VG_(sprintf)(buf, "%d, %d, %s, %d\n", funcinstpointer->fn_number, funcinstpointer->funcinst_number, ((funcinstpointer->num_callees) ? "True":"False"), funcinstpointer->num_calls); */
/*   //VG_(write)(fd, (void*)buf, VG_(strlen)(buf)); */
/*   my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); */
/*   if(depth == MAX_RECURSE_DEPTH) */
/*     return; */
/*   for(j = 0; j < funcinstpointer->num_callees; j++) //Traverse all the callees of the currentfuncinst */
/*     print_recurse_tree(funcinstpointer->callees[j], fd, depth + 1, thread_globvar); */
/*   //If callees were present, the algorithm dictates that a "none" must be printed at the end */
/*   if(funcinstpointer->num_callees){ */
/*     VG_(sprintf)(buf, "None\n"); */
/*     //VG_(write)(fd, (void*)buf, VG_(strlen)(buf)); */
/*     my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); */
/*   } */
/* } */

/*print tree through iterative tree traversal*/
static void print_recurse_tree(funcinst *p, int fd, int depth, drwglobvars *thread_globvar){
  char buf[2048];
  //Special case, when p is the first and only node in the tree
  if (!p->caller && !p->num_callees){
    //Print some information for the node before leaving
    VG_(sprintf)(buf, "%d, %d, %s, %d\n", p->fn_number, p->funcinst_number, ((p->num_callees) ? "True":"False"), p->num_calls);
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
    return;
  }
  do
  {
	while(p != 0)
	{
		//Print some information for the node before leaving
		VG_(sprintf)(buf, "%d, %d, %s, %d\n", p->fn_number, p->funcinst_number, ((p->num_callees) ? "True":"False"), p->num_calls);
		my_fwrite(fd, (void*)buf, VG_(strlen)(buf));

		//Go to the left-most child node. This means that the index has to be zero first
		if(!p->num_callees || p->callee_prnt_idx >= p->num_callees)
			break;
		else{
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
		}
	}
	do{ //Until a caller is valid, keep doing this. This loop goes back up the callstack (pops the callstack) until it encounters a node that has children as of yet untraversed.
		p = p->caller;
		if (p->callee_prnt_idx < p->num_callees){
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
			break;
		}
		p->callee_prnt_idx = 0; //Reset index for any re-traversals of tree
		VG_(sprintf)(buf, "None\n");
		my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
	} while(p->caller);
  } while(p->caller && p != 0); //Repeat while node is not null and stack is not empty
}

static void print_hist_data (funcinst *funcinstpointer, int fd){
  char buf[2048];
  //Print the local histogram
  VG_(sprintf)(buf, "%d, %d, $ ",funcinstpointer->fn_number, funcinstpointer->funcinst_number);
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  print_histlist(funcinstpointer->local_histogram);
  VG_(sprintf)(buf, "\n");
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  
  VG_(sprintf)(buf, "%d, %d, @ ",funcinstpointer->fn_number, funcinstpointer->funcinst_number);
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  print_histlist(funcinstpointer->local_reuse_counts);
  /*   for(j = 0; j < HISTOGRAM_NUM_BINS; j++){ */
  /*     VG_(sprintf)(buf, "%d,%d, ",j,funcinstpointer->local_histogram[j]); */
  /*     my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf)); */
  /*   } */
  VG_(sprintf)(buf, "\n");
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  
  //Print the input histogram
  VG_(sprintf)(buf, "%d, %d, # ",funcinstpointer->fn_number, funcinstpointer->funcinst_number);
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  print_histlist(funcinstpointer->input_histogram);
  VG_(sprintf)(buf, "\n");
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  VG_(sprintf)(buf, "%d, %d, ! ",funcinstpointer->fn_number, funcinstpointer->funcinst_number);
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
  print_histlist(funcinstpointer->input_reuse_counts);
  /*   for(j = 0; j < HISTOGRAM_NUM_BINS; j++){ */
  /*     VG_(sprintf)(buf, "%d,%d, ",j,funcinstpointer->input_histogram[j]); */
  /*     my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf)); */
  /*   } */
  VG_(sprintf)(buf, "\n");
  my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
}

/* static void print_recurse_reusedata (funcinst *funcinstpointer, int fd, int depth, drwglobvars *thread_globvar){ //Depth indicates how many recursions have been done.  */
/*   int j; */
/*   char buf[1024]; */
/*   print_hist_data(funcinstpointer, fd); */
/*   if(depth == MAX_RECURSE_DEPTH) */
/*     return; */

/*   for(j = 0; j < funcinstpointer->num_callees; j++) //Traverse all the callees of the currentfuncinst */
/*     print_recurse_reusedata(funcinstpointer->callees[j], fd, depth + 1, thread_globvar); */
/* } */

static void print_recurse_reusedata (funcinst *p, int fd, int depth, drwglobvars *thread_globvar){
  char buf[2048];
  //Special case, when p is the first and only node in the tree
  if (!p->caller && !p->num_callees){
    //Print some information for the node before leaving
    print_hist_data(p, fd);
    return;
  }
  do
  {
	while(p != 0)
	{
		//Print some information for the node before leaving
		print_hist_data(p, fd);

		//Go to the left-most child node. This means that the index has to be zero first
		if(!p->num_callees || p->callee_prnt_idx >= p->num_callees)
			break;
		else{
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
		}
	}
	do{ //Until a caller is valid, keep doing this. This loop goes back up the callstack (pops the callstack) until it encounters a node that has children as of yet untraversed.
		p = p->caller;
		if (p->callee_prnt_idx < p->num_callees){
			p = p->callees[p->callee_prnt_idx];
			p->caller->callee_prnt_idx++;
			break;
		}
		p->callee_prnt_idx = 0; //Reset index for any re-traversals of tree
		VG_(sprintf)(buf, "None\n");
		my_fwrite(fd, (void*)buf, VG_(strlen)(buf));
	} while(p->caller);
  } while(p->caller && p != 0); //Repeat while node is not null and stack is not empty
}

void CLG_(print_to_file) ()
{
  char drw_filename[50], buf[4096];
  SysRes res;
  int i, fd, num_valid_threads = 0;
  drwglobvars* thread_globvar = 0;

  VG_(printf)("PRINTING Sigil's output now\n");
  
  if(CLG_(clo).drw_events){
    VG_(printf)("Total Number of Shared Reads MISSED due to dumping of events: %llu and due to lost pointers: %llu\n", CLG_(shared_reads_missed_dump), CLG_(shared_reads_missed_pointer));
    //Close out any remaining events and close the file in the following loop
    if(!CLG_(clo).drw_thread_or_func)
      dump_eventlist_to_file();
    else dump_eventlist_to_file_serialfunc();
  }

  if(CLG_(clo).drw_calcmem){
    VG_(printf)("SUMMARY: \n\n");
    VG_(printf)("Total Memory Reads(bytes): %-20llu Total Memory Writes(bytes): %-20llu Total Instrs: %-20llu Total Flops: %-20llu Iops: %-20llu\n\n",CLG_(total_data_reads), CLG_(total_data_writes), CLG_(total_instrs), CLG_(total_flops), CLG_(total_iops));
    print_debuginfo();
    return;
  }

  //Perform printing for each thread separately
  for(i = 0; i < MAX_THREADS; i++){
    thread_globvar = CLG_(thread_globvars)[i];
    if(thread_globvar) num_valid_threads++;
    if (!thread_globvar) continue;

    //Close the event files for each thread
    fwrite_flush();
    if(!CLG_(clo).drw_thread_or_func)
      VG_(close)( (Int)sr_Res(thread_globvar->previous_funcinst->res) );
    else
      VG_(close)( (Int)sr_Res(CLG_(drw_funcserial_res)) );

  /***1. CREATE THE NECESSARY FILES***/
    if(CLG_(clo).drw_datareuse)
    {
      VG_(sprintf)(drw_filename, "sigil.datareuse.out-%d",i);
      res = VG_(open)(drw_filename, VKI_O_WRONLY|VKI_O_TRUNC, 0);
      
      if (sr_isError(res)) {
	res = VG_(open)(drw_filename, VKI_O_CREAT|VKI_O_WRONLY,
			VKI_S_IRUSR|VKI_S_IWUSR);
	if (sr_isError(res)) {
	  file_err(); // If can't open file, then create one and then open. If still erroring, Valgrind will die using this call - Sid
	}
      }
      CLG_(drw_datareuse_res) = res;
      CLG_(drw_datareuse_fd) = (Int) sr_Res(res);
      VG_(sprintf)(buf, "FUNCTION NUMBER, FUNCINST NUMBER, $ LOCAL HISTOGRAM(BINS 0 - 1000)\n");
      my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
      VG_(sprintf)(buf, "FUNCTION NUMBER, FUNCINST NUMBER, # INPUT HISTOGRAM(BINS 0 - 1000)\n");
      my_fwrite(CLG_(drw_datareuse_fd), (void*)buf, VG_(strlen)(buf));
      
      //Print reuse data and then close the file
      print_recurse_reusedata(thread_globvar->funcinst_first, CLG_(drw_datareuse_fd), 0, thread_globvar);
      fwrite_flush();
      VG_(close)( (Int)sr_Res(CLG_(drw_datareuse_res)) );
    }
    
    //Similarly process the totals.out file
    VG_(sprintf)(drw_filename, "sigil.totals.out-%d",i);
    res = VG_(open)(drw_filename, VKI_O_WRONLY|VKI_O_TRUNC, 0);
    
    if (sr_isError(res)) {
      res = VG_(open)(drw_filename, VKI_O_CREAT|VKI_O_WRONLY,
		      VKI_S_IRUSR|VKI_S_IWUSR);
      if (sr_isError(res)) {
	file_err(); // If can't open file, then create one and then open. If still erroring, Valgrind will die using this call - Sid
      }
    }
    
    fd = (Int) sr_Res(res);
    /***1. DONE CREATION***/
    
    //PRINT METADATA
    VG_(sprintf)(buf, "Tool: Sigil \nVersion: 1.0\n\n");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the original callgrind.out file
    
    VG_(sprintf)(buf, "SUMMARY: \n\n");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the original callgrind.out file

    VG_(sprintf)(buf, "Total Memory Reads(bytes): %-20llu Total Memory Writes(bytes): %-20llu Total Instrs: %-20llu Total Flops: %-20llu Iops: %-20llu\n\n",CLG_(total_data_reads), CLG_(total_data_writes), CLG_(total_instrs), CLG_(total_flops), CLG_(total_iops));
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file
    
    if(CLG_(clo).drw_events)
      CLG_(tot_memory_usage) = CLG_(num_sms) * sizeof(SM_event) + CLG_(num_funcinsts) * sizeof(funcinst) + sizeof(PM);
    else
      CLG_(tot_memory_usage) = CLG_(num_sms) * sizeof(SM) + CLG_(num_funcinsts) * sizeof(funcinst) + sizeof(PM);
    VG_(sprintf)(buf, "Num SMs: %-20llu Num funcinsts: %-20llu Memory for SM(bytes): %-20llu Memory for funcinsts(bytes): %-20llu Memory for PM(bytes): %-20d Total Memory Usage: %-20llu\n\n",CLG_(num_sms), CLG_(num_funcinsts), CLG_(num_sms) * sizeof(SM), CLG_(num_funcinsts) * sizeof(funcinst), (UInt) sizeof(PM), CLG_(tot_memory_usage));
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file

    VG_(sprintf)(buf, "\n\nTREE DUMP\n\n");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file
    
    VG_(sprintf)(buf, "%s, %s, %s, %s\n", "FUNCTION NUMBER", "FUNC_INST NUM", "Children?", "Number of calls");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file

	/* Print the tree first. Post-processing will build the tree and then populate it with data*/
    if(thread_globvar->funcinst_first)
      print_recurse_tree(thread_globvar->funcinst_first, fd, 0, thread_globvar);
    
    VG_(sprintf)(buf, "\n\nEND TREE DUMP\n\n");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file
    
    VG_(sprintf)(buf, "\nDATA DUMP\n\n");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file
    
    VG_(sprintf)(buf, "%20s %20s %20s %50s %20s %20s %20s %20s %20s %20s %20s %20s\n\n\n","THREAD NUMBER", "FUNCTION NUMBER", "FUNC_INST NUM", "FUNCTION NAME", "PROD?", "INSTRS", "FLOPS", "IOPS", "IPCOMM_UNIQUE", "OPCOMM_UNIQUE", "IPCOMM", "OPCOMM");
    my_fwrite(fd, (void*)buf, VG_(strlen)(buf)); // This will end up writing into the orignial callgrind.out file
    
    /* Print the data for all functions. Post-processing will build the tree and then populate it with data */
    if(thread_globvar->funcinst_first)
      print_recurse_data(thread_globvar->funcinst_first, fd, 0, thread_globvar);
    
    //Do a final flush to ensure print buffers are completely empty
    fwrite_flush();
    VG_(close)( (Int)sr_Res(res) );
    if (num_valid_threads == CLG_(num_threads))
      break;
  }
  VG_(printf)("Done printing Sigil's output now\n");
}

/***
 * The following functions are helpers to insert and search for address ranges in event data structures.
 * Event tracking is expensive in terms of storage and logic. The biggest lookup expense in these functions.
 * If many disjoint address ranges are touched by an event, it can also lead to an explosion in storage
*/
static int insert_to_evtaddrchunklist(evt_addrchunknode** chunkoriginal, Addr range_first, Addr range_last, ULong* refarg, int shared_read_tid, ULong shared_read_event_num) {

  evt_addrchunknode *chunk = *chunkoriginal; // chunk points to the first element of addrchunk array.
  evt_addrchunknode *chunknodetemp, *chunknodecurr, *next_chunk;
  Addr curr_range_last;
  ULong count = range_last - range_first + 1;
  ULong return_count = count, curr_count;
  int partially_found = 0, return_value2 = 0;

  curr_range_last = range_last;

    //if the determined array element contains any address chunks, then those address
    //chunks are searched to determine if the address range being inserted is already present or should be inserted
    // if the original pointer points to nothing, then a new node is inserted
    
    if(chunk == 0) {
      chunknodetemp = (evt_addrchunknode*) CLG_MALLOC("c1.evt_addrchunknode.gc.1",sizeof(evt_addrchunknode));
      chunknodetemp -> prev = 0;
      chunknodetemp -> next =0;
      chunknodetemp -> rangefirst = range_first;
      chunknodetemp -> rangelast = curr_range_last;
      chunknodetemp -> shared_read_tid = shared_read_tid;
      chunknodetemp -> shared_read_event_num = shared_read_event_num;
      *chunkoriginal = chunknodetemp;
      //CLG_(tot_eventinfo_size) += sizeof(evt_addrchunknode);
      CLG_(num_eventaddrchunk_nodes)++;
    } // end of if that checks if original == 0
    else {
      chunknodecurr = *chunkoriginal;
      curr_count = curr_range_last - range_first +1;
      
      //while iterates over evt_addrchunknodes present in the given array element until it finds
      // the desired address or it reaches end of the list.
      while(1) {
	// if the address being inserted is less than current node's address, then a new node is inserted,
	// and further search is terminated as the node's are in increasing order of addresses
	if(curr_range_last < (chunknodecurr->rangefirst-1)) {
	  chunknodetemp = (evt_addrchunknode*) CLG_MALLOC("c1.evt_addrchunknode.gc.2",sizeof(evt_addrchunknode));
	  chunknodetemp -> prev = chunknodecurr->prev;
	  chunknodetemp -> next = chunknodecurr;
	  
	  if(chunknodecurr->prev!=0) chunknodecurr -> prev->next = chunknodetemp;
	  else *chunkoriginal = chunknodetemp;
	  chunknodecurr -> prev = chunknodetemp;
	  chunknodetemp -> rangefirst = range_first;
	  chunknodetemp -> rangelast = curr_range_last;
	  chunknodetemp -> shared_read_tid = shared_read_tid;
	  chunknodetemp -> shared_read_event_num = shared_read_event_num;
	  //	  CLG_(tot_eventinfo_size) += sizeof(evt_addrchunknode);
	  CLG_(num_eventaddrchunk_nodes)++;
	  break;
	}
	// if current node falls within the address range being inserted, then the current node is deleted and the
	// counts are adjusted appropriately
	else if((range_first <= (chunknodecurr->rangefirst-1)) && (curr_range_last >= (chunknodecurr->rangelast +1))) {
	  return_value2 =1;
	  return_count -= chunknodecurr->rangelast - chunknodecurr->rangefirst + 1;
	  partially_found =1;
	  if(chunknodecurr->next ==0 ){
	    chunknodecurr -> rangefirst = range_first;
	    chunknodecurr -> rangelast = curr_range_last;
	    chunknodecurr -> shared_read_tid = shared_read_tid;
	    chunknodecurr -> shared_read_event_num = shared_read_event_num;
	    break;
	  }
	  else {
	    if(chunknodecurr->prev!=0) chunknodecurr->prev->next = chunknodecurr->next;
	    else *chunkoriginal = chunknodecurr->next;
	    
	    if(chunknodecurr->next!=0) chunknodecurr->next->prev = chunknodecurr->prev;
	    next_chunk = chunknodecurr ->next;
	    drw_free(chunknodecurr, &CLG_(num_eventaddrchunk_nodes));
	    chunknodecurr = next_chunk;
	  }
	}
	// if the address being inserted is more than current node's address range, search continues with the next
	// node
	else if(range_first > (chunknodecurr->rangelast +1)){
	  if(chunknodecurr->next != 0) chunknodecurr = chunknodecurr->next;
	  else {
	    
	    chunknodetemp = (evt_addrchunknode*) CLG_MALLOC("c1.evt_addrchunknode.gc.1",sizeof(evt_addrchunknode));
	    chunknodetemp -> prev = chunknodecurr;
	    chunknodetemp -> next = 0;
	    chunknodecurr -> next = chunknodetemp;
	    
	    chunknodetemp -> rangefirst = range_first;
	    chunknodetemp -> rangelast = curr_range_last;
	    chunknodetemp -> shared_read_tid = shared_read_tid;
	    chunknodetemp -> shared_read_event_num = shared_read_event_num;
	    //	    CLG_(tot_eventinfo_size) += sizeof(evt_addrchunknode);
	    CLG_(num_eventaddrchunk_nodes)++;
	    break;
	  }
	}
	// if the address range being inserted falls within the current node's address range, count of current node is adjusted
	// appropriately, and the search is terminated
	else if((range_first >= (chunknodecurr->rangefirst))&&(curr_range_last<=(chunknodecurr->rangelast))) {
	  return_count -= (curr_range_last- range_first+1);
	  return_value2=1;
	  break;
	}
	
	else {
	  //if last address of the address range being inserted falls within current node, then range and count of current node
	  // are adjusted appropriately and search is terminated
	  if(range_first <= (chunknodecurr->rangefirst -1))
	    {
	      if(curr_range_last != (chunknodecurr->rangefirst -1)) return_value2 =1;
	      return_count -= (curr_range_last - (chunknodecurr->rangefirst)+1);
	      chunknodecurr -> rangefirst = range_first;
	      break;
	    }
	  // if the last address of the address range being inserted is after the current node's address,
	  //and it is less than next node's start address, then current node's range is adjusted appropriately and the search terminates
	  //This implicitly takes care of the case where rangefirst is greater than chunknodecurr->rangefirst but less than chunknodecurr->rangelast. This is true by process of elimination above.
	  if((curr_range_last >= (chunknodecurr->rangelast +1)) &&((chunknodecurr->next ==0)||(curr_range_last < chunknodecurr->next->rangefirst -1)))  {
	    if(range_first != (chunknodecurr->rangelast+1)) return_value2 =1;
	    return_count -= ((chunknodecurr->rangelast)-range_first+1);
	    chunknodecurr -> rangelast = curr_range_last;
	    break;
	  }
	  
	  // if the last address of the address range being inserted falls exactly at the edge or within the next node's range, then
	  //current node is deleted and the search is continued
	  else if((curr_range_last >= (chunknodecurr->rangelast +1)) &&(chunknodecurr->next !=0)&&(curr_range_last >= (chunknodecurr->next->rangefirst -1))) {
	    if(range_first != (chunknodecurr->rangelast+1)) return_value2 =1;
	    return_count -= ((chunknodecurr -> rangelast)- range_first +1);
	    range_first = chunknodecurr->rangefirst;
	    if(chunknodecurr->prev!=0) chunknodecurr->prev->next = chunknodecurr->next;
	    else *chunkoriginal = chunknodecurr->next;
	    
	    if(chunknodecurr->next !=0) chunknodecurr->next->prev = chunknodecurr->prev;
	    next_chunk = chunknodecurr->next;
	    drw_free(chunknodecurr, &CLG_(num_eventaddrchunk_nodes));
	    chunknodecurr = next_chunk;
	  }
	}
	//break while loop if end of the evt_addrchunknode list is reached
	if(chunknodecurr == 0) break;
      } // end of while (1)
    } // end of else corresponding to original ==0 check

  //number of addresses not found are returned
  *refarg = return_count;
  // if the address range is fully found, return 2
  if (return_count == 0) partially_found = 2;
  // if the address range is partially found return 1, else return 0.
  else if(return_value2 == 1) partially_found = 1;
  return partially_found;
}

/***
 * Search the list to see if the address range is present in the list
 * If fully present, returns 2, if partially present, returns 1, otherwise returns 0.
 */

static int search_evtaddrchunklist(evt_addrchunknode** chunkoriginal, Addr range_first, Addr range_last, ULong* refarg, int* address_array) { //The local_flag indicates if the search is in a local produced list or not.

  evt_addrchunknode *chunk = *chunkoriginal;  // chunk points to the first element of addrchunk array.
  evt_addrchunknode *chunknodecurr;
  Addr curr_range_last, curr_range_first;
  ULong count = range_last - range_first + 1;
  ULong datasize = count, return_count = count;
  int i, partially_found =0, return_value2=0;

  // If the addrchunk array pointer is '0', indicating that there are no addresses inserted into the given addrchunk array, return 0
  if(chunk == 0) {
    *refarg = datasize;
    return 0;
  }

  curr_range_first = range_first;
  curr_range_last = range_last;

    //if the determined array element contains any address chunks, then those address
    //chunks are searched, if not, then the search is done for the next array element.
    // This process repeats until hash of the last range falls into an element's hash range.
    if(chunk != 0) {

      chunknodecurr = *chunkoriginal;
      //while iterates over addrchunknodes present in the given array element until it finds
      // the desired address or it reaches end of the list.
      while(1) {
	// if the address being looked for is less than the current node's address, then we
	// don't need to search further nodes, as they are all in ascending order of their addresses
	if (curr_range_last < chunknodecurr ->rangefirst) break;

	// if the address being looked for lies within the current node's address range,then it is partially found
	// and the next nodes are still searched to find if the last range falls into next node's address range
	else if ((curr_range_first >= chunknodecurr->rangefirst) && (curr_range_last <= chunknodecurr->rangelast)) {
	  for(i= (curr_range_first % range_first); i<=(curr_range_last % range_first); i++) address_array[i]=1;
	  return_count -= (curr_range_last -curr_range_first +1);
	  return_value2=1;
	  chunknodecurr=chunknodecurr->next;
	}

	// if the current node falls within the address range being searched, then the return count and address_array
	// are initialized appropriately and the search continues with the next node to determine if the last range
	// falls within next node's address range
	else if((curr_range_first <= chunknodecurr->rangefirst) && (curr_range_last >= chunknodecurr->rangelast)) {
	  return_count -= chunknodecurr->rangelast - chunknodecurr->rangefirst + 1;
	  partially_found =1;
	  for(i= (chunknodecurr->rangefirst %range_first); i<=(chunknodecurr->rangelast % range_first); i++)
	    address_array[i]=1;
	  chunknodecurr = chunknodecurr->next;
	  if(chunknodecurr !=0) continue;
	  else break;
	}

	// Following two cases handle the cases where address range being searched either starts within current node
	// or ends within current node. return_count and address_array are appropriately initialized, and then search
	// continues with the next node.
	else if((curr_range_first >= chunknodecurr->rangefirst)&& (curr_range_first <= chunknodecurr->rangelast)) {
	  return_count -= (chunknodecurr->rangelast - curr_range_first +1);
	  for(i=0; i<= (chunknodecurr->rangelast % range_first); i++)
	    address_array[i] =1;
	  partially_found =1;
	  chunknodecurr = chunknodecurr->next;
	}
	else if((curr_range_last >= chunknodecurr->rangefirst)&& (curr_range_last <= chunknodecurr->rangelast)) {
	  return_count -= (curr_range_last - chunknodecurr->rangefirst +1);
	  for(i=(chunknodecurr->rangefirst % range_first); i<datasize; i++)
	    address_array[i] =1;
	  partially_found =1;
	  break;
	}
	else chunknodecurr = chunknodecurr->next;

	//break while loop if end of the addrchunknode list is reached
	if(chunknodecurr ==0) break;
      } // end of while (1)
    } // end of if corresponding to original !=0 check

  //number of addresses not found are returned
  *refarg = return_count;
  // if the address range is fully found, return 2
  if(return_count ==0) return 2;
  // if the address range is partially found return 1, else return 0.
  else if (return_value2==1) partially_found =1;

  return partially_found;
}

//FUNCTION CALLS TO INSTRUMENT THINGS DURING SYSTEM CALLS
/* Valgrind can intercept system calls and perform either pre or post-processing using the arguments to the call. 
 * In the case of memory map requests, linux brk requests etc. Valgrind tells Sigil what address ranges are touched.
 * Sigil is then able to use this information for establishing producer-consumer relationships across system calls as well.
 */
void CLG_(new_mem_startup) ( Addr start_a, SizeT len,
			     Bool rr, Bool ww, Bool xx, ULong di_handle )
{
  Context *cxt;
  /* Ignore permissions */
  cxt = CLG_(current_state).cxt;
  if(CLG_(clo).drw_syscall){
    if(cxt){
      if(CLG_(clo).drw_debugtrace){
	VG_(printf)("DEBUG TRACE HAS BEEN TURNED ON. NO INSTRUMENTATION WILL OCCUR. A MEMORY TRACE WILL BE PRINTED\n");
	VG_(printf)("=  new startup mem: %x..%x, %d, function: %s\n", (UInt) start_a, (UInt) (start_a+len), (UInt) len, cxt->fn[0]->name);
      }
      CLG_(new_mem_mmap) (start_a, len, rr, ww, xx, di_handle);
    }
    else{
      if(CLG_(clo).drw_debugtrace){
	VG_(printf)("DEBUG TRACE HAS BEEN TURNED ON. NO INSTRUMENTATION WILL OCCUR. A MEMORY TRACE WILL BE PRINTED\n");
	VG_(printf)("=  new startup mem: %x..%x, %d, no function\n", (UInt) start_a, (UInt) (start_a+len), (UInt) len);
      }
      CLG_(storeIDRWcontext)( NULL, (UInt) len, start_a, 1, 5); //Put it down as a memory write for the current function
    }
  }
  //  VG_(printf)("=  new startup mem: rr = %s, ww = %s, xx = %s\n",rr ? "True" : "False", ww ? "True" : "False", xx ? "True" : "False");
  //rx_new_mem("startup", make_lazy( RX_(specials)[SpStartup] ), start_a, len, False);

}

void CLG_(new_mem_mmap) ( Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle )
{  
   // (Ignore permissions)
   // Make it look like any other syscall that outputs to memory

//DEBUG CODE   
/*   CLG_(last_mem_write_addr) = a; */
/*   CLG_(last_mem_write_len)  = len; */
  //VG_(printf)("=  new mmap mem: %x..%x, %d, function: %s\n", a, a+len,len, CLG_(current_state).cxt->fn[0]->name);
/*   VG_(printf)("=  new mmap mem: rr = %s, ww = %s, xx = %s\n",rr ? "True" : "False", ww ? "True" : "False", xx ? "True" : "False"); */

  if(CLG_(clo).drw_syscall){
    CLG_(storeIDRWcontext)( NULL, (UInt) len, a, 1, 2); //Put it down as a memory write for the current function for now
  }
}

void CLG_(new_mem_brk) ( Addr a, SizeT len, ThreadId tid )
{
	//DEBUG CODE
  //rx_new_mem("brk",  RX_(specials)[SpBrk], a, len, False);
  //  VG_(printf)("=  new brk mem: %x..%x, %d, function: %s\n", a, a+len,len, CLG_(current_state).cxt->fn[0]->name);
  //VG_(printf)("=  new brk mem: rr = %s, ww = %s, xx = %s\n",rr ? "True" : "False", ww ? "True" : "False", xx ? "True" : "False");
}

// This may be called 0, 1 or several times for each syscall, depending on
// how many memory args it has.  For each memory arg, we record all the
// relevant information, including the actual words referenced.
static 
void rx_pre_mem_read_common(CorePart part, Bool is_asciiz, Addr a, UInt len)
{
  Context *cxt;
  /* Ignore permissions */
  cxt = CLG_(current_state).cxt;
  if(CLG_(clo).drw_syscall){
    if(cxt){
		//DEBUG CODE
      //VG_(printf)("=  pre mem read %s: %x..%x, %d, function: %s\n", is_asciiz ? "asciiz":"normal", a, a+len,len, cxt->fn[0]->name);
      //Insert into a temp array and pop when doing the syscall if a syscall is not already underway
      if(-1 == CLG_(current_syscall)){
	CLG_(syscall_addrchunks)[CLG_(syscall_addrchunk_idx)].first = a;
	CLG_(syscall_addrchunks)[CLG_(syscall_addrchunk_idx)].last = a + len - 1;
	CLG_(syscall_addrchunks)[CLG_(syscall_addrchunk_idx)].original = 0;
	CLG_(syscall_addrchunk_idx)++;
      }
      else{
	CLG_(storeIDRWcontext)( NULL, len, a, 0, 1); //Put it down as a memory read for the current function
      }
    }
    else{
	//DEBUG CODE
      //    VG_(printf)("=  pre mem read %s: %x..%x, %d, no function\n", is_asciiz ? "asciiz":"normal", a, a+len,len);
    }
  }
}

void CLG_(pre_mem_read_asciiz)(CorePart part, ThreadId tid, Char* s, Addr a)
{
   UInt len = VG_(strlen)( (Char*)a );
   // Nb: no +1 for '\0' -- we don't want to print it on the graph
   rx_pre_mem_read_common(part, /*is_asciiz*/True, a, len);
}

void CLG_(pre_mem_read)(CorePart part, ThreadId tid, Char* s, Addr a, SizeT len)
{
  rx_pre_mem_read_common(part, /*is_asciiz*/False, a, len);
}

void CLG_(pre_mem_write)(CorePart part, ThreadId tid, Char* s, 
			 //                             Addr a, UInt len)
                             Addr a, SizeT len)
{
	//DEBUG CODE
  //VG_(printf)("=  pre mem write: %x..%x, %d, function: %s\n", a, a+len,len, CLG_(current_state).cxt->fn[0]->name);
}

//I read in the Valgrind documentation that post functions are not called if the syscall fails or returns an errorl.
void CLG_(post_mem_write)(CorePart part, ThreadId tid,
			     Addr a, SizeT len)
{
   if (-1 != CLG_(current_syscall)) {

   //DEBUG CODE
/*       // AFAIK, no syscalls write more than one block of memory;  check this */
/*       if (INVALID_TEMPREG != CLG_(last_mem_write_addr)) { */
/*          VG_(printf)("sys# = %d\n", CLG_(current_syscall)); */
/*          VG_(skin_panic)("can't handle syscalls that write more than one block (eg. readv()), sorry"); */
/*       } */
/*       tl_assert(INVALID_TEMPREG == CLG_(last_mem_write_len)); */

/*       CLG_(last_mem_write_addr) = a; */
/*       CLG_(last_mem_write_len)  = len; */
     //VG_(printf)("=  post mem write: %x..%x, %d, function: %s\n", a, a+len,len, CLG_(current_state).cxt->fn[0]->name);
     if(CLG_(clo).drw_syscall){
       CLG_(storeIDRWcontext)( NULL, len, a, 1, 2); //Put it down as a memory write for the current function
     }
   }
}
