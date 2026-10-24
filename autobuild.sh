#!/bin/bash
#########################################################################
# File Name: autobuild.sh
# Author: shi lei
# mail: shitou3000@qq.com
# Created Time: 2020年02月23日 星期日 18时40分28秒
#########################################################################

set -x

rm -rf `pwd`/build/*
mkdir -p `pwd`/build
cd `pwd`/build &&
	cmake .. &&
	make
