set(current_dir "${CMAKE_CURRENT_LIST_DIR}")
find_program(AWK awk REQUIRED)

function(process_msg_file OUT IN EXTRA_PATH)
  # 获取 IN 文件的相对路径
  file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${IN}")

  # 在 binary 目录中创建相应的目录结构
  get_filename_component(out_dir "${CMAKE_BINARY_DIR}/_gen/${EXTRA_PATH}/${rel_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${out_dir}")

  # 设置输出文件的完整路径
  set(out_file "${CMAKE_BINARY_DIR}/_gen/${EXTRA_PATH}/${rel_path}")

  # 创建自定义命令来处理文件
  add_custom_command(
    OUTPUT "${out_file}"
    COMMAND ${AWK} -f ${current_dir}/../scripts/process_msg.awk "${IN}" > "${out_file}"
    DEPENDS "${IN}"
    COMMENT "Processing ${IN} to ${out_file}")

  # 将输出文件路径设置到传入的 OUT 变量
  set(${OUT}
      "${out_file}"
      PARENT_SCOPE)
endfunction()

function(process_msg_directory OUTPUT_VAR MSG_DIR EXTRA_PATH)
  file(GLOB_RECURSE MSG_FILES "${MSG_DIR}/*.h")
  set(PROCESSED_FILES)

  foreach(MSG_FILE ${MSG_FILES})
    get_filename_component(FILE_NAME "${MSG_FILE}" NAME)
    process_msg_file(PROCESSED_FILE "${MSG_FILE}" "${EXTRA_PATH}")
    list(APPEND PROCESSED_FILES "${PROCESSED_FILE}")
  endforeach()

  set(${OUTPUT_VAR}
      "${PROCESSED_FILES}"
      PARENT_SCOPE)
endfunction()
