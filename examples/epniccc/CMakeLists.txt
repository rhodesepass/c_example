add_executable(epniccc
  main.c
  niccc.c
)

target_include_directories(epniccc PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

# 编译期嵌入资源（避免硬编码路径）
target_compile_definitions(epniccc PRIVATE
  EP_NICCC_SCENE1_BIN=\"${CMAKE_CURRENT_LIST_DIR}/scene1.bin\"
)

target_link_libraries(epniccc PRIVATE
  c_examples_libs
)

# epniccc 会链接到 c_examples_libs 里的 C++ 源（crrefont / RREFont）
set_target_properties(epniccc PROPERTIES LINKER_LANGUAGE CXX)

