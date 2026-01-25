# 修改为你的程序名
set(APPLICATION_NAME template)

# 添加你的源文件
add_executable(${APPLICATION_NAME}
  main.c
)

# 添加你的头文件目录
target_include_directories(${APPLICATION_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(${APPLICATION_NAME} PRIVATE
  c_examples_libs
)

set_target_properties(${APPLICATION_NAME} PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS ${APPLICATION_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
install(FILES ${CMAKE_CURRENT_LIST_DIR}/appconfig.json DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})

# 如果有其他文件需要安装，可以在这里添加，如：
# install(FILES ${CMAKE_SOURCE_DIR}/assets/icon.png DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
# 会把assets/icon.png文件安装到dist/template/assets/icon.png