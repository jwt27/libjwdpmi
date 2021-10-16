#!/bin/bash

set -e
declare -A map

while read line; do
	name=${line#\#define }
	name=${name%% *}
	map[$name]=1
done < <( echo | "$@" -x c++ -dM -E -mgeneral-regs-only - )

while read line; do
	name=${line#\#define }
	name=${name%% *}
	value=${line##* }
	[[ -v map[$name] ]] || echo "-DHAVE$name=$value" | tr '\r\n' ' ' | tr -s ' '
done < <( echo | "$@" -x c++ -dM -E - )
