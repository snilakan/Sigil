#!/bin/bash
#running sigil_pre_setup script to prepare system

unset USER_DIR
export sigil_path=~/sigil/

line_header=">>>>>"
read -p "$line_header Specify the Sigil install directory [$sigil_path]:" USER_DIR 

if ! [ -z "$USER_DIR" ]
then
  sigil_path=$USER_DIR
fi
#eval sigil_path=$sigil_path
echo $sigil_path
mkdir $sigil_path
export sigil_path=$sigil_path
cd $sigil_path
#downloading sigil
git clone dragon:/archgroup/projects/sigil.git

#making sigil
cd $sigil_path/sigil/valgrind-3.7/

./autogen.sh
./configure
make

#make sure you add the scripts path to your command lines when running sigil
#this part will setup the post processing scripts

#this is for sed added the bash and aggregate_costs_gran
postprocessing_path="$postprocessing_path $sigil_path/sigil/postprocessing/aggregate_costs_gran.py"
export postprocessing_path

#where sigil's callgrind_annotate which is in valgrind-3.7_original/callgrind/callgrind_annotate 
callgrind_annotate_path="$callgrind_annotate_path $sigil_path/sigil/valgrind-3.7/callgrind"
export callgrind_annotate_path

#change the path to where your post-processing script is
callgrind_annotate_new='cg_anno = "perl $sigil_path/sigil/valgrind-3.7/callgrind/callgrind_annotate --threshold=100 " + callgrind_filename'
callgrind_annotate_inclusive_new='cg_anno = "perl $sigil_path/sigil/valgrind-3.7/callgrind/callgrind_annotate --inclusive=yes --threshold=100 " + callgrind_filename'


#don't change anything from here it is what it was in old script
callgrind_annotate_old='cg_anno = "perl /archgroup/archtools/Profilers/valgrind-3.7_original/callgrind/callgrind_annotate --threshold=100 " + callgrind_filename'
callgrind_annotate_inclusive_old='cg_anno = "perl /archgroup/archtools/Profilers/valgrind-3.7_original/callgrind/callgrind_annotate --inclusive=yes --threshold=100 " + callgrind_filename'



#echo callgrind_annotate_new

sed -i 's|'"$callgrind_annotate_old"'|'"$callgrind_annotate_new"'|' $sigil_path/sigil/postprocessing/aggregate_costs_gran.py
sed -i 's|'"$callgrind_annotate_inclusive_old"'|'"$callgrind_annotate_inclusive_new"'|' $postprocessing_path


#echo 'the test program is running'

cd
export exe_file_path="$exe_file_path $sigil_path/sigil/sigil_test"
cd $exe_file_path
g++ $exe_file_path/sigil_test.c -o $exe_file_path/test_math

$sigil_path/sigil/valgrind-3.7/vg-in-place --tool=callgrind --sigil-tool=yes --separate-callers=100 --cache-sim=yes --drw-func=yes $exe_file_path/test_math #--drw-events=yes,

$sigil_path/sigil/valgrind-3.7/vg-in-place --tool=callgrind --cache-sim=yes --branch-sim=yes --callgrind-out-file=callgrind_out $exe_file_path/test_math

$sigil_path/sigil/postprocessing/aggregate_costs_gran.py sigil.totals.out-1 --trim-tree --cgfile=callgrind_out --gran-mode=metric > $exe_file_path/postprocessing_result_math


