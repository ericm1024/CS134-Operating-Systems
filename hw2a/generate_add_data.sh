#!/bin/sh

FILE=lab2_add.csv
rm -f $FILE

# generate lab2_add-1.png
for niters in 100, 1000, 10000, 100000

for nthreads in 2 4 8 12
do
    for niters in 10 20 40 80 100 1000 10000 100000
    do
        ./lab2_add --threads=$nthreads --iterations=$niters --yield >> $FILE
    done
done
