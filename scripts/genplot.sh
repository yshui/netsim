#!/bin/sh
analyzer/analyze $1 node_type_speed cloud out $2 > "${1}_co"
analyzer/analyze $1 node_type_speed cloud in $2 > "${1}_ci"
analyzer/analyze $1 node_type_speed server out $2 > "${1}_so"
analyzer/analyze $1 node_type_speed client in $2 > "${1}_cli"
analyzer/analyze $1 node_type_speed client out $2 > "${1}_clo"
analyzer/analyze $1 stale_client > "${1}_sc"
