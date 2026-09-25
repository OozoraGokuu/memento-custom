add_executable(
    memento_subtitle_tests
    test_subtitles.cpp
)
target_compile_features(memento_subtitle_tests PRIVATE cxx_std_20)
target_compile_options(memento_subtitle_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_subtitle_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_subtitle_tests
    PRIVATE Qt6::Test
    PRIVATE subtitle
)
add_test(NAME subtitle-model-and-parser COMMAND memento_subtitle_tests)
set_tests_properties(
    subtitle-model-and-parser
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(
    memento_torrentsearch_tests
    test_torrentsearch.cpp
)
target_compile_features(memento_torrentsearch_tests PRIVATE cxx_std_20)
target_compile_options(memento_torrentsearch_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_torrentsearch_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_torrentsearch_tests
    PRIVATE torrentsearch
    PRIVATE Qt6::Test
)
add_test(NAME torrentsearch-feed-parser COMMAND memento_torrentsearch_tests)

add_executable(
    memento_torrentengine_tests
    test_torrentengine.cpp
)
target_compile_features(memento_torrentengine_tests PRIVATE cxx_std_20)
target_compile_options(memento_torrentengine_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_torrentengine_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_torrentengine_tests
    PRIVATE episodefolder
    PRIVATE Qt6::Network
    PRIVATE Qt6::Test
)
add_test(NAME torrent-engine-safety COMMAND memento_torrentengine_tests)

add_executable(
    memento_jimaku_tests
    test_jimaku.cpp
)
target_compile_features(memento_jimaku_tests PRIVATE cxx_std_20)
target_compile_options(memento_jimaku_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_jimaku_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_jimaku_tests
    PRIVATE jimaku
    PRIVATE libzip::libzip
    PRIVATE Qt6::Test
)
add_test(NAME jimaku-matching COMMAND memento_jimaku_tests)

add_executable(
    memento_migaku_tests
    test_migaku.cpp
)
target_compile_features(memento_migaku_tests PRIVATE cxx_std_20)
target_compile_options(memento_migaku_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_migaku_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_migaku_tests
    PRIVATE migaku
    PRIVATE Qt6::Test
)
add_test(NAME migaku-media-guards COMMAND memento_migaku_tests)

add_executable(
    memento_playermanager_tests
    test_playermanager.cpp
)
target_compile_features(memento_playermanager_tests PRIVATE cxx_std_20)
target_compile_options(memento_playermanager_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_playermanager_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_playermanager_tests
    PRIVATE managers
    PRIVATE Qt6::Test
)
add_test(NAME player-manager-auto-pause COMMAND memento_playermanager_tests)

add_executable(
    memento_mpvtrack_tests
    test_mpvtrack.cpp
)
target_compile_features(memento_mpvtrack_tests PRIVATE cxx_std_20)
target_compile_options(memento_mpvtrack_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_mpvtrack_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(
    memento_mpvtrack_tests
    PRIVATE mpvplayer
    PRIVATE Qt6::Test
)
add_test(NAME mpvtrack-bitmap-subtitle-codecs COMMAND memento_mpvtrack_tests)

add_executable(memento_kitsunekko_tests test_kitsunekko.cpp)
target_compile_features(memento_kitsunekko_tests PRIVATE cxx_std_20)
target_compile_options(memento_kitsunekko_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_kitsunekko_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(memento_kitsunekko_tests PRIVATE jimaku Qt6::Test)
add_test(NAME kitsunekko-online-mirror COMMAND memento_kitsunekko_tests)

add_executable(memento_torrentsearch_network_tests test_torrentsearchnetworkreply.cpp)
target_compile_features(memento_torrentsearch_network_tests PRIVATE cxx_std_20)
target_compile_options(memento_torrentsearch_network_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_torrentsearch_network_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(memento_torrentsearch_network_tests PRIVATE torrentsearch Qt6::Test)
add_test(NAME torrentsearch-dns-fallback COMMAND memento_torrentsearch_network_tests)

add_executable(memento_anilist_tests test_anilist.cpp)
target_compile_features(memento_anilist_tests PRIVATE cxx_std_20)
target_compile_options(memento_anilist_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_anilist_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(memento_anilist_tests PRIVATE anilist episodefolder Qt6::Test)
add_test(NAME anilist-native-sync COMMAND memento_anilist_tests)

add_executable(memento_dictionary_tests test_dictionary.cpp)
target_compile_features(memento_dictionary_tests PRIVATE cxx_std_20)
target_compile_options(memento_dictionary_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_dictionary_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(memento_dictionary_tests PRIVATE dictionary libzip::libzip Qt6::Test)
if(WIN32)
    # Match the application's UTF-8 active code page for native library paths.
    target_sources(memento_dictionary_tests PRIVATE ../res/appicon.rc)
endif()
add_test(NAME dictionary-import-and-lookup COMMAND memento_dictionary_tests)

add_executable(memento_anki_tests test_anki.cpp)
target_compile_features(memento_anki_tests PRIVATE cxx_std_20)
target_compile_options(memento_anki_tests PRIVATE ${MEMENTO_COMPILER_FLAGS})
target_include_directories(memento_anki_tests PRIVATE ${MEMENTO_INCLUDE_DIRS})
target_link_libraries(memento_anki_tests PRIVATE context Qt6::Network Qt6::Test)
add_test(NAME anki-http-and-note COMMAND memento_anki_tests)
if(APPLE)
    # This integration test creates a real player. The macOS renderer needs
    # Cocoa's OpenGL context support; Qt's offscreen plugin cannot provide it.
    set_tests_properties(anki-http-and-note PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
else()
    set_tests_properties(anki-http-and-note PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
