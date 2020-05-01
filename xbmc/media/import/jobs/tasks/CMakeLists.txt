set(SOURCES IMediaImportTask.cpp
            MediaImportChangesetTask.cpp
            MediaImportCleanupTask.cpp
            MediaImportImportItemsRetrievalTask.cpp
            MediaImportLocalItemsRetrievalTask.cpp
            MediaImportRemovalTask.cpp
            MediaImportSynchronisationTask.cpp
            MediaImportUpdateTask.cpp)

set(HEADERS IMediaImportTask.h
            MediaImportChangesetTask.h
            MediaImportCleanupTask.h
            MediaImportImportItemsRetrievalTask.h
            MediaImportLocalItemsRetrievalTask.h
            MediaImportRemovalTask.h
            MediaImportSynchronisationTask.h
            MediaImportUpdateTask.h)

core_add_library(media_import_jobs_tasks)
