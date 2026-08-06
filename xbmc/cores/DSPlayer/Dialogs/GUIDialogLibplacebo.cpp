/*
 *      Copyright (C) 2024 Team XBMC
 *      http://xbmc.org
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GUIDialogLibplacebo.h"
#include "Application/Application.h"
#include "URL.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/File.h"
#include "resources/LocalizeStrings.h"
#include "profiles/ProfileManager.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "utils/LangCodeExpander.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "input/keyboard/Key.h"
#include "utils/XMLUtils.h"
#include "Filters/RendererSettings.h"
#include "PixelShaderList.h"
#include "cores/playercorefactory/PlayerCoreFactory.h"
#include "Filters/LAVAudioSettings.h"
#include "Filters/LAVVideoSettings.h"
#include "Filters/LAVSplitterSettings.h"
#include "utils/CharsetConverter.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "addons/Skin.h"
#include "settings/SettingUtils.h"
#include "guilib/GUIComponent.h"
#include "Filters/MPCVideoRenderer/Include/libplacebo/options.h"
#include "VideoRenderers/MPCVRRenderer.h"
#include "ServiceBroker.h"

#define SETTING_LIBPLACEBO_DEBAND_ITERATIONS       "video.libplacebo.beband.iterations"
#define SETTING_LIBPLACEBO_DEBAND_THRESHOLD        "video.libplacebo.beband.threshold"
#define SETTING_LIBPLACEBO_DEBAND_RADIUS        "video.libplacebo.beband.radius"
#define SETTING_LIBPLACEBO_DEBAND_GRAIN        "video.libplacebo.beband.grain"
#define SETTING_LIBPLACEBO_SIGMOID_CENTER        "video.libplacebo.sigmoid.center"
#define SETTING_LIBPLACEBO_SIGMOID_SLOPE        "video.libplacebo.sigmoid.slope"
#define SETTING_LIB_PLACEBO_BRIGHTNESS          "video.libplacebo.brightness"
#define SETTING_LIB_PLACEBO_CONTRAST            "video.libplacebo.contrast"
#define SETTING_LIB_PLACEBO_SATURATION          "video.libplacebo.saturation"
#define SETTING_LIB_PLACEBO_GAMMA               "video.libplacebo.gamma"
#define SETTING_LIB_PLACEBO_TEMPERATURE         "video.libplacebo.temperature"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SMOOTHING_PERIOD		"video.libplacebo. peak_detect_params.smoothing_period"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_LOW		"video.libplacebo. peak_detect_params.scene_threshold_low"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_HIGH		"video.libplacebo. peak_detect_params.scene_threshold_high"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_MINIMUM_PEAK		"video.libplacebo. peak_detect_params.minimum_peak"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_PERCENTILE		"video.libplacebo. peak_detect_params.percentile"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_BLACK_CUTOFF		"video.libplacebo. peak_detect_params.black_cutoff"
#define SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ALLOW_DELAYED		"video.libplacebo. peak_detect_params.allow_delayed"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_MAPPING		"video.libplacebo. color_map_params.gamut_mapping"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE		"video.libplacebo. color_map_params.gamut_constants.perceptual_deadzone"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH		"video.libplacebo. color_map_params.gamut_constants.perceptual_strength"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA		"video.libplacebo. color_map_params.gamut_constants.colorimetric_gamma"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_SOFTCLIP_KNEE		"video.libplacebo. color_map_params.gamut_constants.softclip_knee"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_SOFTCLIP_DESAT		"video.libplacebo. color_map_params.gamut_constants.softclip_desat"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE0		"video.libplacebo. color_map_params.lut3d_size[0]"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE1		"video.libplacebo. color_map_params.lut3d_size[1]"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE2		"video.libplacebo. color_map_params.lut3d_size[2]"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_TRICUBIC		"video.libplacebo. color_map_params.lut3d_tricubic"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_EXPANSION		"video.libplacebo. color_map_params.gamut_expansion"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_MAPPING_FUNCTION		"video.libplacebo. color_map_params.tone_mapping_function"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_ADAPTATION		"video.libplacebo. color_map_params.tone_constants.knee_adaptation"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_MINIMUM		"video.libplacebo. color_map_params.tone_constants.knee_minimum"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_MAXIMUM		"video.libplacebo. color_map_params.tone_constants.knee_maximum"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_DEFAULT		"video.libplacebo. color_map_params.tone_constants.knee_default"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_OFFSET		"video.libplacebo. color_map_params.tone_constants.knee_offset"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SLOPE_TUNING		"video.libplacebo. color_map_params.tone_constants.slope_tuning"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SLOPE_OFFSET		"video.libplacebo. color_map_params.tone_constants.slope_offset"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SPLINE_CONTRAST		"video.libplacebo. color_map_params.tone_constants.spline_contrast"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_REINHARD_CONTRAST		"video.libplacebo. color_map_params.tone_constants.reinhard_contrast"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_LINEAR_KNEE		"video.libplacebo. color_map_params.tone_constants.linear_knee"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_EXPOSURE		"video.libplacebo. color_map_params.tone_constants.exposure"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_INVERSE_TONE_MAPPING		"video.libplacebo. color_map_params.inverse_tone_mapping"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_METADATA		"video.libplacebo. color_map_params.metadata"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT_SIZE		"video.libplacebo. color_map_params.lut_size"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_CONTRAST_RECOVERY		"video.libplacebo. color_map_params.contrast_recovery"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_CONTRAST_SMOOTHNESS		"video.libplacebo. color_map_params.contrast_smoothness"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_FORCE_TONE_MAPPING_LUT		"video.libplacebo. color_map_params.force_tone_mapping_lut"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_LUT		"video.libplacebo. color_map_params.visualize_lut"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_X0		"video.libplacebo. color_map_params.visualize_rect.x0"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_Y0		"video.libplacebo. color_map_params.visualize_rect.y0"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_X1		"video.libplacebo. color_map_params.visualize_rect.x1"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_Y1		"video.libplacebo. color_map_params.visualize_rect.y1"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_HUE		"video.libplacebo. color_map_params.visualize_hue"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_THETA		"video.libplacebo. color_map_params.visualize_theta"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_SHOW_CLIPPING		"video.libplacebo. color_map_params.show_clipping"
#define SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_MAPPING_PARAM		"video.libplacebo. color_map_params.tone_mapping_param"
#define SETTING_LIB_PLACEBO_DITHER_PARAMS		"video.libplacebo. dither_params"
#define SETTING_LIB_PLACEBO_DITHER_PARAMS		"video.libplacebo. dither_params"
#define SETTING_LIB_PLACEBO_DITHER_PARAMS_METHOD		"video.libplacebo. dither_params.method"
#define SETTING_LIB_PLACEBO_DITHER_PARAMS_LUT_SIZE		"video.libplacebo. dither_params.lut_size"
#define SETTING_LIB_PLACEBO_DITHER_PARAMS_TEMPORAL		"video.libplacebo. dither_params.temporal"	
#define SETTING_LIB_PLACEBO_CONE_PARAMS_CONES		"video.libplacebo. cone_params.cones"
#define SETTING_LIB_PLACEBO_CONE_PARAMS_STRENGTH		"video.libplacebo. cone_params.strength"
#define SETTING_LIB_PLACEBO_BLEND_PARAMS_SRC_RGB		"video.libplacebo. blend_params.src_rgb"
#define SETTING_LIB_PLACEBO_BLEND_PARAMS_SRC_ALPHA		"video.libplacebo. blend_params.src_alpha"
#define SETTING_LIB_PLACEBO_BLEND_PARAMS_DST_RGB		"video.libplacebo. blend_params.dst_rgb"
#define SETTING_LIB_PLACEBO_BLEND_PARAMS_DST_ALPHA		"video.libplacebo. blend_params.dst_alpha"
#define SETTING_LIB_PLACEBO_DEINTERLACE_PARAMS_ALGO		"video.libplacebo. deinterlace_params.algo"
#define SETTING_LIB_PLACEBO_DEINTERLACE_SKIP_SPATIAL		"video.libplacebo.deinterlace_skip_spatial"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M00		"video.libplacebo. distort_params.transform.mat.m[0][0]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M11		"video.libplacebo. distort_params.transform.mat.m[1][1]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M01		"video.libplacebo. distort_params.transform.mat.m[0][1]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M10		"video.libplacebo. distort_params.transform.mat.m[1][0]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_C0		"video.libplacebo. distort_params.transform.c[0]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_C1		"video.libplacebo. distort_params.transform.c[1]"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_UNSCALED		"video.libplacebo. distort_params.unscaled"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_CONSTRAIN		"video.libplacebo. distort_params.constrain"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_BICUBIC		"video.libplacebo. distort_params.bicubic"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_ADDRESS_MODE		"video.libplacebo. distort_params.address_mode"
#define SETTING_LIB_PLACEBO_DISTORT_PARAMS_ALPHA_MODE		"video.libplacebo. distort_params.alpha_mode"
#define SETTING_LIB_PLACEBO_PARAMS_ERROR_DIFFUSION		"video.libplacebo. params.error_diffusion"
#define SETTING_LIB_PLACEBO_PARAMS_LUT_TYPE		"video.libplacebo. params.lut_type"
#define SETTING_LIB_PLACEBO_PARAMS_BACKGROUND		"video.libplacebo. params.background"
#define SETTING_LIB_PLACEBO_PARAMS_BORDER		"video.libplacebo. params.border"
#define SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR0		"video.libplacebo. params.background_color[0]"
#define SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR1		"video.libplacebo. params.background_color[1]"
#define SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR2		"video.libplacebo. params.background_color[2]"
#define SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_TRANSPARENCY		"video.libplacebo. params.background_transparency"
#define SETTING_LIB_PLACEBO_PARAMS_SKIP_TARGET_CLEARING		"video.libplacebo. params.skip_target_clearing"
#define SETTING_LIB_PLACEBO_PARAMS_CORNER_ROUNDING		"video.libplacebo. params.corner_rounding"
#define SETTING_LIB_PLACEBO_PARAMS_BLEND_AGAINST_TILES		"video.libplacebo. params.blend_against_tiles"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS00		"video.libplacebo. params.tile_colors[0][0]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS01		"video.libplacebo. params.tile_colors[0][1]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS02		"video.libplacebo. params.tile_colors[0][2]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS10		"video.libplacebo. params.tile_colors[1][0]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS11		"video.libplacebo. params.tile_colors[1][1]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS12		"video.libplacebo. params.tile_colors[1][2]"
#define SETTING_LIB_PLACEBO_PARAMS_TILE_SIZE		"video.libplacebo. params.tile_size"
#define SETTING_LIB_PLACEBO_PARAMS_SKIP_ANTI_ALIASING		"video.libplacebo. params.skip_anti_aliasing"
#define SETTING_LIB_PLACEBO_PARAMS_LUT_ENTRIES		"video.libplacebo. params.lut_entries"
#define SETTING_LIB_PLACEBO_PARAMS_POLAR_CUTOFF		"video.libplacebo. params.polar_cutoff"
#define SETTING_LIB_PLACEBO_PARAMS_PRESERVE_MIXING_CACHE		"video.libplacebo. params.preserve_mixing_cache"
#define SETTING_LIB_PLACEBO_PARAMS_SKIP_CACHING_SINGLE_FRAME		"video.libplacebo. params.skip_caching_single_frame"
#define SETTING_LIB_PLACEBO_PARAMS_DISABLE_LINEAR_SCALING		"video.libplacebo. params.disable_linear_scaling"
#define SETTING_LIB_PLACEBO_PARAMS_DISABLE_BUILTIN_SCALERS		"video.libplacebo. params.disable_builtin_scalers"
#define SETTING_LIB_PLACEBO_PARAMS_CORRECT_SUBPIXEL_OFFSETS		"video.libplacebo. params.correct_subpixel_offsets"
#define SETTING_LIB_PLACEBO_PARAMS_IGNORE_ICC_PROFILES		"video.libplacebo. params.ignore_icc_profiles"
#define SETTING_LIB_PLACEBO_PARAMS_FORCE_DITHER		"video.libplacebo. params.force_dither"
#define SETTING_LIB_PLACEBO_PARAMS_DISABLE_DITHER_GAMMA_CORRECTION		"video.libplacebo. params.disable_dither_gamma_correction"
#define SETTING_LIB_PLACEBO_PARAMS_DISABLE_FBOS		"video.libplacebo. params.disable_fbos"
#define SETTING_LIB_PLACEBO_PARAMS_FORCE_LOW_BIT_DEPTH_FBOS		"video.libplacebo. params.force_low_bit_depth_fbos"
#define SETTING_LIB_PLACEBO_PARAMS_DYNAMIC_CONSTANTS		"video.libplacebo. params.dynamic_constants"

#define CreateGroup(thegroup,thecategory) std::shared_ptr<CSettingGroup> thegroup = AddGroup(thecategory); if (thegroup == NULL) {CLog::Log(LOGERROR, "CGUIDialogLibplacebo: unable to setup settings");  return; }

using namespace std;

CGUIDialogLibplacebo::CGUIDialogLibplacebo()
  : CGUIDialogSettingsManualBase(WINDOW_DIALOG_LIBPLACEBO_OPTIONS, "DialogSettings.xml")
{
}

CGUIDialogLibplacebo::~CGUIDialogLibplacebo()
{ 
}

void CGUIDialogLibplacebo::ShowLibplaceboOptions()
{
  
}

void CGUIDialogLibplacebo::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(55200);

  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_OKAY_BUTTON);
  SET_CONTROL_HIDDEN(CONTROL_SETTINGS_CUSTOM_BUTTON);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 15067);
}

void CGUIDialogLibplacebo::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  bool usePopup = CServiceBroker::GetGUI()->GetSkinInfo()->HasSkinFile("DialogSlider.xml");

  std::shared_ptr<CSettingCategory> category = AddCategory("libplacebosettings", 55201);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogLibplacebo: unable to setup settings");
    return;
  }
  // get all necessary setting groups
  CreateGroup(groupDeband, category);
  CreateGroup(groupSigmoid, category);
  CreateGroup(groupColorAjustment, category);
  CreateGroup(groupPeakDetect, category);
  CreateGroup(groupColorMap, category);
  CreateGroup(groupDither, category);
  CreateGroup(groupIcc, category);
  CreateGroup(groupCone, category);
  CreateGroup(groupBlend, category);
  CreateGroup(groupDeinterlace, category);
  CreateGroup(groupDistort, category);
  pl_options placeboOptions = pl_options_alloc(NULL);
  //CMPCVRRenderer::Get()->GetPlHelper().
  pl_options_reset(placeboOptions, &pl_render_default_params);
  //value, 21468 minimum step maximum
  AddSlider(groupDeband, SETTING_LIBPLACEBO_DEBAND_ITERATIONS, 55202, SettingLevel::Basic, placeboOptions->deband_params.iterations, 21469, 0, 1, 8);
  AddSlider(groupDeband, SETTING_LIBPLACEBO_DEBAND_THRESHOLD, 55203, SettingLevel::Basic, placeboOptions->deband_params.threshold, 21469, (float)0, (float)1, (float)256);
  AddSlider(groupDeband, SETTING_LIBPLACEBO_DEBAND_RADIUS, 55204, SettingLevel::Basic, placeboOptions->deband_params.radius, 21469, (float)0, (float)1, (float)256);
  AddSlider(groupDeband, SETTING_LIBPLACEBO_DEBAND_GRAIN, 55205, SettingLevel::Basic, placeboOptions->deband_params.grain, 21469, (float)0, (float)1, (float)512);

  AddSlider(groupSigmoid, SETTING_LIBPLACEBO_SIGMOID_CENTER, 55206, SettingLevel::Basic, placeboOptions->sigmoid_params.center, 21469, 0.0, 0.1, 1.0);
  AddSlider(groupSigmoid, SETTING_LIBPLACEBO_SIGMOID_SLOPE, 55207, SettingLevel::Basic, placeboOptions->sigmoid_params.slope, 21469, 0.0, 1.0, 100.0);

  AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_BRIGHTNESS, 55207, SettingLevel::Basic, placeboOptions->color_adjustment.brightness, 21469, -1.0, 0.1, 1.0);
  AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_CONTRAST, 55207, SettingLevel::Basic, placeboOptions->color_adjustment.contrast, 21469, 0.0, 0.1, 10.0);
  AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_SATURATION, 55207, SettingLevel::Basic, placeboOptions->color_adjustment.saturation, 21469, 0.0, 0.1, 10.0);
  AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_GAMMA, 55207, SettingLevel::Basic, placeboOptions->color_adjustment.gamma, 21469, 0.0, 0.1, 10.0);
  AddSlider(groupColorAjustment, SETTING_LIB_PLACEBO_TEMPERATURE, 55207, SettingLevel::Basic, placeboOptions->color_adjustment.temperature, 21469, 3000.0, 10000.0, 5.0);
  //.min = (2500  - 6500) / 3500.0, // see `pl_white_from_temp`
  //.max = (25000 - 6500) / 3500.0),
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SMOOTHING_PERIOD, 55208, SettingLevel::Basic, placeboOptions->peak_detect_params.smoothing_period, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_LOW, 55209, SettingLevel::Basic, placeboOptions->peak_detect_params.scene_threshold_low, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_SCENE_THRESHOLD_HIGH, 55210, SettingLevel::Basic, placeboOptions->peak_detect_params.scene_threshold_high, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_MINIMUM_PEAK, 55211, SettingLevel::Basic, placeboOptions->peak_detect_params.minimum_peak, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_PERCENTILE, 55212, SettingLevel::Basic, placeboOptions->peak_detect_params.percentile, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_BLACK_CUTOFF, 55213, SettingLevel::Basic, placeboOptions->peak_detect_params.black_cutoff, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PEAK_DETECT_PARAMS_ALLOW_DELAYED, 55214, SettingLevel::Basic, placeboOptions->peak_detect_params.allow_delayed, 21469, 3000.0, 10000.0, 5.0);
  AddToggle(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_MAPPING, 55215, SettingLevel::Basic, placeboOptions->color_map_params.gamut_mapping);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_PERCEPTUAL_DEADZONE, 55216, SettingLevel::Basic, placeboOptions->color_map_params.gamut_constants.perceptual_deadzone, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_PERCEPTUAL_STRENGTH, 55217, SettingLevel::Basic, placeboOptions->color_map_params.gamut_constants.perceptual_strength, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_COLORIMETRIC_GAMMA, 55218, SettingLevel::Basic, placeboOptions->color_map_params.gamut_constants.colorimetric_gamma, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_SOFTCLIP_KNEE, 55219, SettingLevel::Basic, placeboOptions->color_map_params.gamut_constants.softclip_knee, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_CONSTANTS_SOFTCLIP_DESAT, 55220, SettingLevel::Basic, placeboOptions->color_map_params.gamut_constants.softclip_desat, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE0, 55221, SettingLevel::Basic, placeboOptions->color_map_params.lut3d_size[0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE1, 55222, SettingLevel::Basic, placeboOptions->color_map_params.lut3d_size[1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_SIZE2, 55223, SettingLevel::Basic, placeboOptions->color_map_params.lut3d_size[2], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT3D_TRICUBIC, 55224, SettingLevel::Basic, placeboOptions->color_map_params.lut3d_tricubic, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_GAMUT_EXPANSION, 55225, SettingLevel::Basic, placeboOptions->color_map_params.gamut_expansion, 21469, 3000.0, 10000.0, 5.0);
  //AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_MAPPING_FUNCTION, 55207, SettingLevel::Basic, placeboOptions->color_map_params.tone_mapping_function, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_ADAPTATION, 55227, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.knee_adaptation, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_MINIMUM, 55228, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.knee_minimum, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_MAXIMUM, 55229, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.knee_maximum, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_DEFAULT, 55230, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.knee_default, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_KNEE_OFFSET, 55231, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.knee_offset, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SLOPE_TUNING, 55232, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.slope_tuning, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SLOPE_OFFSET, 55233, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.slope_offset, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_SPLINE_CONTRAST, 55234, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.spline_contrast, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_REINHARD_CONTRAST, 55235, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.reinhard_contrast, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_LINEAR_KNEE, 55236, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.linear_knee, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_CONSTANTS_EXPOSURE, 55237, SettingLevel::Basic, placeboOptions->color_map_params.tone_constants.exposure, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_INVERSE_TONE_MAPPING, 55238, SettingLevel::Basic, placeboOptions->color_map_params.inverse_tone_mapping, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_METADATA, 55239, SettingLevel::Basic, placeboOptions->color_map_params.metadata, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_LUT_SIZE, 55240, SettingLevel::Basic, placeboOptions->color_map_params.lut_size, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_CONTRAST_RECOVERY, 55241, SettingLevel::Basic, placeboOptions->color_map_params.contrast_recovery, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_CONTRAST_SMOOTHNESS, 55242, SettingLevel::Basic, placeboOptions->color_map_params.contrast_smoothness, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_FORCE_TONE_MAPPING_LUT, 55243, SettingLevel::Basic, placeboOptions->color_map_params.force_tone_mapping_lut, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_LUT, 55244, SettingLevel::Basic, placeboOptions->color_map_params.visualize_lut, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_X0, 55245, SettingLevel::Basic, placeboOptions->color_map_params.visualize_rect.x0, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_Y0, 55246, SettingLevel::Basic, placeboOptions->color_map_params.visualize_rect.y0, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_X1, 55247, SettingLevel::Basic, placeboOptions->color_map_params.visualize_rect.x1, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_RECT_Y1, 55248, SettingLevel::Basic, placeboOptions->color_map_params.visualize_rect.y1, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_HUE, 55249, SettingLevel::Basic, placeboOptions->color_map_params.visualize_hue, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_VISUALIZE_THETA, 55250, SettingLevel::Basic, placeboOptions->color_map_params.visualize_theta, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_SHOW_CLIPPING, 55251, SettingLevel::Basic, placeboOptions->color_map_params.show_clipping, 21469, 3000.0, 10000.0, 5.0);
  //AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_COLOR_MAP_PARAMS_TONE_MAPPING_PARAM, 55252, SettingLevel::Basic, placeboOptions->color_map_params.tone_mapping_param, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DITHER_PARAMS_METHOD, 55255, SettingLevel::Basic, placeboOptions->dither_params.method, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DITHER_PARAMS_LUT_SIZE, 55256, SettingLevel::Basic, placeboOptions->dither_params.lut_size, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DITHER_PARAMS_TEMPORAL, 55257, SettingLevel::Basic, placeboOptions->dither_params.temporal, 21469, 3000.0, 10000.0, 5.0);
  //AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_CONE_PARAMS_CONES, 55258, SettingLevel::Basic, placeboOptions->cone_params.cones, 21469, 3000.0, 10000.0, 5.0);
  //AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_CONE_PARAMS_STRENGTH, 55259, SettingLevel::Basic, placeboOptions->cone_params.strength, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_BLEND_PARAMS_SRC_RGB, 55262, SettingLevel::Basic, placeboOptions->blend_params.src_rgb, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_BLEND_PARAMS_SRC_ALPHA, 55263, SettingLevel::Basic, placeboOptions->blend_params.src_alpha, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_BLEND_PARAMS_DST_RGB, 55264, SettingLevel::Basic, placeboOptions->blend_params.dst_rgb, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_BLEND_PARAMS_DST_ALPHA, 55265, SettingLevel::Basic, placeboOptions->blend_params.dst_alpha , 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DEINTERLACE_PARAMS_ALGO, 55266, SettingLevel::Basic, placeboOptions->deinterlace_params.algo, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DEINTERLACE_SKIP_SPATIAL, 55267, SettingLevel::Basic, placeboOptions->deinterlace_params.skip_spatial_check, 21469, 3000.0, 10000.0, 5.0);
  //add preset
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M00, 55270, SettingLevel::Basic, placeboOptions->distort_params.transform.mat.m[0][0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M11, 55271, SettingLevel::Basic, placeboOptions->distort_params.transform.mat.m[1][1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M01, 55272, SettingLevel::Basic, placeboOptions->distort_params.transform.mat.m[0][1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_MAT_M10, 55273, SettingLevel::Basic, placeboOptions->distort_params.transform.mat.m[1][0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_C0, 55274, SettingLevel::Basic, placeboOptions->distort_params.transform.c[0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_TRANSFORM_C1, 55275, SettingLevel::Basic, placeboOptions->distort_params.transform.c[1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_UNSCALED, 55276, SettingLevel::Basic, placeboOptions->distort_params.unscaled, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_CONSTRAIN, 55277, SettingLevel::Basic, placeboOptions->distort_params.constrain, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_BICUBIC, 55278, SettingLevel::Basic, placeboOptions->distort_params.bicubic, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_ADDRESS_MODE, 55279, SettingLevel::Basic, placeboOptions->distort_params.address_mode, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_DISTORT_PARAMS_ALPHA_MODE, 55280, SettingLevel::Basic, placeboOptions->distort_params.alpha_mode, 21469, 3000.0, 10000.0, 5.0);
  //pl_error_diffusion_kernel
  //AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_ERROR_DIFFUSION, 55281, SettingLevel::Basic, placeboOptions->params.error_diffusion, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_LUT_TYPE, 55282, SettingLevel::Basic, placeboOptions->params.lut_type, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BACKGROUND, 55283, SettingLevel::Basic, placeboOptions->params.background, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BORDER, 55284, SettingLevel::Basic, placeboOptions->params.border, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR0, 55285, SettingLevel::Basic, placeboOptions->params.background_color[0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR1, 55286, SettingLevel::Basic, placeboOptions->params.background_color[1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_COLOR2, 55287, SettingLevel::Basic, placeboOptions->params.background_color[2], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BACKGROUND_TRANSPARENCY, 55288, SettingLevel::Basic, placeboOptions->params.background_transparency, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_SKIP_TARGET_CLEARING, 55289, SettingLevel::Basic, placeboOptions->params.skip_target_clearing, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_CORNER_ROUNDING, 55290, SettingLevel::Basic, placeboOptions->params.corner_rounding, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_BLEND_AGAINST_TILES, 55291, SettingLevel::Basic, placeboOptions->params.blend_against_tiles, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS00, 55292, SettingLevel::Basic, placeboOptions->params.tile_colors[0][0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS01, 55293, SettingLevel::Basic, placeboOptions->params.tile_colors[0][1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS02, 55294, SettingLevel::Basic, placeboOptions->params.tile_colors[0][2], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS10, 55295, SettingLevel::Basic, placeboOptions->params.tile_colors[1][0], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS11, 55296, SettingLevel::Basic, placeboOptions->params.tile_colors[1][1], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_COLORS12, 55297, SettingLevel::Basic, placeboOptions->params.tile_colors[1][2], 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_TILE_SIZE, 55298, SettingLevel::Basic, placeboOptions->params.tile_size, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_SKIP_ANTI_ALIASING, 55299, SettingLevel::Basic, placeboOptions->params.skip_anti_aliasing, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_LUT_ENTRIES, 55300, SettingLevel::Basic, placeboOptions->params.lut_entries, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_POLAR_CUTOFF, 55301, SettingLevel::Basic, placeboOptions->params.polar_cutoff, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_PRESERVE_MIXING_CACHE, 55302, SettingLevel::Basic, placeboOptions->params.preserve_mixing_cache, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_SKIP_CACHING_SINGLE_FRAME, 55303, SettingLevel::Basic, placeboOptions->params.skip_caching_single_frame, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_DISABLE_LINEAR_SCALING, 55304, SettingLevel::Basic, placeboOptions->params.disable_linear_scaling, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_DISABLE_BUILTIN_SCALERS, 55305, SettingLevel::Basic, placeboOptions->params.disable_builtin_scalers, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_CORRECT_SUBPIXEL_OFFSETS, 55306, SettingLevel::Basic, placeboOptions->params.correct_subpixel_offsets, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_IGNORE_ICC_PROFILES, 55307, SettingLevel::Basic, placeboOptions->params.ignore_icc_profiles, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_FORCE_DITHER, 55308, SettingLevel::Basic, placeboOptions->params.force_dither, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_DISABLE_DITHER_GAMMA_CORRECTION, 55309, SettingLevel::Basic, placeboOptions->params.disable_dither_gamma_correction, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_DISABLE_FBOS, 55310, SettingLevel::Basic, placeboOptions->params.disable_fbos, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_FORCE_LOW_BIT_DEPTH_FBOS, 55311, SettingLevel::Basic, placeboOptions->params.force_low_bit_depth_fbos, 21469, 3000.0, 10000.0, 5.0);
  AddSlider(groupSigmoid, SETTING_LIB_PLACEBO_PARAMS_DYNAMIC_CONSTANTS, 55312, SettingLevel::Basic, placeboOptions->params.dynamic_constants, 21469, 3000.0, 10000.0, 5.0);



  /*void nk_property_int(struct nk_context* ctx, const char* name, int min, int* val, int max, int step, float inc_per_pixel);
    Convert to human-friendly temperature values for display
     int temp = (int) roundf(adj->temperature * 3500) + 6500;
     nk_property_int(nk, "Temperature (K)", 3000, &temp, 10000, 10, 5);
     adj->temperature = (temp - 6500) / 3500.0;*/


  /* 
      // Backing storage for all of the various rendering parameters. Whether
    // or not these params are active is determined by whether or not
    // `params.*_params` is set to this address or NULL.
    done struct pl_deband_params deband_params;
    struct pl_sigmoid_params sigmoid_params;
    struct pl_color_adjustment color_adjustment;
    struct pl_peak_detect_params peak_detect_params;
    struct pl_color_map_params color_map_params;
    struct pl_dither_params dither_params;
    struct pl_icc_params icc_params PL_DEPRECATED_IN(v6.327);
    struct pl_cone_params cone_params;
    struct pl_blend_params blend_params;
    struct pl_deinterlace_params deinterlace_params;
    struct pl_distort_params distort_params;

    // Backing storage for "custom" scalers. `params.upscaler` etc. will
    // always be a pointer either to a built-in pl_filter_config, or one of
    // these structs. `name`, `description` and `allowed` will always be
    // valid for the respective type of filter config.
    struct pl_filter_config upscaler;
    struct pl_filter_config downscaler;
    struct pl_filter_config plane_upscaler;
    struct pl_filter_config plane_downscaler;
    struct pl_filter_config frame_mixer;
  
  
  
  void nk_property_int(struct nk_context* ctx, const char* name, int min, int* val, int max, int step, float inc_per_pixel);
  * sigmoid_params
     nk_property_float(nk, "Sigmoid center", 0, &spar->center, 1, 0.1, 0.01);
     nk_property_float(nk, "Sigmoid slope", 0, &spar->slope, 100, 1, 0.1);
     
     color_adjustment
     nk_property_float(nk, "Brightness", -1, &adj->brightness, 1, 0.1, 0.005);
     nk_property_float(nk, "Contrast", 0, &adj->contrast, 10, 0.1, 0.005);
     nk_property_float(nk, "Saturation", 0, &adj->saturation, 10, 0.1, 0.005);
     nk_property_float(nk, "Gamma", 0, &adj->gamma, 10, 0.1, 0.005);
     Convert to human-friendly temperature values for display
     int temp = (int) roundf(adj->temperature * 3500) + 6500;
     nk_property_int(nk, "Temperature (K)", 3000, &temp, 10000, 10, 5);
     adj->temperature = (temp - 6500) / 3500.0;
     */

    /* deinterlace_params
     static const char *deint_algos[PL_DEINTERLACE_ALGORITHM_COUNT] = {
                [PL_DEINTERLACE_WEAVE]  = "Field weaving (no-op)",
                [PL_DEINTERLACE_BOB]    = "Naive bob (line doubling)",
                [PL_DEINTERLACE_YADIF]  = "Yadif (\"yet another deinterlacing filter\")",
            };

            nk_label(nk, "Deinterlacing algorithm", NK_TEXT_LEFT);
            dpar->algo = nk_combo(nk, deint_algos, PL_DEINTERLACE_ALGORITHM_COUNT,
                                  dpar->algo, 16, nk_vec2(nk_widget_width(nk), 300));
    

    struct pl_distort_params *dpar = &opts->distort_params;
            nk_layout_row_dynamic(nk, 24, 2);
            par->distort_params = nk_check_label(nk, "Enable", par->distort_params) ? dpar : NULL;
            if (nk_button_label(nk, "Reset settings"))
                *dpar = pl_distort_default_params;

            static const char *address_modes[PL_TEX_ADDRESS_MODE_COUNT] = {
                [PL_TEX_ADDRESS_CLAMP]  = "Clamp edges",
                [PL_TEX_ADDRESS_REPEAT] = "Repeat edges",
                [PL_TEX_ADDRESS_MIRROR] = "Mirror edges",
            };

            nk_checkbox_label(nk, "Constrain bounds", &dpar->constrain);
            dpar->address_mode = nk_combo(nk, address_modes, PL_TEX_ADDRESS_MODE_COUNT,
                                          dpar->address_mode, 16, nk_vec2(nk_widget_width(nk), 100));
            bool alpha = nk_check_label(nk, "Transparent background", dpar->alpha_mode);
            dpar->alpha_mode = alpha ? PL_ALPHA_INDEPENDENT : PL_ALPHA_NONE;
            nk_checkbox_label(nk, "Bicubic interpolation", &dpar->bicubic);

            struct pl_transform2x2 *tf = &dpar->transform;
            nk_property_float(nk, "Scale X", -10.0, &tf->mat.m[0][0], 10.0, 0.1, 0.005);
            nk_property_float(nk, "Shear X", -10.0, &tf->mat.m[0][1], 10.0, 0.1, 0.005);
            nk_property_float(nk, "Shear Y", -10.0, &tf->mat.m[1][0], 10.0, 0.1, 0.005);
            nk_property_float(nk, "Scale Y", -10.0, &tf->mat.m[1][1], 10.0, 0.1, 0.005);
            nk_property_float(nk, "Offset X", -10.0, &tf->c[0], 10.0, 0.1, 0.005);
            nk_property_float(nk, "Offset Y", -10.0, &tf->c[1], 10.0, 0.1, 0.005);

            float zoom_ref = fabsf(tf->mat.m[0][0] * tf->mat.m[1][1] -
                                   tf->mat.m[0][1] * tf->mat.m[1][0]);
            zoom_ref = logf(fmaxf(zoom_ref, 1e-4));
            float zoom = zoom_ref;
            nk_property_float(nk, "log(Zoom)", -10.0, &zoom, 10.0, 0.1, 0.005);
            pl_transform2x2_scale(tf, expf(zoom - zoom_ref));

            float angle_ref = (atan2f(tf->mat.m[1][0], tf->mat.m[1][1]) -
                               atan2f(tf->mat.m[0][1], tf->mat.m[0][0])) / 2;
            angle_ref = fmodf(angle_ref * 180/M_PI + 540, 360) - 180;
            float angle = angle_ref;
            nk_property_float(nk, "Rotate (°)", -200, &angle, 200, -5, -0.2);
            float angle_delta = (angle - angle_ref) * M_PI / 180;
            const pl_matrix2x2 rot = pl_matrix2x2_rotation(angle_delta);
            pl_matrix2x2_rmul(&rot, &tf->mat);

            bool flip_ox = nk_button_label(nk, "Flip output X");
            bool flip_oy = nk_button_label(nk, "Flip output Y");
            bool flip_ix = nk_button_label(nk, "Flip input X");
            bool flip_iy = nk_button_label(nk, "Flip input Y");
            if (flip_ox ^ flip_ix)
                tf->mat.m[0][0] = -tf->mat.m[0][0];
            if (flip_ox ^ flip_iy)
                tf->mat.m[0][1] = -tf->mat.m[0][1];
            if (flip_oy ^ flip_ix)
                tf->mat.m[1][0] = -tf->mat.m[1][0];
            if (flip_oy ^ flip_iy)
                tf->mat.m[1][1] = -tf->mat.m[1][1];
            if (flip_ox)
                tf->c[0] = -tf->c[0];
            if (flip_oy)
                tf->c[1] = -tf->c[1];

      struct pl_cone_params *cpar = &opts->cone_params;
            nk_layout_row_dynamic(nk, 24, 2);
            par->cone_params = nk_check_label(nk, "Color blindness", par->cone_params) ? cpar : NULL;
            if (nk_button_label(nk, "Default values"))
                *cpar = pl_vision_normal;
            nk_layout_row(nk, NK_DYNAMIC, 24, 5, (float[]){ 0.25, 0.25/3, 0.25/3, 0.25/3, 0.5 });
            nk_label(nk, "Cone model:", NK_TEXT_LEFT);
            unsigned int cones = cpar->cones;
            nk_checkbox_flags_label(nk, "L", &cones, PL_CONE_L);
            nk_checkbox_flags_label(nk, "M", &cones, PL_CONE_M);
            nk_checkbox_flags_label(nk, "S", &cones, PL_CONE_S);
            cpar->cones = cones;
            nk_property_float(nk, "Sensitivity", 0.0, &cpar->strength, 5.0, 0.1, 0.01);
  */

}

void CGUIDialogLibplacebo::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  
  CGUIDialogSettingsManualBase::OnSettingChanged(setting);
  const std::string &settingId = setting->GetId();

}

void CGUIDialogLibplacebo::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingAction(setting);
  const std::string &settingId = setting->GetId();

  
}