add_executable(mangly_tests
  test_main.cpp
  test_demangle.cpp
  test_roundtrip.cpp
  test_cli.cpp
  test_corpus.cpp
  test_grammar.cpp
)
target_link_libraries(mangly_tests PRIVATE mangly)
target_include_directories(mangly_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mangly_tests PRIVATE
  MANGLY_SAMPLES_PATH="${CMAKE_SOURCE_DIR}/samples.txt"
  MANGLY_CORPUS_PATH="${CMAKE_SOURCE_DIR}/tests/corpus.txt")

add_test(NAME mangly_tests COMMAND mangly_tests)
