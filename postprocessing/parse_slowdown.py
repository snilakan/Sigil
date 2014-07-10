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
import commands
import subprocess

patString = 'Usr\s+(\d+)\s+(\d+):(\d+):(\d+),\s+Sys\s+(\d+)\s+(\d+):(\d+):(\d+)\s+\-\s+Total Remote Usage'
pattern1 = re.compile( patString )

class benchmark_data :

	def __init__ ( self, benchmark ) :
		self.benchmark = benchmark
		self.days = self.cg_days = 0
		self.hours = self.cg_hours = 0
		self.minutes = self.cg_days = 0
		self.seconds = self.cg_seconds = 0
	def parse_cg ( self, line) :
		match1 = pattern1.search( line )
		if match1:
			self.cg_days = int(match1.group(1)) + int(match1.group(5))
			self.cg_hours = int(match1.group(2)) + int(match1.group(6))
			self.cg_minutes = int(match1.group(3)) + int(match1.group(7))
			self.cg_seconds = int(match1.group(4)) + int(match1.group(8))
	def parse ( self, line) :
		match1 = pattern1.search( line )
		if match1:
			self.days = int(match1.group(1)) + int(match1.group(5))
			self.hours = int(match1.group(2)) + int(match1.group(6))
			self.minutes = int(match1.group(3)) + int(match1.group(7))
			self.seconds = int(match1.group(4)) + int(match1.group(8))
	def slowdown ( self ) :
		temp1 = self.days * 86400 + self.hours * 3600 + self.minutes * 60 + self.seconds
		temp2 = self.cg_days * 86400 + self.cg_hours * 3600 + self.cg_minutes * 60 + self.cg_seconds
		if temp2 == 0:
			temp2 = 5
		return temp1/temp2
def main() :

	results = []

	aggregate_costs = 'grep Usr *.log | grep Sys | grep "Total Remote Usage"'
	parse_cg_serial = 'grep Usr pure_cg_serial.log | grep Sys | grep "Total Remote Usage"'
	parse_serial = 'grep Usr serial.log | grep Sys | grep "Total Remote Usage"'
	
	fh = open( "benchmark_list", "r")
	for l in fh :
		if "#" not in l :
			results.append(benchmark_data(l.strip()))
		
	for result in results :
		exec_command = None
		temp1 =  subprocess.Popen(parse_cg_serial, cwd = result.benchmark, shell=True, stdout=subprocess.PIPE)
		for l in temp1.stdout :
			result.parse_cg(l.strip())
		temp1 =  subprocess.Popen(parse_serial, cwd = result.benchmark, shell=True, stdout=subprocess.PIPE)
		for l in temp1.stdout :
			result.parse(l.strip())
	for result in results :
		print result.benchmark + " - slowdown: " + str(result.slowdown())
main()
