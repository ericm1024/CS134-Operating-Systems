#!/bin/bash
# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

FILE=lab2_list.csv
rm -f $FILE

# generate data for lab2_list-1.png
for niters in 10 100 1000 10000 20000
do
    ./lab2_list --iterations=$niters >> $FILE
done

# generate data for lab2_list-2.png
for niters in 1 10 100 1000
do
    for nthreads in 2 4 8 12
    do
        ./lab2_list --iterations=$niters --threads=$nthreads >> $FILE \
                    2>/dev/null \
            || true
    done
done
for niters in 1 2 4 8 16 32
do
    for nthreads in 2 4 8 12
    do
        for yops in i d il dl
        do
            ./lab2_list --iterations=$niters --threads=$nthreads \
                        --yield=$yops >> $FILE 2>/dev/null \
                || true
        done
    done
done

# generate data for lab2_list-3.png
for yops in i d il dl
do
    for sop in s m
    do
        ./lab2_list --iterations=32 --threads=12 --yield=$yops \
                    --sync=$sop >> $FILE
    done
done

# generate data for lab2_list-4.png
for nthreads in 1 2 4 8 12 16 24
do
    for sop in s m
    do
        ./lab2_list --iterations=1000 --threads=$nthreads --sync=$sop >> $FILE
    done
done
