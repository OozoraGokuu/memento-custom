add_library(
    torrentsearch
    torrentsearchclient.cpp
    torrentsearchclient.h
    torrentsearchnetworkreply.cpp
    torrentsearchnetworkreply.h
)

find_package(CURL REQUIRED)
find_package(LibtorrentRasterbar 2.0 REQUIRED CONFIG)
target_compile_features(torrentsearch PRIVATE cxx_std_20)
target_include_directories(torrentsearch PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_compile_options(torrentsearch PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_link_libraries(
    torrentsearch
    PRIVATE LibtorrentRasterbar::torrent-rasterbar
    PRIVATE utils
    PRIVATE CURL::libcurl
    PRIVATE Qt6::Concurrent
    PUBLIC Qt6::Core
    PUBLIC Qt6::Network
)
