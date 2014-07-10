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
		self.bottom_nodes_partial_swtime = 0
		self.uppertree_softwaretime = 0
		self.area_table = []
		self.accel_comp_100x = 0 #These numbers represent the total time of the area-constrained selected hardware nodes (offload + compute) 
		self.accel_comp_1000x = 0
		self.accel_comp_infinite = 0
		self.bottom_nodes_softwaretime = 0
		
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
	patString = "^(.+)\s+\*\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\-\d.]+)\s+(\d+)\s+(\d+)\s+([\-\d.]+)\s+(\d+)\s+(\d+)"
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
				node.scale(workload_class.weight) #Scale each node in each benchmark of the workload class. This will cause overall weightage to change
			benchmark.uppertree_softwaretime *= workload_class.weight
			benchmark.bottom_nodes_softwaretime *= workload_class.weight
		rootnode.time += workload_class.time
		
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
		if "simsmall" in benchmark.name :
			labels.append(benchmark.name.replace('/simsmall', ''))
		else :
			labels.append(benchmark.name.replace('/simmedium', ''))
	
	#PLOT the bargraph for coverage of software times post granularity here. This will be useful to show that have covered a good portion of each application using our metric as well
	# while having minimized communication and area/op. "While our metric explicitly seeks to minimze communication and area/op, let us see what happens to coverage of our application"
	# Leads naturally to a better coverage, but only by picking the nodes with less communication and more computation
	
	fig1 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot1 = fig1.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	idx = np.arange(len(benchmarks))
	bottom = plt.bar(idx+width/3., bottomtime, width, color='r')
	upper = plt.bar(idx+width/3., uppertime, width, color='y',bottom=bottomtime)
	plt.xticks(idx+width/2., labels, rotation=45, size=12 )
	plt.yticks( np.arange(0,1.0,0.2), size=12 )
	plt.ylabel('Normalized Software Time', size=12)
	fig1.subplots_adjust(bottom=0.3)
	#plt.yticks(np.arange(0,81,10))
	plt.legend((upper[0],bottom[0]),("Software functions","Accel candidates"),prop={'size':11},loc=4)
	plt.savefig("post_gran_cov.pdf")
	
	#Area constrained selection
	#A1. Variable for plotting
	benchmark_speedup_100x = []
	benchmark_speedup_1000x = []
	benchmark_speedup_yerr = []
	benchmark_speedup_yerr_neg = []
	idx = np.arange(len(benchmarks))
	benchmark_labels = []
	#A2. For each benchmarkgo through and order functions by amdahl and constrain area to 2/3rds the total.
	for benchmark in benchmarks :
		total_area = 0
		#a. recalculate area as it is based on an incorrect estimate. This should not affect the granularity selection though because we would have scaled that by function calls anyway
		for node in benchmark.gran_table :
			node.recalculate_area()
			total_area += node.est_area
		#b. Sort by software - comm time normalized to benchmark time, because we are evaluating speedup in just the benchmark context
		benchmark.gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/(benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)), reverse=True)
		#c. Now add functions until we reach the area constraint
		area_constraint = 2 * total_area/3.0
		area = 0
		for node in benchmark.gran_table :
			if (area + node.est_area) < area_constraint and node.est_area > 0: #calculate total accelerator time for a benchmark based on each node in the area_table being computationally spedup by 100x and 1000x
				area += node.est_area
				benchmark.area_table.append(node)
				benchmark.accel_comp_100x += (node.software_time/100 + node.ip_offload + node.op_offload)
				benchmark.accel_comp_1000x += (node.software_time/1000 + node.ip_offload + node.op_offload)
			else : #Store off the bottom nodes that are not selected in a separate softwaretiem
				benchmark.bottom_nodes_partial_swtime += node.software_time
		#d. Add speedups into a list for plotting
		speedup_100x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.accel_comp_100x + benchmark.bottom_nodes_partial_swtime + benchmark.uppertree_softwaretime)
		speedup_1000x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.accel_comp_1000x + benchmark.bottom_nodes_partial_swtime + benchmark.uppertree_softwaretime)
		benchmark_speedup_100x.append(speedup_100x)
		benchmark_speedup_1000x.append(speedup_1000x)
		benchmark_speedup_yerr.append(speedup_1000x - speedup_100x)
		benchmark_speedup_yerr_neg.append(speedup_100x - 1)
		if "simsmall" in benchmark.name :
			benchmark_labels.append(benchmark.name.replace('/simsmall', ''))
		else :
			benchmark_labels.append(benchmark.name.replace('/simmedium', ''))
	#A3. PloT!
	#Merge two lists into numpy array
	#yerr = np.array([benchmark_speedup_yerr,benchmark_speedup_yerr_neg])
	#yerr.reshape
	fig2 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot2 = fig2.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	bottom = plt.bar(idx+width/3., benchmark_speedup_100x, width, color='r', yerr=benchmark_speedup_yerr)
	#bottom = plt.bar(idx+width/3., benchmark_speedup_100x, width, color='r', yerr=[benchmark_speedup_yerr_neg,benchmark_speedup_yerr])
	plot2.set_xticks(idx+width/2.)
	plot2.grid('on')
	plt.ylim(ymax=5)
	plt.text(4.2,5,str(int(benchmark_speedup_100x[4])))
	plot2.set_xticklabels( labels, rotation=30, size=12 )
	fig2.subplots_adjust(bottom=0.2)
	plt.ylabel('Application Speedup', size=12)
	#plt.legend((upper[0],bottom[0]),("Redundant Communication","Unique Communication"),prop={'size':12})
	plt.savefig("area_constrained_speedup.pdf")
	#plt.show()
	
	#Study to relax area constraints
	#B1. Variables
	benchmark_speedup_100x =[]
	benchmark_speedup_1000x = []
	benchmark_speedup_yerr = []
	benchmark_speedup_yerr_neg = []
	idx = np.arange(len(benchmarks))
	benchmark_labels = []
	#B2. 
	for benchmark in benchmarks :
		benchmark.bottom_nodes_partial_swtime = 0
		benchmark.accel_comp_100x = 0 #These numbers represent the total time of the area-constrained selected hardware nodes (offload + compute) 
		benchmark.accel_comp_1000x = 0
		#a. Sort by software - comm time normalized to benchmark time, because we are evaluating speedup in just the benchmark context
		benchmark.gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/(benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)), reverse=True)
		#b. Now add functions until we reach the area constraint
		for node in benchmark.gran_table :
			if	(node.software_time/100 + node.ip_offload + node.op_offload) < node.software_time and node.est_area > 0 :
				benchmark.accel_comp_100x += (node.software_time/100 + node.ip_offload + node.op_offload)
				benchmark.accel_comp_1000x += (node.software_time/1000 + node.ip_offload + node.op_offload)
			else :	
				benchmark.bottom_nodes_partial_swtime += node.software_time
		#c. Add speedups into a list for plotting
		speedup_100x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.accel_comp_100x + benchmark.bottom_nodes_partial_swtime + benchmark.uppertree_softwaretime)
		speedup_1000x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.accel_comp_1000x + benchmark.bottom_nodes_partial_swtime + benchmark.uppertree_softwaretime)
		benchmark_speedup_100x.append(speedup_100x)
		benchmark_speedup_1000x.append(speedup_1000x)
		benchmark_speedup_yerr.append(speedup_1000x - speedup_100x)
		benchmark_speedup_yerr_neg.append(speedup_100x - 1)
		if "simsmall" in benchmark.name :
			benchmark_labels.append(benchmark.name.replace('/simsmall', ''))
		else :
			benchmark_labels.append(benchmark.name.replace('/simmedium', ''))
	#B3. pLoT!
	fig3 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot3 = fig3.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	bottom = plt.bar(idx+width/3., benchmark_speedup_100x, width, color='r', yerr=benchmark_speedup_yerr)
	#plt.xticks(idx+width/2., labels, rotation=45 )
	plot3.set_xticks(idx+width/2.)
	plot3.set_xticklabels( labels, rotation=30, size=12 )
	plot3.grid('on')
	#plt.yticks( np.arange(0,5.0,1.0), size=12 )
	plt.ylim(ymax=5)
	plt.text(4.2,5,str(int(benchmark_speedup_100x[4])))
	#plt.autoscale(enable="False")
	fig3.subplots_adjust(bottom=0.2)
	plt.ylabel('Application Speedup', size=12)
	#plot2.set_yticks(np.arange(0,81,10))
	#plt.legend((upper[0],bottom[0]),("Redundant Communication","Unique Communication"),prop={'size':12})
	plt.savefig("area_unconstrained_speedup.pdf")
	
	#Study to overlap Software and Hardware completely - performance bound for completely overlapping software pieces assuming no dependencies
	#C1. Variables
	benchmark_speedup_100x =[]
	benchmark_speedup_1000x = []
	benchmark_speedup_yerr = []
	benchmark_speedup_yerr_neg = []
	idx = np.arange(len(benchmarks))
	benchmark_labels = []
	#C2.
	for benchmark in benchmarks :
		benchmark.bottom_nodes_partial_swtime = 0
		benchmark.accel_comp_100x = 0 #These numbers represent the total time of the area-constrained selected hardware nodes (offload + compute) 
		benchmark.accel_comp_1000x = 0
		#a. Sort by software - comm time normalized to benchmark time, because we are evaluating speedup in just the benchmark context
		benchmark.gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/(benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)), reverse=True)
		#b. Now add functions until we reach the area constraint
		for node in benchmark.gran_table :
			if	(node.software_time/100 + node.ip_offload + node.op_offload) < node.software_time and node.est_area > 0 :
				benchmark.accel_comp_100x += (node.software_time/100 + node.ip_offload + node.op_offload)
				benchmark.accel_comp_1000x += (node.software_time/1000 + node.ip_offload + node.op_offload)
			else :
				benchmark.bottom_nodes_partial_swtime += node.software_time
		#c. Add speedups into a list for plotting
		speedup_100x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.bottom_nodes_partial_swtime + max(benchmark.accel_comp_100x,benchmark.uppertree_softwaretime))
		speedup_1000x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.bottom_nodes_partial_swtime + max(benchmark.accel_comp_1000x,benchmark.uppertree_softwaretime))
		benchmark_speedup_100x.append(speedup_100x)
		benchmark_speedup_1000x.append(speedup_1000x)
		benchmark_speedup_yerr.append(speedup_1000x - speedup_100x)
		benchmark_speedup_yerr_neg.append(speedup_100x - 1)
		if "simsmall" in benchmark.name :
			benchmark_labels.append(benchmark.name.replace('/simsmall', ''))
		else :
			benchmark_labels.append(benchmark.name.replace('/simmedium', ''))
	#C3. pLoT!
	fig4 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot4 = fig4.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	bottom = plt.bar(idx+width/3., benchmark_speedup_100x, width, color='r', yerr=benchmark_speedup_yerr)
	#plt.xticks(idx+width/2., labels, rotation=45 )
	plot4.set_xticks(idx+width/2.)
	plot4.set_xticklabels( labels, rotation=30, size=12 )
	plot4.grid('on')
	plt.ylim(ymax=5)
	plt.text(4.2,5,str(int(benchmark_speedup_100x[4])))
	#plt.yticks( np.arange(0,1.0,0.2), size=12 )
	fig4.subplots_adjust(bottom=0.2)
	plt.ylabel('Application Speedup', size=12)
	#plot2.set_yticks(np.arange(0,81,10))
	#plt.legend((upper[0],bottom[0]),("Redundant Communication","Unique Communication"),prop={'size':12})
	plt.savefig("hwsw_overlap_speedup.pdf")

	#Study to overlap everything completely - performance bound for completely overlapping software with hardware and hardware with hardware
	#D1. Variables
	benchmark_speedup_100x =[]
	benchmark_speedup_1000x = []
	benchmark_speedup_infinite = []
	benchmark_speedup_yerr = []
	benchmark_speedup_yerr_neg = []
	idx = np.arange(len(benchmarks))
	benchmark_labels = []
	#D2.
	for benchmark in benchmarks :
		benchmark.bottom_nodes_partial_swtime = 0
		benchmark.accel_comp_100x = 0 #These numbers represent the total time of the area-constrained selected hardware nodes (offload + compute) 
		benchmark.accel_comp_1000x = 0
		benchmark.accel_comp_infinite = 0
		#a. Sort by software - comm time normalized to benchmark time, because we are evaluating speedup in just the benchmark context
		benchmark.gran_table.sort(key=lambda x: (float(x.software_time - x.ip_offload - x.op_offload)/(benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)), reverse=True)
		#b. Now add functions until we reach the area constraint
		for node in benchmark.gran_table :
			if	(node.software_time/100 + node.ip_offload + node.op_offload) < node.software_time and node.est_area > 0 :
				benchmark.accel_comp_100x += (node.software_time/100 + node.ip_offload + node.op_offload)
				benchmark.accel_comp_1000x += (node.software_time/1000 + node.ip_offload + node.op_offload)
				benchmark.accel_comp_infinite += (node.ip_offload + node.op_offload) #Infinite speedup, so complete overlap of hardware time
			else :
				benchmark.bottom_nodes_partial_swtime += node.software_time
		#c. Add speedups into a list for plotting
		speedup_100x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.bottom_nodes_partial_swtime + max(benchmark.accel_comp_100x,benchmark.uppertree_softwaretime))
		speedup_1000x = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.bottom_nodes_partial_swtime + max(benchmark.accel_comp_1000x,benchmark.uppertree_softwaretime))
		speedup_infinite = (benchmark.bottom_nodes_softwaretime + benchmark.uppertree_softwaretime)/(benchmark.bottom_nodes_partial_swtime + max(benchmark.accel_comp_infinite,benchmark.uppertree_softwaretime))
		benchmark_speedup_100x.append(speedup_100x)
		benchmark_speedup_1000x.append(speedup_1000x)
		benchmark_speedup_infinite.append(speedup_infinite)
		benchmark_speedup_yerr.append(speedup_1000x - speedup_100x)
		benchmark_speedup_yerr_neg.append(speedup_100x - 1)
		if "simsmall" in benchmark.name :
			benchmark_labels.append(benchmark.name.replace('/simsmall', ''))
		else :
			benchmark_labels.append(benchmark.name.replace('/simmedium', ''))
	#D3. pLoT!
	fig5 = plt.figure(num=None, figsize=(8, 4), dpi=200)
	plot5 = fig5.add_subplot(1,1,1)
	#plot!
	width = 0.6 #barwidth
	bottom = plt.bar(idx+width/3., benchmark_speedup_infinite, width, color='r')
	#plt.xticks(idx+width/2., labels, rotation=45 )
	plot5.set_xticks(idx+width/2.)
	plot5.set_xticklabels( labels, rotation=30, size=12 )
	plot5.grid('on')
	plt.ylim(ymax=5)
	plt.text(4.2,5,str(int(benchmark_speedup_100x[4])))
	#plt.yticks( np.arange(0,1.0,0.2), size=12 )
	fig5.subplots_adjust(bottom=0.2)
	plt.ylabel('Application Speedup', size=12)
	#plot2.set_yticks(np.arange(0,81,10))
	#plt.legend((upper[0],bottom[0]),("Redundant Communication","Unique Communication"),prop={'size':12})
	plt.savefig("hwhw_overlap_speedup.pdf")
	
	for i in range(0,len(benchmark_speedup_infinite)) :
		print benchmark_labels[i]+" "+str(benchmark_speedup_infinite[i])	
	#plt.show()
main()
