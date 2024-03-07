#!/usr/bin/env bash

TARGET=10.1.0.1

DIR=$( dirname "$0" )
cd "${DIR}" || exit $?

PING="../../builddir/ping/ping"

report_test()
{
    echo "------------------------------------------------------------------------------"
    echo "Test  : ping" "$@" "${TARGET}"
    echo "Expect:"
    echo "  Status: $1"
    [ -n "$2" ] && echo "  Output: $2"
    echo ""
}


report_failure()
{
    local _first=Y
    
    while [ "$1" != "--" ]
    do
	if [ ${_first} = Y ]
	then	    
	    echo "FAIL: $1"
	    _first=N
	else
	    echo "      $1"
	fi
	shift
    done
}

test_ping()
{
local _e_status=$1
local _e_output="$2"
shift 2 # Drop test arguments to keep ping arguments

    report_test "${_e_status}" "${_e_output}" "$@"
    local _out=$( sudo ${PING} "$@" ${TARGET} 2>&1 | tee /dev/stderr )
    local _status=$?
    echo ""
    
    (( _status != _e_states )) && report_failure "wrong exit code, expected ${_e_status} got ${_status}" -- "$@"
    if [ -n "${_e_output}" ]
    then
	local LINE
	local _rx="" _loc _line="" _count=""
        if [[ "${_e_output}" =~ ^([0-9:]+)[|](.+)$ ]]
	then
	  _loc=${BASH_REMATCH[1]}
          _rx=${BASH_REMATCH[2]}
          [[ ${_loc} =~ ^[0-9]+[:][0-9]+$ ]] && _line=${BASH_REMATCH[1]} && _count=${BASH_REMATCH[2]}
          [[ ${_loc} =~ ^[0-9]+$ ]] && _line=${BASH_REMATCH[1]} && _count=1
	  [ -z "${_line}" ] && echo "BUG: invalid output expression" && exit 1
	else
	 _rx="${_e_output}"
         _line=1
         _count=1
	fi

	local _lineno=0

	#echo "output: $out"
	while read LINE
	do
	  _lineno=$((_linno+1))
	  #echo "test line $_lineno: $LINE"
	  if [[ ! "$LINE" =~ $_rx ]]
	  then
	      #echo "  test failed _line=$_line _count=$_count"
	      (( _lineno >= _line && _lineno < (_line+_count) )) && report_failure "unexpected output in line ${_lineno}" "expected: ${_rx}" "got     : $LINE" -- "$@"
	  fi	  
	done <<<$_out
    fi
}


test_ping 0 "^5[/]0[/][\\^]{5}\$" -c 10 -x "5s:xNm(:^_)"
test_ping 1 "^4[/]0[/][\\^]{4}\$" -c 4 -x "5s:xNm(:^_)"
