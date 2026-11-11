# 每次构建生成编译标记（git hash + UTC 时间），供开始界面显示
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${LB_SRC}
                OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT HASH)
    set(HASH "nogit")
endif()
string(TIMESTAMP NOW "%Y-%m-%d %H:%M" UTC)
file(WRITE ${LB_OUT} "#pragma once\n#define LB_GIT_HASH \"${HASH}\"\n#define LB_BUILD_TIME \"${NOW}\"\n")
