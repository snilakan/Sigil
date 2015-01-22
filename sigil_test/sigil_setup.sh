#!/bin/bash

unset USER_DIR
sigil_path=$(readlink -m ../)
line_header=">>>>>"
read -p "$line_header Specify the Sigil install directory [$sigil_path]:" USER_DIR 

if ! [ -z "$USER_DIR" ]
then
  sigil_path=$USER_DIR
fi

####################################################################################
#                       Build valgrind 3.10.1 with sigil                           #
####################################################################################
pushd $sigil_path/valgrind-3.10.1/
./autogen.sh
./configure
make -j5

#replace some old stuff in the postprocessing script
postprocessing_path="$sigil_path/postprocessing/aggregate_costs_gran.py"

callgrind_annotate_old="/archgroup/archtools/Profilers/valgrind-3.7_original/"
callgrind_annotate_new="$sigil_path/valgrind-3.10.1/"

callgrind_annotate_inclusive_old="/archgroup/archtools/Profilers/valgrind-3.7_original/"
callgrind_annotate_inclusive_new="$sigil_path/valgrind-3.10.1/"

sed -i.tmp "s|$callgrind_annotate_old|$callgrind_annotate_new|; s|$callgrind_annotate_inclusive_old|$callgrind_annotate_inclusive_new|" $postprocessing_path

####################################################################################
#                             Example using sigil                                  #
####################################################################################
exe_file_path="$sigil_path/sigil_test"
g++ $exe_file_path/sigil_test.c -o $exe_file_path/test_math

$sigil_path/valgrind-3.10.1/vg-in-place --tool=callgrind --sigil-tool=yes --separate-callers=100 --cache-sim=yes --drw-func=yes $exe_file_path/test_math #--drw-events=yes,
$sigil_path/valgrind-3.10.1/vg-in-place --tool=callgrind --cache-sim=yes --branch-sim=yes --callgrind-out-file=callgrind_out $exe_file_path/test_math
$sigil_path/postprocessing/aggregate_costs_gran.py sigil.totals.out-1 --trim-tree --cgfile=callgrind_out --gran-mode=metric > $exe_file_path/postprocessing_result_math
