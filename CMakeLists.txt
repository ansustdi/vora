cmake_minimum_required(VERSION 3.21)
project(Vora VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGLWidgets Concurrent Svg)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
# Not IMPORTED_TARGET: some MSYS2 .pc files leak bogus MSYS-style include paths
pkg_check_modules(MPV REQUIRED mpv)
pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/MpvWidget.cpp src/MpvWidget.h
    src/ControlBar.cpp src/ControlBar.h
    src/FolderList.cpp src/FolderList.h
    src/Settings.cpp src/Settings.h
    src/SettingsDialog.cpp src/SettingsDialog.h
    src/Platform.cpp src/Platform.h
    src/subs/OsHash.cpp src/subs/OsHash.h
    src/subs/BsPlayerClient.cpp src/subs/BsPlayerClient.h
    src/subs/OpenSubtitlesClient.cpp src/subs/OpenSubtitlesClient.h
    src/subs/SubtitleManager.cpp src/subs/SubtitleManager.h
    src/subs/SubtitleSearchDialog.cpp src/subs/SubtitleSearchDialog.h
    src/subs/SubtitlePickerDialog.cpp src/subs/SubtitlePickerDialog.h
    src/stream/YtDlp.cpp src/stream/YtDlp.h
    src/stream/OpenStreamDialog.cpp src/stream/OpenStreamDialog.h
    src/stream/YouTube.cpp src/stream/YouTube.h
    src/stream/Playlists.cpp src/stream/Playlists.h
    src/stream/StreamResolver.cpp src/stream/StreamResolver.h
    src/stream/YouTubePage.cpp src/stream/YouTubePage.h
    src/image/ImageView.cpp src/image/ImageView.h
    src/image/ImageLoader.cpp src/image/ImageLoader.h
    src/image/ThumbStrip.cpp src/image/ThumbStrip.h
    src/image/Exif.cpp src/image/Exif.h
    src/image/ExifPanel.cpp src/image/ExifPanel.h
    src/image/Editor.cpp src/image/Editor.h
    src/ui/Theme.cpp src/ui/Theme.h
    src/ui/IconButton.cpp src/ui/IconButton.h
    src/VideoPage.cpp src/VideoPage.h
    src/WelcomePage.cpp src/WelcomePage.h
    resources/app.qrc
)
if(WIN32)
    enable_language(RC)
    list(APPEND SOURCES resources/app.rc)
endif()
add_executable(vora WIN32 MACOSX_BUNDLE ${SOURCES})
target_include_directories(vora PRIVATE src)
set(VORA_OS_KEY "" CACHE STRING "Built-in OpenSubtitles.com API key, so users don't need their own")
target_compile_definitions(vora PRIVATE
    VORA_VERSION="${PROJECT_VERSION}"
    VORA_OS_KEY="${VORA_OS_KEY}")
target_link_libraries(vora PRIVATE
    Qt6::Widgets Qt6::Network Qt6::OpenGLWidgets Qt6::Concurrent Qt6::Svg
    ZLIB::ZLIB ${MPV_LINK_LIBRARIES})
target_include_directories(vora PRIVATE ${MPV_INCLUDEDIR})

if(EXIV2_FOUND)
    target_link_libraries(vora PRIVATE PkgConfig::EXIV2)
    target_compile_definitions(vora PRIVATE HAVE_EXIV2)
    message(STATUS "exiv2 ${EXIV2_VERSION}: EXIF panel, RAW preview, lossless JPEG rotate enabled")
else()
    message(STATUS "exiv2 not found: EXIF features disabled")
endif()

if(WIN32)
    target_link_libraries(vora PRIVATE user32 shell32 dwmapi)
endif()
