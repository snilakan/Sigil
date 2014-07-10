#! /usr/bin/python

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
import commands
import shlex
import subprocess
import pipes
from operator import attrgetter
import numpy as np
import matplotlib.pyplot as plt

#Global variables
bottomtime = []
uppertime = []
labels = []
gran_table = [] #Flat list of functions from all workloads with no weights. Only to be used to find common functions
common_funcs = {} #Flat list of common functions

class systemnodeinfo :
	def __init__( self, name ) :
		self.name = name #Refers to a particular benchmark
		self.table = {} #This table could hold other system nodes of benchmarks
		self.time = 0
		self.weight = 0

class benchmarkinfo :
	def __init__( self, name ) :
		self.name = name #Refers to a particular benchmark
		self.gran_table = []
		self.bottom_nodes_softwaretime = 0
		self.uppertree_softwaretime = 0
		
class funcgraninfo :
	def __init__( self, name, numcalls, instrs, flops, iops, ipcomm_uniq, opcomm_uniq, local_uniq, ipcomm, opcomm, local, comp_comm_uniq, comp_comm, est_cyc, est_area, dlmr, dlmw, metric, ip_offload, op_offload, benchmark) :
		self.name = name #Refers to a particular function
		self.numcalls = int(numcalls)
		self.instrs = int(instrs)
		self.flops = int(flops)
		self.iops = int(iops)
		self.local = int(local)
		self.local_uniq = int(local_uniq)
		self.ipcomm = int(ipcomm)
		self.ipcomm_uniq = int(ipcomm_uniq)
		self.opcomm = int(opcomm)
		self.opcomm_uniq = int(opcomm_uniq)
		self.comp_comm_uniq = float(comp_comm_uniq)
		self.comp_comm = float(comp_comm)
		self.software_time = int(est_cyc)
		self.est_area = float(est_area)
		self.dlmr = int(dlmr)
		self.dlmw = int(dlmw)
		self.metric = float(metric)
		self.ip_offload = int(ip_offload)
		self.op_offload = int(op_offload)
		self.benchmark = benchmark
		self.common_updated_flag = 0
		self.breakeven_speedup = float(self.software_time)/(self.software_time - self.ip_offload - self.op_offload)
		self.breakeven_speedup_norm = self.breakeven_speedup / (self.iops + self.flops)
		self.metric = self.breakeven_speedup_norm * self.est_area
	def scale( self, factor) :
		self.instrs *= factor
		self.flops *= factor
		self.iops *= factor
		self.local *= factor
		self.local_uniq *= factor
		self.ipcomm *= factor
		self.ipcomm_uniq *= factor
		self.opcomm *= factor
		self.opcomm_uniq *= factor
		self.software_time *= factor
		self.est_area *= factor
		self.dlmr *= factor
		self.dlmw *= factor
		self.ip_offload *= factor
		self.op_offload *= factor
		self.breakeven_speedup = float(self.software_time)/(self.software_time - self.ip_offload - self.op_offload)
		self.breakeven_speedup_norm = self.breakeven_speedup / (self.iops + self.flops)
		self.metric = self.breakeven_speedup_norm * self.est_area		
	def recalculate_area( self ) :
		self.est_area = 2.41e5 - 1.98 * (self.flops + self.iops)/float(self.numcalls) + 28.277 * (self.local_uniq + self.ipcomm_uniq + self.opcomm_uniq)/float(self.numcalls)
		
class commonfuncgraninfo :
	def __init__( self, node) :
		self.name = node.name #Refers to a particular function
		self.numcalls = node.numcalls
		self.instrs = node.instrs
		self.flops = node.flops
		self.iops = node.iops
		self.local = node.local
		self.local_uniq = node.local_uniq
		self.ipcomm = node.ipcomm
		self.ipcomm_uniq = node.ipcomm_uniq
		self.opcomm = node.opcomm
		self.opcomm_uniq = node.opcomm_uniq
		self.comp_comm_uniq = node.comp_comm_uniq
		self.comp_comm = node.comp_comm
		self.software_time = node.software_time
		self.est_area = node.est_area
		self.dlmr = node.dlmr
		self.dlmw = node.dlmw
		self.ip_offload = node.ip_offload
		self.op_offload = node.op_offload
		self.benchmark = []
		self.benchmark.append(node.benchmark)
		self.breakeven_speedup = float(self.software_time)/(self.software_time - self.ip_offload - self.op_offload)
		self.breakeven_speedup_norm = self.breakeven_speedup / (self.iops + self.flops)
		self.metric = self.breakeven_speedup_norm * self.est_area
	def update( self, node) :
		#Name will be updated manually
		self.numcalls += node.numcalls
		self.instrs += node.instrs
		self.flops += node.flops
		self.iops += node.iops
		self.local += node.local
		self.local_uniq += node.local_uniq
		self.ipcomm += node.ipcomm
		self.ipcomm_uniq += node.ipcomm_uniq
		self.opcomm += node.opcomm
		self.opcomm_uniq += node.opcomm_uniq
		self.comp_comm_uniq = (self.flops + self.iops)/float(self.ipcomm_uniq + self.opcomm_uniq)
		self.comp_comm = (self.flops + self.iops)/float(self.ipcomm+ self.opcomm)
		self.software_time += node.software_time
		self.est_area += node.est_area
		self.dlmr += node.dlmr
		self.dlmw += node.dlmw
		self.ip_offload += node.ip_offload
		self.op_offload += node.op_offload
		self.benchmark.append(node.benchmark)
		self.breakeven_speedup = float(self.software_time)/(self.software_time - self.ip_offload - self.op_offload)
		self.breakeven_speedup_norm = self.breakeven_speedup / (self.iops + self.flops)
		self.metric = self.breakeven_speedup_norm * self.est_area

def usage() :
	print "Usage:"
	print "\t" + sys.argv[0] + " [<file>]"
	print "\t Continuing with default options - file: gran.out"
	print ""

def print_node_for_gran ( node, caller_star, append_flag ) :

	print "%-50s %-30s %-10s %-15d %-15d %-15d %-10lu %-10lu %-10lu %-10lu %-10lu %-10lu %-15f %-15f %-15d %-25f %-15d %-15d %-35.20f %-15d %-15d" % (node.name, node.benchmark.name, node.numcalls, node.instrs, node.flops, node.iops, node.ipcomm_uniq, node.opcomm_uniq, node.local_uniq, node.ipcomm, node.opcomm, node.local, node.comp_comm_uniq, node.comp_comm, node.software_time, node.est_area, node.dlmr, node.dlmw, node.metric, node.ip_offload, node.op_offload)
	
	#Start putting things into a list for plotting
	if append_flag :
		comm.append( node.ipcomm_incl_uniq + node.opcomm_incl_uniq )
		comp.append( node.flops_incl + node.iops_incl )
		label.append( node.function_info.function_name )
		
def read_gran_file(benchmark,filename) :
	global gran_table
	start_flag = 0
	#Pattern to wait for, before starting
	patString = "^Uppertree Software Time \(Cycles\): ([\d.]+)"
	pattern1 = re.compile( patString )
	patString = "^Bottom nodes Software Time \(Cycles\): ([\d.]+)"
	pattern2 = re.compile( patString )
	patString = "^(.+)\s+\*\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\d.]+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+(\d+)\s+(\d+)"
	pattern3 = re.compile( patString )
	fh = open( benchmark.name+"/"+filename, "r" )
	for l in fh :
		l = l.strip() #Removes leading and ending whitespaces (we should have none in our output)
		if not start_flag :
			#1. Keep iterating until you find the necessary line and then set the start_flag to start storing gran information
			match1 = pattern1.search( l )
			match2 = pattern2.search( l )
			if match1 :
				benchmark.uppertree_softwaretime = float(match1.group(1))
			elif match2 :
				benchmark.bottom_nodes_softwaretime = float(match2.group(1))
				start_flag = 1
		else :
			#2. Start flag is set, so search for our favourite pattern
			match3 = pattern3.search( l )
			if match3 :
				functiongraninfotemp = funcgraninfo(match3.group(1),match3.group(2),match3.group(3),match3.group(4),match3.group(5),match3.group(6),match3.group(7),match3.group(8),match3.group(9),match3.group(10),match3.group(11),match3.group(12),match3.group(13),match3.group(14),match3.group(15),match3.group(16),match3.group(17),match3.group(18),match3.group(19),match3.group(20),benchmark)
				if functiongraninfotemp.software_time - functiongraninfotemp.ip_offload - functiongraninfotemp.op_offload >= 0 :
					benchmark.gran_table.append(functiongraninfotemp)
					gran_table.append(functiongraninfotemp)
	#Recalculate bottom time based on read in numbers
	benchmark.bottom_nodes_softwaretime = 0
	for node in benchmark.gran_table :
		benchmark.bottom_nodes_softwaretime += node.software_time				

def print_node_for_gran_commonfunc ( node, caller_star, append_flag ) :
	print "%50s for benchmarks: " % (node.name)
	for temp_benchmark in node.benchmark : 
		print "%15s" % (temp_benchmark.name)
	print "%-10s %-15d %-15d %-15d %-10lu %-10lu %-10lu %-10lu %-10lu %-10lu %-15f %-15f %-15d %-25f %-15d %-15d %-35.20f %-15d %-15d" % (node.numcalls, node.instrs, node.flops, node.iops, node.ipcomm_uniq, node.opcomm_uniq, node.local_uniq, node.ipcomm, node.opcomm, node.local, node.comp_comm_uniq, node.comp_comm, node.software_time, node.est_area, node.dlmr, node.dlmw, node.metric, node.ip_offload, node.op_offload)
	print ""
	
	#Start putting things into a list for plotting
	if append_flag :
		comm.append( node.ipcomm_incl_uniq + node.opcomm_incl_uniq )
		comp.append( node.flops_incl + node.iops_incl )
		label.append( node.function_info.function_name )				

def search_print_commonality( gran_table ) :
	#Check for commonality in a flat list of functions
	for x in range(0, len(gran_table)):
		for y in range(x+1, len(gran_table)):
			#Check for complete commonality in the name by simply checking that both subsume each other! This will prevent random matchups that are a possibility
			if gran_table[x].name in gran_table[y].name and gran_table[y].name in gran_table[x].name :
				#check if the name is present in any of the keys for the common func
				key = gran_table[x].name
				if key in common_funcs.keys() :
					if not gran_table[x].common_updated_flag :
						common_funcs[key].update(gran_table[x])
						gran_table[x].common_updated_flag = 1
					if not gran_table[y].common_updated_flag :
						common_funcs[key].update(gran_table[y])
						gran_table[y].common_updated_flag = 1
				else :
					common_funcs[key] = commonfuncgraninfo(gran_table[x])
					gran_table[x].common_updated_flag = 1
					if not gran_table[y].common_updated_flag : #This condition should always succeed
						common_funcs[key].update(gran_table[y])
						gran_table[y].common_updated_flag = 1
					else :
						print "Error!"
						sys.exit(1)
	#Print out common functions also
	print ""
	for common_func in common_funcs.itervalues() :
		print_node_for_gran_commonfunc ( common_func, "*", 0 )
	
def sortbymetric_and_print( gran_table ) :
	#Sort the list by order of sum of flops and iops OR sort by "the metric"
	#gran_table.sort(key=lambda x: (x.flops + x.iops), reverse=True)
	gran_table.sort(key=lambda x: (x.metric), reverse=False)
	print "%-50s %-30s %-10s %-15s %-15s %-15s %-10s %-10s %-10s %-10s %-10s %-10s %-15s %-15s %-15s %-25s %-15s %-15s %-35s %-15s %-15s" % ("Function name", "Benchmark name", "Numcalls", "Instrs", "Flops", "Iops", "Ipcomm_uq", "Opcomm_uq", "Local_uq", "Ipcomm", "Opcomm", "Local", "Comp_Comm_uniq", "Comp_Comm", "Est. Cyc", "Est. Area", "Est. Dlmr time", "Est. Dlmw time", "Der. metr","ip_offload","op_offload")
	#Print nodes based 
	for node in gran_table :
		print_node_for_gran ( node, "*", 0 )

def sortbyamdahl_and_print( gran_table, overall_time ) :	
	#Sort by the simple Amdahl's law metric as we don't care so much area, ops etc. this time around. We just want to cull away functions
	# that do not contribute much to the application in terms of speedup. Since this is done on the flat list, it does not have any real bearing
	gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/overall_time), reverse=True)
	print "%-50s %-30s %-10s %-15s %-15s %-15s %-10s %-10s %-10s %-10s %-10s %-10s %-15s %-15s %-15s %-25s %-15s %-15s %-35s %-15s %-15s" % ("Function name", "Benchmark name", "Numcalls", "Instrs", "Flops", "Iops", "Ipcomm_uq", "Opcomm_uq", "Local_uq", "Ipcomm", "Opcomm", "Local", "Comp_Comm_uniq", "Comp_Comm", "Est. Cyc", "Est. Area", "Est. Dlmr time", "Est. Dlmw time", "Der. metr","ip_offload","op_offload")
	#Print nodes
	for node in gran_table :
		print_node_for_gran ( node, "*", 0 )		

def apply_weights_and_select( weights, rootnode, max_benchmark_swtime ) :
	for workload_class,weight in weights.iteritems() :
		rootnode.table[workload_class].weight = weight
	for workload_class in rootnode.table.itervalues() :
		workload_class.time = max_benchmark_swtime * workload_class.weight #Even if we go through and accumulate times this is what we will end up with
		for benchmark in workload_class.table.itervalues() :
			for node in benchmark.gran_table :
				node.scale(workload_class.weight/len(workload_class.table)) #Scale each node in each benchmark of the workload class. This will cause overall weightage to change. Weight each benchmark differently based on how many benchmarks in a class
			benchmark.uppertree_softwaretime *= workload_class.weight
			benchmark.bottom_nodes_softwaretime *= workload_class.weight
		rootnode.time += workload_class.time
		
def plot_bar_comm_red ( gran_table ) :
	#PLOT bargraph for i/p, o/p communicaton coverage of software times post granularity here. We want to find the top function in each benchmark and plot the plot its unique to non-unique
	# communication. Less data-reuse means they are better accelerator candidates. Think of all the cache you will be saving.
	covered_benchmarks = []
	func_from_benchmark = [] #Will have the unique communication normalized
	redundant_comm = [] #Will have the unique communication normalized
	del labels[:]
	#Iterate to fill at least one function from each benchmark and find its ratio
	for node in gran_table :
		if node.benchmark.name not in covered_benchmarks :
			covered_benchmarks.append(node.benchmark.name)
			unique_comm = (node.ipcomm_uniq + node.opcomm_uniq + node.local_uniq)/(node.ipcomm + node.opcomm + node.local)
			non_unique_comm = 1 - unique_comm
			func_from_benchmark.append(unique_comm)
			redundant_comm.append(non_unique_comm)
			labels.append(node.benchmark.name.replace('/simsmall', ''))
	fig2 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot2 = fig2.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	idx = np.arange(len(covered_benchmarks))
	bottom = plt.bar(idx+width/3., func_from_benchmark, width, color='r')
	upper = plt.bar(idx+width/3., redundant_comm, width, color='y',bottom=func_from_benchmark)
	#plt.xticks(idx+width/2., labels, rotation=45 )
	plot2.set_xticks(idx+width/2.)
	plot2.set_xticklabels( labels, rotation=30, size=12 )
	plt.yticks( np.arange(0,1.0,0.2), size=12 )
	plt.ylabel('Normalized function-level communication', size=12)
	#plot2.set_yticks(np.arange(0,81,10))
	plt.legend((upper[0],bottom[0]),("Redundant Communication","Unique Communication"),prop={'size':12})
	#plt.show()
	plt.savefig("red_comm.pdf")
		
def main() :
	if len( sys.argv ) < 2 :
		filename = "gran.out"
		usage()
	else :
		filename = sys.argv[1]
	benchmarks = [] #Central list of benchmarks!
	rootnode = systemnodeinfo("root")
	#Pattern to detect workload classes
	patString = "^(.+)-(.+)"
	pattern1 = re.compile( patString )
	fh = open( "benchmark_list_classes", "r")
	for l in fh :
		match1 = pattern1.search( l.strip() )
		if match1:
			if "#" not in match1.group(1) : #Leave out commented ones
				#Create node under root for this class if not already created
				if match1.group(2) not in rootnode.table.keys() :
					rootnode.table[match1.group(2)] = systemnodeinfo(match1.group(2))
				rootnode.table[match1.group(2)].table[match1.group(1)] = benchmarkinfo(match1.group(1)) #For a particular class, for a particular benchmark put in its things
				benchmarks.append(rootnode.table[match1.group(2)].table[match1.group(1)])
	
	#Benchmarks can be populated in one go here, even though the object is also referenced in the rootnode. 
	max_benchmark_swtime = 0
	for benchmark in benchmarks :
		if "#" in benchmark.name :
			continue
		read_gran_file(benchmark,filename)
		if max_benchmark_swtime < benchmark.uppertree_softwaretime + benchmark.bottom_nodes_softwaretime :
			max_benchmark_swtime = benchmark.uppertree_softwaretime + benchmark.bottom_nodes_softwaretime
		#For plotting
		temp_sum = benchmark.uppertree_softwaretime + benchmark.bottom_nodes_softwaretime
		uppertime.append(benchmark.uppertree_softwaretime/float(temp_sum))
		bottomtime.append(benchmark.bottom_nodes_softwaretime/float(temp_sum))
		labels.append(benchmark.name.replace('/simsmall', ''))
	
	#Sort by the simple Amdahl's law metric as we don't care so much area, ops etc. this time around. We just want to cull away functions
	# that do not contribute much to the application in terms of speedup. Since this is done on the flat list, it does not have any real bearing
	# # # print "\nSort by (soft-comm/app.soft). For equal weightage"
	# # # gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/(x.benchmark.bottom_nodes_softwaretime + x.benchmark.uppertree_softwaretime)), reverse=True)
	# # # print "%-50s %-30s %-10s %-15s %-15s %-15s %-10s %-10s %-10s %-10s %-10s %-10s %-15s %-15s %-15s %-25s %-15s %-15s %-35s %-15s %-15s" % ("Function name", "Benchmark name", "Numcalls", "Instrs", "Flops", "Iops", "Ipcomm_uq", "Opcomm_uq", "Local_uq", "Ipcomm", "Opcomm", "Local", "Comp_Comm_uniq", "Comp_Comm", "Est. Cyc", "Est. Area", "Est. Dlmr time", "Est. Dlmw time", "Der. metr","ip_offload","op_offload")
	# # # #Print nodes
	# # # for node in gran_table :
		# # # print_node_for_gran ( node, "*", 0 )
	# # # print ""
	
	#Check commonality - need to keep the flat list of graninfo for each benchmark alive to execute this step
	search_print_commonality( gran_table )
	#PLOT bargraph for i/p, o/p communicaton unique normalized to overall
	plot_bar_comm_red( gran_table )
	
main()
