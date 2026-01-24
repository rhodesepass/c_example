set(APPLICATION_NAME flappy_bird)

add_executable(${APPLICATION_NAME}
  main.c
)

target_include_directories(${APPLICATION_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${APPLICATION_NAME} PRIVATE
  c_examples_libs
)

set_target_properties(${APPLICATION_NAME} PROPERTIES LINKER_LANGUAGE CXX)
