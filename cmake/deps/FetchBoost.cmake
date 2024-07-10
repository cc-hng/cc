set(BOOST_ISNTALL_PREFIX
    "${CMAKE_INSTALL_PREFIX}"
    CACHE PATH "Boost dependencies path")

message(STATUS "[cc] boost fetching ...")
set(BOOST_VERSION boost-1.83.0)

FetchContent_Declare(
  boost
  URL https://github.com/boostorg/boost/archive/refs/tags/${BOOST_VERSION}.tar.gz
  # URL_HASH SHA256=5846c20c3508d77047cc633856a29fe756205ac5c203965ee7c4864cad456757
  DOWNLOAD_NO_PROGRESS TRUE)

set(boost_ADDED NO)
FetchContent_GetProperties(boost)
if(NOT boost_POPULATED)
  set(boost_ADDED YES)
  FetchContent_Populate(boost)
endif()

message(STATUS "[cc] boost.cmake fetching ...")
FetchContent_Declare(
  boostcmake
  URL https://github.com/boostorg/cmake/archive/${BOOST_VERSION}.tar.gz
  # URL_HASH SHA256=2a19bf0089047d66058121c58431a2974ae535a04d9606c1214a037102ce22e2
  DOWNLOAD_NO_PROGRESS TRUE)
FetchContent_GetProperties(boostcmake)
if(NOT boostcmake_POPULATED)
  FetchContent_Populate(boostcmake)
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${boostcmake_SOURCE_DIR} ${boost_SOURCE_DIR}/tools/cmake/)
endif()

function(boost_add_submodule name commit_id sha256)
  message(STATUS "[cc] boost.${name} fetching ...")

  set(depname "boost${name}")
  FetchContent_Declare(
    ${depname}
    URL https://github.com/boostorg/${name}/archive/${commit_id}.tar.gz
    # URL_HASH SHA256=${sha256}
    DOWNLOAD_NO_PROGRESS TRUE)

  FetchContent_GetProperties(${depname})
  if(NOT ${depname}_POPULATED)
    FetchContent_Populate(${depname})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${${depname}_SOURCE_DIR}
                            ${boost_SOURCE_DIR}/libs/${name})
  endif()
endfunction()

boost_add_submodule(type_index ${BOOST_VERSION} "")
boost_add_submodule(atomic ${BOOST_VERSION} "")
boost_add_submodule(filesystem ${BOOST_VERSION} "")
boost_add_submodule(process ${BOOST_VERSION} "")

boost_add_submodule(url ${BOOST_VERSION} "")
boost_add_submodule(headers ${BOOST_VERSION} "")
boost_add_submodule(core ${BOOST_VERSION} "")
boost_add_submodule(config ${BOOST_VERSION} "")
boost_add_submodule(callable_traits ${BOOST_VERSION} "")
boost_add_submodule(hana ${BOOST_VERSION} "")

boost_add_submodule(function ${BOOST_VERSION} "")
boost_add_submodule(functional ${BOOST_VERSION} "")
boost_add_submodule(range ${BOOST_VERSION} "")
boost_add_submodule(integer ${BOOST_VERSION} "")
boost_add_submodule(conversion ${BOOST_VERSION} "")
boost_add_submodule(intrusive ${BOOST_VERSION} "")
boost_add_submodule(iterator ${BOOST_VERSION} "")
boost_add_submodule(typeof ${BOOST_VERSION} "")
boost_add_submodule(tokenizer ${BOOST_VERSION} "")
boost_add_submodule(mpl ${BOOST_VERSION} "")
boost_add_submodule(regex ${BOOST_VERSION} "")
boost_add_submodule(utility ${BOOST_VERSION} "")
boost_add_submodule(detail ${BOOST_VERSION} "")
boost_add_submodule(predef ${BOOST_VERSION} "")
boost_add_submodule(function_types ${BOOST_VERSION} "")
boost_add_submodule(unordered ${BOOST_VERSION} "")
boost_add_submodule(describe ${BOOST_VERSION} "")
boost_add_submodule(winapi ${BOOST_VERSION} "")

boost_add_submodule(numeric_conversion ${BOOST_VERSION} "")
boost_add_submodule(container ${BOOST_VERSION} "")
boost_add_submodule(container_hash ${BOOST_VERSION} "")
boost_add_submodule(preprocessor ${BOOST_VERSION} "")
boost_add_submodule(concept_check ${BOOST_VERSION} "")
boost_add_submodule(lexical_cast ${BOOST_VERSION} "")

boost_add_submodule(bind ${BOOST_VERSION} "")
boost_add_submodule(io ${BOOST_VERSION} "")
boost_add_submodule(array ${BOOST_VERSION} "")
boost_add_submodule(algorithm ${BOOST_VERSION} "")
boost_add_submodule(date_time ${BOOST_VERSION} "")
boost_add_submodule(optional ${BOOST_VERSION} "")
boost_add_submodule(fusion ${BOOST_VERSION} "")
boost_add_submodule(mp11 ${BOOST_VERSION} "")

boost_add_submodule(assert ${BOOST_VERSION} "")
boost_add_submodule(throw_exception ${BOOST_VERSION} "")
boost_add_submodule(variant2 ${BOOST_VERSION} "")
boost_add_submodule(system ${BOOST_VERSION} "")

boost_add_submodule(tuple ${BOOST_VERSION} "")
boost_add_submodule(type_traits ${BOOST_VERSION} "")
boost_add_submodule(move ${BOOST_VERSION} "")
boost_add_submodule(static_assert ${BOOST_VERSION} "")
boost_add_submodule(smart_ptr ${BOOST_VERSION} "")
boost_add_submodule(exception ${BOOST_VERSION} "")
boost_add_submodule(align ${BOOST_VERSION} "")
boost_add_submodule(asio ${BOOST_VERSION} "")

boost_add_submodule(endian ${BOOST_VERSION} "")
boost_add_submodule(static_string ${BOOST_VERSION} "")
boost_add_submodule(logic ${BOOST_VERSION} "")
boost_add_submodule(beast ${BOOST_VERSION} "")

# if(boost_ADDED)
#   # Bring the populated content into the build
#   add_subdirectory(${boost_SOURCE_DIR} ${boost_BINARY_DIR})
# endif()

if(boost_ADDED)
  file(MAKE_DIRECTORY ${BOOST_ISNTALL_PREFIX})
  execute_process(
    COMMAND ${CMAKE_COMMAND} -G${CMAKE_GENERATOR} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${BOOST_ISNTALL_PREFIX} ${boost_SOURCE_DIR}
    WORKING_DIRECTORY ${boost_BINARY_DIR}
    RESULT_VARIABLE configure_result)
  if(configure_result)
    message(FATAL_ERROR "Boost configure failed")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${boost_BINARY_DIR} -j3)
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${boost_BINARY_DIR} --target install)
endif()
