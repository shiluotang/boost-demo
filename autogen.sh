#!/usr/bin/env bash

function mksdir() {
    for d in "${@}"
    do
        if [[ ! -d ${d} ]]; then
            mkdir -p ${d}
        fi
    done
}

function main() {
    mksdir bin build-aux m4
    if [[ ! -f ./configure ]]; then
        if ! autoreconf -vfi; then
            rm -fr ./configure
            return 1
        fi
    fi
    if [[ ! -f bin/Makefile ]]; then
        if pushd bin >& /dev/null; then
            if ! ../configure; then
                rm -fr ./Makefile
                return 1
            fi
            popd >& /dev/null
        fi
    fi
    if [[ -f bin/Makefile ]]; then
        make -C bin check
    fi
}

main "${@}"
