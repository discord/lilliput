if(ANDROID)
  # --------------------------------------------------------------------------------------------
  #  Installation for Android ndk-build makefile:  OpenCV.mk
  #  Part 1/2: ${BIN_DIR}/OpenCV.mk              -> For use *without* "make install"
  #  Part 2/2: ${BIN_DIR}/unix-install/OpenCV.mk -> For use with "make install"
  # -------------------------------------------------------------------------------------------

  # build type
  if(BUILD_SHARED_LIBS)
    set(OPENCV_LIBTYPE_CONFIGMAKE "SHARED")
  else()
    set(OPENCV_LIBTYPE_CONFIGMAKE "STATIC")
  endif()

  if(BUILD_FAT_JAVA_LIB)
    set(OPENCV_LIBTYPE_CONFIGMAKE "SHARED")
    set(OPENCV_STATIC_LIBTYPE_CONFIGMAKE "STATIC")
  else()
    set(OPENCV_STATIC_LIBTYPE_CONFIGMAKE ${OPENCV_LIBTYPE_CONFIGMAKE})
  endif()

  # build the list of opencv libs and dependencies for all modules
  ocv_get_all_libs(OPENCV_MODULES_CONFIGMAKE OPENCV_EXTRA_COMPONENTS_CONFIGMAKE OPENCV_3RDPARTY_COMPONENTS_CONFIGMAKE)

  # list -> string
  string(REPLACE ";" " " OPENCV_MODULES_CONFIGMAKE "${OPENCV_MODULES_CONFIGMAKE}")
  string(REPLACE ";" " " OPENCV_EXTRA_COMPONENTS_CONFIGMAKE "${OPENCV_EXTRA_COMPONENTS_CONFIGMAKE}")
  string(REPLACE ";" " " OPENCV_3RDPARTY_COMPONENTS_CONFIGMAKE "${OPENCV_3RDPARTY_COMPONENTS_CONFIGMAKE}")

  # replace 'opencv_<module>' -> '<module>''
  string(REPLACE "opencv_" "" OPENCV_MODULES_CONFIGMAKE "${OPENCV_MODULES_CONFIGMAKE}")

  if(BUILD_FAT_JAVA_LIB)
    set(OPENCV_LIBS_CONFIGMAKE java3)
  else()
    set(OPENCV_LIBS_CONFIGMAKE "${OPENCV_MODULES_CONFIGMAKE}")
  endif()

  # -------------------------------------------------------------------------------------------
  #  Part 1/2: ${BIN_DIR}/OpenCV.mk              -> For use *without* "make install"
  # -------------------------------------------------------------------------------------------
  set(OPENCV_INCLUDE_DIRS_CONFIGCMAKE "\"${OPENCV_CONFIG_FILE_INCLUDE_DIR}\" \"${OpenCV_SOURCE_DIR}/include\" \"${OpenCV_SOURCE_DIR}/include/opencv\"")
  set(OPENCV_BASE_INCLUDE_DIR_CONFIGCMAKE "\"${OpenCV_SOURCE_DIR}\"")
  set(OPENCV_LIBS_DIR_CONFIGCMAKE "\$(OPENCV_THIS_DIR)/lib/\$(OPENCV_TARGET_ARCH_ABI)")
  set(OPENCV_3RDPARTY_LIBS_DIR_CONFIGCMAKE "\$(OPENCV_THIS_DIR)/3rdparty/lib/\$(OPENCV_TARGET_ARCH_ABI)")

  configure_file("${OpenCV_SOURCE_DIR}/cmake/templates/OpenCV.mk.in" "${CMAKE_BINARY_DIR}/OpenCV.mk" @ONLY)
  configure_file("${OpenCV_SOURCE_DIR}/cmake/templates/OpenCV-abi.mk.in" "${CMAKE_BINARY_DIR}/OpenCV-${ANDROID_NDK_ABI_NAME}.mk" @ONLY)

  # -------------------------------------------------------------------------------------------
  #  Part 2/2: ${BIN_DIR}/unix-install/OpenCV.mk -> For use with "make install"
  # -------------------------------------------------------------------------------------------
  set(OPENCV_INCLUDE_DIRS_CONFIGCMAKE "\"\$(LOCAL_PATH)/\$(OPENCV_THIS_DIR)/include/opencv\" \"\$(LOCAL_PATH)/\$(OPENCV_THIS_DIR)/include\"")
  set(OPENCV_BASE_INCLUDE_DIR_CONFIGCMAKE "")
  set(OPENCV_LIBS_DIR_CONFIGCMAKE "\$(OPENCV_THIS_DIR)/../libs/\$(OPENCV_TARGET_ARCH_ABI)")
  set(OPENCV_3RDPARTY_LIBS_DIR_CONFIGCMAKE "\$(OPENCV_THIS_DIR)/../3rdparty/libs/\$(OPENCV_TARGET_ARCH_ABI)")

  configure_file("${OpenCV_SOURCE_DIR}/cmake/templates/OpenCV.mk.in" "${CMAKE_BINARY_DIR}/unix-install/OpenCV.mk" @ONLY)
  configure_file("${OpenCV_SOURCE_DIR}/cmake/templates/OpenCV-abi.mk.in" "${CMAKE_BINARY_DIR}/unix-install/OpenCV-${ANDROID_NDK_ABI_NAME}.mk" @ONLY)
  install(FILES ${CMAKE_BINARY_DIR}/unix-install/OpenCV.mk DESTINATION ${OPENCV_CONFIG_INSTALL_PATH} COMPONENT dev)
  install(FILES ${CMAKE_BINARY_DIR}/unix-install/OpenCV-${ANDROID_NDK_ABI_NAME}.mk DESTINATION ${OPENCV_CONFIG_INSTALL_PATH} COMPONENT dev)
endif(ANDROID)
