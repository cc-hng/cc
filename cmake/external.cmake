set(DEPS_PREFIX
    "${PROJECT_BINARY_DIR}/_install/deps"
    CACHE PATH "Path prefix for finding dependencies")
list(INSERT CMAKE_PREFIX_PATH 0 ${DEPS_PREFIX})

if(EXISTS ${DEPS_PREFIX})
  message(STATUS "[cc] deps already exists !!!")
  return()
endif()

set(DEPS_BUILD_DIR ${PROJECT_BINARY_DIR}/_build/deps)
file(MAKE_DIRECTORY ${DEPS_BUILD_DIR})

execute_process(
  COMMAND
    ${CMAKE_COMMAND} -G${CMAKE_GENERATOR} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCC_ENABLE_IO_URING=${CC_ENABLE_IO_URING} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
    ${CMAKE_CURRENT_LIST_DIR}/deps
  WORKING_DIRECTORY ${DEPS_BUILD_DIR}
  RESULT_VARIABLE deps_configure_result)
if(deps_configure_result)
  message(FATAL_ERROR "Deps configure failed")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build ${DEPS_BUILD_DIR} -j RESULT_VARIABLE deps_build_result)
if(deps_build_result)
  message(FATAL_ERROR "Deps build failed")
endif()
