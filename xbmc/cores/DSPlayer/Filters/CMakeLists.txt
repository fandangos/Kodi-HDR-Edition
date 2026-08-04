set(SOURCES AllocatorCommon.cpp
            asyncio.cpp
            asyncrdr.cpp
            BaseSharedRender.cpp
            DSBluraySource.cpp
            DSDvdSource.cpp
            LavSettings.cpp
            madVRAllocatorPresenter.cpp
            MadvrSettings.cpp
            MadvrSettingsManager.cpp
            MadvrSharedRender.cpp
            RendererSettings.cpp
            XBMCFileReader.cpp
            XBMCFileSource.cpp)

set(HEADERS AllocatorCommon.h
            AsyncCallback.h
            asyncio.h
            asyncrdr.h
            BaseSharedRender.h
            DSBluraySource.h
            DSDvdSource.h
            IDirectVobSub.h
            IMpaDecFilter.h
            IMPCVideoDecFilter.h
            LAVAudioSettings.h
            LavSettings.h
            LAVSplitterSettings.h
            LAVVideoSettings.h
            madVRAllocatorPresenter.h
            MadvrSettings.h
            MadvrSettingsManager.h
            MadvrSharedRender.h
            mvrInterfaces.h
            RendererSettings.h
            XBMCFileReader.h
            XBMCFileSource.h)

core_add_library(Filters)
