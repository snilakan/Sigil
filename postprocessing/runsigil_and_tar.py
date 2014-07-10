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
import subprocess
import pipes

def main() :
	#vg_in_place = "/archgroup/archtools/Profilers/valgrind-3.7_modified/valgrind-3.7/vg-in-place"
	vg_in_place = "/archgroup/archtools/Profilers/valgrind-3.7_modified/valgrind-3.7/vg-in-place"
	if len(sys.argv) > 1:
		#print [vg_in_place,sys.argv]
		vg_in_place_args = sys.argv
		#Remove the arg which has the name of this script itself
		vg_in_place_args.pop(0)
		#Prepend the vg_in_place executable itself
		vg_in_place_args.insert(0,vg_in_place)
	else:
		print "Please enter arguments for Sigil."
		sys.exit(0)
	tar_cmd = "tar czf sigil.events.out.tar.gz sigil.events.out-*"
	rm_cmd = "rm sigil.events.out-*"

	#If using shell=False (default) need arguments and executable to be a list
	sigil_run = subprocess.call(vg_in_place_args) #subprocess.call automatically waits for this to finish
	#If using shell=True (default) need arguments to be a single string and need to wait for it to finish explicitly
	tar_run = subprocess.Popen(tar_cmd, shell=True, stdout=subprocess.PIPE)
	tar_run.wait()
	rm_run = subprocess.Popen(rm_cmd, shell=True, stdout=subprocess.PIPE)
	rm_run.wait()
main()

# def main() :
# 	vg_in_place = "/archgroup/archtools/Profilers/valgrind-3.7_modified/valgrind-3.7/vg-in-place"
# 	if len(sys.argv) > 1:
# 		#print [vg_in_place,sys.argv]
# 		vg_in_place_args = sys.argv
# 		#Remove the arg which has the name of this script itself
# 		vg_in_place_args.pop(0)
# 		#Prepend the vg_in_place executable itself
# 		vg_in_place_args.insert(0,vg_in_place)
# 		vg_in_place_args = " ".join(pipes.quote(s) for s in vg_in_place_args)
# 	else:
# 		print "Please enter arguments for Sigil."
# 		sys.exit(0)
# 	tar_cmd = "tar czf sigil.events.out.tar.gz sigil.events.out-*"
# 	rm_cmd = "rm sigil.events.out-*"

# 	sigil_run = subprocess.Popen(vg_in_place_args, shell=True, stdout=subprocess.PIPE)
# 	sigil_run.wait()
# 	tar_run = subprocess.Popen(tar_cmd, shell=True, stdout=subprocess.PIPE)
# 	tar_run.wait()
# 	rm_run = subprocess.Popen(rm_cmd, shell=True, stdout=subprocess.PIPE)
# 	rm_run.wait()
# main()
