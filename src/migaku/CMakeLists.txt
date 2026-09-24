add_library(
    migaku
    migakuclient.cpp
    migakuclient.h
)
target_compile_features(migaku PUBLIC cxx_std_20)
target_compile_options(migaku PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(migaku PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    migaku
    PRIVATE "$<$<BOOL:${MEMENTO_SYSTEM_QCORO}>:QCoro::Network>"
    PRIVATE "$<$<NOT:$<BOOL:${MEMENTO_SYSTEM_QCORO}>>:QCoro6Network>"
    PRIVATE context
    PRIVATE Qt6::Concurrent
    PRIVATE utils
    PUBLIC "$<$<BOOL:${MEMENTO_SYSTEM_QCORO}>:QCoro::Coro>"
    PUBLIC "$<$<BOOL:${MEMENTO_SYSTEM_QCORO}>:QCoro::Qml>"
    PUBLIC "$<$<NOT:$<BOOL:${MEMENTO_SYSTEM_QCORO}>>:QCoro6Coro>"
    PUBLIC "$<$<NOT:$<BOOL:${MEMENTO_SYSTEM_QCORO}>>:QCoro6Qml>"
    PUBLIC Qt6::Core
    PUBLIC Qt6::Network
    PUBLIC Qt6::Gui
)
