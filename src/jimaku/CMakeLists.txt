add_library(
    jimaku
    jimakuclient.cpp
    jimakuclient.h
    kitsunekkoclient.cpp
    kitsunekkoclient.h
)
target_compile_features(jimaku PUBLIC cxx_std_20)
target_compile_options(jimaku PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(jimaku PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    jimaku
    PRIVATE context
    PRIVATE libzip::libzip
    PRIVATE utils
    PUBLIC Qt6::Core
    PUBLIC Qt6::Network
)
