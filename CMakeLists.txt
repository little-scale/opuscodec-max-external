cmake_minimum_required(VERSION 3.19)

# Set project name
set(PROJECT_NAME opuscodec_tilde)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

include_directories( 
	"${MAX_SDK_INCLUDES}"
	"${MAX_SDK_MSP_INCLUDES}"
	"${MAX_SDK_JIT_INCLUDES}"
)

# Find Opus library using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

# Include Opus headers
include_directories(${OPUS_INCLUDE_DIRS})

# Add library directories
link_directories(${OPUS_LIBRARY_DIRS})

# Source files
file(GLOB PROJECT_SRC 
    "*.h" 
    "*.c" 
    "*.cpp"
)

# Create the external
add_library(${PROJECT_NAME} MODULE ${PROJECT_SRC})

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)

# Link libraries (after max-posttarget.cmake)
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})

# Add compiler flags if needed
if(OPUS_CFLAGS)
    target_compile_options(${PROJECT_NAME} PRIVATE ${OPUS_CFLAGS})
endif()