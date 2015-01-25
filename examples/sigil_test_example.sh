#!/bin/bash

################################################################################################
#                                Sigil Path
################################################################################################

sigil_path="$sigil_path/home/DREXEL/pm626/vg10_4/"
export sigil_path

##############################################################################################
#                         Executable_File_Path
##############################################################################################


exe_file_path="$exe_file_path/home/DREXEL/pm626/Downloads/function/math"
export exe_file_path

#############################################################################################
#				Creates Post Processing Path 
#############################################################################################


postprocessing_path="$postprocessing_path $sigil_path/sigil/postprocessing"
export postprocessing_path



############################################################################################
#					Runs Sigil
############################################################################################


$sigil_path/sigil/valgrind-3.10.1/vg-in-place --tool=callgrind --sigil-tool=yes --separate-callers=100 --cache-sim=yes --drw-func=yes $exe_file_path #--drw-events=yes,

$sigil_path/sigil/valgrind-3.10.1/vg-in-place --tool=callgrind --cache-sim=yes --branch-sim=yes --callgrind-out-file=callgrind_out $exe_file_path

$postprocessing_path/aggregate_costs_gran.py sigil.totals.out-1 --trim-tree --cgfile=callgrind_out --gran-mode=metric >postprocessing_result
