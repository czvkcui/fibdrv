set title "Time to calculate fib(x)"
set ylabel "time(ns)"
set xlabel "nth fibnocci number"

plot "user.txt" using 3 with linespoints linewidth 1 title "kernel", \
'' using 4 with linespoints linewidth 1 title "user"
