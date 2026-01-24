set(APPLICATION_NAME textreader)

add_executable(${APPLICATION_NAME}
  main.c
  stb_impl.c
  reader.c
  state.c
  menu.c
)

target_include_directories(${APPLICATION_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${APPLICATION_NAME} PRIVATE
  c_examples_libs
)

# epniccc 会链接到 c_examples_libs 里的 C++ 源（crrefont / RREFont）
set_target_properties(${APPLICATION_NAME} PROPERTIES LINKER_LANGUAGE CXX)

