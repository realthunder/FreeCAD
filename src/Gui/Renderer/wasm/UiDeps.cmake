# Install the npm dependencies of the viewer's DOM UI bundle, and pick a
# rollup that can actually run on this machine.
#
# Run at BUILD time (cmake -P), not configure time: what it decides
# depends on the state of node_modules, which the first command here is
# what creates.
#
# Expects: NPM, NODE, UI_SRC, STAMP.

# `npm ci` where there is a lock file, NOT `npm install`: install resolves
# the tree afresh and writes package-lock.json back, so a build would
# leave the source tree dirty -- npm 9 here rewrites a lock written by
# npm 11, dropping the `libc` fields it does not know, every single time.
# ci installs exactly what the lock pins and never writes it.
if(EXISTS ${UI_SRC}/package-lock.json)
    set(install_cmd ci)
else()
    set(install_cmd install)
endif()
execute_process(
    COMMAND ${NPM} ${install_cmd} --no-audit --no-fund
    WORKING_DIRECTORY ${UI_SRC}
    RESULT_VARIABLE install_rc)
if(NOT install_rc EQUAL 0)
    message(FATAL_ERROR
        "fcviewer UI: npm ${install_cmd} failed (${install_rc})")
endif()

# rollup 4 moved its hot path into a native addon, and the prebuilt Linux
# binary is linked against GLIBC_2.32 -- a distribution older than that
# (Ubuntu 20.04 carries 2.31) cannot load it at all. npm reports the
# failure as its own long-standing "optional dependencies" bug and tells
# you to reinstall, which sends you looking in entirely the wrong place;
# the real message is two levels down, in the dlopen error.
#
# So ask node whether rollup loads, rather than trying to predict it from
# the libc version, and alias rollup onto @rollup/wasm-node when it does
# not: the same bundler with that hot path compiled to WebAssembly, which
# runs anywhere. --no-save keeps the decision out of the manifest, where
# it would otherwise impose the slower bundler on every machine including
# the ones whose native build was fine.
execute_process(
    COMMAND ${NODE} -e "require('rollup')"
    WORKING_DIRECTORY ${UI_SRC}
    RESULT_VARIABLE rollup_rc
    OUTPUT_QUIET ERROR_QUIET)
if(NOT rollup_rc EQUAL 0)
    message(STATUS
        "fcviewer UI: native rollup does not load here -- using "
        "@rollup/wasm-node instead")
    execute_process(
        COMMAND ${NPM} install --no-save --no-audit --no-fund
                rollup@npm:@rollup/wasm-node@^4
        WORKING_DIRECTORY ${UI_SRC}
        RESULT_VARIABLE wasm_rc)
    if(NOT wasm_rc EQUAL 0)
        message(FATAL_ERROR
            "fcviewer UI: neither the native rollup nor @rollup/wasm-node "
            "could be installed (${wasm_rc})")
    endif()
endif()

file(TOUCH ${STAMP})
