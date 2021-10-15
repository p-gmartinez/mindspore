if(ENABLE_GITEE)
    set(REQ_URL "https://gitee.com/mirrors/zlib/repository/archive/v1.2.11.tar.gz")
    set(MD5 "a8f2746ec14f289580b6976e786c5e6b")
else()
    set(REQ_URL "https://github.com/madler/zlib/archive/v1.2.11.tar.gz")
    set(MD5 "0095d2d2d1f3442ce1318336637b695f")
endif()

mindspore_add_pkg(zlib
        VER 1.2.11
        LIBS z
        URL ${REQ_URL}
        MD5 ${MD5}
        CMAKE_OPTION -DCMAKE_BUILD_TYPE:STRING=Release)

include_directories(${zlib_INC})
add_library(mindspore::z ALIAS zlib::z)
