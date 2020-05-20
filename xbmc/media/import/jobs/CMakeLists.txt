set(SOURCES MediaImportSourceJobBase.cpp
            MediaImportSourceReadyJob.cpp
            MediaImportSourceRegistrationJob.cpp
            MediaImportTaskProcessorJob.cpp)

set(HEADERS MediaImportSourceActivationJob.h
            MediaImportSourceJobBase.h
            MediaImportSourceReadyJob.h
            MediaImportSourceRegistrationJob.h
            MediaImportTaskProcessorJob.h
            MediaImportTaskTypes.h)

core_add_library(media_import_jobs)
