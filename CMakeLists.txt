cmake_minimum_required(VERSION 3.5)

project(stu_lacc LANGUAGES C)

#dir retVal
SET(SRC_ALL)
file( GLOB_RECURSE _tmp_list src/*.h src/*.c )
foreach(f ${_tmp_list})
    if(NOT f MATCHES "arm64/*" AND NOT f MATCHES "lacc.c")
        #message(STATUS "file: ${f}")
        list(APPEND SRC_ALL ${f})
    endif()
endforeach()

function(add_recursive arg)
    #message(STATUS "ARGN: ${ARGN}")
    #message(STATUS "ARGC: ${ARGC}")
    #list(LENGTH ARGV  argv_len)
    #list(GET ARGV 0 argv_value)
   # set(dir ${argv_value})

    file( GLOB_RECURSE _tmp_list ${arg}/*.h ${arg}/*.c )
    foreach(f ${_tmp_list})
        if(NOT f MATCHES "arm64/*")
            message(STATUS "file: ${f}")
            list(APPEND SRC_ALL ${f})
        endif()
    endforeach()
endfunction()

include_directories(include/)

file(GLOB_RECURSE HEADERS  include/*.h)

add_executable(lacc
    ${HEADERS}
    ${SRC_ALL}
    src/lacc.c
    )
#target_compile_definitions(lacc PUBLIC -DAMALGAMATION)

