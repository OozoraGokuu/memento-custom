add_library(
    context
    context.cpp
    context.h
    localsubtitleclient.cpp
    localsubtitleclient.h
    immersionstats.cpp
    immersionstats.h
    lookuphistory.cpp
    lookuphistory.h
    videoexporter.cpp
    videoexporter.h
)
target_compile_features(context PRIVATE cxx_std_20)
target_include_directories(context PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_compile_options(context PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_link_libraries(
    context
    PUBLIC anilist
    PUBLIC anki
    PUBLIC audioplayer
    PUBLIC dictionary
    PUBLIC episodefolder
    PUBLIC fileopenhandler
    PUBLIC keytracker
    PUBLIC jimaku
    PUBLIC migaku
    PUBLIC mpvplayer
    PUBLIC torrentsearch
    PUBLIC Qt6::Core
    PRIVATE Qt6::Concurrent
    PUBLIC settings
    PUBLIC subtitle
    PUBLIC utils
)
