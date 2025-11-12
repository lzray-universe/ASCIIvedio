find_package(PkgConfig)

if (PkgConfig_FOUND)
    pkg_check_modules(PC_FFMPEG QUIET libavformat libavcodec libavutil libswscale libswresample)
endif()

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS ${PC_FFMPEG_INCLUDE_DIRS}
)

foreach(component AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)
    string(TOLOWER ${component} lc)
    find_library(FFMPEG_${component}_LIBRARY
        NAMES av${lc}
        HINTS ${PC_FFMPEG_LIBRARY_DIRS}
    )
    if (NOT FFMPEG_${component}_LIBRARY)
        message(FATAL_ERROR "Could not find FFmpeg component ${component}")
    endif()
endforeach()

set(FFMPEG_LIBRARIES ${FFMPEG_AVFORMAT_LIBRARY} ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY} ${FFMPEG_SWSCALE_LIBRARY} ${FFMPEG_SWRESAMPLE_LIBRARY})
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
set(FFMPEG_DEFINITIONS "")

mark_as_advanced(FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)
