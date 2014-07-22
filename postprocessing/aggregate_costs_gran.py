#! /usr/bin/env python
##--------------------------------------------------------------------##
##--- This one just parses the data in the file sigil.total.out     --##
##---                                                              ---##
##---                                                              ---##
##--- Command line args:                                           ---##
##--------------------------------------------------------------------##

# /*
#    This file is part of Sigil, a tool for call graph profiling programs.
 
#    Copyright (C) 2012, Siddharth Nilakantan, Drexel University
  
#    This tool is derived from and contains code from Callgrind
#    Copyright (C) 2002-2011, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)
 
#    This tool is also derived from and contains code from Cachegrind
#    Copyright (C) 2002-2011 Nicholas Nethercote (njn@valgrind.org)
 
#    This program is free software; you can redistribute it and/or
#    modify it under the terms of the GNU General Public License as
#    published by the Free Software Foundation; either version 2 of the
#    License, or (at your option) any later version.
 
#    This program is distributed in the hope that it will be useful, but
#    WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#    General Public License for more details.
 
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
#    02111-1307, USA.
 
#    The GNU General Public License is contained in the file COPYING.
# */

import sys
import re
#import matplotlib.pyplot as plt
#import numpy as np
import subprocess
import itertools
import gzip
from optparse import OptionParser

DEF_A_CODE = "None"

#Global variables
printcallees = 0
instrs_total = 0
ops_total = 0
percentofinst = 0
comm = []
comp = []
label = []
MAX_DEPENDENCIES = 10
max_depth = 0
comp_speedup = 30
cpu_freq = 2.5 #GHz
bandwidth = 6.4 #16 GB/s / 2.5G cyc/sec = 6.4 bytes/cycle
overall_cpu_cycles = 0
application_cpu_cycles = 0
gran_mode = 0
cpu_scaling = 1 #This can be used to model CPUs of various speeds
special_fns = ['fwrite','fread','printf']

#README
#This script is derived from the original aggregate_costs_gran.py in /archgroup/archtools/postprocess_scripts/accel_select.
#It has modifications to parse the datareuse file as well. This is hardcoded in the script. Eventually this functionality should be merged back into the original script
#This can happen as soon as the keyword arguments are improved and the whole things is cleaned up

def usage() :
	print "Usage:"
	print "\t" + sys.argv[0] + " --file=[<file>] [--printcallees?=<yes|no>] [--percent-of-inst=<0-100>] [mode=<modif|both>] [granmode=<nogran|coarsegran|metric>] [bandwidth_value=<guideline(in GB/s) - 4|8|16|32>] [<callgrind out file>] [software_scaling]"
	print "\t Continuing with default options - file: sigil.totals.out-1, printcallees=no, displaying functions which occupy at least 0.01% of instructions, mode=modif"
	print "\t Will only work for single-threaded (serial) runs at the moment"
	print "This script is derived from the original aggregate_costs_gran.py in /archgroup/archtools/postprocess_scripts/accel_select. For more details see usage() in the script."
	print ""

class dependency_chain : #Incomplete. Not yet used
	
	#The chain should have a chance value and a list of MAX functions
	
	def __init__( self, function_number, funcinst_number, vert_parent_fn, ipcomm=0, ipcomm_uniq=0, opcomm=0, opcomm_uniq=0 ) :
		self.chance = 0
	
class consumerfuncinfo :

	def __init__( self, function_number, funcinst_number, vert_parent_fn, consumed_fn, ipcomm=0, ipcomm_uniq=0, opcomm=0, opcomm_uniq=0 ) :
		self.function_number = function_number #Refers to a particular function
		self.funcinst_number = funcinst_number #Refers to a particular function instance (actually context)
		self.vert_parent_fn = vert_parent_fn  #Points to the parent function whose consumed list that this is a part of.
		self.consumed_fn = consumed_fn
		self.dependency_chain = []
		self.ipcomm = int(ipcomm)
		self.ipcomm_uniq = int(ipcomm_uniq)
		self.opcomm = int(opcomm)
		self.opcomm_uniq = int(opcomm_uniq)
		#self.min_dep_calls = 0
		#self.max_dep_calls = 0
		#self.tot_dep_calls = 0
		#self.min_ipcomm_uniq = 0
		#self.max_ipcomm_calls = 0
		#self.tot_ipcomm_uniq = 0
		self.callee_flag = 0 #If set, when found in the consumed list of function A, it means that this function is in the callee chain of A. If found in the consumerlist of function A, means that function A is in the callee chain of this function.
	def update( self, function_number, funcinst_number, vert_parent_fn, consumed_fn, ipcomm=0, ipcomm_uniq=0, opcomm=0, opcomm_uniq=0 ) :
		if self.function_number != function_number or self.funcinst_number != funcinst_number :
			print "Not equal!"
			sys.exit(1)
		if self.vert_parent_fn != vert_parent_fn or self.consumed_fn != consumed_fn :
			print "Not equal!"
			sys.exit(1)
		self.ipcomm += int(ipcomm)
		self.ipcomm_uniq += int(ipcomm_uniq)
		self.opcomm += int(opcomm)
		self.opcomm_uniq += int(opcomm_uniq)
			
class funcinfo :
	'''Array to store central function info. Also has global link to the 
	* structures of functions who consume from this function.
	* Chunks are allocated on demand, and deallocated at program termination.
	* This can also act as a linked list. This is needed because 
	* contiguous locations in the funcarray are not necessarily used. There may
	* be holes in between. Thus we need to track only the locations which are 
	* used from this list. 
	* Alternatively, an array could be used, but needs to be sized statically
	'''
	funcinsttable = None
	
	def __init__( self, function_number, fn_name=None ) :
		self.function_name = fn_name
		self.function_number = function_number
		self.funcinsttable = {}
		#self.number_of_funcinsts = 0; #Removed because we can always query the length of the variable above instead.
		
		#More variables to store costs calculated in the scripts
		self.ipcomm = 0
		self.opcomm = 0
		self.ipcomm_uniq = 0
		self.opcomm_uniq = 0
		self.instrs = 0
		self.flops = 0
		self.iops = 0
	
###   end funcinfo   ###
	
class funcinst :
	''' Array to store data for a function instance. This is separated from other structs
	* to support a dynamic number of functioninfos for a function info item.
	* Chunks are allocated on demand, and deallocated at program termination.
	* This can also act as a linked list. This is needed because 
	* contiguous locations in the funcarray are not necessarily used. There may
	* be holes in between. Thus we need to track only the locations which are 
	* used from this list. 
	* Alternatively, an array could be used, but needs to be sized statically
	'''
	
	def __init__( self, caller, function_number, funcinst_number, num_calls, funcinfotable ) :
		
		#We need to be guaranteed that its corresponding funcinfo is already created. If it was not created, then this one will have funcinst_number to be -1.
		funcinfotemp = funcinfotable[function_number]
		self.consumedlist = {}
		self.consumerlist = {}
		self.function_number = function_number
		self.function_info = funcinfotemp #Store pointer to central info store of function
		self.funcinst_number = funcinst_number
		self.ipcomm_uniq = 0 #Not really useful, because this script is going to count all this based on granularity anyway
		self.opcomm_uniq = 0
		self.ipcomm = 0 #Not really useful, because this script is going to count all this based on granularity anyway
		self.opcomm = 0
		self.local_uniq = 0
		self.local = 0
		self.noprod_uniq = 0
		self.noprod = 0
		self.startup_uniq = 0
		self.startup = 0
		self.instrs = 0
		self.iops = 0
		self.flops = 0
		self.caller = caller #If caller is defined, then go ahead and put that in. (Otherwise it might error out when doing the first ever function)
		self.callees = []
		#More variables to store costs calculated in the scripts
		self.ipcomm_incl = 0 #For inclusive costs
		self.opcomm_incl = 0 #For inclusive costs
		self.ipcomm_incl_uniq = 0 #For inclusive costs
		self.opcomm_incl_uniq = 0 #For inclusive costs
		self.instrs_incl = 0
		self.flops_incl = 0
		self.iops_incl = 0
		self.local_incl_uniq = 0
		self.local_incl = 0
		self.num_calls = num_calls
		self.dependency_chains = []
		self.comp_comm_uniq = 0
		self.comp_comm = 0
		#Stuff from original Callgrind
		self.cg_params = []
		for i in range (0,16) : # Ir - LL_time
			self.cg_params.append(0)
		#self.Ir = 0
		#self.Dr = 0
		#self.Dw = 0
		#self.I1mr = 0
		#self.D1mr = 0
		#self.D1mw = 0
		#self.ILmr = 0
		#self.DLmr = 0
		#self.DLmw = 0
		#self.Bc = 0
		#self.Bcm = 0
		#self.Bi = 0
		#self.Bim = 0
		#self.exec_cycles = 0
		#self.LL_time = 0
		self.area = 0
		self.metric = 0
		self.depth_index = 0
		self.progility = 0
		self.coverage = 0
		self.breakeven_speedup = 0
		self.breakeven_speedup_norm = 0
		self.comp_cycles_accel = 0
		self.input_offload_time = 0
		self.output_offload_time = 0
		self.exec_cycles_cpu = 0
		self.ipcomm_true = 0
		self.local_true = 0
		self.local_reusecount = [] #For a breakdown by count of reuse for a stacked bar graph
		self.ipcomm_reusecount = []
	
		if funcinfotemp != 0 :
			#self.funcinst_list_next = funcinfotemp.funcinst_list
			#self.funcinst_list_prev = 0
    
			#if funcinfotemp.funcinst_list
			#	funcinfotemp.funcinst_list.funcinst_list_prev = self
			#funcinfotemp.funcinst_list = self
    
			#self.funcinst_number = funcinfotemp.number_of_funcinsts
			#funcinfotemp.number_of_funcinsts++
			funcinfotemp.funcinsttable[funcinst_number] = self
		else :
			if funcinst_number != -1 :
				print "Uh Oh! funcinfotemp = 0, but funcinst_number != -1"
			#self.funcinst_list_next = 0
			#self.funcinst_list_prev = 0
		
###   end funcinst   ###

def area_regression( node ) :
	#return 2.5135e5 - 1.7136 * (node.flops_incl + node.iops_incl) + 35.113 * node.local_incl_uniq
	return 2.41e5 - 1.98 * (node.flops_incl + node.iops_incl) + 28.277 * (node.local_incl_uniq + node.ipcomm_incl_uniq + node.opcomm_incl_uniq)

def readfromfile( filename, funcinfotable ) :

	caller_funcinst = None
	funcinstroot = None

	#Patterns for TREE DUMP
	patString = '^TREE DUMP'
	pattern1 = re.compile( patString )
	patString = '^(\d+), (\-*\d+), (\w+), (\d+)'
	pattern2 = re.compile( patString )
	patString = 'None'
	pattern3 = re.compile( patString )
	patString = '^END TREE DUMP'
	pattern4 = re.compile( patString )
	
	#Patterns for DATA DUMP
	patString = 'DATA DUMP'
	pattern5 = re.compile( patString )
	patString = '^(\d+)\s+(\d+)\s+(\-*\d+)\s+(.+)\*\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$' #This is to match the line where the function is defined. After this line will come the consumedlist
	pattern6 = re.compile( patString )
	patString = '^(\d+)\s+(\d+)\s+(\d+)\s+(.+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$' #This is for all lines in the consumedlist
	pattern7 = re.compile( patString )
	# patString = '^(\d+)\s+(\d+)\s+(.+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$' #This is for capturing the parameters in the dependencylist
	# pattern8 = re.compile( patString )
	
	#flags
	tree_flag = 0
	node_flag = 0
	star_flag = 0
	
	#tempvariables for data part
	funcinsttemp = 0
	local_uniq = 0
	local = 0
	noprod_uniq = 0
	noprod = 0

	if ".gz" in filename :
		fh = gzip.open( filename, 'r' )
	else :
		fh = open( filename, 'r' )
	for l in fh :
		l = l.strip() #Removes leading and ending whitespaces (we should have none in our output)

		# Here's the actual search
		if tree_flag :
			match2 = pattern2.search( l )
			match3 = pattern3.search( l )
			match4 = pattern4.search( l )
		elif node_flag :
			match6 = pattern6.search( l )
			match7 = pattern7.search( l )
			#match8 = pattern8.search( l )
			#if l in '\n' :
				#skip_flag = 1 #This is to indicate that we are in/starting consumerlist. We don't want those. So skip until the next match6 occurs
		else :
			match1 = pattern1.search( l )
			match5 = pattern5.search( l )
			#print "match1:", match1, "match5:", match5
			
		if tree_flag :
			#tree_flag += 1
			#print "tree_flag =", tree_flag
			if match2 :
				function_number = match2.group(1)
				funcinst_number = match2.group(2)
				children_flag = match2.group(3)
				num_calls = match2.group(4)	
				#Check if corresponding funcinfo is created and update it with whatever is necessary
				if function_number not in funcinfotable :
					if funcinst_number >= 0 :
						new_funcinfo = funcinfo( function_number ) #Here we do not have the fn_name yet, so leave it blank
						funcinfotable[function_number] = new_funcinfo
					else :
						funcinfotable[function_number] = 0
				
				#Then create funcinst as well.
				new_funcinst = funcinst( caller_funcinst, function_number, funcinst_number, num_calls, funcinfotable ) #This will also make sure the caller for the new funcinst points to the right funcinst object
				if caller_funcinst :#Make sure we only do the following when not examining the first node
					caller_funcinst.callees.append( new_funcinst ) #This will ensure that this new guy is added to the callees of the caller funcinst
				if not funcinstroot :
					funcinstroot = new_funcinst
					caller_funcinst = new_funcinst
				if children_flag == "True" :
					caller_funcinst = new_funcinst
				
			elif match3 : #None was encountered, so go back up one in the calltree
				#if caller_funcinst.caller :
				caller_funcinst = caller_funcinst.caller
				
			elif match4 :
				#If matches END TREE DUMP, then leave
				tree_flag = 0
				#fh.close() # THIS MUST BE REMOVED ONCE WE IMPLEMENT NODE_FLAG FUNCTIONALITY
				#return funcinstroot; # THIS MUST BE REMOVED ONCE WE IMPLEMENT NODE_FLAG FUNCTIONALITY
		
		elif node_flag :
			if match6 :
				#skip_flag = 0#Reset the skip_flag first
				#if star_flag :
					#Save state of previous funcinst
					
				#Figure out new funcinst, group(2) is the function number, group(3) is the funcinst number, group(4) is the name
				#The performance here can be improved drastically, because we are not using the information that the printing
				#of data will also be printed in the same deserialization order as was the calltree
				funcinfotemp = funcinfotable[match6.group(2)]
				funcinsttemp = funcinfotemp.funcinsttable[match6.group(3)]
				if not funcinfotemp.function_name :
					funcinfotemp.function_name = match6.group(4)
				funcinsttemp.instrs = int(match6.group(5))
				funcinsttemp.flops = int(match6.group(6))
				funcinsttemp.iops = int(match6.group(7))
				funcinsttemp.ipcomm_uniq = int(match6.group(8))
				funcinsttemp.opcomm_uniq = int(match6.group(9))
				funcinsttemp.ipcomm = int(match6.group(10))
				funcinsttemp.opcomm = int(match6.group(11))
				#star_flag = 1
				
			#elif match8 or match9 or match10 :
				#if match8 :
					
			#Clause to parse dependency list
			# elif match8 :
				# temp_coslist = funcinsttemp.consumedlist[(int(match8.group(1)), int(match8.group(2)))]
				# temp_coslist.min_ipcomm_uniq = int(match8.group(4))
				# temp_coslist.max_ipcomm_calls = int(match8.group(5))
				# temp_coslist.tot_ipcomm_uniq = int(match8.group(6))
				# temp_coslist.min_dep_calls = int(match8.group(7))
				# temp_coslist.max_dep_calls = int(match8.group(8))
				# temp_coslist.tot_dep_calls = int(match8.group(9))
					
			elif match7 :
				#if skip_flag :
					#continue
				if "SELF" in match7.group(4) :
					funcinsttemp.local_uniq = int(match7.group(6))
					funcinsttemp.local = int(match7.group(8))
				elif "NO PRODUCER" in match7.group(4) :
					funcinsttemp.noprod_uniq = int(match7.group(6))
					funcinsttemp.noprod = int(match7.group(8))
				elif "NO CONSUMER" in match7.group(4) :
					funcinsttemp.nocons_uniq = int(match7.group(6))
					funcinsttemp.nocons = int(match7.group(8))
				elif "Thread-30001" in match7.group(4) :
					funcinsttemp.startup_uniq = int(match7.group(6))
					funcinsttemp.startup = int(match7.group(8))
				else :
					consumed_fn = funcinfotable[match7.group(2)].funcinsttable[match7.group(3)]
					if (int(match7.group(2)), int(match7.group(3))) in funcinsttemp.consumedlist.keys() :
						funcinsttemp.consumedlist[(int(match7.group(2)), int(match7.group(3)))].update( match7.group(2), match7.group(3), funcinsttemp, consumed_fn, match7.group(8), match7.group(6), match7.group(9), match7.group(7) )
					else :
						new_consumerfuncinfo = consumerfuncinfo( match7.group(2), match7.group(3), funcinsttemp, consumed_fn, match7.group(8), match7.group(6), match7.group(9), match7.group(7) )
						funcinsttemp.consumedlist[(int(match7.group(2)), int(match7.group(3)))] = new_consumerfuncinfo
						consumed_fn.consumerlist[(int(match7.group(2)), int(match7.group(3)))] = new_consumerfuncinfo
			
		else : # Matches to see which section is starting (whether its the tree structure or the info on the nodes). Both should be mutually exclusive
			if match1 :
				tree_flag = 1
			elif match5 :
				node_flag = 1
		
	
	#if star_flag
		
	
	fh.close()
	return funcinstroot;
		
def print_recurse_cost( funcinst, printcallees_local ) :		
	
	#Added because when we run with --toggle-collect=main, some funcinst instances have their "function_info" variables as empty (0x0), because I suppose instrumentation does not happen for them, but funcinsts get created for them as their children may get created. We should actually be allowed to skip this and return without printing anything
	print_node ( funcinst, "*", 1 )
  
	#Let us sort the list of callees by FLOP/IOPS? instructions before printing. That way, we'll not print the unimportant ones. We should eventually have a command line option specifying when to stop printing
	funcinst.callees.sort(key=lambda x: (x.flops_incl + x.iops_incl), reverse=True)
  
	if printcallees_local :
		for x in funcinst.callees :
			print_node ( x, "<", 0)
		print ""
	
	for x in funcinst.callees :
		#if x.instrs_incl/float(instrs_total) >= percentofinst/float(100) : #Print and remove the unnecessary functions
		if (x.flops_incl + x.iops_incl)/float(ops_total) >= percentofinst/float(100) : #Print and remove the unnecessary functions
			print_recurse_cost ( x, printcallees_local )

def print_recurse_nonuniq( funcinst_node, filepointer ) :		
	filepointer.write("%50s %-10lu %-10lu %-15.2f %-30s %-10lu %-10lu %-15.2f %-30s\n"%(funcinst_node.function_info.function_name, funcinst_node.ipcomm_uniq, funcinst_node.ipcomm, funcinst_node.ipcomm_true, ",".join(map(str, funcinst_node.ipcomm_reusecount)), funcinst_node.local_uniq, funcinst_node.local, funcinst_node.local_true, ",".join(map(str,funcinst_node.local_reusecount))))
	#Let us sort the list of callees by FLOP/IOPS? instructions before printing. That way, we'll not print the unimportant ones. We should eventually have a command line option specifying when to stop printing
	funcinst_node.callees.sort(key=lambda x: (x.flops_incl + x.iops_incl), reverse=True)
	for x in funcinst_node.callees :
		if (x.flops_incl + x.iops_incl)/float(ops_total) >= percentofinst/float(100) : #Print and remove the unnecessary functions
			print_recurse_nonuniq ( x, filepointer )

def calc_recurse_reuse( funcinst_node ) :
	length_total_local = funcinst_node.local_true
	length_total_ip = funcinst_node.ipcomm_true
	for x in funcinst_node.callees :
		temp_length_total_local, temp_length_total_ip = calc_recurse_reuse ( x )
		length_total_local += temp_length_total_local
		length_total_ip += temp_length_total_ip
	return length_total_local,length_total_ip

def print_only_bottom ( bottom_list ) :
	global gran_mode
	for node in bottom_list :
		#If the metric is inf, then don't print it for now
		if node.metric == float('inf') and gran_mode > 0:
			continue
		if (node.flops_incl + node.iops_incl)/float(ops_total) >= percentofinst/float(100) :
			#Print and remove the unnecessary functions
			print_node_for_gran ( node, "*", 1 )
		
def check_callees_for_funcinst ( funcinst, consumerfuncinfo_ptr ) :
	'''This function simply checks for the funcinst described in consumerfuncinfo
	* against all the callees of funcinst. This is done to check if it its cost
	* should be included for the current caller.
	'''
	#Start by checking an immediate callee and then recursing into it's callees before moving on to the next immediate callee.
	for x in funcinst.callees :
		#Check if this callee is the one we are looking for.
		if (consumerfuncinfo_ptr.function_number == x.function_number and consumerfuncinfo_ptr.funcinst_number == x.funcinst_number) :
			return 1
		#else iterate over its callees
		foundflag = check_callees_for_funcinst ( x, consumerfuncinfo_ptr )
		if foundflag :
			return 1
	return 0
	
def check_callers_for_funcinst ( funcinst, consumerfuncinfo_ptr, special_flag ) :
	'''This function simply checks for the funcinst described in consumerfuncinfo
	* against all the callers in the upper callchain of funcinst. This is done to check if * it its cost should be included for the current caller. The special_flag indicates
	* that the function being checked for is a "print" of I/O variant. In such cases,
	* we must traverse the tree upward and update all the callers without bothering to check
	* The special_flag is set when the calltree actually has the function under consideration
	'''
	#0. returnvalue if special_flag is set
	if special_flag == 1 :
		foundflag = 1
	#1. Start by checking the caller (Checking if a caller exists is done at the end)
	caller_funcinst = funcinst.caller
	if special_flag == 0 :
		if (consumerfuncinfo_ptr.function_number == caller_funcinst.function_number and consumerfuncinfo_ptr.funcinst_number == caller_funcinst.funcinst_number) :
			return 1
	
	#2. Check the caller's immediate callees and their callees
	if special_flag == 0 :
		for x in caller_funcinst.callees :
			#Skip the current funcinst in the callees of course
			if ( x == funcinst) :
				continue
			#Check if this callee is the one we are looking for.
			if (consumerfuncinfo_ptr.function_number == x.function_number and consumerfuncinfo_ptr.funcinst_number == x.funcinst_number) :
				consumerfuncinfo_ptr.callee_flag = 1
				return 1
			#else iterate over its callees
			foundflag = check_callees_for_funcinst ( x, consumerfuncinfo_ptr )
			if foundflag :
				consumerfuncinfo_ptr.callee_flag = 1
				return 1
	
	#3. Update costs in the current caller and then keep moving up
	caller_funcinst.ipcomm_incl += consumerfuncinfo_ptr.ipcomm
	caller_funcinst.opcomm_incl += consumerfuncinfo_ptr.opcomm
	caller_funcinst.ipcomm_incl_uniq += consumerfuncinfo_ptr.ipcomm_uniq
	caller_funcinst.opcomm_incl_uniq += consumerfuncinfo_ptr.opcomm_uniq
	if caller_funcinst.caller :
		#if !caller_funcinst.caller.caller
			#return 1
		#The above statements can be used to save some time as we don't need to process the topmost node at all. If we do not get any (uh-oh) errors, we can try this out.
		foundflag = check_callers_for_funcinst ( caller_funcinst, consumerfuncinfo_ptr, special_flag )
	return foundflag
	
def calculate_clusive_costs( funcinst, funcinfotable, depth_index ) : 
	'''This function calculates both inclusive and exclusive costs for each function in the * calltree
	'''
	global max_depth
	global special_fns
	for x in funcinst.callees :
		calculate_clusive_costs ( x, funcinfotable, depth_index + 1 )
	
	#Alright no more callees, so now start accumulating costs
	funcinst.depth_index = depth_index
	if depth_index > max_depth :
		max_depth = depth_index
	#0. Do the inclusive instructions first
	funcinst.instrs_incl += funcinst.instrs
	funcinst.flops_incl += funcinst.flops
	funcinst.iops_incl += funcinst.iops
	funcinst.local_incl_uniq += funcinst.local_uniq
	funcinst.local_incl += funcinst.local
	if funcinst.caller :
		funcinst.caller.instrs_incl += funcinst.instrs_incl
		funcinst.caller.flops_incl += funcinst.flops_incl
		funcinst.caller.iops_incl += funcinst.iops_incl
		funcinst.caller.local_incl_uniq += funcinst.local_incl_uniq
		funcinst.caller.local_incl += funcinst.local_incl
	
	#1. print the cost for this function in its central store
	funcinst.function_info.ipcomm += funcinst.ipcomm
	funcinst.function_info.opcomm += funcinst.opcomm
	funcinst.function_info.ipcomm_uniq += funcinst.ipcomm_uniq
	funcinst.function_info.opcomm_uniq += funcinst.opcomm_uniq
	
	#2. Print the cost of this funcinst to all its callers by iterating over the consumed/consumer list and for each entry, check the entire calltree
	#mergedlist = [funcinst.consumedlist.itervalues(),funcinst.consumerlist.itervalues()]
	for consumerfuncinfo_ptr in funcinst.consumedlist.itervalues() :
	#for consumerfuncinfo_ptr in itertools.chain(*mergedlist) :
		#1. Check callees
		foundflag = check_callees_for_funcinst ( funcinst, consumerfuncinfo_ptr ) #This call will not check the current funcinst variable as in that case consumerfuncinfotemp would have been SELF
		#1a. If the function is found in the subtree and is a file I/O, then please treat it as external communication
		if foundflag :
			for special_fn in special_fns :
				if special_fn in funcinfotable[consumerfuncinfo_ptr.function_number].function_name :
					#If a special function, go ahead and update this function and callers anyway
					funcinst.ipcomm_incl += consumerfuncinfo_ptr.ipcomm
					funcinst.opcomm_incl += consumerfuncinfo_ptr.opcomm
					funcinst.ipcomm_incl_uniq += consumerfuncinfo_ptr.ipcomm_uniq
					funcinst.opcomm_incl_uniq += consumerfuncinfo_ptr.opcomm_uniq
					if funcinst.caller :
						check_callers_for_funcinst ( funcinst, consumerfuncinfo_ptr, 1 )
		#2. If not found in step1, check callers and update costs as you go if not found in each caller's callee list
		if (foundflag == 0 and funcinst.caller) :
			funcinst.ipcomm_incl += consumerfuncinfo_ptr.ipcomm
			funcinst.opcomm_incl += consumerfuncinfo_ptr.opcomm
			funcinst.ipcomm_incl_uniq += consumerfuncinfo_ptr.ipcomm_uniq
			funcinst.opcomm_incl_uniq += consumerfuncinfo_ptr.opcomm_uniq
			if funcinst.caller :
				foundflag = check_callers_for_funcinst ( funcinst, consumerfuncinfo_ptr, 0 )
		if foundflag == 0 : #If still not found then something really bad has happened in populating the calltree
			print "Uh Oh! foundflag is zero for a certain consumerfuncinfo structure"
				
def compare_callees_compcomm ( node, given_node ) :
	'''This function recursively goes down the calltree and checks if any callees comp_comm_uniq ratio is higher than the given one. It will use the inclusive values'''
	#Iterate over callees and check their comp_comm_uniq
	foundflag = 0
	for x in node.callees :
		calculate_metrics ( x ) #Also calculate area here itself
		#3. Compare x.comp_comm_uniq costs and return 0 if something is found higher than node's comp_comm_uniq
		return_0_flag = compare_granularity_metric ( x, node ) #Will return 1 if metric is lesser for x than node
		if return_0_flag :
			return 0
	
	#Recurse down the calltree
	for x in node.callees :
		foundflag = compare_callees_compcomm ( x, given_node )
		if foundflag == 0 :
			return 0
		
	return 1
	
def reduce_tree_bygran ( node, bottom_list ) :
	'''This function recursively goes down the calltree and checks if all callees should be merged with a particular node, based on computation/communication. It will check the inclusive values'''
	#1. If comp_comm of the current node has not been calculated (that is, it is zero), then calculate it and store it.
	mergeflag = 0
	calculate_metrics ( node ) #Also calculate area here itself
	#2. Iterate over calltree and see if any node has a higher comp/comm ratio.
	if node.metric < float('inf') : #Takes care of the case where the callees might also have inf as metric, in which case the algorithm will fail
		if mergeflag != 1 :
			mergeflag = compare_callees_compcomm( node, node )
	#3. If mergeflag is 1, remove all the callees of this guy and keep this as the last node in the tree so far
	if mergeflag == 1 :
		del node.callees[:] #Empty the list! A better way might be to add a flag marking it for deletion, but putting off the deletion itself for later.
	#4. Recurse down the tree now, to check the remaining nodes against their subtrees
	if not node.callees :
		bottom_list.append( node )
	for x in node.callees :
		reduce_tree_bygran ( x, bottom_list )
	
def search_for_main ( node ) :	

	for x in node.callees :
		if x.funcinst_number >= 0 :
			#Check if this callee is the one we are looking for.
			if "main" in x.function_info.function_name :
				if "dl_main" not in x.function_info.function_name :
					if "below main" not in x.function_info.function_name :
						return x #This is main!
	for x in node.callees :				
		#else iterate over its callees
		foundnode = search_for_main ( x )
		if foundnode != 0 :
			return foundnode
	return 0

def search_for_l_in_cg ( line, funcinfotable, incl_flag ) :
	'''This function hunts to match the contents of a line to a function name in the calltree. 
	* FUNCTIONALITY NOT IMPLEMENTED FROM HERE ON: The hint_node is evaluated first for all its callees (unless hint is the main node).
	* Failing that, the main_node is checked. If at the bottom, the recursive function returns as
	* the bottom needs special processing
	'''
	global application_cpu_cycles
	patString = '^\s*([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+'
	pattern1 = re.compile( patString )
	#1. Iterate over the entire funcinfotable
	for key, funcnode in funcinfotable.iteritems() :
		#2. Check if this is the node. First check name, then check instruction count
		temp_name = funcnode.function_name
		temp_name1 = ":"+temp_name.strip()
		temp_name = temp_name1+"\'"
		if temp_name in line :
			match1 = pattern1.search( line )
			#This is a dangerous line as it may cause much debugging headaches later. This usually happens when the callgrind output is mismatched in that enough variables haven't been captured/too many have been captured. Also, if a line starts with a "." then the match fails
			if not match1:
				print "Warning: A function from callgrind output was found in the funcinfotable, but the re.match failed!"
				return
			if "." not in match1.group(1):
				line_instrs = int(match1.group(1).replace(',', ''))
			else : 
				return
			for node in funcnode.funcinsttable.itervalues() :
				if incl_flag :
					node_instrs = node.instrs_incl
				else :
					node_instrs = node.instrs
				if node_instrs == 0:
					#print "I'm a node and I have zero instructions!"
					return
				#If there is only one element, then no hassles, just do the needful and return
				if len(funcnode.funcinsttable) == 1 :
					diff = 0
				else :
					diff = abs(node_instrs - line_instrs) #In this case we need to use non-inclusive instructions.
				if float(diff)/min(line_instrs, node_instrs) < 0.01 :#if true, found!
					if not node.callees and not incl_flag :
						continue
					for i in range (0, 12) :
						if "." in match1.group(i+1) :
							node.cg_params[i] = 0
						else :
							node.cg_params[i] = int(match1.group(i+1).replace(',', ''))
					node.cg_params[13] = node.cg_params[0] + 10 * (node.cg_params[3] + node.cg_params[4] + node.cg_params[5]) + 100 * (node.cg_params[6] + node.cg_params[7] + node.cg_params[8]) + 10 * (node.cg_params[10] + node.cg_params[12])
					node.cg_params[14] = (node.cg_params[7]) * 100 #Directly getting LL miss cycles
					node.cg_params[15] = (node.cg_params[8]) * 100
					if not incl_flag :
						application_cpu_cycles += node.cg_params[13]/cpu_scaling
					return
			return #Has been found, so return whether we updated or not after iterating over all instances of a function
		elif temp_name1 in line : #Doing this, so that when the line doesn't match exactly, we can still check the instructions to see if there is a match up. Useful, when the function names are too long. Only a limited length is captured by my tool.
			match1 = pattern1.search( line )
			#This is a dangerous line as it may cause much debugging headaches later. This usually happens when the callgrind output is mismatched in that enough variables haven't been captured/too many have been captured. Also, if a line starts with a "." then the match fails
			if not match1:
				print "Warning: A function from callgrind output was found in the funcinfotable, but the re.match failed!"
				return
			line_instrs = int(match1.group(1).replace(',', ''))
			if "." not in match1.group(1):
				line_instrs = int(match1.group(1).replace(',', ''))
			else : 
				return
			for node in funcnode.funcinsttable.itervalues() :
				if incl_flag :
					node_instrs = node.instrs_incl
				else :
					node_instrs = node.instrs
				if node_instrs == 0:
					#print "I'm a node and I have zero instructions!"
					return
				diff = abs(node_instrs - line_instrs) #In this case we need to use non-inclusive instructions.
				if float(diff)/min(line_instrs, node_instrs) < 0.01 :#if true, found!
					if not node.callees and not incl_flag :
						continue
					for i in range (0, 12) :
						if "." in match1.group(i+1) :
							node.cg_params[i] = 0
						else :
							node.cg_params[i] = int(match1.group(i+1).replace(',', ''))
					node.cg_params[13] = node.cg_params[0] + 10 * (node.cg_params[3] + node.cg_params[4] + node.cg_params[5]) + 100 * (node.cg_params[6] + node.cg_params[7] + node.cg_params[8]) + 10 * (node.cg_params[10] + node.cg_params[12])
					node.cg_params[14] = (node.cg_params[7]) * 100
					node.cg_params[15] = (node.cg_params[8]) * 100
					if not incl_flag :
						application_cpu_cycles += node.cg_params[13]/cpu_scaling
					return
			#Here it hasn't been found properly, so keep iterating over all funcnodes and don't simply return
			#return 
		#Closes the if temp_name in line:
	return
		
def search_for_l_in_bottom_list ( line, bottom_list ) :
	'''This function hunts to match the contents of a line to a function name in the bottom_list. 
	'''
	patString = '^\s*([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+([\d,.]+)\s+'
	pattern1 = re.compile( patString )
	#1. Iterate over nodes in bottom list
	for node in bottom_list :
		#2. Check if this is the node. First check name, then check instruction count
		temp_name = ":"+node.function_info.function_name
		temp_name = temp_name.strip()
		#if "memcpy" in temp_name :
		#	if "memcpy" in line :
		#		print "found memcpy"
		if temp_name in line :
			match1 = pattern1.search( line )
			line_instrs = int(match1.group(1).replace(',', ''))
			diff = abs(node.instrs_incl - line_instrs) #In this case we need to use inclusive instructions.
			if float(diff)/min(line_instrs, node.instrs_incl) < 0.01 :#if true, found!
				for i in range (0, 12) :
					if "." in match1.group(i+1) :
						node.cg_params[i] = 0
					else :
						node.cg_params[i] = int(match1.group(i+1).replace(',', ''))
				node.cg_params[13] = node.cg_params[0] + 10 * (node.cg_params[3] + node.cg_params[4] + node.cg_params[5]) + 100 * (node.cg_params[6] + node.cg_params[7] + node.cg_params[8]) + 10 * (node.cg_params[10] + node.cg_params[12])
				node.cg_params[14] = (node.cg_params[7]) * 100
				node.cg_params[15] = (node.cg_params[8]) * 100
				return

def calculate_metrics ( node ) :

	global mode
	global comp_speedup
	global cpu_freq
	global bandwidth
	#1. If comp_comm of the current node has not been calculated (that is, it is zero), then calculate it and store it.
	if(node.comp_comm_uniq == 0) :
		if (node.ipcomm_incl_uniq + node.opcomm_incl_uniq) != 0 :
			node.comp_comm_uniq = (node.flops_incl + node.iops_incl)/float(node.ipcomm_incl_uniq + node.opcomm_incl_uniq)
		else :
			node.comp_comm_uniq = "MAX"
		       	#2. if node.comp_comm_uniq is "MAX" then there is an issue
			print "Node under main has inclusive communication costs as zero!"
			#sys.exit(1)
	if(node.comp_comm == 0) :		
		if (node.ipcomm_incl + node.opcomm_incl) != 0 :
			node.comp_comm = (node.flops_incl + node.iops_incl)/float(node.ipcomm_incl + node.opcomm_incl)
		else :
			node.comp_comm = "MAX"
			#2. if node.comp_comm_uniq is "MAX" then there is an issue
			print "Node under main has inclusive communication costs as zero!"
			#sys.exit(1)

	node.area = area_regression(node)
	node.coverage = float(node.iops_incl + node.flops_incl)/ops_total
	#node.progility = float(node.depth_index)/max_depth
	if node.cg_params[13] == 0 :
		node.cg_params[13] = node.cg_params[0] #This may or may not become a designated bottom node later on. What we are still missing is the adding of instructions to total cycles
	#node.comp_cycles_accel = node.cg_params[0]/float(comp_speedup)
	if gran_mode == 0 :
		node.metric = float('inf') #node.progility might also work
	elif gran_mode == 1 :
		node.metric = node.coverage
	else : #Covers gran mode = 2
		if (node.ipcomm_incl + node.local == 0) :
			node.metric = float('inf')
			return
		node.input_offload_time = node.cg_params[14] + node.ipcomm_incl_uniq/float(bandwidth)
		if (node.opcomm_incl + node.local == 0) :
			node.metric = float('inf')
			return
		node.output_offload_time = node.cg_params[15] + node.opcomm_incl_uniq/float(bandwidth)
		
		#Comment out the following 4 lines if they do not produce realistic output
		#if not node.input_offload_time :
			#node.input_offload_time = node.output_offload_time
		#if not node.output_offload_time :
			#node.output_offload_time = node.input_offload_time
		
		node.exec_cycles_cpu = node.cg_params[13]/cpu_scaling #
		if (node.input_offload_time + node.output_offload_time > node.exec_cycles_cpu) or not node.exec_cycles_cpu :
			node.metric = float('inf')
			return
		node.breakeven_speedup = node.exec_cycles_cpu / (node.exec_cycles_cpu - node.input_offload_time - node.output_offload_time)
		#node.breakeven_speedup_norm = node.breakeven_speedup / (node.iops_incl + node.flops_incl)
		#node.metric = node.breakeven_speedup_norm * node.area
		node.metric = node.breakeven_speedup
			
def print_node( node, caller_star, append_flag ) :

	print "%-10s %-10s %50s %s %-10s %-15d %-15d %-15d %-10lu %-10lu %-10lu %-10lu %-10lu %-10lu %-15f %-15f %-15d %-25f %-15d %-15d %-35.20f %-15d %-15d %-15d %-15d" % (node.function_number, node.funcinst_number, node.function_info.function_name, caller_star, node.num_calls, node.instrs_incl, node.flops_incl, node.iops_incl, node.ipcomm_incl_uniq, node.opcomm_incl_uniq, node.local_incl_uniq, node.ipcomm_incl, node.opcomm_incl, node.local_incl, node.comp_comm_uniq, node.comp_comm, node.exec_cycles_cpu, node.area, node.cg_params[14], node.cg_params[15], node.metric, node.input_offload_time, node.output_offload_time, node.ipcomm_true, node.local_true)
	
	#Start putting things into a list for plotting
	if append_flag :
		comm.append( node.ipcomm_incl_uniq + node.opcomm_incl_uniq )
		comp.append( node.flops_incl + node.iops_incl )
		label.append( node.function_info.function_name )

def print_node_for_gran ( node, caller_star, append_flag ) :

	print "%-10s %-10s %50s %s %-10s %-15d %-15d %-15d %-10lu %-10lu %-10lu %-10lu %-10lu %-10lu %-15s %-15s %-15d %-25f %-15d %-15d %-35.20f %-15d %-15d %-15d %-15d" % (node.function_number, node.funcinst_number, node.function_info.function_name, caller_star, node.num_calls, node.instrs_incl, node.flops_incl, node.iops_incl, node.ipcomm_incl_uniq, node.opcomm_incl_uniq, node.local_incl_uniq, node.ipcomm_incl, node.opcomm_incl, node.local_incl, str(node.comp_comm_uniq), str(node.comp_comm), node.exec_cycles_cpu, node.area, node.cg_params[14], node.cg_params[15], node.metric, node.input_offload_time, node.output_offload_time, node.ipcomm_true, node.local_true)
	
	#Start putting things into a list for plotting
	if append_flag :
		comm.append( node.ipcomm_incl_uniq + node.opcomm_incl_uniq )
		comp.append( node.flops_incl + node.iops_incl )
		label.append( node.function_info.function_name )

def final_calc_print_exectime ( bottom_list ) :
	global overall_cpu_cycles
	global application_cpu_cycles
	global bandwidth

	#print totals of various things
	print "Uppertree Software Time (Cycles): %-20f" % (application_cpu_cycles)
	for node in bottom_list :
		if (node.ipcomm_incl + node.local == 0) :
			node.input_offload_time = 0
		else :
			node.input_offload_time = node.cg_params[14] + node.ipcomm_incl_uniq/float(bandwidth)
		if (node.opcomm_incl + node.local == 0) :
			node.output_offload_time = 0
		else :
			node.output_offload_time = node.cg_params[15] + node.opcomm_incl_uniq/float(bandwidth)
			
		#Comment out the following 4 lines if they do not produce realistic output
		#if not node.input_offload_time :
			#node.input_offload_time = node.output_offload_time
		#if not node.output_offload_time :
			#node.output_offload_time = node.input_offload_time	
			
		overall_cpu_cycles += node.cg_params[13]/cpu_scaling 
		application_cpu_cycles += node.cg_params[13]/cpu_scaling #Only for the bottom nodes does this param have an inclusive cost
	print "Bottom nodes Software Time (Cycles): %-20f" % (overall_cpu_cycles)
	print ""
	
def compare_granularity_metric ( node1, node2 ) :

	#Try our new metric
	if node1.metric < node2.metric :
	#Default metric is to simply compare ratios
#	if node1.comp_comm_uniq > node2.comp_comm_uniq :
		return 1
	return 0
	
def main() :

	global printcallees
	global percentofinst
	global instrs_total
	global ops_total
	global comm
	global comp
	global label
	global max_depth
	global comp_speedup
	global bandwidth
	global cpu_freq
	global overall_cpu_cycles
	global application_cpu_cycles
	global gran_mode
	global cpu_scaling
	filename_dru = "sigil.datareuse.out-1.gz"

	usage = "usage: %prog [options] file\n Will only work for single-threaded (serial) runs at the moment."
	parser = OptionParser( usage )
	#parser.add_option("--file",action="store",type="string",default="sigil.totals.out-1",help='Sigil data file. Default will look for "sigil.totals.out-1"')
	parser.add_option("--printcallees",action="store_true",default=0,help='If this option is given, when printing the communication and computation costs for nodes, data for callees of functions will also be printed.')
	parser.add_option("--percent-of-inst", action="store", type="float", dest="percentofinst", default=0.01, help='If the number of instructions executed in a node is less than some percent of total instructions, specified by this command line option, post-processing discards these nodes. Default: 0.01 (%). 0 < value < 100')
	parser.add_option("--trim-tree", action="store_true", dest="trim_tree", default=0, help='When this option is given, the script can also run the simple demonstrative metric to produce a trimmed control data flow graph with the nodes at the bottom as accelerator candidates. Please see the README, for the exact set of requirements to use this mode.')
	parser.add_option("--gran-mode",action="store",type="string",default="nogran",help='Applicable only when --trim-tree is given, this option specifies what metric to use when trimming the tree. The available metrics are "nogran" which tells the script not to trim or modify the tree at all, "coarsegran" which favors compute over communication and "metric" which uses the simple demonstrative metric (break-even speedup) mentioned in the paper. Default is "nogran"')
	parser.add_option("--bandwidth", action="store", type="float", dest="bandwidth", default=16, help='Applicable only when --trim-tree is given. Specifies the bandwidth in Gigabytes/Second of the theoretical bus used to connect a CPU and accelerators. We assume a CPU with contemporary operating frequency of 2.5GHz, and convert the bandwidth to be in bytes/cycle. The default bandwidth is an optimistic 16GB/s in order to conservatively limit the impact of communication')
	parser.add_option("--cgfile",action="store",type="string",default="callgrind.out.*",help='Applicable only when --trim-tree is given. Specifies the callgrind file to be used for trimming the tree. Please see the README for why this is needed')
	parser.add_option("--cpu-scaling",action="store",type="int",default=1,help='Applicable only when --trim-tree is given. Specifies a blanket scaling factor by which to scale all calculated cpu cycles. This is intended to roughly model the effect of using more/less powerful CPUs')
	
	(options, args) = parser.parse_args()
	if len(args) != 1 :
		print "Script takes just one positional argument. Please provide Sigil data file."
		parser.print_help()
		sys.exit(1)
	else :
		filename = args[0]
	# if (options.file == "") :
		# print "[E] provide valid file"
		# parser.print_help()
		# sys.exit(1)
	# else :
		# filename = options.file
	
	if options.percentofinst < 0 or options.percentofinst > 100 :
		print "Please provide a valid value for --percent-of-inst. Should be greater than 0 and less than 100 "
		parser.print_help()
		sys.exit(1)
	else :
		percentofinst = options.percentofinst
	
	if options.trim_tree :
		mode = 2
		if "nogran" in options.gran_mode :
			gran_mode = 0
		elif "coarsegran" in options.gran_mode :
			gran_mode = 1
		elif "metric" in options.gran_mode :
			gran_mode = 2
		else :
			print "Unknown gran-mode!"
			parser.print_help()
			sys.exit(1)
		if options.bandwidth < 1 or options.bandwidth > 512 :
			print "Please provide a reasonable/valid bandwidth number in GB/s"
			parser.print_help()
			sys.exit(1)
		else :
			bandwidth = float(options.bandwidth)/2.5
		if options.cgfile == "" :
			print "Please provide a valid Callgrind file"
			parser.print_help()
			sys.exit(1)
		else :
			callgrind_filename = options.cgfile
		if options.cpu_scaling > 8 or options.cpu_scaling < 1 : #Would we have a cpu scaling factor of more than 8.
			print "Please provide a valid cpu scaling number"
			parser.print_help()
			sys.exit(1)
		else :
			cpu_scaling = options.cpu_scaling
	else :
		mode = 0
	
	funcinfotable = {} #Declare the funcinfotable so that it can be passed into the constructor for a funcinst. It will do what is necessary
	funcinstroot = None
	bottom_list = []
	
	if mode == 0 or mode == 2 :
		funcinstroot = readfromfile( filename, funcinfotable )

		#i) Search for main from the root
		main_node = search_for_main ( funcinstroot )
		if main_node == 0 :
			print "Main node not found in calltree!"
			sys.exit(1)

		#Calculates and stores the costs
		calculate_clusive_costs ( main_node, funcinfotable, 0 )

		print "%-50s %s %-10s %-15s %-15s %-15s %-10s %-10s %-10s %-10s %-10s %-10s %-15s %-15s %-15s %-25s %-15s %-15s" % ("Function name", " ", "Numcalls", "Instrs", "Flops", "Iops", "Ipcomm_uq", "Opcomm_uq", "Local_uq", "Ipcomm", "Opcomm", "Local", "Comp_Comm_uniq", "Comp_Comm", "Est. Cyc", "Est. Area", "Est. Dlmr time", "Est. Dlmw time")
		print ""
		instrs_total = main_node.instrs_incl
		ops_total = main_node.flops_incl + main_node.iops_incl
		
		if options.printcallees :
			print_recurse_cost( main_node, 1 )
		else :
			print_recurse_cost( main_node, 0 )
		
		if mode == 2 : #For granularity experiment
			print ""
			print ""
			print "Trimming tree....."

			#Now let us parse an original run of programs using Callgrind and (first call to generate it, then run callgrind_annotate and parse the output.
			#Look for :function_name. If found, then thats our guy. Make an execution time estimation calculation for him.
			#Per our original assumption, there should be no repeated names anyway. For each line we'll need to compare against the Ir to see if its the right line.
			#They need to match approx, so if there are multiple call instances, try to see if the difference divided by one of them is less than 0.01.
			#If there are too many funcinstances, sort them by instructions and then try only the first few

			#1. Next lets do the entire calltree for inclusive costs
			cg_anno = "perl /archgroup/archtools/Profilers/valgrind-3.7_original/callgrind/callgrind_annotate --inclusive=yes --threshold=100 " + callgrind_filename
			temp = subprocess.Popen( cg_anno, shell=True, stdout=subprocess.PIPE )
			for l in temp.stdout :
				if re.match( r'.*:\w+', l ) :
					search_for_l_in_cg ( l, funcinfotable, 1 )
			
			#Reduce tree by granularity
			#iii) Reduce the number of nodes by granularity for each callee of main. Could modify this to include main
			for x in main_node.callees :
				reduce_tree_bygran ( x, bottom_list )
			
			bottom_list.sort(key=lambda x: x.metric)
			
		
			#2. Next lets do the rest of the calltree where self costs are needed
			cg_anno = "perl /archgroup/archtools/Profilers/valgrind-3.7_original/callgrind/callgrind_annotate --threshold=100 " + callgrind_filename
			temp = subprocess.Popen( cg_anno, shell=True, stdout=subprocess.PIPE )
			for l in temp.stdout :
				if re.match( r'.*:\w+', l ) :
					search_for_l_in_cg ( l, funcinfotable, 0 )

			print "%-10s %-10s %-50s %s %-10s %-15s %-15s %-15s %-10s %-10s %-10s %-10s %-10s %-10s %-15s %-15s %-15s %-25s %-15s %-15s %-35s %-15s %-15s %15s %15s" % ("Func num", "Func Inst","Function name", " ", "Numcalls", "Instrs", "Flops", "Iops", "Ipcomm_uq", "Opcomm_uq", "Local_uq", "Ipcomm", "Opcomm", "Local", "Comp_Comm_uniq", "Comp_Comm", "Est. Cyc", "Est. Area", "Est. Dlmr time", "Est. Dlmw time", "Der. metr","ip_offload","op_offload", "data-ru-ipcomm", "data-ru-local")
			print ""
			final_calc_print_exectime ( bottom_list )

			##Now we can print, or plot or both
			if options.printcallees :
				print_recurse_cost( main_node, 1 )
			else :
				print_only_bottom ( bottom_list )

main()
