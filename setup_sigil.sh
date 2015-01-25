#!/bin/bash

echo -e "\n#####################################################"
echo -e "#################### Sigil Setup ####################"
echo -e "#####################################################\n"

unset USER_DIR
sigil_path=$(pwd)

lineheader=">>>>>"
valgrind="valgrind-3.10.1"
if [ ! $(ls | grep $valgrind) ]
then
	echo -e "$lineheader Could not find '$valgrind'!"
	read -p "$lineheader Specify sigil directory [$sigil_path]:" USER_DIR 
fi

if ! [ -z "$USER_DIR" ]
then
  sigil_path=$USER_DIR
fi


####################################
# Build valgrind 3.10.1 with sigil #
####################################
cd $sigil_path/valgrind-3.10.1/
./autogen.sh && ./configure && make -j6 --quiet
if ! [ $? == 0 ]
then
	echo "$lineheader ERROR| Sigil build failed!"
	exit
fi
echo "$lineheader Sigil build complete!"

################################
# Modify post processing paths #
################################
echo "$lineheader Modifying Sigil postprocessing paths"
postprocessing_path="$sigil_path/postprocessing/aggregate_costs_gran.py"
oldvalgrind="valgrind-3.7"

callgrind_annotate_old="/archgroup/archtools/Profilers/$oldvalgrind""_original/"
callgrind_annotate_new="$sigil_path/$valgrind/"
sed -i "s|$callgrind_annotate_old|$callgrind_annotate_new|" $postprocessing_path

callgrind_annotate_inclusive_old="/archgroup/archtools/Profilers/$oldvalgrind""_original/"
callgrind_annotate_inclusive_new="$sigil_path/$valgrind/"
sed -i "s|$callgrind_annotate_inclusive_old|$callgrind_annotate_inclusive_new|" $postprocessing_path

echo "$lineheader Sigil setup complete"

#########################
#  Example using sigil  #
#########################
unset yn
read -p "$lineheader Would you like to run the example tests? [Y/n]:" yn 
if ! [ "$yn" == "" ] && ! $(echo $yn | grep -i "^y$\|^yes$" >/dev/null)
then
	echo "$lineheader All done!"
	exit
fi

echo "$lineheader Running example..."

exe_file_path="$sigil_path/sigil_test"
g++ $exe_file_path/sigil_test.c -o $exe_file_path/test_math

$sigil_path/$valgrind/vg-in-place --tool=callgrind --sigil-tool=yes --separate-callers=100 --cache-sim=yes --drw-func=yes $exe_file_path/test_math #--drw-events=yes,
$sigil_path/$valgrind/vg-in-place --tool=callgrind --cache-sim=yes --branch-sim=yes --callgrind-out-file=callgrind_out $exe_file_path/test_math
$sigil_path/postprocessing/aggregate_costs_gran.py sigil.totals.out-1 --trim-tree --cgfile=callgrind_out --gran-mode=metric > $exe_file_path/postprocessing_result_math
