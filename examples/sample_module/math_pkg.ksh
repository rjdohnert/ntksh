# math_pkg.ksh - Sample ntksh script package
# Imported via: module import math_pkg

function math_square {
    typeset -i n=$1
    echo $(( n * n ))
}

function math_cube {
    typeset -i n=$1
    echo $(( n * n * n ))
}

MATH_PKG_LOADED=1
export MATH_PKG_LOADED
