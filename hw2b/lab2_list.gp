#! /usr/bin/gnuplot
# stolen from hw2a

# general plot parameters
set terminal png
set datafile separator ","

# how many threads/iterations we can run without failure (w/o yielding)
set title "List-1: Throughput vs Number of threads for 1000 operations"
set xlabel "Number of threads"
set logscale x 2
set xrange [0.75:]
set ylabel "Throughput (operations/second)"
set logscale y 10
set output 'lab2b_1.png'

plot \
     "< grep 'list-none-m,[0-9]\\+,1000,1,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
        title 'mutex' with linespoints lc rgb 'red', \
     "< grep 'list-none-s,[0-9]\\+,1000,1,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title 'spinlock' with linespoints lc rgb 'green'


set title "List-2: Mutex wait time and time per operation"
set xlabel "Threads"
set logscale x 2
set xrange [0.75:]
set ylabel "Time (ns)"
set logscale y 10
set output 'lab2b_2.png'
# note that unsuccessful runs should have produced no output
plot \
     "< grep 'list-none-m,[0-9]\\+,1000,1,' lab2b_list.csv" \
        using ($2):($7) \
        title 'time per op' with linespoints lc rgb 'red', \
     "< grep 'list-none-m,[0-9]\\+,1000,1,' lab2b_list.csv" \
        using ($2):($8) \
        title 'mutex wait time' with linespoints lc rgb 'green'

set title "List-3: Successful Iterations that run without failure (4 lists)"
set xrange [0.75:17]
set xlabel "Threads"
set ylabel "Successful iterations"
set logscale y 10
set yrange [0.75:100]
set output 'lab2b_3.png'
plot \
    "< grep 'list-id-none,[0-9]\\+,[0-9]\\+,4,' lab2b_list.csv" using ($2):($3)\
	with points lc rgb "red" title "unprotected", \
    "< grep 'list-id-m,[0-9]\\+,[0-9]\\+,4,' lab2b_list.csv" using ($2):($3) \
	with points lc rgb "green" title "mutex protected", \
    "< grep 'list-id-s,[0-9]\\+,[0-9]\\+,4,' lab2b_list.csv" using ($2):($3) \
	with points lc rgb "blue" title "spinlock protected"

set title "List-4: Throughput vs Number of Lists for Mutex Synchronization"
set xlabel "Threads"
set logscale x 2
unset xrange
set xrange [0.75:]
set ylabel "Throughput (operations/second)"
set logscale y
unset yrange
set output 'lab2b_4.png'
plot \
     "< grep 'list-none-m,[0-9][2]\\?,1000,1,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '1 list' with linespoints lc rgb 'red', \
     "< grep 'list-none-m,[0-9][2]\\?,1000,4,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '4 lists' with linespoints lc rgb 'orange', \
     "< grep 'list-none-m,[0-9][2]\\?,1000,8,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '8 lists' with linespoints lc rgb 'blue', \
     "< grep 'list-none-m,[0-9][2]\\?,1000,16,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '16 lists' with linespoints lc rgb 'pink'

set title "List-5: Throughput vs Number of Lists for Spinlock Synchronization"
set xlabel "Threads"
set logscale x 2
set ylabel "Throughput (operations/second)"
set logscale y
set output 'lab2b_5.png'
plot \
     "< grep 'list-none-s,[0-9][2]\\?,1000,1,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '1 list' with linespoints lc rgb 'red', \
     "< grep 'list-none-s,[0-9][2]\\?,1000,4,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '4 lists' with linespoints lc rgb 'orange', \
     "< grep 'list-none-s,[0-9][2]\\?,1000,8,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '8 lists' with linespoints lc rgb 'blue', \
     "< grep 'list-none-s,[0-9][2]\\?,1000,16,' lab2b_list.csv" \
        using ($2):(1000000000/($7)) \
	title '16 lists' with linespoints lc rgb 'pink'