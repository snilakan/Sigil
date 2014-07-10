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
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import cm
import commands
import subprocess
import glob as bashls
import os

#Global variables

class benchmark_data :

	def __init__ ( self, name, benchmark_name, size_name ) :
		self.name = name
		self.benchmark_name = benchmark_name
		self.size_name = size_name
		self.gran_datatable = {} #These should accept objects of type gran_data. One for each different working set
		self.plot_object = None
		
class gran_data :

	def __init__( self, gran_metric ) :
		self.gran_metric = gran_metric
		self.bottom_cpu_cycles = 0
		self.bottom_hw_cycles = 0
		self.uppertree_cpu_cycles = 0
		self.bottom_speedup = 0
		self.application_speedup = 0
		self.func_datatable = []
		
class func_data :

	def __init__( self, match ) :
		self.function_name = match.group(1)
		self.num_calls = int(match.group(2))
		self.instrs = int(match.group(3))
		self.flops = int(match.group(4))
		self.iops = int(match.group(5))
		self.ipcomm_uq = int(match.group(6))
		self.opcomm_uq = int(match.group(7))
		self.local_uq = int(match.group(8))
		self.ipcomm = int(match.group(9))
		self.opcomm = int(match.group(10))
		self.local = int(match.group(11))
		self.comp_comm_uniq = float(match.group(12))
		self.comp_comm = float(match.group(13))
		self.est_cycle = int(match.group(14))
		self.est_area = float(match.group(15))
		self.est_dlmr_time = int(match.group(16))
		self.est_dlmw_time = int(match.group(17))
		self.metric = float(match.group(18))
		self.progility = float(match.group(19))
		self.coverage = float(match.group(20))
		self.comp_cycles_accel = float(match.group(21))

def main() :
	
	benchmark_datatable = {}
	benchmark_array = []
	blacklist = ['memset', 'memcpy', 'mempcpy', 'malloc'] #We want to throw away the parameters for these guys
	gran_metrics = [ 'coarsegran', 'nogran', '+speedup',  '+coverage', '+progility' ]
	gran_legend = [ 'coarse grain', 'fine grain', '+speedup',  '+coverage', '+programmability' ]
	patString = '^(.+)\s+\*\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\d.]+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)'
	pattern1 = re.compile( patString )
	
	fh_benchmark = open( 'benchmark_list', 'r')
	for l in fh_benchmark :
		l = l.strip()
		matchobj = re.match ( r'(.+)\-(.+)' ,l )
		if matchobj :
			benchmark_datatable[ l ] = benchmark_data( l, matchobj.group(1), matchobj.group(2) ) #Intitialize a new object for bmark
			benchmark_array.append( benchmark_datatable[ l ] )
		else :
			print "Error with the following line: " + l
			sys.exit(1)
		
	for benchmark_pointer in benchmark_datatable.itervalues() :
		benchmark = benchmark_pointer.benchmark_name
		size = benchmark_pointer.size_name
		for gran_metric in gran_metrics :
			benchmark_pointer.gran_datatable[ gran_metric ] = gran_data ( gran_metric )
		for gran_metric, gran_metric_pointer in benchmark_pointer.gran_datatable.iteritems() :
			fh_gran = open( benchmark + '/' + size + '/' + gran_metric + '_gran.out', "r" )
			section1_flag = 1
			section2_flag = 0
			section3_flag = 0
			for line in fh_gran :
				line = line.strip()
				if section1_flag == 1 :
					if "With granularity reduced" in line :
						section1_flag = 0
						section2_flag = 1
						continue
					else :
						continue
					
				if section2_flag == 1 :
					matchObj1 = re.match (r'Combined Speedup of bottom nodes: ([\d.]+)', line)
					matchObj2 = re.match (r'Overall Application Speedup: ([\d.]+)', line)
					matchObj3 = re.match (r'Uppertree Software Time \(Cycles\): ([\d.]+)', line)
					matchObj4 = re.match (r'Bottom nodes Software Time \(Cycles\): ([\d.]+)', line)
					matchObj5 = re.match (r'Bottom nodes Hardware Time \(Cycles\): ([\d.]+)', line)
					if matchObj1 :
						gran_metric_pointer.bottom_speedup = float(matchObj1.group(1))
					elif matchObj2 :
						gran_metric_pointer.application_speedup = float(matchObj2.group(1))
						section2_flag = 0
						section3_flag = 1
					elif matchObj3 :
						gran_metric_pointer.uppertree_cpu_cycles = float(matchObj3.group(1))
					elif matchObj4 :
						gran_metric_pointer.bottom_cpu_cycles = float(matchObj4.group(1))
					elif matchObj5 :
						gran_metric_pointer.bottom_hw_cycles = float(matchObj5.group(1))
							
				#if section3_flag == 1:
					#match1 = pattern1.search( line )
					#if match1 :
						#gran_metric_pointer.func_datatable.append( func_data ( match1 ) )
			
	fig = plt.figure()
	plot1 = fig.add_subplot(1,1,1)
	#plt.axhline(y=1, xmin=0, xmax=1, color='black')
	#plot!
	width = 0.30 #barwidth
	spacing = 0.15 #0.01 #spacing between columns of different benchmark-size
	N = len(gran_metrics)
	idx = np.arange(N) # the logical x locations for the bars. If we multiply this by the width we should get the actual starting points. Like 0, 1, 2, 3 with width 0.1 is 0, 0.1, 0.2, 0.3
	benchmark_size_idx = 0 #This will be a unique number for each benchmark-size. Offset index for the plot for every benchmark-size will be (N * width + spacing) * benchmark_size_idx. Then we can add idx * width to each of these
	bottom_speedup = []
	application_speedup = []
	avg_app_speedup = []
	as_plot = [] #Will be used outside scope of for loop as well
	xtick_names = []
	#Set the color for each gran_metric
	gran_colors = []
	number_metrics = len(gran_metrics)
	for i in range( 0, number_metrics ) :
		#gran_colors.append( 0.1875 * i )
		avg_app_speedup.append( 0 )
	#Put whatever is to be plotted in lists
	#for benchmark_pointer in benchmark_datatable.itervalues() :
	for benchmark_pointer in benchmark_array :
		benchmark = benchmark_pointer.benchmark_name
		del as_plot[:]
		offset = (N * width + spacing) * benchmark_size_idx
		i = 0
		for i in range (0, number_metrics) :
			gran_metric = gran_metrics[i]
			gran_metric_pointer = benchmark_pointer.gran_datatable[ gran_metric ]
			as_plot.append(plt.bar( idx[i] * width + offset, gran_metric_pointer.application_speedup, width, bottom = 0, color=cm.gray(1.*i/number_metrics ))) #Create a plot object for each section. They can be separated with specified spacing
			avg_app_speedup[i] += gran_metric_pointer.application_speedup
			i += 1
		benchmark_pointer.plot_object = as_plot
		benchmark_size_idx += 1
		xtick_names.append( benchmark )
	#Plot averages as well
	offset = (N * width + spacing) * benchmark_size_idx
	for i in range (0, number_metrics) :
		plt.bar( idx[i] * width + offset, avg_app_speedup[i] / benchmark_size_idx, width, bottom = 0, color=cm.gray(1.*i/number_metrics ))
	xtick_names.append( 'Average' )
	benchmark_size_idx += 1
	plt.legend( as_plot, gran_legend, 'upper center', ncol = 3 )
	xtick_idx = np.arange( benchmark_size_idx ) #Then we need to add intra-offset being center so 2 * width. inter-offset will be xtick_idx * (spacing + N * width)
	plt.xticks( width * 2 + xtick_idx * ( spacing + N * width ), xtick_names, rotation=50 , fontsize = 13 )
	plt.yticks( np.arange(3) )
	plt.ylabel ('Application Speedup', fontsize = 14)
	plt.xlim ( 0, benchmark_size_idx * ( N * width + spacing ) + width )
	plt.grid (b = True, which='major', )

	#plt.ylim ( 0, 8 )
	#plt.title ('Application Speedup w/accel speedup = 30')
	#plt.savefig( 'plot_speedups.pdf' )
	plt.show()
	
main()
