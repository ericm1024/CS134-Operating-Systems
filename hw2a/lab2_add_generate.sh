#!/bin/sh

FILE=lab2_add.csv
rm -f $FILE

# generate data for lab2_add-{1,2}.png
for nthreads in 1 2 4 6 8 12
do
    for niters in 10 20 40 80 100 1000 10000 100000
    do
        ./lab2_add --threads=$nthreads --iterations=$niters >> $FILE
        ./lab2_add --threads=$nthreads --iterations=$niters --yield >> $FILE
    done
done

# generate data for lab2_add-3.png
for niters in 10 100 1000 10000 100000
do
    ./lab2_add --threads=1 --iterations=$niters >> $FILE
done

# generate data for lab2_add-4.png
for nthreads in 2 4 6 8 12
do
    for niters in 1000 10000
    do
        for method in "s" "m" "c"
        do 
            ./lab2_add --threads=$nthreads --iterations=$niters \
                       --sync=$method --yield >> $FILE
        done
    done
done

# generate data for lab2_add-5.png
for nthreads in 1 2 4 6 8 12
do
    niters=100000
    for method in "s" "m" "c" "a"
    do 
        ./lab2_add --threads=$nthreads --iterations=$niters \
                   --sync=$method >> $FILE
    done
done

./lab2_add.gp
