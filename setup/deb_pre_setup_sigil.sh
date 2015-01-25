#!/bin/bash
#make sure you have aclocal,autoheader,automake,autoconf,gcc or g++ if the package is not installed, it will install it
echo "PLEASE USE AT YOUR OWN RISK, the script needs SUDO PERMISSIONS. PLEASE CHECK the CONTENT of SCRIPT"
read -p "Do you want to proceed? " -n 1 -r
echo    # (optional) move to a new line
if [[ $REPLY =~ ^[Yy]$ ]]
then
    # do dangerous stuff


#sudo apt-get install git
echo "git status is checked"
if [ $(dpkg-query -W -f='${Status}' git 2>/dev/null | grep -c "ok installed") -eq 0 ]; then
  apt-get install git;
fi


#sudo apt-get install auto-apt
echo "auto-apt status is checked"

if [ $(dpkg-query -W -f='${Status}' auto-apt 2>/dev/null | grep -c "ok installed") -eq 0 ]; then
  apt-get install auto-apt;
fi


#sudo auto-apt update # search what packages would provide the header file
#sudo apt-get install autoconf  # creates a configuration script for a package from a template file 

echo "automake status is checked"

if [ $(dpkg-query -W -f='${Status}' automake 2>/dev/null | grep -c "ok installed") -eq 0 ]; then
  apt-get install automake;
fi


echo "autoconf status is checked "
if [ $(dpkg-query -W -f='${Status}' autoconf 2>/dev/null | grep -c "ok installed") -eq 0 ]; then
  apt-get install autoconf;
fi


# you can also install libtool package from Free Software Foundation


echo "python-dev is checked"
if [ $(dpkg-query -W -f='${Status}' python-dev 2>/dev/null | grep -c "ok installed") -eq 0 ];
then
  apt-get install python-dev;
fi

if [[ ! $(python -V 2>&1 | grep "Python 2") ]]
then
  python -V
  echo "WARNING!!!"
  echo "Post processing script only works with python 2.x"
fi


fi
