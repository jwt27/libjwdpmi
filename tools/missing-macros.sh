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
	[[ -v map[$name] ]] || echo -n "-DHAVE$name=$value" | tr '\r\n' ' '
done < <( echo | "$@" -x c++ -dM -E - )
