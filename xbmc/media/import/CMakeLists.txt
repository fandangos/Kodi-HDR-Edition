set(SOURCES MediaImport.cpp
            MediaImportManager.cpp
            MediaImportSettingsBase.cpp
            MediaImportSource.cpp)

set(HEADERS IMediaImporter.h
            IMediaImporterManager.h
            IMediaImportHandler.h
            IMediaImportHandlerManager.h
            IMediaImportRepository.h
            MediaImport.h
            MediaImportChangesetTypes.h
            MediaImportManager.h
            MediaImportSettingsBase.h
            MediaImportSource.h)

core_add_library(media_import)
