/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemWin32DX.h"

#include "commons/ilog.h"
#include "rendering/dx/RenderContext.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/SystemInfo.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include "platform/win32/CharsetConverter.h"

#include "system.h"

#ifndef _M_X64
#include "utils/SystemInfo.h"
#endif
#if _DEBUG
#pragma comment(lib, "detoursd.lib")
#else
#pragma comment(lib, "detours.lib")
#endif
#pragma comment(lib, "dxgi.lib")
#include <windows.h>
#include <winnt.h>
#include <winternl.h>
#pragma warning(disable: 4091)
#include <d3d10umddi.h>
#pragma warning(default: 4091)
#include <detours.h>

#pragma comment(lib, "nvapi64.lib")
#include "cores/VideoPlayer/nvapi.h"

#pragma comment(lib, "amd_ags_x64.lib")
#include <amd_ags.h>

using KODI::PLATFORM::WINDOWS::FromW;

// User Mode Driver hooks definitions
void APIENTRY HookCreateResource(D3D10DDI_HDEVICE hDevice, const D3D10DDIARG_CREATERESOURCE* pResource, D3D10DDI_HRESOURCE hResource, D3D10DDI_HRTRESOURCE hRtResource);
HRESULT APIENTRY HookCreateDevice(D3D10DDI_HADAPTER hAdapter, D3D10DDIARG_CREATEDEVICE* pCreateData);
HRESULT APIENTRY HookOpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pOpenData);
static PFND3D10DDI_OPENADAPTER s_fnOpenAdapter10_2{ nullptr };
static PFND3D10DDI_CREATEDEVICE s_fnCreateDeviceOrig{ nullptr };
static PFND3D10DDI_CREATERESOURCE s_fnCreateResourceOrig{ nullptr };

std::unique_ptr<CWinSystemBase> CWinSystemBase::CreateWinSystem()
{
  std::unique_ptr<CWinSystemBase> winSystem(new CWinSystemWin32DX());
  return winSystem;
}

CWinSystemWin32DX::CWinSystemWin32DX() : CRenderSystemDX()
  , m_hDriverModule(nullptr)
{
}

CWinSystemWin32DX::~CWinSystemWin32DX()
{
}

void CWinSystemWin32DX::PresentRenderImpl(bool rendered)
{
  if (rendered)
    m_deviceResources->Present();

  if (m_delayDispReset && m_dispResetTimer.IsTimePast())
  {
    m_delayDispReset = false;
    OnDisplayReset();
  }

  if (!rendered)
    KODI::TIME::Sleep(40);
}

bool CWinSystemWin32DX::CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res)
{
  const MONITOR_DETAILS* monitor = GetDisplayDetails(CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_VIDEOSCREEN_MONITOR));
  if (!monitor)
    return false;

  m_hMonitor = monitor->hMonitor;
  m_deviceResources = DX::DeviceResources::Get();
  // setting monitor before creating window for proper hooking into a driver
  m_deviceResources->SetMonitor(m_hMonitor);

  return CWinSystemWin32::CreateNewWindow(name, fullScreen, res) && m_deviceResources->HasValidDevice();
}

void CWinSystemWin32DX::SetWindow(HWND hWnd) const
{
  m_deviceResources->SetWindow(hWnd);
}

bool CWinSystemWin32DX::DestroyRenderSystem()
{
  CRenderSystemDX::DestroyRenderSystem();

  m_deviceResources->Release();
  m_deviceResources.reset();
  return true;
}

void CWinSystemWin32DX::SetDeviceFullScreen(bool fullScreen, RESOLUTION_INFO& res)
{
  if (m_deviceResources->SetFullScreen(fullScreen, res))
  {
    ResolutionChanged();
  }
}

bool CWinSystemWin32DX::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CWinSystemWin32::ResizeWindow(newWidth, newHeight, newLeft, newTop);
  CRenderSystemDX::OnResize();

  return true;
}

void CWinSystemWin32DX::OnMove(int x, int y)
{
  // do not handle moving at window creation because MonitorFromWindow
  // returns default system monitor in case of m_hWnd is null
  if (!m_hWnd)
    return;

  HMONITOR newMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
  if (newMonitor != m_hMonitor)
  {
    MONITOR_DETAILS* details = GetDisplayDetails(newMonitor);
    CDisplaySettings::GetInstance().SetMonitor(KODI::PLATFORM::WINDOWS::FromW(details->MonitorNameW));
    m_deviceResources->SetMonitor(newMonitor);
    m_hMonitor = newMonitor;
  }
}

bool CWinSystemWin32DX::DPIChanged(WORD dpi, RECT windowRect) const
{
  // on Win10 FCU the OS keeps window size exactly the same size as it was
  if (CSysInfo::IsWindowsVersionAtLeast(CSysInfo::WindowsVersionWin10_FCU))
    return true;

  m_deviceResources->SetDpi(dpi);
  if (!IsAlteringWindow())
    return __super::DPIChanged(dpi, windowRect);

  return true;
}

void CWinSystemWin32DX::ReleaseBackBuffer()
{
  m_deviceResources->ReleaseBackBuffer();
}

void CWinSystemWin32DX::CreateBackBuffer()
{
  m_deviceResources->CreateBackBuffer();
}

void CWinSystemWin32DX::ResizeDeviceBuffers()
{
  m_deviceResources->ResizeBuffers();
}

bool CWinSystemWin32DX::IsStereoEnabled()
{
  return m_deviceResources->IsStereoEnabled();
}

void CWinSystemWin32DX::OnScreenChange(HMONITOR monitor)
{
  m_deviceResources->SetMonitor(monitor);
}

bool CWinSystemWin32DX::ChangeResolution(const RESOLUTION_INFO &res, bool forceChange)
{
  bool changed = CWinSystemWin32::ChangeResolution(res, forceChange);
  // this is a try to fix FCU issue after changing resolution
  if (m_deviceResources && changed)
    m_deviceResources->ResizeBuffers();
  return changed;
}

void CWinSystemWin32DX::OnResize(int width, int height)
{
  if (!m_IsAlteringWindow)
    ReleaseBackBuffer();

  m_deviceResources->SetLogicalSize(static_cast<float>(width), static_cast<float>(height));

  if (!m_IsAlteringWindow)
    CreateBackBuffer();
}

bool CWinSystemWin32DX::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  bool const result = CWinSystemWin32::SetFullScreen(fullScreen, res, blankOtherDisplays);
  CRenderSystemDX::OnResize();
  return result;
}

void CWinSystemWin32DX::UninitHooks()
{
  // uninstall
  if (!s_fnOpenAdapter10_2)
    return;

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourDetach(reinterpret_cast<void**>(&s_fnOpenAdapter10_2), HookOpenAdapter10_2);
  DetourTransactionCommit();
  if (m_hDriverModule)
  {
    FreeLibrary(m_hDriverModule);
    m_hDriverModule = nullptr;
  }
}

void CWinSystemWin32DX::InitHooks(IDXGIOutput* pOutput)
{
  DXGI_OUTPUT_DESC outputDesc;
  if (!pOutput || FAILED(pOutput->GetDesc(&outputDesc)))
    return;

  DISPLAY_DEVICEW displayDevice;
  displayDevice.cb = sizeof(DISPLAY_DEVICEW);
  DWORD adapter = 0;
  bool deviceFound = false;

  // delete exiting hooks.
  if (s_fnOpenAdapter10_2)
    UninitHooks();

  // enum devices to find matched
  while (EnumDisplayDevicesW(nullptr, adapter, &displayDevice, 0))
  {
    if (wcscmp(displayDevice.DeviceName, outputDesc.DeviceName) == 0)
    {
      deviceFound = true;
      break;
    }
    adapter++;
  }
  if (!deviceFound)
    return;

  CLog::LogF(LOGDEBUG, "Hooking into UserModeDriver on device %s. ", FromW(displayDevice.DeviceKey));
  wchar_t* keyName =
#ifndef _M_X64
  // on x64 system and x32 build use UserModeDriverNameWow key
  CSysInfo::GetKernelBitness() == 64 ? keyName = L"UserModeDriverNameWow" :
#endif // !_WIN64
    L"UserModeDriverName";

  DWORD dwType = REG_MULTI_SZ;
  HKEY hKey = nullptr;
  wchar_t value[1024];
  DWORD valueLength = sizeof(value);
  LSTATUS lstat;

  // to void \Registry\Machine at the beginning, we use shifted pointer at 18
  if (ERROR_SUCCESS == (lstat = RegOpenKeyExW(HKEY_LOCAL_MACHINE, displayDevice.DeviceKey + 18, 0, KEY_READ, &hKey))
    && ERROR_SUCCESS == (lstat = RegQueryValueExW(hKey, keyName, nullptr, &dwType, (LPBYTE)&value, &valueLength)))
  {
    // 1. registry value has a list of drivers for each API with the following format: dx9\0dx10\0dx11\0dx12\0\0
    // 2. we split the value by \0
    std::vector<std::wstring> drivers;
    const wchar_t* pValue = value;
    while (*pValue)
    {
      drivers.push_back(std::wstring(pValue));
      pValue += drivers.back().size() + 1;
    }
    // no entries in the registry
    if (drivers.empty())
      return;
    // 3. we take only first three values (dx12 driver isn't needed if it exists ofc)
    if (drivers.size() > 3)
      drivers = std::vector<std::wstring>(drivers.begin(), drivers.begin() + 3);
    // 4. and then iterate with reverse order to start iterate with the best candidate for d3d11 driver
    for (auto it = drivers.rbegin(); it != drivers.rend(); ++it)
    {
      m_hDriverModule = LoadLibraryW(it->c_str());
      if (m_hDriverModule != nullptr)
      {
        s_fnOpenAdapter10_2 = reinterpret_cast<PFND3D10DDI_OPENADAPTER>(GetProcAddress(m_hDriverModule, "OpenAdapter10_2"));
        if (s_fnOpenAdapter10_2 != nullptr)
        {
          DetourTransactionBegin();
          DetourUpdateThread(GetCurrentThread());
          DetourAttach(reinterpret_cast<void**>(&s_fnOpenAdapter10_2), HookOpenAdapter10_2);
          if (NO_ERROR == DetourTransactionCommit())
          // install and activate hook into a driver
          {
            CLog::LogF(LOGDEBUG, "D3D11 hook installed and activated.");
            break;
          }
          else
          {
            CLog::Log(LOGDEBUG, __FUNCTION__": Unable ot install and activate D3D11 hook.");
            s_fnOpenAdapter10_2 = nullptr;
            FreeLibrary(m_hDriverModule);
            m_hDriverModule = nullptr;
          }
        }
      }
    }
  }

  if (lstat != ERROR_SUCCESS)
    CLog::LogF(LOGDEBUG, "error open registry key with error %ld.", lstat);

  if (hKey != nullptr)
    RegCloseKey(hKey);
}

void CWinSystemWin32DX::FixRefreshRateIfNecessary(const D3D10DDIARG_CREATERESOURCE* pResource) const
{
  if (pResource && pResource->pPrimaryDesc)
  {
    float refreshRate = RATIONAL_TO_FLOAT(pResource->pPrimaryDesc->ModeDesc.RefreshRate);
    if (refreshRate > 10.0f && refreshRate < 300.0f)
    {
      // interlaced
      if (pResource->pPrimaryDesc->ModeDesc.ScanlineOrdering > DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE)
        refreshRate /= 2;

      uint32_t refreshNum, refreshDen;
      DX::GetRefreshRatio(static_cast<uint32_t>(floor(m_fRefreshRate)), &refreshNum, &refreshDen);
      float diff = fabs(refreshRate - static_cast<float>(refreshNum) / static_cast<float>(refreshDen)) / refreshRate;
      CLog::LogF(LOGDEBUG, "refreshRate: %0.4f, desired: %0.4f, deviation: %.5f, fixRequired: %s, %d",
        refreshRate, m_fRefreshRate, diff, (diff > 0.0005 && diff < 0.1) ? "yes" : "no", pResource->pPrimaryDesc->Flags);
      if (diff > 0.0005 && diff < 0.1)
      {
        pResource->pPrimaryDesc->ModeDesc.RefreshRate.Numerator = refreshNum;
        pResource->pPrimaryDesc->ModeDesc.RefreshRate.Denominator = refreshDen;
        if (pResource->pPrimaryDesc->ModeDesc.ScanlineOrdering > DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE)
          pResource->pPrimaryDesc->ModeDesc.RefreshRate.Numerator *= 2;
        CLog::LogF(LOGDEBUG, "refreshRate fix applied -> %0.3f", RATIONAL_TO_FLOAT(pResource->pPrimaryDesc->ModeDesc.RefreshRate));
      }
    }
  }
}

void APIENTRY HookCreateResource(D3D10DDI_HDEVICE hDevice, const D3D10DDIARG_CREATERESOURCE* pResource, D3D10DDI_HRESOURCE hResource, D3D10DDI_HRTRESOURCE hRtResource)
{
  if (pResource && pResource->pPrimaryDesc)
  {
    DX::Windowing()->FixRefreshRateIfNecessary(pResource);
  }
  s_fnCreateResourceOrig(hDevice, pResource, hResource, hRtResource);
}

HRESULT APIENTRY HookCreateDevice(D3D10DDI_HADAPTER hAdapter, D3D10DDIARG_CREATEDEVICE* pCreateData)
{
  HRESULT hr = s_fnCreateDeviceOrig(hAdapter, pCreateData);
  if (pCreateData->pDeviceFuncs->pfnCreateResource)
  {
    CLog::LogF(LOGDEBUG, "hook into pCreateData->pDeviceFuncs->pfnCreateResource");
    s_fnCreateResourceOrig = pCreateData->pDeviceFuncs->pfnCreateResource;
    pCreateData->pDeviceFuncs->pfnCreateResource = HookCreateResource;
  }
  return hr;
}

HRESULT APIENTRY HookOpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pOpenData)
{
  HRESULT hr = s_fnOpenAdapter10_2(pOpenData);
  if (pOpenData->pAdapterFuncs->pfnCreateDevice)
  {
    CLog::LogF(LOGDEBUG, "hook into pOpenData->pAdapterFuncs->pfnCreateDevice");
    s_fnCreateDeviceOrig = pOpenData->pAdapterFuncs->pfnCreateDevice;
    pOpenData->pAdapterFuncs->pfnCreateDevice = HookCreateDevice;
  }
  return hr;
}

int CWinSystemWin32DX::set12bits()
{
  DXGI_ADAPTER_DESC id = {};
  DX::DeviceResources::Get()->GetAdapterDesc(&id);

  if (id.VendorId != 0x10DE)
    return 0;

  NvAPI_Initialize();

  NvAPI_Status nvStatus = NVAPI_OK;
  NvDisplayHandle hNvDisplay = NULL;

  NvU32 gpuCount = 0;
  NvU32 maxDisplayIndex = 0;
  NvPhysicalGpuHandle ahGPU[NVAPI_MAX_PHYSICAL_GPUS] = {};

  // get the list of displays connected, populate the dynamic components
  nvStatus = NvAPI_EnumPhysicalGPUs(ahGPU, &gpuCount);

  for (NvU32 i = 0; i < gpuCount; ++i)
  {
    NvU32 displayIdCount = 16;
    NvU32 flags = 0;
    NV_GPU_DISPLAYIDS displayIdArray[16] = {};
    displayIdArray[0].version = NV_GPU_DISPLAYIDS_VER;

    nvStatus = NvAPI_GPU_GetConnectedDisplayIds(ahGPU[i], displayIdArray, &displayIdCount, flags);

    if (NVAPI_OK == nvStatus)
    {
      printf("Display count %d\r\n", displayIdCount);

      for (maxDisplayIndex = 0; maxDisplayIndex < displayIdCount; ++maxDisplayIndex)
      {
        printf("Display tested %d\r\n", maxDisplayIndex);

          NV_COLOR_DATA colorData;
          memset(&colorData, 0, sizeof(NV_COLOR_DATA));

          colorData.version = NV_COLOR_DATA_VER;
          colorData.size = sizeof(NV_COLOR_DATA);
          colorData.data.bpc = NV_BPC_12;
	      colorData.data.colorFormat = NV_COLOR_FORMAT_RGB;
          colorData.cmd = NV_COLOR_CMD_SET;

          nvStatus = NvAPI_Disp_ColorControl(displayIdArray[maxDisplayIndex].displayId, &colorData);
      }
    }
  }
  return 0;
}

int CWinSystemWin32DX::WinHDR()
{
  uint32_t pathCount = 0;
  uint32_t modeCount = 0;
  bool success = false;

  if (ERROR_SUCCESS == GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount))
  {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (ERROR_SUCCESS == QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                            &modeCount, modes.data(), nullptr))
    {
      DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getColorInfo = {};
      getColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      getColorInfo.header.size = sizeof(getColorInfo);

      DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {};
      setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
      setColorState.header.size = sizeof(setColorState);

      for (const auto& mode : modes)
      {
        if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
        {
          getColorInfo.header.adapterId.HighPart = mode.adapterId.HighPart;
          getColorInfo.header.adapterId.LowPart = mode.adapterId.LowPart;
          getColorInfo.header.id = mode.id;

          setColorState.header.adapterId.HighPart = mode.adapterId.HighPart;
          setColorState.header.adapterId.LowPart = mode.adapterId.LowPart;
          setColorState.header.id = mode.id;

          if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&getColorInfo.header))
          {
            // HDR is OFF
            if (getColorInfo.advancedColorSupported && !getColorInfo.advancedColorEnabled)
            {
              setColorState.enableAdvancedColor = TRUE;
              CLog::LogF(LOGNOTICE, "Toggle Windows HDR On (OFF => ON).");
              success = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setColorState.header));
              break;
            }
            // HDR is ON
            else if (getColorInfo.advancedColorSupported && getColorInfo.advancedColorEnabled)
            {
              setColorState.enableAdvancedColor = FALSE;
              CLog::LogF(LOGNOTICE, "Toggle Windows HDR Off (ON => OFF).");
              success = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setColorState.header));
              break;
            }
          }
        }
      }
    }
  }
  return 0;
}

int CWinSystemWin32DX::WinHDR_ON()
{
  uint32_t pathCount = 0;
  uint32_t modeCount = 0;
  bool success = false;

  if (ERROR_SUCCESS == GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount))
  {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (ERROR_SUCCESS == QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                            &modeCount, modes.data(), nullptr))
    {
      DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getColorInfo = {};
      getColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      getColorInfo.header.size = sizeof(getColorInfo);

      DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {};
      setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
      setColorState.header.size = sizeof(setColorState);

      for (const auto& mode : modes)
      {
        if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
        {
          getColorInfo.header.adapterId.HighPart = mode.adapterId.HighPart;
          getColorInfo.header.adapterId.LowPart = mode.adapterId.LowPart;
          getColorInfo.header.id = mode.id;

          setColorState.header.adapterId.HighPart = mode.adapterId.HighPart;
          setColorState.header.adapterId.LowPart = mode.adapterId.LowPart;
          setColorState.header.id = mode.id;

          if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&getColorInfo.header))
          {
            // HDR is OFF
            if (getColorInfo.advancedColorSupported && !getColorInfo.advancedColorEnabled)
            {
              setColorState.enableAdvancedColor = TRUE;
              success = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setColorState.header));
              break;
            }
          }
        }
      }
    }
  }
  return 0;
}


int CWinSystemWin32DX::WinHDR_OFF()
{
  uint32_t pathCount = 0;
  uint32_t modeCount = 0;
  bool success = false;

  if (ERROR_SUCCESS == GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount))
  {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (ERROR_SUCCESS == QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                            &modeCount, modes.data(), nullptr))
    {
      DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getColorInfo = {};
      getColorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      getColorInfo.header.size = sizeof(getColorInfo);

      DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState = {};
      setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
      setColorState.header.size = sizeof(setColorState);

      for (const auto& mode : modes)
      {
        if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
        {
          getColorInfo.header.adapterId.HighPart = mode.adapterId.HighPart;
          getColorInfo.header.adapterId.LowPart = mode.adapterId.LowPart;
          getColorInfo.header.id = mode.id;

          setColorState.header.adapterId.HighPart = mode.adapterId.HighPart;
          setColorState.header.adapterId.LowPart = mode.adapterId.LowPart;
          setColorState.header.id = mode.id;

          if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&getColorInfo.header))
          {
            setColorState.enableAdvancedColor = FALSE;
            success = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setColorState.header));
            break;
          }
        }
      }
    }
  }

  return 0;
}



void CWinSystemWin32DX::SetHdrAMD(bool enableHDR,
                                  double rx,
                                  double ry,
                                  double gx,
                                  double gy,
                                  double bx,
                                  double by,
                                  double wx,
                                  double wy,
                                  double minMaster,
                                  double maxMaster,
                                  double maxCLL,
                                  double maxFALL)
{
  DXGI_ADAPTER_DESC id = {};
  DX::DeviceResources::Get()->GetAdapterDesc(&id);

  if (id.VendorId == 0x1002)
  {
    if ((agsInit) && (agsDeInit) && (agsSetDisplayMode))
    {
      AGSContext* context = NULL;
      AGSGPUInfo gpuInfo;
      memset(&gpuInfo, 0, sizeof(gpuInfo));


      if (agsInit(&context, NULL, &gpuInfo) == AGS_SUCCESS)
      {
        for (int i1 = 0; i1 < gpuInfo.numDevices; i1++)
          for (int i2 = 0; i2 < gpuInfo.devices[i1].numDisplays; i2++)
            if (gpuInfo.devices[i1].displays[i2].displayDeviceName)
            {
              AGSDisplaySettings settings;
              ZeroMemory(&settings, sizeof(settings));
              settings.mode =
                  enableHDR ? AGSDisplaySettings::Mode_HDR10_PQ : AGSDisplaySettings::Mode_SDR;
              if (enableHDR)
              {
                settings.chromaticityRedX = (rx);
                settings.chromaticityRedY = (ry);
                settings.chromaticityGreenX = (gx);
                settings.chromaticityGreenY = (gy);
                settings.chromaticityBlueX = (bx);
                settings.chromaticityBlueY = (by);
                settings.chromaticityWhitePointX = (wx);
                settings.chromaticityWhitePointY = (wy);
                settings.minLuminance = (minMaster);
                settings.maxLuminance = (maxMaster);
                settings.maxContentLightLevel = (maxCLL);
                settings.maxFrameAverageLightLevel = (maxFALL);
                settings.flags = 0;
              }
              agsSetDisplayMode(context, i1, i2, &settings) == AGS_SUCCESS;
            }
         agsDeInit(context);
      }
    }
  }
}

void CWinSystemWin32DX::SetHdrMonitorMode(bool enableHDR,
                                          double rx,
                                          double ry,
                                          double gx,
                                          double gy,
                                          double bx,
                                          double by,
                                          double wx,
                                          double wy,
                                          double minMaster,
                                          double maxMaster,
                                          double maxCLL,
                                          double maxFALL)
{
  DXGI_ADAPTER_DESC id = {};
  DX::DeviceResources::Get()->GetAdapterDesc(&id);

  if (id.VendorId == 0x10DE)
  {
    NvAPI_Initialize();

    NvAPI_Status nvStatus = NVAPI_OK;
    NvDisplayHandle hNvDisplay = NULL;

    NvU32 gpuCount = 0;
    NvU32 maxDisplayIndex = 0;
    NvPhysicalGpuHandle ahGPU[NVAPI_MAX_PHYSICAL_GPUS] = {};

    // get the list of displays connected, populate the dynamic components
    nvStatus = NvAPI_EnumPhysicalGPUs(ahGPU, &gpuCount);

    for (NvU32 i = 0; i < gpuCount; ++i)
    {
      NvU32 displayIdCount = 16;
      NvU32 flags = 0;
      NV_GPU_DISPLAYIDS displayIdArray[16] = {};
      displayIdArray[0].version = NV_GPU_DISPLAYIDS_VER;

      nvStatus = NvAPI_GPU_GetConnectedDisplayIds(ahGPU[i], displayIdArray, &displayIdCount, flags);

      if (NVAPI_OK == nvStatus)
      {
        printf("Display count %d\r\n", displayIdCount);

        for (maxDisplayIndex = 0; maxDisplayIndex < displayIdCount; ++maxDisplayIndex)
        {
          printf("Display tested %d\r\n", maxDisplayIndex);

          NV_HDR_CAPABILITIES hdrCapabilities = {};

          hdrCapabilities.version = NV_HDR_CAPABILITIES_VER;

          if (NVAPI_OK == NvAPI_Disp_GetHdrCapabilities(displayIdArray[maxDisplayIndex].displayId,
                                                        &hdrCapabilities))
          {

            if (hdrCapabilities.isST2084EotfSupported)
            {
              NV_HDR_COLOR_DATA hdrColorData = {};

              memset(&hdrColorData, 0, sizeof(hdrColorData));

              hdrColorData.version = NV_HDR_COLOR_DATA_VER;
              hdrColorData.cmd = NV_HDR_CMD_SET;
              hdrColorData.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;

              hdrColorData.hdrMode = enableHDR ? NV_HDR_MODE_UHDA_PASSTHROUGH : NV_HDR_MODE_OFF;
              //						hdrColorData.hdrMode = enableHDR ? NV_HDR_MODE_DOLBY_VISION : NV_HDR_MODE_OFF;

              hdrColorData.static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;

              hdrColorData.mastering_display_data.displayPrimary_x0 = (rx);
              hdrColorData.mastering_display_data.displayPrimary_y0 = (ry);
              hdrColorData.mastering_display_data.displayPrimary_x1 = (gx);
              hdrColorData.mastering_display_data.displayPrimary_y1 = (gy);
              hdrColorData.mastering_display_data.displayPrimary_x2 = (bx);
              hdrColorData.mastering_display_data.displayPrimary_y2 = (by);
              hdrColorData.mastering_display_data.displayWhitePoint_x = (wx);
              hdrColorData.mastering_display_data.displayWhitePoint_y = (wy);
              hdrColorData.mastering_display_data.max_content_light_level = (maxCLL);
              hdrColorData.mastering_display_data.max_display_mastering_luminance = (maxMaster);
              hdrColorData.mastering_display_data.max_frame_average_light_level = (maxFALL);
              hdrColorData.mastering_display_data.min_display_mastering_luminance = (minMaster);

              nvStatus = NvAPI_Disp_HdrColorControl(displayIdArray[maxDisplayIndex].displayId,
                                                    &hdrColorData);
            }
          }
        }
      }
    }
  }
}
