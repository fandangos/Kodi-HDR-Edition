set(SOURCES Bookmark.cpp
            ContextMenus.cpp
            GUIViewStateVideo.cpp
            PlayerController.cpp
            Teletext.cpp
            VideoDatabase.cpp
            VideoDbUrl.cpp
            VideoInfoDownloader.cpp
            VideoInfoScanner.cpp
            VideoInfoTag.cpp
            VideoThumbLoader.cpp
            ViewModeSettings.cpp)

set(HEADERS Bookmark.h
            ContextMenus.h
            Episode.h
            GUIViewStateVideo.h
            PlayerController.h
            Teletext.h
            TeletextDefines.h
            VideoDatabase.h
            VideoDbUrl.h
            VideoInfoDownloader.h
            VideoInfoScanner.h
            VideoInfoTag.h
            VideoThumbLoader.h
            ViewModeSettings.h)

core_add_library(video)
