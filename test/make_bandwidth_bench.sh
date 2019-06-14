#! /bin/bash
#
gcc -O0 -Wall -Werror bandwidth_bench.c -o bandwidth_bench -lnuma -lpthread
if [ $? -ne 0 ]; then
  echo "Compile error"
  exit
fi
#
echo "Compilation Successful!"
