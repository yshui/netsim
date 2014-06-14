#!/bin/sh
time ./netsim modules/p2p
analyzer/analyze p2p_record node_type_speed cloud out $1 > p2p_record_co
analyzer/analyze p2p_record node_type_speed cloud in $1 > p2p_record_ci
analyzer/analyze p2p_record node_type_speed server out $1 > p2p_record_so
analyzer/analyze p2p_record node_type_speed client in $1 > p2p_record_cli
analyzer/analyze p2p_record node_type_speed client out $1 > p2p_record_clo
