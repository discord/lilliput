if(WIN32)
  try_compile(__VALID_DIRECTX
    "${OpenCV_BINARY_DIR}"
    "${OpenCV_SOURCE_DIR}/cmake/checks/directx.cpp"
    OUTPUT_VARIABLE TRY_OUT
  )
  if(NOT __VALID_DIRECTX)
    return()
  endif()
  try_compile(__VALID_DIRECTX_NV12
    "${OpenCV_BINARY_DIR}"
    "${OpenCV_SOURCE_DIR}/cmake/checks/directx.cpp"
    COMPILE_DEFINITIONS "-DCHECK_NV12"
    OUTPUT_VARIABLE TRY_OUT
  )
  if(__VALID_DIRECTX_NV12)
    set(HAVE_DIRECTX_NV12 ON)
  else()
    message(STATUS "No support for DirectX NV12 format (install Windows 8 SDK)")
  endif()
  set(HAVE_DIRECTX ON)
  set(HAVE_D3D11 ON)
  set(HAVE_D3D10 ON)
  set(HAVE_D3D9 ON)
endif()
