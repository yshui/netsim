#!/bin/sh
gnuplot -p -e "set term postscript mono dashed enhanced eps;
	       set output '${1}.eps';
	       set xlabel \"Time (hr)\";set ylabel \"Bandwidth (Kbit/s)\";
	       plot \"${1}_co\" with linespoints title \"Cloud nodes outbound\",
		    \"${1}_clo\" with linespoints title \"Client nodes outbound\",
		    \"${1}_so\" with linespoints title \"Server outbound\",
		    \"${1}_ci\" with linespoints title \"Cloud nodes inbound\",
		    \"${1}_cli\" with linespoints title \"Client nodes inbound\";"
