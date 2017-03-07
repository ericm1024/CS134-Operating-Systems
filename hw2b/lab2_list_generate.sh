#!/bin/bash
# NAME: Eric Mueller
# EMAIL: emueller@hmc.edu
# ID: 40160869

FILE=lab2b_list.csv
rm -f $FILE

# generate data for lab2_list-1.png and lab2_list-2.png
for nthreads in 1 2 4 8 12 16 24
do
    for sop in m s
    do
        ./lab2_list --iterations=1000 --sync=$sop --threads=$nthreads >> $FILE
    done
done

# generate data for lab2_list-3.png
for nthreads in 1 4 8 12 16
do
    # no sync
    for niters in 1 2 4 8 16
    do
        ./lab2_list --iterations=$niters --threads=$nthreads --yield=id \
                    --lists=4 >> $FILE 2>/dev/null || true
    done
    # sync
    for niters in 10, 20, 40, 80
    do
        for sop in s m
        do
            ./lab2_list --iterations=$niters --threads=$nthreads --yield=id \
                    --sync=$sop --lists=4 >> $FILE
        done
    done
done

# generate data for lab2_list-{4,5}.png
for nthreads in 1 2 4 8 12
do
    for nlists in 4 8 16
    do
        for sop in s m
        do
            ./lab2_list --iterations=1000 --threads=$nthreads --sync=$sop \
                        --lists=$nlists >> $FILE
        done
    done
done
