if(ENABLE_CCACHE AND NOT CMAKE_COMPILER_IS_CCACHE)
  # This works fine with Unix Makefiles and Ninja generators
  find_host_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    message(STATUS "Looking for ccache - found (${CCACHE_PROGRAM})")
    get_property(__OLD_RULE_LAUNCH_COMPILE GLOBAL PROPERTY RULE_LAUNCH_COMPILE)
    if(__OLD_RULE_LAUNCH_COMPILE)
      message(STATUS "Can't replace CMake compiler launcher")
    else()
      set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
      # NOTE: Actually this check doesn't work as expected.
      # "RULE_LAUNCH_COMPILE" is ignored by CMake during try_compile() step.
      # ocv_check_compiler_flag(CXX "" IS_CCACHE_WORKS)
      set(IS_CCACHE_WORKS 1)
      if(IS_CCACHE_WORKS)
        set(CMAKE_COMPILER_IS_CCACHE 1)
      else()
        message(STATUS "Unable to compile program with enabled ccache, reverting...")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${__OLD_RULE_LAUNCH_COMPILE}")
      endif()
    else()
      message(STATUS "Looking for ccache - not found")
    endif()
  endif()
endif()

if((CMAKE_COMPILER_IS_CLANGCXX OR CMAKE_COMPILER_IS_CLANGCC OR CMAKE_COMPILER_IS_CCACHE) AND NOT CMAKE_GENERATOR MATCHES "Xcode")
  set(ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
endif()

if(MINGW OR (X86 AND UNIX AND NOT APPLE))
  # mingw compiler is known to produce unstable SSE code with -O3 hence we are trying to use -O2 instead
  if(CMAKE_COMPILER_IS_GNUCXX)
    foreach(flags CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG)
      string(REPLACE "-O3" "-O2" ${flags} "${${flags}}")
    endforeach()
  endif()

  if(CMAKE_COMPILER_IS_GNUCC)
    foreach(flags CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG)
      string(REPLACE "-O3" "-O2" ${flags} "${${flags}}")
    endforeach()
  endif()
endif()

if(MSVC)
  string(REGEX REPLACE "^  *| * $" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REGEX REPLACE "^  *| * $" "" CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT}")
  if(CMAKE_CXX_FLAGS STREQUAL CMAKE_CXX_FLAGS_INIT)
    # override cmake default exception handling option
    string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}"  CACHE STRING "Flags used by the compiler during all build types." FORCE)
  endif()
endif()

set(OPENCV_EXTRA_FLAGS "")
set(OPENCV_EXTRA_C_FLAGS "")
set(OPENCV_EXTRA_CXX_FLAGS "")
set(OPENCV_EXTRA_FLAGS_RELEASE "")
set(OPENCV_EXTRA_FLAGS_DEBUG "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE "")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_DEBUG "")

macro(add_extra_compiler_option option)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_BUILD_TYPE})
  endif()
  ocv_check_flag_support(CXX "${option}" _varname "${OPENCV_EXTRA_CXX_FLAGS} ${ARGN}")
  if(${_varname})
    set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} ${option}")
  endif()

  ocv_check_flag_support(C "${option}" _varname "${OPENCV_EXTRA_C_FLAGS} ${ARGN}")
  if(${_varname})
    set(OPENCV_EXTRA_C_FLAGS "${OPENCV_EXTRA_C_FLAGS} ${option}")
  endif()
endmacro()

# Gets environment variable and puts its value to the corresponding preprocessor definition
# Useful for WINRT that has no access to environment variables
macro(add_env_definitions option)
  set(value $ENV{${option}})
  if("${value}" STREQUAL "")
    message(WARNING "${option} environment variable is empty. Please set it to appropriate location to get correct results")
  else()
    string(REPLACE "\\" "\\\\" value ${value})
  endif()
  add_definitions("-D${option}=\"${value}\"")
endmacro()

# OpenCV fails some tests when 'char' is 'unsigned' by default
add_extra_compiler_option(-fsigned-char)

if(MINGW)
  # http://gcc.gnu.org/bugzilla/show_bug.cgi?id=40838
  # here we are trying to workaround the problem
  add_extra_compiler_option(-mstackrealign)
  if(NOT HAVE_CXX_MSTACKREALIGN)
    add_extra_compiler_option(-mpreferred-stack-boundary=2)
  endif()
endif()

if(CV_ICC AND NOT ENABLE_FAST_MATH)
  add_extra_compiler_option("-fp-model precise")
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
  # High level of warnings.
  add_extra_compiler_option(-W)
  add_extra_compiler_option(-Wall)
  add_extra_compiler_option(-Werror=return-type)
  add_extra_compiler_option(-Werror=non-virtual-dtor)
  add_extra_compiler_option(-Werror=address)
  add_extra_compiler_option(-Werror=sequence-point)
  add_extra_compiler_option(-Wformat)
  add_extra_compiler_option(-Werror=format-security -Wformat)
  add_extra_compiler_option(-Wmissing-declarations)
  add_extra_compiler_option(-Wmissing-prototypes)
  add_extra_compiler_option(-Wstrict-prototypes)
  add_extra_compiler_option(-Wundef)
  add_extra_compiler_option(-Winit-self)
  add_extra_compiler_option(-Wpointer-arith)
  add_extra_compiler_option(-Wshadow)
  add_extra_compiler_option(-Wsign-promo)

  if(ENABLE_NOISY_WARNINGS)
    add_extra_compiler_option(-Wcast-align)
    add_extra_compiler_option(-Wstrict-aliasing=2)
  else()
    add_extra_compiler_option(-Wno-narrowing)
    add_extra_compiler_option(-Wno-delete-non-virtual-dtor)
    add_extra_compiler_option(-Wno-unnamed-type-template-args)
    add_extra_compiler_option(-Wno-comment)
  endif()
  add_extra_compiler_option(-fdiagnostics-show-option)

  # The -Wno-long-long is required in 64bit systems when including sytem headers.
  if(X86_64)
    add_extra_compiler_option(-Wno-long-long)
  endif()

  # We need pthread's
  if(UNIX AND NOT ANDROID AND NOT (APPLE AND CMAKE_COMPILER_IS_CLANGCXX))
    add_extra_compiler_option(-pthread)
  endif()

  if(CMAKE_COMPILER_IS_CLANGCXX)
    add_extra_compiler_option(-Qunused-arguments)
  endif()

  if(OPENCV_WARNINGS_ARE_ERRORS)
    add_extra_compiler_option(-Werror)
  endif()

  if(X86 AND NOT MINGW64 AND NOT X86_64 AND NOT APPLE)
    add_extra_compiler_option(-march=i686)
  endif()

  if(APPLE)
    add_extra_compiler_option(-Wno-semicolon-before-method-body)
  endif()

  # Other optimizations
  if(ENABLE_OMIT_FRAME_POINTER)
    add_extra_compiler_option(-fomit-frame-pointer)
  else()
    add_extra_compiler_option(-fno-omit-frame-pointer)
  endif()
  if(ENABLE_FAST_MATH)
    add_extra_compiler_option(-ffast-math)
  endif()
  if(ENABLE_POWERPC)
    add_extra_compiler_option("-mcpu=G3 -mtune=G5")
  endif()
  if(ENABLE_SSE)
    add_extra_compiler_option(-msse)
  endif()
  if(ENABLE_SSE2)
    add_extra_compiler_option(-msse2)
  elseif(X86 OR X86_64)
    add_extra_compiler_option(-mno-sse2)
  endif()
  if(ARM)
    add_extra_compiler_option("-mfp16-format=ieee")
  endif(ARM)
  if(ENABLE_NEON)
    add_extra_compiler_option("-mfpu=neon")
  endif()
  if(ENABLE_VFPV3 AND NOT ENABLE_NEON)
    add_extra_compiler_option("-mfpu=vfpv3")
  endif()

  # SSE3 and further should be disabled under MingW because it generates compiler errors
  if(NOT MINGW)
    if(ENABLE_AVX)
      add_extra_compiler_option(-mavx)
    elseif(X86 OR X86_64)
      add_extra_compiler_option(-mno-avx)
    endif()
    if(ENABLE_AVX2)
      add_extra_compiler_option(-mavx2)

      if(ENABLE_FMA3)
        add_extra_compiler_option(-mfma)
      endif()
    endif()

    # GCC depresses SSEx instructions when -mavx is used. Instead, it generates new AVX instructions or AVX equivalence for all SSEx instructions when needed.
    if(NOT OPENCV_EXTRA_CXX_FLAGS MATCHES "-mavx")
      if(ENABLE_SSE3)
        add_extra_compiler_option(-msse3)
      elseif(X86 OR X86_64)
        add_extra_compiler_option(-mno-sse3)
      endif()

      if(ENABLE_SSSE3)
        add_extra_compiler_option(-mssse3)
      elseif(X86 OR X86_64)
        add_extra_compiler_option(-mno-ssse3)
      endif()

      if(ENABLE_SSE41)
        add_extra_compiler_option(-msse4.1)
      elseif(X86 OR X86_64)
        add_extra_compiler_option(-mno-sse4.1)
      endif()

      if(ENABLE_SSE42)
        add_extra_compiler_option(-msse4.2)
      elseif(X86 OR X86_64)
        add_extra_compiler_option(-mno-sse4.2)
      endif()

      if(ENABLE_POPCNT)
        add_extra_compiler_option(-mpopcnt)
      endif()
    endif()
  endif(NOT MINGW)

  if(X86 OR X86_64)
    if(NOT APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 4)
      if(OPENCV_EXTRA_CXX_FLAGS MATCHES "-m(sse2|avx)")
        add_extra_compiler_option(-mfpmath=sse)# !! important - be on the same wave with x64 compilers
      else()
        add_extra_compiler_option(-mfpmath=387)
      endif()
    endif()
  endif()

  # Profiling?
  if(ENABLE_PROFILING)
    add_extra_compiler_option("-pg -g")
    # turn off incompatible options
    foreach(flags CMAKE_CXX_FLAGS CMAKE_C_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
                  OPENCV_EXTRA_FLAGS_RELEASE OPENCV_EXTRA_FLAGS_DEBUG OPENCV_EXTRA_C_FLAGS OPENCV_EXTRA_CXX_FLAGS)
      string(REPLACE "-fomit-frame-pointer" "" ${flags} "${${flags}}")
      string(REPLACE "-ffunction-sections" "" ${flags} "${${flags}}")
    endforeach()
  elseif(NOT APPLE AND NOT ANDROID)
    # Remove unreferenced functions: function level linking
    add_extra_compiler_option(-ffunction-sections)
  endif()

  if(ENABLE_COVERAGE)
    set(OPENCV_EXTRA_C_FLAGS "${OPENCV_EXTRA_C_FLAGS} --coverage")
    set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} --coverage")
  endif()

  if(ENABLE_INSTRUMENTATION)
    set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} --std=c++11")
    set(WITH_VTK OFF) # There are issues with VTK 6.0
  endif()

  set(OPENCV_EXTRA_FLAGS_RELEASE "${OPENCV_EXTRA_FLAGS_RELEASE} -DNDEBUG")
  set(OPENCV_EXTRA_FLAGS_DEBUG "${OPENCV_EXTRA_FLAGS_DEBUG} -O0 -DDEBUG -D_DEBUG")
endif()

if(MSVC)
  set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /D _CRT_SECURE_NO_DEPRECATE /D _CRT_NONSTDC_NO_DEPRECATE /D _SCL_SECURE_NO_WARNINGS")
  # 64-bit portability warnings, in MSVC80
  if(MSVC80)
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /Wp64")
  endif()

  if(BUILD_WITH_DEBUG_INFO)
    set(OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE "${OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE} /debug")
  endif()

  # Remove unreferenced functions: function level linking
  set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /Gy")
  if(NOT MSVC_VERSION LESS 1400)
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /bigobj")
  endif()
  if(BUILD_WITH_DEBUG_INFO)
    set(OPENCV_EXTRA_FLAGS_RELEASE "${OPENCV_EXTRA_FLAGS_RELEASE} /Zi")
  endif()

  if(ENABLE_AVX2 AND NOT MSVC_VERSION LESS 1800)
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:AVX2")
  endif()
  if(ENABLE_AVX AND NOT MSVC_VERSION LESS 1600 AND NOT OPENCV_EXTRA_FLAGS MATCHES "/arch:")
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:AVX")
  endif()

  if(ENABLE_SSE4_1 AND CV_ICC AND NOT OPENCV_EXTRA_FLAGS MATCHES "/arch:")
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:SSE4.1")
  endif()

  if(ENABLE_SSE3 AND CV_ICC AND NOT OPENCV_EXTRA_FLAGS MATCHES "/arch:")
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:SSE3")
  endif()

  if(NOT MSVC64)
    # 64-bit MSVC compiler uses SSE/SSE2 by default
    if(ENABLE_SSE2 AND NOT OPENCV_EXTRA_FLAGS MATCHES "/arch:")
      set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:SSE2")
    endif()
    if(ENABLE_SSE AND NOT OPENCV_EXTRA_FLAGS MATCHES "/arch:")
      set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /arch:SSE")
    endif()
  endif()

  if(ENABLE_SSE OR ENABLE_SSE2 OR ENABLE_SSE3 OR ENABLE_SSE4_1 OR ENABLE_AVX OR ENABLE_AVX2)
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /Oi")
  endif()

  if(X86 OR X86_64)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND ENABLE_SSE2)
      set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /fp:fast") # !! important - be on the same wave with x64 compilers
    endif()
  endif()

  if(OPENCV_WARNINGS_ARE_ERRORS)
    set(OPENCV_EXTRA_FLAGS "${OPENCV_EXTRA_FLAGS} /WX")
  endif()
endif()

if(MSVC12 AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  set(OPENCV_EXTRA_C_FLAGS "${OPENCV_EXTRA_C_FLAGS} /FS")
  set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} /FS")
endif()

# Adding additional using directory for WindowsPhone 8.0 to get Windows.winmd properly
if(WINRT_PHONE AND WINRT_8_0)
  set(OPENCV_EXTRA_CXX_FLAGS "${OPENCV_EXTRA_CXX_FLAGS} /AI\$(WindowsSDK_MetadataPath)")
endif()

# Extra link libs if the user selects building static libs:
if(NOT BUILD_SHARED_LIBS AND CMAKE_COMPILER_IS_GNUCXX AND NOT ANDROID)
  # Android does not need these settings because they are already set by toolchain file
  set(OPENCV_LINKER_LIBS ${OPENCV_LINKER_LIBS} stdc++)
  set(OPENCV_EXTRA_FLAGS "-fPIC ${OPENCV_EXTRA_FLAGS}")
endif()

# Add user supplied extra options (optimization, etc...)
# ==========================================================
set(OPENCV_EXTRA_FLAGS         "${OPENCV_EXTRA_FLAGS}"         CACHE INTERNAL "Extra compiler options")
set(OPENCV_EXTRA_C_FLAGS       "${OPENCV_EXTRA_C_FLAGS}"       CACHE INTERNAL "Extra compiler options for C sources")
set(OPENCV_EXTRA_CXX_FLAGS     "${OPENCV_EXTRA_CXX_FLAGS}"     CACHE INTERNAL "Extra compiler options for C++ sources")
set(OPENCV_EXTRA_FLAGS_RELEASE "${OPENCV_EXTRA_FLAGS_RELEASE}" CACHE INTERNAL "Extra compiler options for Release build")
set(OPENCV_EXTRA_FLAGS_DEBUG   "${OPENCV_EXTRA_FLAGS_DEBUG}"   CACHE INTERNAL "Extra compiler options for Debug build")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS         "${OPENCV_EXTRA_EXE_LINKER_FLAGS}"         CACHE INTERNAL "Extra linker flags")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE "${OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE}" CACHE INTERNAL "Extra linker flags for Release build")
set(OPENCV_EXTRA_EXE_LINKER_FLAGS_DEBUG   "${OPENCV_EXTRA_EXE_LINKER_FLAGS_DEBUG}"   CACHE INTERNAL "Extra linker flags for Debug build")

# set default visibility to hidden
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_OPENCV_GCC_VERSION_NUM GREATER 399)
  add_extra_compiler_option(-fvisibility=hidden)
  add_extra_compiler_option(-fvisibility-inlines-hidden)
endif()

if(NOT OPENCV_FP16_DISABLE AND NOT IOS)
  if(ARM AND ENABLE_NEON)
    set(FP16_OPTION "-mfpu=neon-fp16")
  elseif((X86 OR X86_64) AND NOT MSVC AND ENABLE_AVX)
    set(FP16_OPTION "-mf16c")
  endif()
  try_compile(__VALID_FP16
    "${OpenCV_BINARY_DIR}"
    "${OpenCV_SOURCE_DIR}/cmake/checks/fp16.cpp"
    COMPILE_DEFINITIONS "-DCHECK_FP16" "${FP16_OPTION}"
    OUTPUT_VARIABLE TRY_OUT
    )
  if(NOT __VALID_FP16)
    if((X86 OR X86_64) AND NOT MSVC AND NOT ENABLE_AVX)
      # GCC enables AVX when mf16c is passed
      message(STATUS "FP16: Feature disabled")
    else()
      message(STATUS "FP16: Compiler support is not available")
    endif()
  else()
    message(STATUS "FP16: Compiler support is available")
    set(HAVE_FP16 1)
    if(NOT ${FP16_OPTION} STREQUAL "")
      add_extra_compiler_option(${FP16_OPTION})
    endif()
  endif()
endif()

#combine all "extra" options
set(CMAKE_C_FLAGS           "${CMAKE_C_FLAGS} ${OPENCV_EXTRA_FLAGS} ${OPENCV_EXTRA_C_FLAGS}")
set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} ${OPENCV_EXTRA_FLAGS} ${OPENCV_EXTRA_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPENCV_EXTRA_FLAGS_RELEASE}")
set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} ${OPENCV_EXTRA_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} ${OPENCV_EXTRA_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_DEBUG     "${CMAKE_C_FLAGS_DEBUG} ${OPENCV_EXTRA_FLAGS_DEBUG}")
set(CMAKE_EXE_LINKER_FLAGS         "${CMAKE_EXE_LINKER_FLAGS} ${OPENCV_EXTRA_EXE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${OPENCV_EXTRA_EXE_LINKER_FLAGS_RELEASE}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG   "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${OPENCV_EXTRA_EXE_LINKER_FLAGS_DEBUG}")

if(MSVC)
  # avoid warnings from MSVC about overriding the /W* option
  # we replace /W3 with /W4 only for C++ files,
  # since all the 3rd-party libraries OpenCV uses are in C,
  # and we do not care about their warnings.
  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS}")
  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}")

  if(NOT ENABLE_NOISY_WARNINGS)
    if(MSVC_VERSION EQUAL 1400)
      ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4510 /wd4610 /wd4312 /wd4201 /wd4244 /wd4328 /wd4267)
    endif()
    if(MSVC_VERSION LESS 1900) # MSVS2015
      ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4127) # warning C4127: conditional expression is constant
    endif()
  endif()

  # allow extern "C" functions throw exceptions
  foreach(flags CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG)
    string(REPLACE "/EHsc-" "/EHs" ${flags} "${${flags}}")
    string(REPLACE "/EHsc"  "/EHs" ${flags} "${${flags}}")

    string(REPLACE "/Zm1000" "" ${flags} "${${flags}}")
  endforeach()

  if(NOT ENABLE_NOISY_WARNINGS)
    ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4251) # class 'std::XXX' needs to have dll-interface to be used by clients of YYY
    ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4324) # 'struct_name' : structure was padded due to __declspec(align())
    ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4275) # non dll-interface class 'std::exception' used as base for dll-interface class 'cv::Exception'
    ocv_warnings_disable(CMAKE_CXX_FLAGS /wd4589) # Constructor of abstract class 'cv::ORB' ignores initializer for virtual base class 'cv::Algorithm'
  endif()

  if(CV_ICC AND NOT ENABLE_NOISY_WARNINGS)
    foreach(flags CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG)
      string(REGEX REPLACE "( |^)/W[0-9]+( |$)" "\\1\\2" ${flags} "${${flags}}")
    endforeach()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Qwd673") # PCH warning
  endif()
endif()

if(APPLE AND NOT CMAKE_CROSSCOMPILING AND NOT DEFINED ENV{LDFLAGS} AND EXISTS "/usr/local/lib")
  link_directories("/usr/local/lib")
endif()
