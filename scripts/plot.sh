#!/bin/sh
gnuplot -p -e "plot \"${1}_co\" with lines ,\"${1}_clo\" with lines ,\"${1}_so\" with lines, \"${1}_ci\" with lines, \"${1}_cli\" with lines;"
