#!/bin/bash

set -e

cpp=$(mktemp -t tmp.XXXXXXXX.cpp)
normal=$(mktemp)
nofpu=$(mktemp)
diff=$(mktemp)

cd $(dirname $cpp)

echo "void main(){}" > $cpp

"$@" -x c++ -dM -E $cpp > $normal
"$@" -x c++ -mgeneral-regs-only -dM -E $cpp > $nofpu

diff -e $nofpu $normal > $diff || :

while read line; do
    case "$line" in
    *a) while read line && [ "$line" != "." ]; do
            line=${line#\#define }
            line=${line%% *}
            echo -n " -DHAVE${line}"
        done
        ;;
    esac
done < $diff

rm $cpp $normal $nofpu $diff

exit 0
