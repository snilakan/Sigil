#!/bin/bash

#!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
# USERS MUST SET THIS TO THEIR TOP SIGIL DIRECTORY !
#!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
sigil_path="."







sigil_path=$(readlink -m $sigil_path)
valgrind="valgrind-3.10.1"
export VALGRIND_LIB="$sigil_path/$valgrind/.in_place" #must be set to run valgrind
VALGRIND_exe="$sigil_path/$valgrind/coregrind/valgrind"

#check if valgrind has been built
if [ -z $sigil_path ] || [ ! -f $VALGRIND_exe ]
then
	echo "ERROR| Cannot find $VALGRIND_exe"
	echo "ERROR| Make sure Sigil/Valgrind has been built"
	echo "ERROR| Make sure 'sigil_path' is set in this script"
	echo "ERROR| ...exiting"
	exit 1
fi

sigil_opts="   --tool=callgrind\
               --cache-sim=yes\
               --separate-callers=400\
               --sigil-tool=yes\
               --drw-func=yes\
               --drw-syscalltracing=no"
               
$VALGRIND_exe $sigil_opts $@
