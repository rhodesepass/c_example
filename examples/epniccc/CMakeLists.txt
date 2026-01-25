set(APPLICATION_NAME epniccc)

add_executable(${APPLICATION_NAME}
  main.c
  niccc.c
)

target_include_directories(${APPLICATION_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

# 编译期嵌入资源（避免硬编码路径）
target_compile_definitions(${APPLICATION_NAME} PRIVATE
  EP_NICCC_SCENE1_BIN=\"${CMAKE_CURRENT_LIST_DIR}/scene1.bin\"
)

target_link_libraries(${APPLICATION_NAME} PRIVATE
  c_examples_libs
)

set_target_properties(${APPLICATION_NAME} PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS ${APPLICATION_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
install(FILES ${CMAKE_CURRENT_LIST_DIR}/appconfig.json DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
