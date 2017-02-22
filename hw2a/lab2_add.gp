#! /usr/bin/gnuplot
#
# purpose:
#	 generate data reduction graphs for the multi-threaded add project
#
# input: lab2_add.csv
#	1. test name
#	2. # threads
#	3. # iterations per thread
#	4. # add operations
#	5. run time (ns)
#	6. run time per operation (ns)
#	7. total sum at end of run (should be zero)
#
# output:
#	lab2_add-1.png ... threads and iterations that run (unprotected) w/o failure
#	lab2_add-2.png ... cost per operation of yielding
#	lab2_add-3.png ... cost per operation vs number of iterations
#	lab2_add-4.png ... threads and iterations that run (protected) w/o failure
#	lab2_add-5.png ... cost per operation vs number of threads
#
# Note:
#	Managing data is simplified by keeping all of the results in a single
#	file.  But this means that the individual graphing commands have to
#	grep to select only the data they want.
#

# general plot parameters
set terminal png
set datafile separator ","

# how many threads/iterations we can run without failure (w/o yielding)
set title "Add-1: Threads and Iterations that run without failure"
set xlabel "Threads"
set logscale x 2
set xrange [0.75:]
set ylabel "Iterations per thread"
set logscale y 10
set output 'lab2_add-1.png'

# grep out only successful (sum=0)  unsynchronized runs
plot \
     "< grep add-none lab2_add.csv | grep ',0$'" using ($2):($3) \
	title 'w/o yields' with points lc rgb 'green', \
     "< grep add-yield-none lab2_add.csv | grep ',0$'" using ($2):($3) \
	title 'w/yields' with points lc rgb 'red'

# per operation times vs # threads/iterations w/ and w/o yielding
set title "Add-2: The cost of yielding"
set xlabel "Iterations"
set logscale x 10
set xrange [1:]
set ylabel "Cost per operation (ns)"
set logscale y 10
set output 'lab2_add-2.png'

plot \
     "< grep 'add-none,2,' lab2_add.csv" using ($3):($6) \
		title 'Threads=2, w/o yields' with points lc rgb 'green', \
     "< grep 'add-yield-none,2,' lab2_add.csv" using ($3):($6) \
		title 'Threads=2, w/yields' with points lc rgb 'red', \
     "< grep 'add-none,8,' lab2_add.csv" using ($3):($6) \
		title 'Threads=8, w/o yields' with points lc rgb 'green', \
     "< grep 'add-yield-none,8,' lab2_add.csv" using ($3):($6) \
		title 'Threads=8, w/yields' with points lc rgb 'red'

# testing artifact vs number of iterations
set title "Add-3: per operation cost vs number of iterations"
set xlabel "Iterations"
set logscale x 10
set ylabel "Cost per operation (ns)"
set logscale y 10
set output 'lab2_add-3.png'
plot "< grep 'add-none,1,' lab2_add.csv" using ($3):($6) \
	title 'Threads=1, w/o yields' with points


set title "Add-4: Threads and Iterations that run without failure"
set xlabel "Threads"
set logscale x 2
set xrange [0.75:]
set ylabel "Iterations per thread"
set logscale y 10
set output 'lab2_add-4.png'
# grep out only successful (sum=0) yield runs
plot \
     "< grep add-yield-none lab2_add.csv | grep '0$'" using ($2):($3) \
	title 'unprotected w/yields' with points lc rgb 'red', \
     "< grep add-yield-c lab2_add.csv | grep '0$'" using ($2):($3) \
	title 'CAS w/yields' with points lc rgb 'green', \
     "< grep add-yield-m lab2_add.csv | grep '0$'" using ($2):($3) \
	title 'mutex w/yields' with points lc rgb 'blue', \
     "< grep add-yield-s lab2_add.csv | grep '0$'" using ($2):($3) \
	title 'spin w/yields' with points lc rgb 'orange'

set title "Add-5: per operation cost vs number of threads"
set xlabel "Threads"
set logscale x 2
set xrange [0.75:]
set ylabel "cost per operation(ns)"
set logscale y 10
set output 'lab2_add-5.png'
set key left top
# grep out only successful (sum=0) yield runs
plot \
     "< grep 'add-none,1,10000,' lab2_add.csv" using ($2):($6) \
	title 'unprotected' with linespoints lc rgb 'red', \
     "< grep add-c lab2_add.csv" using ($2):($6) \
	title 'CAS' with linespoints lc rgb 'green', \
     "< grep add-m lab2_add.csv" using ($2):($6) \
	title 'mutex' with linespoints lc rgb 'blue', \
     "< grep add-s lab2_add.csv" using ($2):($6) \
	title 'spin' with linespoints lc rgb 'orange', \
     "< grep add-a lab2_add.csv" using ($2):($6) \
	title 'atomic' with linespoints lc rgb 'purple'
