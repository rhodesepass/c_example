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

set_target_properties(${APPLICATION_NAME} PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS ${APPLICATION_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
install(FILES ${CMAKE_CURRENT_LIST_DIR}/appconfig.json DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME})
# 字体
install(FILES ${CMAKE_SOURCE_DIR}/assets/AlibabaPuHuiTi-3-65-Medium.otf DESTINATION ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME}/fonts)

install(FILES ${CMAKE_SOURCE_DIR}/assets/SourceHanSansSC-Regular.ttf 
  DESTINATION 
  ${CMAKE_INSTALL_PREFIX}/${APPLICATION_NAME}/fonts
)
