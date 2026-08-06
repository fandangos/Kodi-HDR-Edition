/*
 *  Copyright (C) 2010-2013 Eduard Kytmanov
 *  http://www.avmedia.su
 *
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
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#if HAS_DS_PLAYER
#include "DSPlayerDatabase.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "dbwrappers/dataset.h"
#include "filesystem/StackDirectory.h"
#include "video/VideoInfoTag.h"
#include "utils/StringUtils.h"
#include "Filters/LAVAudioSettings.h"
#include "Filters/LAVVideoSettings.h"
#include "Filters/LAVSplitterSettings.h"
#include "utils/CharsetConverter.h"
#include "utils/JSONVariantWriter.h"
#include "utils/JSONVariantParser.h"
#include "settings/MediaSettings.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "settings/SettingsComponent.h"
#include "video/VideoFileItemClassify.h"

using namespace XFILE;

CEdition::CEdition()
  :editionNumber(0)
  , editionName("")
{
}

bool CEdition::IsSet() const
{
  return (!editionName.empty() && editionNumber >= 0);
}

// Upstream deleted CDatabase's default constructor; every database now names its type, which
// is the label the upgrade reporting uses. This one is the player's own, so it is not in
// Kodi's DatabaseTypes.h.
CDSPlayerDatabase::CDSPlayerDatabase(void) : CDatabase("dsplayer")
{
}


CDSPlayerDatabase::~CDSPlayerDatabase(void)
{
}

bool CDSPlayerDatabase::Open()
{
  return CDatabase::Open(CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->m_databaseDSPlayer);
}

void CDSPlayerDatabase::CreateTables()
{
  CLog::Log(LOGINFO, "create edition table");
  m_pDS->exec("CREATE TABLE edition (idEdition integer primary key, file text, editionName text, editionNumber integer)\n");

  CLog::Log(LOGINFO, "create madvr setting table");
  m_pDS->exec("CREATE TABLE madvrFileSettings ( file text, Resolution integer, TvShowName txt, madvrJson txt)\n");

  CLog::Log(LOGINFO, "create madvr settings for tvshow table");
  m_pDS->exec("CREATE TABLE madvrTvShowSettings ( TvShowName txt, Resolution integer, madvrJson txt)\n");

  CLog::Log(LOGINFO, "create madvr settings for resolution table");
  m_pDS->exec("CREATE TABLE madvrResSettings ( Resolution integer, madvrJson txt)\n");

  CLog::Log(LOGINFO, "create madvr settings for user table");
  m_pDS->exec("CREATE TABLE madvrUserSettings ( User integer, madvrJson txt)\n");

  std::string strSQL = "CREATE TABLE lavvideoSettings (id integer, bTrayIcon integer, dwStreamAR integer, dwNumThreads integer, ";
  for (int i = 0; i < LAVOutPixFmt_NB; ++i)
    strSQL += PrepareSQL("bPixFmts%i integer, ", i);
  strSQL += "dwRGBRange integer, dwHWAccel integer, ";
  for (int i = 0; i < HWAccel_NB; ++i)
    strSQL += PrepareSQL("dwHWAccelDeviceIndex%i integer, ", i);
  for (int i = 0; i < HWCodec_NB; ++i)
    strSQL += PrepareSQL("bHWFormats%i integer, ", i);
  for (int i = 0; i < Codec_VideoNB; ++i)
    strSQL += PrepareSQL("bVideoFormats%i integer, ", i);
  strSQL += "dwHWAccelResFlags integer, dwHWDeintMode integer, dwHWDeintOutput integer, dwDeintFieldOrder integer, deintMode integer, dwSWDeintMode integer, "
    "dwSWDeintOutput integer, dwDitherMode integer, bUseMSWMV9Decoder integer, bDVDVideoSupport integer"
    ")\n";

  CLog::Log(LOGINFO, "create lavvideo setting table");
  m_pDS->exec(strSQL);

  strSQL = "CREATE TABLE lavaudioSettings (id integer, bTrayIcon integer, bDRCEnabled integer, iDRCLevel integer, ";
  for (int i = 0; i < Bitstream_NB; ++i)
    strSQL += PrepareSQL("bBitstream%i integer, ", i);
  strSQL += "bDTSHDFraming integer, bAutoAVSync integer, bExpandMono integer, bExpand61 integer, bOutputStandardLayout integer, b51Legacy integer, bAllowRawSPDIF integer, ";
  for (int i = 0; i < SampleFormat_NB; ++i)
    strSQL += PrepareSQL("bSampleFormats%i integer, ", i);
  for (int i = 0; i < Codec_AudioNB; ++i)
    strSQL += PrepareSQL("bAudioFormats%i integer, ", i);
  strSQL += "bSampleConvertDither integer, bAudioDelayEnabled integer, iAudioDelay integer, bMixingEnabled integer, dwMixingLayout integer, dwMixingFlags integer, dwMixingMode integer, "
    "dwMixingCenterLevel integer, dwMixingSurroundLevel integer, dwMixingLFELevel integer"
    ")\n";

  CLog::Log(LOGINFO, "create lavaudio setting table");
  m_pDS->exec(strSQL);

  strSQL = "CREATE TABLE lavsplitterSettings (id integer, bTrayIcon integer, prefAudioLangs txt, prefSubLangs txt, subtitleAdvanced txt, subtitleMode integer, bPGSForcedStream integer, bPGSOnlyForced integer, "
    "iVC1Mode integer, bSubstreams integer, bMatroskaExternalSegments integer, bStreamSwitchRemoveAudio integer, bImpairedAudio integer, bPreferHighQualityAudio integer, dwQueueMaxSize integer, dwQueueMaxPacketsSize integer, dwNetworkAnalysisDuration integer"
    ")\n";

  CLog::Log(LOGINFO, "create lavsplitter setting table");
  m_pDS->exec(strSQL);

  m_pDS->exec("CREATE TABLE lastTvId (lastPlayed integer, lastWatched integer)\n");
  m_pDS->exec("INSERT INTO lastTvId (lastPlayed, lastWatched) VALUES (-1,-1)\n");
  CLog::Log(LOGINFO, "create lastTvId setting table");

  m_pDS->exec("CREATE TABLE settings (file text, extSubTrackName text)\n");
  CLog::Log(LOGINFO, "create settings setting table");
}

void CDSPlayerDatabase::CreateAnalytics()
{
  m_pDS->exec("CREATE INDEX idxEdition ON edition (file)");
  m_pDS->exec("CREATE INDEX idxMadvrFileSettings ON madvrFileSettings (file)");
  m_pDS->exec("CREATE INDEX idxMadvrTvShowSettings ON madvrTvShowSettings (TvShowName)");
  m_pDS->exec("CREATE INDEX idxMadvrResSettings ON madvrResSettings (Resolution)");
  m_pDS->exec("CREATE INDEX idxSettings ON settings (file)");
  m_pDS->exec("CREATE INDEX idxLavVideo ON lavvideoSettings(id)");
  m_pDS->exec("CREATE INDEX idxLavAudio ON lavaudioSettings (id)");
  m_pDS->exec("CREATE INDEX idxlavSplitter ON lavsplitterSettings (id)");
}

int CDSPlayerDatabase::GetSchemaVersion() const
{ 
  return 21; 
}

void CDSPlayerDatabase::UpdateTables(int version)
{
  if (version < 6)
  {
    m_pDS->exec("ALTER TABLE lavaudioSettings ADD b51Legacy integer");
    m_pDS->query("SELECT * FROM lavaudioSettings WHERE id = 0");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE lavaudioSettings SET b51Legacy=0");
    }
    m_pDS->exec("ALTER TABLE lavsplitterSettings ADD dwQueueMaxPacketsSize integer");
    m_pDS->query("SELECT * FROM lavsplitterSettings WHERE id = 0");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE lavsplitterSettings SET dwQueueMaxPacketsSize=350");
    }
    m_pDS->exec("ALTER TABLE madvrSettings ADD NoSmallScaling integer");
    m_pDS->query("SELECT * FROM madvrSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrSettings SET NoSmallScaling=-1");
    }
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD NoSmallScaling integer");
    m_pDS->query("SELECT * FROM madvrDefResSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrDefResSettings SET NoSmallScaling=-1");
    }
  }
  if (version < 7)
  {
    m_pDS->exec("ALTER TABLE madvrSettings ADD MoveSubs bool");
    m_pDS->exec("ALTER TABLE madvrSettings ADD SuperResSharpness float");
    m_pDS->exec("ALTER TABLE madvrSettings ADD SuperResLinear bool");
    m_pDS->query("SELECT * FROM madvrSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrSettings SET MoveSubs=0, SuperResSharpness=2.0, SuperResLinear=0");
    }
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD MoveSubs bool");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD SuperResSharpness float");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD SuperResLinear bool");
    m_pDS->query("SELECT * FROM madvrDefResSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrDefResSettings SET MoveSubs=0, SuperResSharpness=2.0, SuperResLinear=0");
    }
  }
  if (version < 8)
  {
    //zoom control
    m_pDS->exec("ALTER TABLE madvrSettings ADD DetectBars bool");
    m_pDS->exec("ALTER TABLE madvrSettings ADD ArChange integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD QuickArChange integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD ShiftImage integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD DontCropSubs integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD CleanBorders integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD ReduceBigBars integer");
    m_pDS->exec("ALTER TABLE madvrSettings ADD CropSmallBars bool");
    m_pDS->exec("ALTER TABLE madvrSettings ADD CropBars bool");

    //sharp
    m_pDS->exec("CREATE TABLE madvrSettings_new ( file text, Resolution integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResPasses integer, ChromaSuperResStrength float, ChromaSuperResSoftness float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpLinear bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrSettings_new (file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT " 
      "file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrSettings" );

    m_pDS->exec("DROP TABLE madvrSettings");
    m_pDS->exec("ALTER TABLE madvrSettings_new RENAME TO madvrSettings");

    m_pDS->query("SELECT * FROM madvrSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrSettings SET DetectBars=0, ArChange=-1, QuickArChange=-1, ShiftImage=-1, DontCropSubs=-1, CleanBorders=-1, ReduceBigBars=-1, CropSmallBars=0, CropBars=0, "
        "SharpenEdges=0, SharpenEdgesStrength=1.0, CrispenEdges=0, CrispenEdgesStrength=1.0, ThinEdges=0, ThinEdgesStrength=1.0, EnhanceDetail=0, EnhanceDetailStrength=1.0, "
        "UpRefSharpenEdges=0, UpRefSharpenEdgesStrength=1.0, UpRefCrispenEdges=0, UpRefCrispenEdgesStrength=1.0, UpRefThinEdges=0, UpRefThinEdgesStrength=1.0, UpRefEnhanceDetail=0, UpRefEnhanceDetailStrength=1.0"
        );
    }

    //zoom control
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD DetectBars bool");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD ArChange integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD QuickArChange integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD ShiftImage integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD DontCropSubs integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD CleanBorders integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD ReduceBigBars integer");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD CropSmallBars bool");
    m_pDS->exec("ALTER TABLE madvrDefResSettings ADD CropBars bool");

    //sharp
    m_pDS->exec("CREATE TABLE madvrDefResSettings_new ( Resolution integer, ResolutionInternal integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResPasses integer, ChromaSuperResStrength float, ChromaSuperResSoftness float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpLinear bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrDefResSettings_new (Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT "
      "Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrDefResSettings" );

    m_pDS->exec("DROP TABLE madvrDefResSettings");
    m_pDS->exec("ALTER TABLE madvrDefResSettings_new RENAME TO madvrDefResSettings");

    m_pDS->query("SELECT * FROM madvrDefResSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrDefResSettings SET DetectBars=0, ArChange=-1, QuickArChange=-1, ShiftImage=-1, DontCropSubs=-1, CleanBorders=-1, ReduceBigBars=-1, CropSmallBars=0, CropBars=0, "
        "SharpenEdges=0, SharpenEdgesStrength=1.0, CrispenEdges=0, CrispenEdgesStrength=1.0, ThinEdges=0, ThinEdgesStrength=1.0, EnhanceDetail=0, EnhanceDetailStrength=1.0, "
        "UpRefSharpenEdges=0, UpRefSharpenEdgesStrength=1.0, UpRefCrispenEdges=0, UpRefCrispenEdgesStrength=1.0, UpRefThinEdges=0, UpRefThinEdgesStrength=1.0, UpRefEnhanceDetail=0, UpRefEnhanceDetailStrength=1.0"
        );
    }
  }
  if (version < 9)
  {
    //remove dwHWDeintHQ

    //create new lavvideo table
    std::string strSQL = "CREATE TABLE lavvideoSettings_new (id integer, bTrayIcon integer, dwStreamAR integer, dwNumThreads integer, ";
    for (int i = 0; i < 18/*LAVOutPixFmt_NB*/; ++i)
      strSQL += PrepareSQL("bPixFmts%i integer, ", i);
    strSQL += "dwRGBRange integer, dwHWAccel integer, ";
    for (int i = 0; i < 7/*HWCodec_NB*/; ++i)
      strSQL += PrepareSQL("bHWFormats%i integer, ", i);
    strSQL += "dwHWAccelResFlags integer, dwHWDeintMode integer, dwHWDeintOutput integer, dwDeintFieldOrder integer, deintMode integer, dwSWDeintMode integer, "
      "dwSWDeintOutput integer, dwDitherMode integer"
      ")\n";
    m_pDS->exec(strSQL);

    // insert old value on new table
    strSQL = "INSERT INTO lavvideoSettings_new (id, bTrayIcon, dwStreamAR, dwNumThreads, ";
    for (int i = 0; i < 18/*LAVOutPixFmt_NB*/; ++i)
      strSQL += PrepareSQL("bPixFmts%i, ", i);
    strSQL += "dwRGBRange, dwHWAccel, ";
    for (int i = 0; i < 7/*HWCodec_NB*/; ++i)
      strSQL += PrepareSQL("bHWFormats%i, ", i);
    strSQL += "dwHWAccelResFlags, dwHWDeintMode, dwHWDeintOutput, dwDeintFieldOrder, deintMode, dwSWDeintMode, dwSWDeintOutput, dwDitherMode) SELECT ";

    strSQL += "id, bTrayIcon, dwStreamAR, dwNumThreads, ";
    for (int i = 0; i < 18/*LAVOutPixFmt_NB*/; ++i)
      strSQL += PrepareSQL("bPixFmts%i, ", i);
    strSQL += "dwRGBRange, dwHWAccel, ";
    for (int i = 0; i < 7/*HWCodec_NB*/; ++i)
      strSQL += PrepareSQL("bHWFormats%i, ", i);
    strSQL += "dwHWAccelResFlags, dwHWDeintMode, dwHWDeintOutput, dwDeintFieldOrder, deintMode, dwSWDeintMode, dwSWDeintOutput, dwDitherMode FROM lavvideoSettings";

    m_pDS->exec(strSQL.c_str());

    //delete old table and rename the new table
    m_pDS->exec("DROP TABLE lavvideoSettings");
    m_pDS->exec("ALTER TABLE lavvideoSettings_new RENAME TO lavvideoSettings");
  }
  if (version < 10)
  {
    m_pDS->exec("ALTER TABLE lavvideoSettings ADD bHWFormats7 integer");
    m_pDS->query("SELECT * FROM lavvideoSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE lavvideoSettings SET bHWFormats7=0");
    }
  }
  if (version < 11)
  {
    m_pDS->exec("CREATE TABLE headsets (bOn integer)\n");
    m_pDS->exec("INSERT INTO headsets (bOn) VALUES (0)");
  }
  if (version < 12)
  {
    m_pDS->exec("CREATE TABLE madvrSettings_new ( file, Resolution integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResPasses integer, ChromaSuperResStrength float, ChromaSuperResSoftness float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpSigmoidal bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrSettings_new (file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT "
      "file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrSettings" );

    m_pDS->exec("DROP TABLE madvrSettings");
    m_pDS->exec("ALTER TABLE madvrSettings_new RENAME TO madvrSettings");

    m_pDS->exec("CREATE TABLE madvrDefResSettings_new ( Resolution integer, ResolutionInternal integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResPasses integer, ChromaSuperResStrength float, ChromaSuperResSoftness float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpSigmoidal bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrDefResSettings_new (Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT "
      "Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResPasses, ChromaSuperResStrength, ChromaSuperResSoftness, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpLinear, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrDefResSettings");

    m_pDS->exec("DROP TABLE madvrDefResSettings");
    m_pDS->exec("ALTER TABLE madvrDefResSettings_new RENAME TO madvrDefResSettings");
  }
  if (version < 13)
  {
    m_pDS->exec("DROP TABLE headsets");
  }
  if (version < 14)
  {
    m_pDS->exec("ALTER TABLE lavvideoSettings ADD dwHWAccelDeviceIndex integer");
    m_pDS->query("SELECT * FROM lavvideoSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE lavvideoSettings SET dwHWAccelDeviceIndex=-1");
    }
  }
  if (version < 15)
  {
    m_pDS->exec("CREATE TABLE madvrSettings_new ( file text, Resolution integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResStrength float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpSigmoidal bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, CrispenEdgesLL bool, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefCrispenEdgesLL bool, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SharpenAR bool, UpRefSharpenAR bool, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrSettings_new (file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResStrength, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT "
      "file, Resolution, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResStrength, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrSettings");

    m_pDS->exec("DROP TABLE madvrSettings");
    m_pDS->exec("ALTER TABLE madvrSettings_new RENAME TO madvrSettings");
    m_pDS->query("SELECT * FROM madvrSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrSettings SET CrispenEdgesLL=0, UpRefCrispenEdgesLL=0, SharpenAR=1, UpRefSharpenAR=1");
    }


    m_pDS->exec("CREATE TABLE madvrDefResSettings_new ( Resolution integer, ResolutionInternal integer, TvShowName txt, "
      "ChromaUpscaling integer, ChromaAntiRing bool, ChromaSuperRes bool, ChromaSuperResStrength float, ImageUpscaling integer, ImageUpAntiRing bool, ImageUpSigmoidal bool, ImageDownscaling integer, ImageDownAntiRing bool, ImageDownLinear bool, "
      "ImageDoubleLuma integer, ImageDoubleChroma integer, ImageQuadrupleLuma integer, ImageQuadrupleChroma integer, "
      "ImageDoubleLumaFactor integer, ImageDoubleChromaFactor integer, ImageQuadrupleLumaFactor integer, ImageQuadrupleChromaFactor integer, "
      "DeintActive integer, DeintForce interger, DeintLookPixels bool, "
      "SmoothMotion integer, Dithering integer, DitheringColoredNoise bool, DitheringEveryFrame bool, "
      "Deband bool, DebandLevel integer, DebandFadeLevel integer, "
      "SharpenEdges bool, SharpenEdgesStrength float, CrispenEdges bool, CrispenEdgesStrength float, CrispenEdgesLL bool, ThinEdges bool, ThinEdgesStrength float, EnhanceDetail bool, EnhanceDetailStrength float, "
      "UpRefSharpenEdges bool, UpRefSharpenEdgesStrength float, UpRefCrispenEdges bool, UpRefCrispenEdgesStrength float, UpRefCrispenEdgesLL bool, UpRefThinEdges bool, UpRefThinEdgesStrength float, UpRefEnhanceDetail bool, UpRefEnhanceDetailStrength float, "
      "LumaSharpen bool, LumaSharpenStrength float, AdaptiveSharpen bool, AdaptiveSharpenStrength float, "
      "NoSmallScaling integer, MoveSubs integer, DetectBars bool, ArChange integer, QuickArChange interger, ShiftImage integer, DontCropSubs integer, CleanBorders integer, ReduceBigBars integer, CropSmallBars bool, CropBars bool, "
      "UpRefLumaSharpen bool, UpRefLumaSharpenStrength float, UpRefAdaptiveSharpen bool, UpRefAdaptiveSharpenStrength float, SuperRes bool, SuperResStrength float, "
      "SharpenAR bool, UpRefSharpenAR bool, "
      "SuperResLinear bool, RefineOnce bool"
      ")\n");

    m_pDS->exec("INSERT INTO madvrDefResSettings_new (Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResStrength, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce"
      ") SELECT "
      "Resolution, ResolutionInternal, TvShowName, "
      "ChromaUpscaling, ChromaAntiRing, ChromaSuperRes, ChromaSuperResStrength, "
      "ImageUpscaling, ImageUpAntiRing, ImageUpSigmoidal, "
      "ImageDownscaling, ImageDownAntiRing, ImageDownLinear, "
      "ImageDoubleLuma, ImageDoubleChroma, ImageQuadrupleLuma, ImageQuadrupleChroma, "
      "ImageDoubleLumaFactor, ImageDoubleChromaFactor, ImageQuadrupleLumaFactor, ImageQuadrupleChromaFactor, "
      "DeintActive, DeintForce, DeintLookPixels, "
      "SmoothMotion, Dithering, DitheringColoredNoise, DitheringEveryFrame, "
      "Deband, DebandLevel, DebandFadeLevel, "
      "SharpenEdges, SharpenEdgesStrength, CrispenEdges, CrispenEdgesStrength, ThinEdges, ThinEdgesStrength, EnhanceDetail, EnhanceDetailStrength, "
      "UpRefSharpenEdges, UpRefSharpenEdgesStrength, UpRefCrispenEdges, UpRefCrispenEdgesStrength, UpRefThinEdges, UpRefThinEdgesStrength, UpRefEnhanceDetail, UpRefEnhanceDetailStrength, "
      "LumaSharpen, LumaSharpenStrength, AdaptiveSharpen, AdaptiveSharpenStrength, "
      "NoSmallScaling, MoveSubs, DetectBars, ArChange, QuickArChange, ShiftImage, DontCropSubs, CleanBorders, ReduceBigBars, CropSmallBars, CropBars, "
      "UpRefLumaSharpen, UpRefLumaSharpenStrength, UpRefAdaptiveSharpen, UpRefAdaptiveSharpenStrength, SuperRes, SuperResStrength, "
      "SuperResLinear, RefineOnce "
      "FROM madvrDefResSettings");

    m_pDS->exec("DROP TABLE madvrDefResSettings");
    m_pDS->exec("ALTER TABLE madvrDefResSettings_new RENAME TO madvrDefResSettings");

    m_pDS->query("SELECT * FROM madvrDefResSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE madvrDefResSettings SET CrispenEdgesLL=0, UpRefCrispenEdgesLL=0, SharpenAR=1, UpRefSharpenAR=1");
    }
  }
  if (version < 16)
  {
    InitOldSettings();

    //per file database
    m_pDS->exec("CREATE TABLE madvrSettings_new ( file text, Resolution integer, TvShowName txt, madvrJson txt)\n");
    m_pDS->query("SELECT * FROM madvrSettings");
    while (!m_pDS->eof())
    {
      std::string strJson = PrepareSQL(
        "{\"DC\":\"{}\",\"DL\":\"{}\",\"QC\":\"{}\",\"QL\":\"{}\",\"adaptiveSharpen\":{},\"adaptiveSharpenStrength\":%f,\"arChange\":%i,\"chromaAntiRinging\":{},"
        "\"chromaUp\":\"{}\",\"cleanborders\":\"{}\",\"coloredDither\":{},\"contentType\":\"{}\",\"crispenEdges\":{},\"crispenEdgesLL\":{},\"crispenEdgesStrength\":%f,"
        "\"cropBars\":{},\"cropSmallBars\":{},\"debandActive\":{},\"debandFadeLevel\":%i,\"debandLevel\":%i,\"autoActivateDeinterlacing\":%i,\"detectBars\":{},\"dontDither\":\"{}\","
        "\"dontCropSubs\":%i,\"dynamicDither\":{},\"enhanceDetail\":{},\"enhanceDetailStrength\":%f,\"lumaDown\":\"{}\",\"lumaDownAntiRinging\":{},\"lumaDownLinear\":{},"
        "\"lumaSharpen\":{},\"lumaSharpenStrength\":%f,\"lumaUp\":\"{}\",\"lumaUpAntiRinging\":{},\"lumaUpSigmoidal\":{},\"moveSubs\":%i,\"nnediDCScalingFactor\":\"{}\","
        "\"nnediDLScalingFactor\":\"{}\",\"nnediQCScalingFactor\":\"{}\",\"nnediQLScalingFactor\":\"{}\",\"noSmallScaling\":%i,\"quickArChange\":\"{}\",\"reduceBigBars\":%i,"
        "\"refineOnce\":{},\"scanPartialFrame\":{},\"sharpenAR\":{},\"sharpenEdges\":{},\"sharpenEdgesStrength\":%f,\"shiftImage\":%i,\"smoothMotionEnabled\":\"{}\","
        "\"superChromaRes\":{},\"superChromaResStrength\":%f,\"superRes\":{},\"superResLinear\":{},\"superResStrength\":%f,\"thinEdges\":{},\"thinEdgesStrength\":%f,"
        "\"upRefEnhanceDetailStrength\":%f,\"upRefAdaptiveSharpen\":{},\"upRefAdaptiveSharpenStrength\":%f,\"upRefCrispenEdges\":{},\"upRefCrispenEdgesLL\":{},"
        "\"upRefCrispenEdgesStrength\":%f,\"upRefEnhanceDetail\":{},\"upRefLumaSharpen\":{},\"upRefLumaSharpenStrength\":%f,\"upRefSharpenAR\":{},"
        "\"upRefSharpenEdges\":{},\"upRefSharpenEdgesStrength\":%f,\"upRefThinEdges\":{},\"upRefThinEdgesStrength\":%f}",
        m_oldSettings["DC"][m_pDS->fv("ImageDoubleChroma").get_asInt()].c_str(),
        m_oldSettings["DL"][m_pDS->fv("ImageDoubleLuma").get_asInt()].c_str(),
        m_oldSettings["QC"][m_pDS->fv("ImageQuadrupleChroma").get_asInt()].c_str(),
        m_oldSettings["QL"][m_pDS->fv("ImageQuadrupleLuma").get_asInt()].c_str(),
        m_pDS->fv("AdaptiveSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("AdaptiveSharpenStrength").get_asFloat(),
        m_pDS->fv("ArChange").get_asInt(),
        m_pDS->fv("ChromaAntiRing").get_asBool() ? "true" : "false",
        m_oldSettings["chromaUp"][m_pDS->fv("ChromaUpscaling").get_asInt()].c_str(),
        StringUtils::Format("%i", m_pDS->fv("CleanBorders").get_asInt()).c_str(),
        m_pDS->fv("DitheringColoredNoise").get_asBool() ? "true" : "false",
        m_oldSettings["contentType"][m_pDS->fv("DeintForce").get_asInt()].c_str(),
        m_pDS->fv("CrispenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("CrispenEdgesLL").get_asBool() ? "true" : "false",
        m_pDS->fv("CrispenEdgesStrength").get_asFloat(),
        m_pDS->fv("CropBars").get_asBool() ? "true" : "false",
        m_pDS->fv("CropSmallBars").get_asBool() ? "true" : "false",
        m_pDS->fv("Deband").get_asBool() ? "true" : "false",
        m_pDS->fv("DebandFadeLevel").get_asInt(),
        m_pDS->fv("DebandLevel").get_asInt(),
        m_pDS->fv("DeintActive").get_asInt() >-1 ? !m_pDS->fv("DeintActive").get_asBool() : m_pDS->fv("DeintActive").get_asInt(),
        m_pDS->fv("DetectBars").get_asBool() ? "true" : "false",
        m_oldSettings["dontDither"][m_pDS->fv("Dithering").get_asInt()].c_str(),
        m_pDS->fv("DontCropSubs").get_asInt(),
        m_pDS->fv("DitheringEveryFrame").get_asBool() ? "true" : "false",
        m_pDS->fv("EnhanceDetail").get_asBool() ? "true" : "false",
        m_pDS->fv("EnhanceDetailStrength").get_asFloat(),
        m_oldSettings["lumaDown"][m_pDS->fv("ImageDownscaling").get_asInt()].c_str(),
        m_pDS->fv("ImageDownAntiRing").get_asBool() ? "true" : "false",
        m_pDS->fv("ImageDownLinear").get_asBool() ? "true" : "false",
        m_pDS->fv("LumaSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("LumaSharpenStrength").get_asFloat(),
        m_oldSettings["lumaUp"][m_pDS->fv("ImageUpscaling").get_asInt()].c_str(),
        m_pDS->fv("ImageUpAntiRing").get_asBool() ? "true" : "false",
        m_pDS->fv("ImageUpSigmoidal").get_asBool() ? "true" : "false",
        m_pDS->fv("MoveSubs").get_asInt(),
        m_oldSettings["nnediDCScalingFactor"][m_pDS->fv("ImageDoubleChromaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediDLScalingFactor"][m_pDS->fv("ImageDoubleLumaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediQCScalingFactor"][m_pDS->fv("ImageQuadrupleChromaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediQLScalingFactor"][m_pDS->fv("ImageQuadrupleLumaFactor").get_asInt()].c_str(),
        m_pDS->fv("NoSmallScaling").get_asInt(),
        StringUtils::Format("%i", m_pDS->fv("QuickArChange").get_asInt()).c_str(),
        m_pDS->fv("ReduceBigBars").get_asInt(),
        m_pDS->fv("RefineOnce").get_asBool() ? "true" : "false",
        m_pDS->fv("DeintLookPixels").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenAR").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenEdgesStrength").get_asFloat(),
        m_pDS->fv("ShiftImage").get_asInt(),
        m_oldSettings["smoothMotionEnabled"][m_pDS->fv("SmoothMotion").get_asInt()].c_str(),
        m_pDS->fv("ChromaSuperRes").get_asBool() ? "true" : "false",
        m_pDS->fv("ChromaSuperResStrength").get_asFloat(),
        m_pDS->fv("SuperRes").get_asBool() ? "true" : "false",
        m_pDS->fv("SuperResLinear").get_asBool() ? "true" : "false",
        m_pDS->fv("SuperResStrength").get_asFloat(),
        m_pDS->fv("ThinEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("ThinEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefEnhanceDetailStrength").get_asFloat(),
        m_pDS->fv("UpRefAdaptiveSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefAdaptiveSharpenStrength").get_asFloat(),
        m_pDS->fv("UpRefCrispenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefCrispenEdgesLL").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefCrispenEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefEnhanceDetail").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefLumaSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefLumaSharpenStrength").get_asFloat(),
        m_pDS->fv("UpRefSharpenAR").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefSharpenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefSharpenEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefThinEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefThinEdgesStrength").get_asFloat()
        );
      
      std::string strSQL = PrepareSQL("INSERT INTO madvrSettings_new (file, Resolution, TvShowName, madvrJson) VALUES ('{}', %i, '{}','{}')",
        m_pDS->fv("file").get_asString().c_str(), m_pDS->fv("Resolution").get_asInt(), m_pDS->fv("TvShowName").get_asString().c_str(), strJson.c_str());

      m_pDS->exec(strSQL);
      m_pDS->next();
    }
    m_pDS->close();

    m_pDS->exec("DROP TABLE madvrSettings");
    m_pDS->exec("ALTER TABLE madvrSettings_new RENAME TO madvrSettings");

    //default settings per resolution
    m_pDS->exec("CREATE TABLE madvrDefResSettings_new ( Resolution integer, ResolutionInternal integer, TvShowName txt, madvrJson txt)\n");
    m_pDS->query("SELECT * FROM madvrDefResSettings");
    while (!m_pDS->eof())
    {
      std::string strJson = PrepareSQL(
        "{\"DC\":\"{}\",\"DL\":\"{}\",\"QC\":\"{}\",\"QL\":\"{}\",\"adaptiveSharpen\":{},\"adaptiveSharpenStrength\":%f,\"arChange\":%i,\"chromaAntiRinging\":{},"
        "\"chromaUp\":\"{}\",\"cleanborders\":\"{}\",\"coloredDither\":{},\"contentType\":\"{}\",\"crispenEdges\":{},\"crispenEdgesLL\":{},\"crispenEdgesStrength\":%f,"
        "\"cropBars\":{},\"cropSmallBars\":{},\"debandActive\":{},\"debandFadeLevel\":%i,\"debandLevel\":%i,\"autoActivateDeinterlacing\":%i,\"detectBars\":{},\"dontDither\":\"{}\","
        "\"dontCropSubs\":%i,\"dynamicDither\":{},\"enhanceDetail\":{},\"enhanceDetailStrength\":%f,\"lumaDown\":\"{}\",\"lumaDownAntiRinging\":{},\"lumaDownLinear\":{},"
        "\"lumaSharpen\":{},\"lumaSharpenStrength\":%f,\"lumaUp\":\"{}\",\"lumaUpAntiRinging\":{},\"lumaUpSigmoidal\":{},\"moveSubs\":%i,\"nnediDCScalingFactor\":\"{}\","
        "\"nnediDLScalingFactor\":\"{}\",\"nnediQCScalingFactor\":\"{}\",\"nnediQLScalingFactor\":\"{}\",\"noSmallScaling\":%i,\"quickArChange\":\"{}\",\"reduceBigBars\":%i,"
        "\"refineOnce\":{},\"scanPartialFrame\":{},\"sharpenAR\":{},\"sharpenEdges\":{},\"sharpenEdgesStrength\":%f,\"shiftImage\":%i,\"smoothMotionEnabled\":\"{}\","
        "\"superChromaRes\":{},\"superChromaResStrength\":%f,\"superRes\":{},\"superResLinear\":{},\"superResStrength\":%f,\"thinEdges\":{},\"thinEdgesStrength\":%f,"
        "\"upRefEnhanceDetailStrength\":%f,\"upRefAdaptiveSharpen\":{},\"upRefAdaptiveSharpenStrength\":%f,\"upRefCrispenEdges\":{},\"upRefCrispenEdgesLL\":{},"
        "\"upRefCrispenEdgesStrength\":%f,\"upRefEnhanceDetail\":{},\"upRefLumaSharpen\":{},\"upRefLumaSharpenStrength\":%f,\"upRefSharpenAR\":{},"
        "\"upRefSharpenEdges\":{},\"upRefSharpenEdgesStrength\":%f,\"upRefThinEdges\":{},\"upRefThinEdgesStrength\":%f}",
        m_oldSettings["DC"][m_pDS->fv("ImageDoubleChroma").get_asInt()].c_str(),
        m_oldSettings["DL"][m_pDS->fv("ImageDoubleLuma").get_asInt()].c_str(),
        m_oldSettings["QC"][m_pDS->fv("ImageQuadrupleChroma").get_asInt()].c_str(),
        m_oldSettings["QL"][m_pDS->fv("ImageQuadrupleLuma").get_asInt()].c_str(),
        m_pDS->fv("AdaptiveSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("AdaptiveSharpenStrength").get_asFloat(),
        m_pDS->fv("ArChange").get_asInt(),
        m_pDS->fv("ChromaAntiRing").get_asBool() ? "true" : "false",
        m_oldSettings["chromaUp"][m_pDS->fv("ChromaUpscaling").get_asInt()].c_str(),
        StringUtils::Format("%i", m_pDS->fv("CleanBorders").get_asInt()).c_str(),
        m_pDS->fv("DitheringColoredNoise").get_asBool() ? "true" : "false",
        m_oldSettings["contentType"][m_pDS->fv("DeintForce").get_asInt()].c_str(),
        m_pDS->fv("CrispenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("CrispenEdgesLL").get_asBool() ? "true" : "false",
        m_pDS->fv("CrispenEdgesStrength").get_asFloat(),
        m_pDS->fv("CropBars").get_asBool() ? "true" : "false",
        m_pDS->fv("CropSmallBars").get_asBool() ? "true" : "false",
        m_pDS->fv("Deband").get_asBool() ? "true" : "false",
        m_pDS->fv("DebandFadeLevel").get_asInt(),
        m_pDS->fv("DebandLevel").get_asInt(),
        m_pDS->fv("DeintActive").get_asInt() >-1 ? !m_pDS->fv("DeintActive").get_asBool() : m_pDS->fv("DeintActive").get_asInt(),
        m_pDS->fv("DetectBars").get_asBool() ? "true" : "false",
        m_oldSettings["dontDither"][m_pDS->fv("Dithering").get_asInt()].c_str(),
        m_pDS->fv("DontCropSubs").get_asInt(),
        m_pDS->fv("DitheringEveryFrame").get_asBool() ? "true" : "false",
        m_pDS->fv("EnhanceDetail").get_asBool() ? "true" : "false",
        m_pDS->fv("EnhanceDetailStrength").get_asFloat(),
        m_oldSettings["lumaDown"][m_pDS->fv("ImageDownscaling").get_asInt()].c_str(),
        m_pDS->fv("ImageDownAntiRing").get_asBool() ? "true" : "false",
        m_pDS->fv("ImageDownLinear").get_asBool() ? "true" : "false",
        m_pDS->fv("LumaSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("LumaSharpenStrength").get_asFloat(),
        m_oldSettings["lumaUp"][m_pDS->fv("ImageUpscaling").get_asInt()].c_str(),
        m_pDS->fv("ImageUpAntiRing").get_asBool() ? "true" : "false",
        m_pDS->fv("ImageUpSigmoidal").get_asBool() ? "true" : "false",
        m_pDS->fv("MoveSubs").get_asInt(),
        m_oldSettings["nnediDCScalingFactor"][m_pDS->fv("ImageDoubleChromaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediDLScalingFactor"][m_pDS->fv("ImageDoubleLumaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediQCScalingFactor"][m_pDS->fv("ImageQuadrupleChromaFactor").get_asInt()].c_str(),
        m_oldSettings["nnediQLScalingFactor"][m_pDS->fv("ImageQuadrupleLumaFactor").get_asInt()].c_str(),
        m_pDS->fv("NoSmallScaling").get_asInt(),
        StringUtils::Format("%i", m_pDS->fv("QuickArChange").get_asInt()).c_str(),
        m_pDS->fv("ReduceBigBars").get_asInt(),
        m_pDS->fv("RefineOnce").get_asBool() ? "true" : "false",
        m_pDS->fv("DeintLookPixels").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenAR").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("SharpenEdgesStrength").get_asFloat(),
        m_pDS->fv("ShiftImage").get_asInt(),
        m_oldSettings["smoothMotionEnabled"][m_pDS->fv("SmoothMotion").get_asInt()].c_str(),
        m_pDS->fv("ChromaSuperRes").get_asBool() ? "true" : "false",
        m_pDS->fv("ChromaSuperResStrength").get_asFloat(),
        m_pDS->fv("SuperRes").get_asBool() ? "true" : "false",
        m_pDS->fv("SuperResLinear").get_asBool() ? "true" : "false",
        m_pDS->fv("SuperResStrength").get_asFloat(),
        m_pDS->fv("ThinEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("ThinEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefEnhanceDetailStrength").get_asFloat(),
        m_pDS->fv("UpRefAdaptiveSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefAdaptiveSharpenStrength").get_asFloat(),
        m_pDS->fv("UpRefCrispenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefCrispenEdgesLL").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefCrispenEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefEnhanceDetail").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefLumaSharpen").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefLumaSharpenStrength").get_asFloat(),
        m_pDS->fv("UpRefSharpenAR").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefSharpenEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefSharpenEdgesStrength").get_asFloat(),
        m_pDS->fv("UpRefThinEdges").get_asBool() ? "true" : "false",
        m_pDS->fv("UpRefThinEdgesStrength").get_asFloat()
        );

      std::string strSQL = PrepareSQL("INSERT INTO madvrDefResSettings_new (Resolution, ResolutionInternal, TvShowName, madvrJson) VALUES (%i, %i, '{}','{}')",
        m_pDS->fv("Resolution").get_asInt(), m_pDS->fv("ResolutionInternal").get_asInt(), m_pDS->fv("TvShowName").get_asString().c_str(), strJson.c_str());

      m_pDS->exec(strSQL);
      m_pDS->next();
    }
    m_pDS->close();

    m_pDS->exec("DROP TABLE madvrDefResSettings");
    m_pDS->exec("ALTER TABLE madvrDefResSettings_new RENAME TO madvrDefResSettings");
  }
  if (version < 17)
  {
    
    m_pDS->exec("CREATE TABLE lastTvId (lastPlayed integer, lastWatched integer)\n");
    m_pDS->exec("INSERT INTO lastTvId (lastPlayed, lastWatched) VALUES (-1,-1)\n");

    // per file database
    m_pDS->exec("CREATE TABLE madvrFileSettings ( file text, Resolution integer, TvShowName txt, madvrJson txt)\n");
    m_pDS->query("SELECT * FROM madvrSettings");
    while (!m_pDS->eof())
    {
      int res = m_pDS->fv("Resolution").get_asInt();
      if (res == 0)
        res = 480;
      else if (res == 1)
        res = 720;
      else if (res == 2)
        res = 1080;
      else if (res == 3)
        res = 2160;
      else if (res == 4)
        res = 0;

      std::string strSQL = PrepareSQL("INSERT INTO madvrFileSettings (file, Resolution, TvShowName, madvrJson) VALUES ('{}', %i, '{}','{}')",
        m_pDS->fv("file").get_asString().c_str(), res, m_pDS->fv("TvShowName").get_asString().c_str(), m_pDS->fv("madvrJson").get_asString().c_str());

      m_pDS->exec(strSQL);
      m_pDS->next();
    }
    m_pDS->close();

    m_pDS->exec("DROP TABLE madvrSettings");
    
    //default settings per resolution
    m_pDS->exec("CREATE TABLE madvrTvShowSettings ( TvShowName txt, Resolution integer, madvrJson txt)\n");
    m_pDS->exec("CREATE TABLE madvrResSettings ( Resolution integer, madvrJson txt)\n");
    m_pDS->query("SELECT * FROM madvrDefResSettings");
    while (!m_pDS->eof())
    {
      int res = m_pDS->fv("ResolutionInternal").get_asInt();
      if (res == 0)
        res = 480;
      else if (res == 1)
        res = 720;
      else if (res == 2)
        res = 1080;
      else if (res == 3)
        res = 2160;
      else if (res == 4)
        res = 0;

      std::string strTvShow = m_pDS->fv("TvShowName").get_asString();
      std::string strSQL;
      if (strTvShow == "NOTVSHOW_NULL")
      {
        strSQL = PrepareSQL("INSERT INTO madvrResSettings (Resolution, madvrJson) VALUES (%i, '{}')", res, m_pDS->fv("madvrJson").get_asString().c_str());
      }
      else 
      {
        strSQL = PrepareSQL("INSERT INTO madvrTvShowSettings (TvShowName, Resolution, madvrJson) VALUES ('{}', %i, '{}')",
          strTvShow.c_str(), res, m_pDS->fv("madvrJson").get_asString().c_str());
      }

      m_pDS->exec(strSQL);
      m_pDS->next();
    }
    
    m_pDS->close();

    m_pDS->exec("DROP TABLE madvrDefResSettings");
  }
  if (version < 18)
  {
    m_pDS->exec("CREATE TABLE settings (file text, extSubTrackName text)\n");
  }

  if (version < 19)
  {
    m_pDS->exec("CREATE TABLE madvrUserSettings ( User integer, madvrJson txt)\n");
  }

  if (version < 20)
  {
    std::string strSQL;

    //lavvideo
    m_pDS->exec("ALTER TABLE lavvideoSettings ADD bUseMSWMV9Decoder integer");
    m_pDS->exec("ALTER TABLE lavvideoSettings ADD bDVDVideoSupport integer");
    m_pDS->query("SELECT * FROM lavvideoSettings");
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      m_pDS->exec("UPDATE lavvideoSettings SET bUseMSWMV9Decoder=1, bDVDVideoSupport=1");
    }

    for (int i = 0; i < 59 /*Codec_VideoNB*/; ++i)
    {
      strSQL = PrepareSQL("ALTER TABLE lavvideoSettings ADD bVideoFormats%i integer", i);
      m_pDS->exec(strSQL);
      m_pDS->query("SELECT * FROM lavvideoSettings");
      if (m_pDS->num_rows() > 0)
      {
        m_pDS->close();
        strSQL = PrepareSQL("UPDATE lavvideoSettings SET bVideoFormats%i=1", i);
        m_pDS->exec(strSQL);
      }
    }

    //lavaudio
    for (int i = 0; i < 26 /*Codec_AudioNB*/; ++i)
    {
      strSQL = PrepareSQL("ALTER TABLE lavaudioSettings ADD bAudioFormats%i integer", i);
      m_pDS->exec(strSQL);
      m_pDS->query("SELECT * FROM lavaudioSettings");
      if (m_pDS->num_rows() > 0)
      {
        m_pDS->close();
        strSQL = PrepareSQL("UPDATE lavaudioSettings SET bAudioFormats%i=1", i);
        m_pDS->exec(strSQL);
      }
    }
  }
  if (version < 21)
  {
    std::string strSQL;

    strSQL = "CREATE TABLE lavvideoSettings_new (id integer, bTrayIcon integer, dwStreamAR integer, dwNumThreads integer, ";
    for (int i = 0; i < 18 /* LAVOutPixFmt_NB */; ++i)
      strSQL += PrepareSQL("bPixFmts%i integer, ", i);
    strSQL += "dwRGBRange integer, dwHWAccel integer, ";
    for (int i = 0; i < 8 /* HWCodec_NB */; ++i)
      strSQL += PrepareSQL("bHWFormats%i integer, ", i);
    for (int i = 0; i < 59 /* Codec_VideoNB */; ++i)
      strSQL += PrepareSQL("bVideoFormats%i integer, ", i);
    strSQL += "dwHWAccelResFlags integer, dwHWDeintMode integer, dwHWDeintOutput integer, dwDeintFieldOrder integer, deintMode integer, dwSWDeintMode integer, "
      "dwSWDeintOutput integer, dwDitherMode integer, bUseMSWMV9Decoder integer, bDVDVideoSupport integer"
      ")\n";
    m_pDS->exec(strSQL);

    strSQL = "INSERT INTO lavvideoSettings_new (id, bTrayIcon, dwStreamAR, dwNumThreads, ";
    for (int i = 0; i < 18 /* LAVOutPixFmt_NB */; ++i)
      strSQL += PrepareSQL("bPixFmts%i, ", i);
    strSQL += "dwRGBRange, dwHWAccel, ";
    for (int i = 0; i < 8 /* HWCodec_NB */; ++i)
      strSQL += PrepareSQL("bHWFormats%i, ", i);
    for (int i = 0; i < 59 /* Codec_VideoNB*/; ++i)
      strSQL += PrepareSQL("bVideoFormats%i, ", i);
    strSQL += "dwHWAccelResFlags, dwHWDeintMode, dwHWDeintOutput, dwDeintFieldOrder, deintMode, dwSWDeintMode, "
      "dwSWDeintOutput, dwDitherMode, bUseMSWMV9Decoder, bDVDVideoSupport"
      ") SELECT ";
    strSQL += "id, bTrayIcon, dwStreamAR, dwNumThreads, ";
    for (int i = 0; i < 18 /*LAVOutPixFmt_NB */; ++i)
      strSQL += PrepareSQL("bPixFmts%i, ", i);
    strSQL += "dwRGBRange, dwHWAccel, ";
    for (int i = 0; i < 8 /* HWCodec_NB */; ++i)
      strSQL += PrepareSQL("bHWFormats%i, ", i);
    for (int i = 0; i < 59 /* Codec_VideoNB */; ++i)
      strSQL += PrepareSQL("bVideoFormats%i, ", i);
    strSQL += "dwHWAccelResFlags, dwHWDeintMode, dwHWDeintOutput, dwDeintFieldOrder, deintMode, dwSWDeintMode, "
      "dwSWDeintOutput, dwDitherMode, bUseMSWMV9Decoder, bDVDVideoSupport "
      "FROM lavvideoSettings";
    m_pDS->exec(strSQL);

    m_pDS->exec("DROP TABLE lavvideoSettings");
    m_pDS->exec("ALTER TABLE lavvideoSettings_new RENAME TO lavvideoSettings");

    for (int i = 0; i < 6 /* HWAccel_NB */; ++i)
    {
      strSQL = PrepareSQL("ALTER TABLE lavvideoSettings ADD dwHWAccelDeviceIndex%i interger ", i);    
      m_pDS->exec(strSQL);
      m_pDS->query("SELECT * FROM lavvideoSettings");
      if (m_pDS->num_rows() > 0)
      {
        m_pDS->close();
        strSQL = PrepareSQL("UPDATE lavvideoSettings SET dwHWAccelDeviceIndex%i=-1", i);
        m_pDS->exec(strSQL);
      }
    }
  }
}

bool CDSPlayerDatabase::GetResumeEdition(const std::string& strFilenameAndPath, CEdition &edition)
{
  VECEDITIONS editions;
  GetEditionForFile(strFilenameAndPath, editions);
  if (editions.size() > 0)
  {
    edition = editions[0];
    return true;
  }
  return false;
}

bool CDSPlayerDatabase::GetResumeEdition(const CFileItem *item, CEdition &edition)
{
  std::string strPath = item->GetPath();
  
  if ((KODI::VIDEO::IsVideoDb(*item) || item->IsDVD()) && item->HasVideoInfoTag())
    strPath = item->GetVideoInfoTag()->m_strFileNameAndPath;

  return GetResumeEdition(strPath, edition);
}

void CDSPlayerDatabase::GetEditionForFile(const std::string& strFilenameAndPath, VECEDITIONS& editions)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("select * from edition where file='{}' order by editionNumber", strFilenameAndPath.c_str());
    m_pDS->query(strSQL.c_str());
    while (!m_pDS->eof())
    {
      CEdition edition;
      edition.editionName = m_pDS->fv("editionName").get_asString();
      edition.editionNumber = m_pDS->fv("editionNumber").get_asInt();

      editions.push_back(edition);
      m_pDS->next();
    }

    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} ({}) failed", __FUNCTION__, strFilenameAndPath.c_str());
  }
}

void CDSPlayerDatabase::AddEdition(const std::string& strFilenameAndPath, const CEdition &edition)
{
  try
  {
    if (!edition.IsSet())		return;
    if (NULL == m_pDB.get())    return;
    if (NULL == m_pDS.get())    return;

    std::string strSQL;
    int idEdition = -1;

    strSQL = PrepareSQL("select idEdition from edition where file='{}'", strFilenameAndPath.c_str());

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() != 0)
      idEdition = m_pDS->get_field_value("idEdition").get_asInt();
    m_pDS->close();

    if (idEdition >= 0)
      strSQL = PrepareSQL("update edition set  editionName = '{}', editionNumber = '%i' where idEdition = %i", edition.editionName.c_str(), edition.editionNumber, idEdition);
    else
      strSQL = PrepareSQL("insert into edition (idEdition, file, editionName, editionNumber) values(NULL, '{}', '{}', %i)", strFilenameAndPath.c_str(), edition.editionName.c_str(), edition.editionNumber);


    m_pDS->exec(strSQL.c_str());
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} ({}) failed", __FUNCTION__, strFilenameAndPath.c_str());
  }
}

void CDSPlayerDatabase::ClearEditionOfFile(const std::string& strFilenameAndPath)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("delete from edition where file='{}'", strFilenameAndPath.c_str());
    m_pDS->exec(strSQL.c_str());
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} ({}) failed", __FUNCTION__, strFilenameAndPath.c_str());
  }
}

bool CDSPlayerDatabase::GetVideoSettings(const std::string &strFilenameAndPath, CMadvrSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file
    std::string strSQL = PrepareSQL("select * from madvrFileSettings where file = '{}'", strFilenameAndPath.c_str());

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the madvr settings info
      JsonToVariant(m_pDS->fv("madvrJson").get_asString(), settings);
      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

bool CDSPlayerDatabase::GetResSettings(int resolution, CMadvrSettings &settings)
{
  try
  {
    if (resolution < 0 ) return false;
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file

    std::string strSQL = PrepareSQL("select * from madvrResSettings where Resolution=%i", resolution);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the madvr settings info
      JsonToVariant(m_pDS->fv("madvrJson").get_asString(), settings);
      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

bool CDSPlayerDatabase::GetUserSettings(int userId, CMadvrSettings &settings)
{
  try
  {
    if (userId < 1) return false;
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file

    std::string strSQL = PrepareSQL("select * from madvrUserSettings where User=%i", userId);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the madvr settings info
      JsonToVariant(m_pDS->fv("madvrJson").get_asString(), settings);
      m_pDS->close();
      return true;
    }
    else

    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

bool CDSPlayerDatabase::GetTvShowSettings(const std::string &tvShowName, CMadvrSettings &settings)
{
  try
  {
    if (tvShowName.empty()) return false;
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file

    std::string strSQL = PrepareSQL("select * from madvrTvShowSettings where TvShowName='{}'", tvShowName.c_str());

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the madvr settings info
      JsonToVariant(m_pDS->fv("madvrJson").get_asString(), settings);
      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

/// \brief Sets the settings for a particular video file
void CDSPlayerDatabase::SetVideoSettings(const std::string& strFilenameAndPath, const CMadvrSettings &setting)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strJson;
    std::string strSQL = PrepareSQL("select * from madvrFileSettings where file='{}'", strFilenameAndPath.c_str());
    CJSONVariantWriter::Write(setting.m_db,strJson, true);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

      strSQL = PrepareSQL("UPDATE madvrFileSettings set Resolution=%i, TvShowName='{}', madvrJson='{}' where file='{}'",
        setting.m_Resolution, setting.m_TvShowName.c_str(), strJson.c_str(), strFilenameAndPath.c_str());
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();
      strSQL = PrepareSQL("INSERT INTO madvrFileSettings (file, Resolution, TvShowName, madvrJson) VALUES ('{}', %i, '{}','{}')",
        strFilenameAndPath.c_str(), setting.m_Resolution, setting.m_TvShowName.c_str(), strJson.c_str());
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} ({}) failed", __FUNCTION__, strFilenameAndPath.c_str());
  }
}

void CDSPlayerDatabase::SetResSettings(int resolution, const CMadvrSettings &settings)
{
  try
  {
    if (resolution < 0) return;
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("select * from madvrResSettings where Resolution=%i", resolution);
    std::string strJson;
    CJSONVariantWriter::Write(settings.m_db, strJson, true);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

      strSQL = PrepareSQL("UPDATE madvrResSettings set madvrJson='{}' where Resolution=%i", strJson.c_str(), resolution);
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();
      strSQL = PrepareSQL("INSERT INTO madvrResSettings (Resolution, madvrJson) VALUES (%i, '{}')",
        resolution, strJson.c_str());
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} (%ip) failed", __FUNCTION__, resolution);
  }
}

void CDSPlayerDatabase::SetUserSettings(int userId, const CMadvrSettings &settings)
{
  try
  {
    if (userId < 1) return;
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strJson;
    std::string strSQL = PrepareSQL("select * from madvrUserSettings where User=%i", userId);
    
    CJSONVariantWriter::Write(settings.m_db, strJson, true);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

      strSQL = PrepareSQL("UPDATE madvrUserSettings set madvrJson='{}' where User=%i", strJson.c_str(), userId);
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();
      strSQL = PrepareSQL("INSERT INTO madvrUserSettings (User, madvrJson) VALUES (%i, '{}')",
        userId, strJson.c_str());
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} (%ip) failed", __FUNCTION__, userId);
  }
}

void CDSPlayerDatabase::SetTvShowSettings(const std::string &tvShowName, const CMadvrSettings &settings)
{
  try
  {
    if (tvShowName.empty()) return;
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("select * from madvrTvShowSettings where TvShowName='{}'", tvShowName.c_str());
    std::string strJson;
    CJSONVariantWriter::Write(settings.m_db, strJson, true);

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

      strSQL = PrepareSQL("UPDATE madvrTvShowSettings set madvrJson='{}' where TvShowName='{}'", strJson.c_str(), tvShowName.c_str());
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();
      strSQL = PrepareSQL("INSERT INTO madvrTvShowSettings (TvShowName, Resolution, madvrJson) VALUES ('{}', %i,'{}')",
        tvShowName.c_str(), settings.m_Resolution, strJson.c_str());
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} ({}) failed", __FUNCTION__, tvShowName.c_str());
  }
}

void CDSPlayerDatabase::EraseVideoSettings()
{
  try
  {
    std::string sql = "DELETE FROM madvrFileSettings";
    m_pDS->exec(sql);

    sql = "DELETE FROM madvrResSettings";
    m_pDS->exec(sql);

    sql = "DELETE FROM madvrTvShowSettings";
    m_pDS->exec(sql);

    CLog::Log(LOGINFO, "Deleting madvr settings information for all files");
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::EraseResSettings(int resolution)
{
  try
  {
    if (resolution < 0)
      return;

    std::string strSQL = PrepareSQL("DELETE FROM madvrFileSettings where Resolution=%i", resolution);
    CLog::Log(LOGINFO, "Deleting madvr settings information for %ip files", resolution);
    m_pDS->exec(strSQL);

    strSQL = PrepareSQL("DELETE FROM madvrTvShowSettings where Resolution=%i", resolution);
    CLog::Log(LOGINFO, "Deleting madvr default tvshow settings information for %ip files", resolution);
    m_pDS->exec(strSQL);
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::EraseUserSettings(int userId)
{
  try
  {
    if (userId < 1)
      return;

    std::string strSQL = PrepareSQL("DELETE FROM madvrUserSettings where User=%i", userId);
    CLog::Log(LOGINFO, "Deleting madvr settings information for user %i", userId);
    m_pDS->exec(strSQL);

  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}


void CDSPlayerDatabase::EraseTvShowSettings(const std::string &tvShowName)
{
  try
  {
    if (tvShowName.empty())
      return;

    std::string strSQL = PrepareSQL("DELETE FROM madvrFileSettings where TvShowName='{}'", tvShowName.c_str());
    CLog::Log(LOGINFO, "Deleting madvr settings information for {} files", tvShowName.c_str());
    m_pDS->exec(strSQL);

  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

bool CDSPlayerDatabase::GetLAVVideoSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file
    std::string strSQL = PrepareSQL("select * from lavvideoSettings where id = 0");

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the lavvideo settings info

      settings.video_bTrayIcon = m_pDS->fv("bTrayIcon").get_asInt();
      settings.video_dwStreamAR = m_pDS->fv("dwStreamAR").get_asInt();
      settings.video_dwNumThreads = m_pDS->fv("dwNumThreads").get_asInt();
      for (int i = 0; i < LAVOutPixFmt_NB; ++i)
        settings.video_bPixFmts[i] = m_pDS->fv(PrepareSQL("bPixFmts%i", i).c_str()).get_asInt();
      settings.video_dwRGBRange = m_pDS->fv("dwRGBRange").get_asInt();
      settings.video_dwHWAccel = m_pDS->fv("dwHWAccel").get_asInt();
      for (int i = 0; i < HWAccel_NB; ++i)
        settings.video_dwHWAccelDeviceIndex[i] = m_pDS->fv(PrepareSQL("dwHWAccelDeviceIndex%i", i).c_str()).get_asInt();
      for (int i = 0; i < HWCodec_NB; ++i)
        settings.video_bHWFormats[i] = m_pDS->fv(PrepareSQL("bHWFormats%i", i).c_str()).get_asInt();
      for (int i = 0; i < Codec_VideoNB; ++i)
        settings.video_bVideoFormats[i] = m_pDS->fv(PrepareSQL("bVideoFormats%i", i).c_str()).get_asInt();
      settings.video_dwHWAccelResFlags = m_pDS->fv("dwHWAccelResFlags").get_asInt();
      settings.video_dwHWDeintMode = m_pDS->fv("dwHWDeintMode").get_asInt();
      settings.video_dwHWDeintOutput = m_pDS->fv("dwHWDeintOutput").get_asInt();
      settings.video_dwDeintFieldOrder = m_pDS->fv("dwDeintFieldOrder").get_asInt();
      settings.video_deintMode = (LAVDeintMode)m_pDS->fv("deintMode").get_asInt();
      settings.video_dwSWDeintMode = m_pDS->fv("dwSWDeintMode").get_asInt();
      settings.video_dwSWDeintOutput = m_pDS->fv("dwSWDeintOutput").get_asInt();
      settings.video_dwDitherMode = m_pDS->fv("dwDitherMode").get_asInt();
      settings.video_bUseMSWMV9Decoder = m_pDS->fv("bUseMSWMV9Decoder").get_asInt();
      settings.video_bDVDVideoSupport = m_pDS->fv("bDVDVideoSupport").get_asInt();

      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

bool CDSPlayerDatabase::GetLAVAudioSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file
    std::string strSQL = PrepareSQL("select * from lavaudioSettings where id = 0");

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the lavaudio settings info

      settings.audio_bTrayIcon = m_pDS->fv("bTrayIcon").get_asInt();
      settings.audio_bDRCEnabled = m_pDS->fv("bDRCEnabled").get_asInt();
      settings.audio_iDRCLevel = m_pDS->fv("iDRCLevel").get_asInt();
      for (int i = 0; i < Bitstream_NB; ++i)
        settings.audio_bBitstream[i] = m_pDS->fv(PrepareSQL("bBitstream%i", i).c_str()).get_asInt();
      settings.audio_bDTSHDFraming = m_pDS->fv("bDTSHDFraming").get_asInt();
      settings.audio_bAutoAVSync = m_pDS->fv("bAutoAVSync").get_asInt();
      settings.audio_bExpandMono = m_pDS->fv("bExpandMono").get_asInt();
      settings.audio_bExpand61 = m_pDS->fv("bExpand61").get_asInt();
      settings.audio_bOutputStandardLayout = m_pDS->fv("bOutputStandardLayout").get_asInt();
      settings.audio_b51Legacy = m_pDS->fv("b51Legacy").get_asInt();
      settings.audio_bAllowRawSPDIF = m_pDS->fv("bAllowRawSPDIF").get_asInt();
      for (int i = 0; i < SampleFormat_NB; ++i)
        settings.audio_bSampleFormats[i] = m_pDS->fv(PrepareSQL("bSampleFormats%i", i).c_str()).get_asInt();
      for (int i = 0; i < Codec_AudioNB; ++i)
        settings.audio_bAudioFormats[i] = m_pDS->fv(PrepareSQL("bAudioFormats%i", i).c_str()).get_asInt();
      settings.audio_bSampleConvertDither = m_pDS->fv("bSampleConvertDither").get_asInt();
      settings.audio_bAudioDelayEnabled = m_pDS->fv("bAudioDelayEnabled").get_asInt();
      settings.audio_iAudioDelay = m_pDS->fv("iAudioDelay").get_asInt();
      settings.audio_bMixingEnabled = m_pDS->fv("bMixingEnabled").get_asInt();
      settings.audio_dwMixingLayout = m_pDS->fv("dwMixingLayout").get_asInt();
      settings.audio_dwMixingFlags = m_pDS->fv("dwMixingFlags").get_asInt();
      settings.audio_dwMixingMode = m_pDS->fv("dwMixingMode").get_asInt();
      settings.audio_dwMixingCenterLevel = m_pDS->fv("dwMixingCenterLevel").get_asInt();
      settings.audio_dwMixingSurroundLevel = m_pDS->fv("dwMixingSurroundLevel").get_asInt();
      settings.audio_dwMixingLFELevel = m_pDS->fv("dwMixingLFELevel").get_asInt();

      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}

bool CDSPlayerDatabase::GetLAVSplitterSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return false;
    if (NULL == m_pDS.get()) return false;
    // ok, now obtain the settings for this file
    std::string strSQL = PrepareSQL("select * from lavsplitterSettings where id = 0");

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    { // get the lavsplitter settings info

      std::wstring strW;
      settings.splitter_bTrayIcon = m_pDS->fv("bTrayIcon").get_asInt();     
      g_charsetConverter.utf8ToW(m_pDS->fv("prefAudioLangs").get_asString(), strW, false);
      settings.splitter_prefAudioLangs = strW;
      g_charsetConverter.utf8ToW(m_pDS->fv("prefSubLangs").get_asString(), strW, false);
      settings.splitter_prefSubLangs = strW;
      g_charsetConverter.utf8ToW(m_pDS->fv("subtitleAdvanced").get_asString(), strW, false);
      settings.splitter_subtitleAdvanced = strW;
      settings.splitter_subtitleMode = (LAVSubtitleMode)m_pDS->fv("subtitleMode").get_asInt();
      settings.splitter_bPGSForcedStream = m_pDS->fv("bPGSForcedStream").get_asInt();
      settings.splitter_bPGSOnlyForced = m_pDS->fv("bPGSOnlyForced").get_asInt();
      settings.splitter_iVC1Mode = m_pDS->fv("iVC1Mode").get_asInt();
      settings.splitter_bSubstreams = m_pDS->fv("bSubstreams").get_asInt();
      settings.splitter_bMatroskaExternalSegments = m_pDS->fv("bMatroskaExternalSegments").get_asInt();
      settings.splitter_bStreamSwitchRemoveAudio = m_pDS->fv("bStreamSwitchRemoveAudio").get_asInt();
      settings.splitter_bImpairedAudio = m_pDS->fv("bImpairedAudio").get_asInt();
      settings.splitter_bPreferHighQualityAudio = m_pDS->fv("bPreferHighQualityAudio").get_asInt();
      settings.splitter_dwQueueMaxSize = m_pDS->fv("dwQueueMaxSize").get_asInt();
      settings.splitter_dwQueueMaxPacketsSize = m_pDS->fv("dwQueueMaxPacketsSize").get_asInt();
      settings.splitter_dwNetworkAnalysisDuration = m_pDS->fv("dwNetworkAnalysisDuration").get_asInt();

      m_pDS->close();
      return true;
    }
    m_pDS->close();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return false;
}


/// \brief Sets the settings for a particular video file
void CDSPlayerDatabase::SetLAVVideoSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("select * from lavvideoSettings where id = 0");
    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();

      // update the item
      strSQL = "update lavvideoSettings set ";
      strSQL += PrepareSQL("bTrayIcon=%i, ", settings.video_bTrayIcon);
      strSQL += PrepareSQL("dwStreamAR=%i, ", settings.video_dwStreamAR);
      strSQL += PrepareSQL("dwNumThreads=%i, ", settings.video_dwNumThreads);
      for (int i = 0; i < LAVOutPixFmt_NB; ++i)
        strSQL += PrepareSQL("bPixFmts%i=%i, ", i, settings.video_bPixFmts[i]);
      strSQL += PrepareSQL("dwRGBRange=%i, ", settings.video_dwRGBRange);
      strSQL += PrepareSQL("dwHWAccel=%i, ", settings.video_dwHWAccel);
      for (int i = 0; i < HWAccel_NB; ++i)
        strSQL += PrepareSQL("dwHWAccelDeviceIndex%i=%i, ", i, settings.video_dwHWAccelDeviceIndex[i]);
      for (int i = 0; i < HWCodec_NB; ++i)
        strSQL += PrepareSQL("bHWFormats%i=%i, ", i, settings.video_bHWFormats[i]);
      for (int i = 0; i < Codec_VideoNB; ++i)
        strSQL += PrepareSQL("bVideoFormats%i=%i, ", i, settings.video_bVideoFormats[i]);
      strSQL += PrepareSQL("dwHWAccelResFlags=%i, ", settings.video_dwHWAccelResFlags);
      strSQL += PrepareSQL("dwHWDeintMode=%i, ", settings.video_dwHWDeintMode);
      strSQL += PrepareSQL("dwHWDeintOutput=%i, ", settings.video_dwHWDeintOutput);
      strSQL += PrepareSQL("dwDeintFieldOrder=%i, ", settings.video_dwDeintFieldOrder);
      strSQL += PrepareSQL("deintMode=%i, ", settings.video_deintMode);
      strSQL += PrepareSQL("dwSWDeintMode=%i, ", settings.video_dwSWDeintMode);
      strSQL += PrepareSQL("dwSWDeintOutput=%i, ", settings.video_dwSWDeintOutput);
      strSQL += PrepareSQL("dwDitherMode=%i, ", settings.video_dwDitherMode);
      strSQL += PrepareSQL("bUseMSWMV9Decoder=%i, ", settings.video_bUseMSWMV9Decoder);
      strSQL += PrepareSQL("bDVDVideoSupport=%i ", settings.video_bDVDVideoSupport);
      strSQL += "where id=0";
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();

      strSQL = "INSERT INTO lavvideoSettings (id, bTrayIcon, dwStreamAR, dwNumThreads, ";
      for (int i = 0; i < LAVOutPixFmt_NB; ++i)
        strSQL += PrepareSQL("bPixFmts%i, ", i);
      strSQL += "dwRGBRange, dwHWAccel, ";
      for (int i = 0; i < HWAccel_NB; ++i)
        strSQL += PrepareSQL("dwHWAccelDeviceIndex%i, ", i);
      for (int i = 0; i < HWCodec_NB; ++i)
        strSQL += PrepareSQL("bHWFormats%i, ", i);
      for (int i = 0; i < Codec_VideoNB; ++i)
        strSQL += PrepareSQL("bVideoFormats%i, ", i);
      strSQL += "dwHWAccelResFlags, dwHWDeintMode, dwHWDeintOutput, dwDeintFieldOrder, deintMode, dwSWDeintMode, "
        "dwSWDeintOutput, dwDitherMode, bUseMSWMV9Decoder, bDVDVideoSupport"
        ") VALUES (0, ";

      strSQL += PrepareSQL("%i, ", settings.video_bTrayIcon);
      strSQL += PrepareSQL("%i, ", settings.video_dwStreamAR);
      strSQL += PrepareSQL("%i, ", settings.video_dwNumThreads);
      for (int i = 0; i < LAVOutPixFmt_NB; ++i)
        strSQL += PrepareSQL("%i, ", settings.video_bPixFmts[i]);
      strSQL += PrepareSQL("%i, ", settings.video_dwRGBRange);
      strSQL += PrepareSQL("%i, ", settings.video_dwHWAccel);
      for (int i = 0; i < HWAccel_NB; ++i)
        strSQL += PrepareSQL("%i, ", settings.video_dwHWAccelDeviceIndex[i]);
      for (int i = 0; i < HWCodec_NB; ++i)
        strSQL += PrepareSQL("%i, ", settings.video_bHWFormats[i]);
      for (int i = 0; i < Codec_VideoNB; ++i)
        strSQL += PrepareSQL("%i, ", settings.video_bVideoFormats[i]);
      strSQL += PrepareSQL("%i, ", settings.video_dwHWAccelResFlags);
      strSQL += PrepareSQL("%i, ", settings.video_dwHWDeintMode);
      strSQL += PrepareSQL("%i, ", settings.video_dwHWDeintOutput);
      strSQL += PrepareSQL("%i, ", settings.video_dwDeintFieldOrder);
      strSQL += PrepareSQL("%i, ", settings.video_deintMode);
      strSQL += PrepareSQL("%i, ", settings.video_dwSWDeintMode);
      strSQL += PrepareSQL("%i, ", settings.video_dwSWDeintOutput);
      strSQL += PrepareSQL("%i, ", settings.video_dwDitherMode);
      strSQL += PrepareSQL("%i, ", settings.video_bUseMSWMV9Decoder);
      strSQL += PrepareSQL("%i ", settings.video_bDVDVideoSupport);
      strSQL += ")";

      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::SetLAVAudioSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string strSQL = PrepareSQL("select * from lavaudioSettings where id = 0");
    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();

      // update the item
      strSQL = "update lavaudioSettings set ";
      strSQL += PrepareSQL("bTrayIcon=%i, ", settings.audio_bTrayIcon);
      strSQL += PrepareSQL("bDRCEnabled=%i, ", settings.audio_bDRCEnabled);
      strSQL += PrepareSQL("iDRCLevel=%i, ", settings.audio_iDRCLevel);
      for (int i = 0; i < Bitstream_NB; ++i)
        strSQL += PrepareSQL("bBitstream%i=%i, ", i, settings.audio_bBitstream[i]);
      strSQL += PrepareSQL("bDTSHDFraming=%i, ", settings.audio_bDTSHDFraming);
      strSQL += PrepareSQL("bAutoAVSync=%i, ", settings.audio_bAutoAVSync);
      strSQL += PrepareSQL("bExpandMono=%i, ", settings.audio_bExpandMono);
      strSQL += PrepareSQL("bExpand61=%i, ", settings.audio_bExpand61);
      strSQL += PrepareSQL("bOutputStandardLayout=%i, ", settings.audio_bOutputStandardLayout);
      strSQL += PrepareSQL("b51Legacy=%i, ", settings.audio_b51Legacy);
      strSQL += PrepareSQL("bAllowRawSPDIF=%i, ", settings.audio_bAllowRawSPDIF);
      for (int i = 0; i < SampleFormat_NB; ++i)
        strSQL += PrepareSQL("bSampleFormats%i=%i, ", i, settings.audio_bSampleFormats[i]);
      for (int i = 0; i < Codec_AudioNB; ++i)
        strSQL += PrepareSQL("bAudioFormats%i=%i, ", i, settings.audio_bAudioFormats[i]);
      strSQL += PrepareSQL("bSampleConvertDither=%i, ", settings.audio_bSampleConvertDither);
      strSQL += PrepareSQL("bAudioDelayEnabled=%i, ", settings.audio_bAudioDelayEnabled);
      strSQL += PrepareSQL("iAudioDelay=%i, ", settings.audio_iAudioDelay);
      strSQL += PrepareSQL("bMixingEnabled=%i, ", settings.audio_bMixingEnabled);
      strSQL += PrepareSQL("dwMixingLayout=%i, ", settings.audio_dwMixingLayout);
      strSQL += PrepareSQL("dwMixingFlags=%i, ", settings.audio_dwMixingFlags);
      strSQL += PrepareSQL("dwMixingMode=%i, ", settings.audio_dwMixingMode);
      strSQL += PrepareSQL("dwMixingCenterLevel=%i, ", settings.audio_dwMixingCenterLevel);
      strSQL += PrepareSQL("dwMixingSurroundLevel=%i, ", settings.audio_dwMixingSurroundLevel);
      strSQL += PrepareSQL("dwMixingLFELevel=%i ", settings.audio_dwMixingLFELevel);
      strSQL += "where id=0";
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();

      strSQL = "INSERT INTO lavaudioSettings (id, bTrayIcon, bDRCEnabled, iDRCLevel, ";
      for (int i = 0; i < Bitstream_NB; ++i)
        strSQL += PrepareSQL("bBitstream%i, ", i);
      strSQL += "bDTSHDFraming, bAutoAVSync, bExpandMono, bExpand61, bOutputStandardLayout, b51Legacy, bAllowRawSPDIF, ";
      for (int i = 0; i < SampleFormat_NB; ++i)
        strSQL += PrepareSQL("bSampleFormats%i, ", i);
      for (int i = 0; i < Codec_AudioNB; ++i)
        strSQL += PrepareSQL("bAudioFormats%i, ", i);
      strSQL += "bSampleConvertDither, bAudioDelayEnabled, iAudioDelay, bMixingEnabled, dwMixingLayout, dwMixingFlags, dwMixingMode, "
        "dwMixingCenterLevel, dwMixingSurroundLevel, dwMixingLFELevel"
        ") VALUES (0, ";

      strSQL += PrepareSQL("%i, ", settings.audio_bTrayIcon);
      strSQL += PrepareSQL("%i, ", settings.audio_bDRCEnabled);
      strSQL += PrepareSQL("%i, ", settings.audio_iDRCLevel);
      for (int i = 0; i < Bitstream_NB; ++i)
        strSQL += PrepareSQL("%i, ", settings.audio_bBitstream[i]);
      strSQL += PrepareSQL("%i, ", settings.audio_bDTSHDFraming);
      strSQL += PrepareSQL("%i, ", settings.audio_bAutoAVSync);
      strSQL += PrepareSQL("%i, ", settings.audio_bExpandMono);
      strSQL += PrepareSQL("%i, ", settings.audio_bExpand61);
      strSQL += PrepareSQL("%i, ", settings.audio_bOutputStandardLayout);
      strSQL += PrepareSQL("%i, ", settings.audio_b51Legacy);
      strSQL += PrepareSQL("%i, ", settings.audio_bAllowRawSPDIF);
      for (int i = 0; i < SampleFormat_NB; ++i)
        strSQL += PrepareSQL("%i, ", settings.audio_bSampleFormats[i]);
      for (int i = 0; i < Codec_AudioNB; ++i)
        strSQL += PrepareSQL("%i, ", settings.audio_bAudioFormats[i]);
      strSQL += PrepareSQL("%i, ", settings.audio_bSampleConvertDither);
      strSQL += PrepareSQL("%i, ", settings.audio_bAudioDelayEnabled);
      strSQL += PrepareSQL("%i, ", settings.audio_iAudioDelay);
      strSQL += PrepareSQL("%i, ", settings.audio_bMixingEnabled);
      strSQL += PrepareSQL("%i, ", settings.audio_dwMixingLayout);
      strSQL += PrepareSQL("%i, ", settings.audio_dwMixingFlags);
      strSQL += PrepareSQL("%i, ", settings.audio_dwMixingMode);
      strSQL += PrepareSQL("%i, ", settings.audio_dwMixingCenterLevel);
      strSQL += PrepareSQL("%i, ", settings.audio_dwMixingSurroundLevel);
      strSQL += PrepareSQL("%i ", settings.audio_dwMixingLFELevel);
      strSQL += ")";

      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::SetLAVSplitterSettings(CLavSettings &settings)
{
  try
  {
    if (NULL == m_pDB.get()) return;
    if (NULL == m_pDS.get()) return;

    std::string str;
    std::string strSQL = PrepareSQL("select * from lavsplitterSettings where id = 0");
    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();

      // update the item
      strSQL = "update lavsplitterSettings set ";
      strSQL += PrepareSQL("bTrayIcon=%i, ", settings.splitter_bTrayIcon);
      g_charsetConverter.wToUTF8(settings.splitter_prefAudioLangs, str, false);
      strSQL += PrepareSQL("prefAudioLangs='{}', ", str.c_str());
      g_charsetConverter.wToUTF8(settings.splitter_prefSubLangs, str, false);
      strSQL += PrepareSQL("prefSubLangs='{}', ", str.c_str());
      g_charsetConverter.wToUTF8(settings.splitter_subtitleAdvanced, str, false);
      strSQL += PrepareSQL("subtitleAdvanced='{}', ", str.c_str());
      strSQL += PrepareSQL("subtitleMode=%i, ", settings.splitter_subtitleMode);
      strSQL += PrepareSQL("bPGSForcedStream=%i, ", settings.splitter_bPGSForcedStream);
      strSQL += PrepareSQL("bPGSOnlyForced=%i, ", settings.splitter_bPGSOnlyForced);
      strSQL += PrepareSQL("iVC1Mode=%i, ", settings.splitter_iVC1Mode);
      strSQL += PrepareSQL("bSubstreams=%i, ", settings.splitter_bSubstreams);
      strSQL += PrepareSQL("bMatroskaExternalSegments=%i, ", settings.splitter_bMatroskaExternalSegments);
      strSQL += PrepareSQL("bStreamSwitchRemoveAudio=%i, ", settings.splitter_bStreamSwitchRemoveAudio);
      strSQL += PrepareSQL("bImpairedAudio=%i, ", settings.splitter_bImpairedAudio);
      strSQL += PrepareSQL("bPreferHighQualityAudio=%i, ", settings.splitter_bPreferHighQualityAudio);
      strSQL += PrepareSQL("dwQueueMaxSize=%i, ", settings.splitter_dwQueueMaxSize);
      strSQL += PrepareSQL("dwQueueMaxPacketsSize=%i, ", settings.splitter_dwQueueMaxPacketsSize);
      strSQL += PrepareSQL("dwNetworkAnalysisDuration=%i ", settings.splitter_dwNetworkAnalysisDuration);

      strSQL += "where id=0";
      m_pDS->exec(strSQL.c_str());
      return;
    }
    else
    { // add the items
      m_pDS->close();

      strSQL = "INSERT INTO lavsplitterSettings (id, bTrayIcon, prefAudioLangs, prefSubLangs, subtitleAdvanced, subtitleMode, bPGSForcedStream, bPGSOnlyForced, "
        "iVC1Mode, bSubstreams, bMatroskaExternalSegments, bStreamSwitchRemoveAudio, bImpairedAudio, bPreferHighQualityAudio, dwQueueMaxSize, dwQueueMaxPacketsSize, dwNetworkAnalysisDuration"
        ") VALUES (0, ";

      strSQL += PrepareSQL("%i, ", settings.splitter_bTrayIcon);
      g_charsetConverter.wToUTF8(settings.splitter_prefAudioLangs, str, false);
      strSQL += PrepareSQL("'{}', ", str.c_str());
      g_charsetConverter.wToUTF8(settings.splitter_prefSubLangs, str, false);
      strSQL += PrepareSQL("'{}', ", str.c_str());
      g_charsetConverter.wToUTF8(settings.splitter_subtitleAdvanced, str, false);
      strSQL += PrepareSQL("'{}', ", str.c_str());
      strSQL += PrepareSQL("%i, ", settings.splitter_subtitleMode);
      strSQL += PrepareSQL("%i, ", settings.splitter_bPGSForcedStream);
      strSQL += PrepareSQL("%i, ", settings.splitter_bPGSOnlyForced);
      strSQL += PrepareSQL("%i, ", settings.splitter_iVC1Mode);
      strSQL += PrepareSQL("%i, ", settings.splitter_bSubstreams);
      strSQL += PrepareSQL("%i, ", settings.splitter_bMatroskaExternalSegments);
      strSQL += PrepareSQL("%i, ", settings.splitter_bStreamSwitchRemoveAudio);
      strSQL += PrepareSQL("%i, ", settings.splitter_bImpairedAudio);
      strSQL += PrepareSQL("%i, ", settings.splitter_bPreferHighQualityAudio);
      strSQL += PrepareSQL("%i, ", settings.splitter_dwQueueMaxSize);
      strSQL += PrepareSQL("%i, ", settings.splitter_dwQueueMaxPacketsSize);
      strSQL += PrepareSQL("%i ", settings.splitter_dwNetworkAnalysisDuration);
      strSQL += ")";

      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::EraseLAVVideo()
{
  try
  {
    std::string sql = "DELETE FROM lavvideoSettings";
    m_pDS->exec(sql);
    CLog::Log(LOGINFO, "Deleting LAVVideo settings");
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}
void CDSPlayerDatabase::EraseLAVAudio()
{
  try
  {
    std::string sql = "DELETE FROM lavaudioSettings";
    m_pDS->exec(sql);
    CLog::Log(LOGINFO, "Deleting LAVAudio settings");
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}
void CDSPlayerDatabase::EraseLAVSplitter()
{
  try
  {
    std::string sql = "DELETE FROM lavsplitterSettings";
    m_pDS->exec(sql);
    CLog::Log(LOGINFO, "Deleting LAVSplitter settings");
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

int CDSPlayerDatabase::GetLastTvShowId(bool bLastWatched)
{
  try
  {
    std::string strSelect = bLastWatched ? "lastWatched" : "lastPlayed";
    std::string strSQL = PrepareSQL("SELECT {} FROM lastTvId LIMIT 1", strSelect.c_str());

    if (!m_pDS->query(strSQL.c_str()))
      return -1;

    return m_pDS->fv(strSelect.c_str()).get_asInt();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return -1;
}

void CDSPlayerDatabase::SetLastTvShowId(bool bLastWatched, int id)
{
  if (id < 0)
    return;

  try
  {
    std::string strSelect = bLastWatched ? "lastWatched" : "lastPlayed";
    std::string strSQL = PrepareSQL("SELECT * FROM lastTvId LIMIT 1");

    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

      strSQL = PrepareSQL("UPDATE lastTvId set {}=%i ", strSelect.c_str(), id);
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

void CDSPlayerDatabase::SetSubtitleExtTrackName(const std::string &strFilenameAndPath, const std::string &subTrackName)
{
  if (strFilenameAndPath.empty())
    return;

  try
  {
    std::string strSQL;

    if (subTrackName.empty())
    {
      strSQL = PrepareSQL("DELETE FROM settings WHERE file like '{}'", strFilenameAndPath.c_str());
      m_pDS->exec(strSQL.c_str());
      return;
    }

    strSQL = PrepareSQL("SELECT * FROM settings WHERE file like '{}'", strFilenameAndPath.c_str());
    m_pDS->query(strSQL.c_str());
    if (m_pDS->num_rows() > 0)
    {
      m_pDS->close();
      // update the item

       strSQL = PrepareSQL("UPDATE settings set extSubTrackName='{}'", subTrackName.c_str());

      m_pDS->exec(strSQL.c_str());
    }
    else
    {
      m_pDS->close();

      strSQL = PrepareSQL("INSERT INTO settings (file, extSubTrackName) VALUES ('{}','{}')", strFilenameAndPath.c_str(), subTrackName.c_str());
      m_pDS->exec(strSQL.c_str());
    }
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
}

std::string CDSPlayerDatabase::GetSubtitleExtTrackName(const std::string &strFilenameAndPath)
{
  try
  {
    std::string strSQL = PrepareSQL("SELECT * FROM settings WHERE file like '{}'", strFilenameAndPath.c_str());

    if (!m_pDS->query(strSQL.c_str()))
      return "";

    return m_pDS->fv("extSubTrackName").get_asString();
  }
  catch (...)
  {
    CLog::Log(LOGERROR, "{} failed", __FUNCTION__);
  }
  return "";
}

void CDSPlayerDatabase::JsonToVariant(const std::string &strJson, CMadvrSettings &settings)
{
  CVariant tmp;
  CJSONVariantParser::Parse(strJson, tmp);
  //CVariant tmp = CJSONVariantParser::Parse(reinterpret_cast<const unsigned char*>(strJson.c_str()), strJson.size());
  for (auto setting = settings.m_db.begin_map(); setting != settings.m_db.end_map(); setting++)
  {
    if (!tmp.isMember(setting->first))
    {
      // if there isn't a value stored for this setting than assign the default value
      tmp[setting->first] = setting->second;
      
      continue;
    }

    auto option = settings.m_options.find(setting->first);
    if (option != settings.m_options.end())
    {
      // if the stored value for this setting is invalid than assign the default value
      const CVariant &value = tmp[setting->first];
      auto it = std::find_if(settings.m_options[setting->first].begin(), settings.m_options[setting->first].end(),
        [value](const CVariant option) {
        if (value.isInteger())
          return option.asInteger() == value.asInteger();
        else if (value.isString())
          return option.asString() == value.asString();
        return false;
      });
      if (it == settings.m_options[setting->first].end())
      {
        CLog::Log(LOGDEBUG, "{} the stored value for '{}' is invalid, will be applied the default value ", __FUNCTION__, setting->first.c_str());
        tmp[setting->first] = setting->second;
      }
    }
  }
  settings.m_db = tmp;
}

void CDSPlayerDatabase::InitOldSettings()
{
  std::map<int, std::string> setting;

  // LUMAUP, LUMADOWN, CHROMAUP
  setting[0] = "Nearest Neighbor";
  setting[1] = "Bilinear";
  setting[2] = "Dxva";
  setting[3] = "Mitchell-Netravali";
  setting[4] = "Catmull-Rom";
  setting[5] = "Bicubic50";
  setting[6] = "Bicubic60";
  setting[7] = "Bicubic75";
  setting[8] = "Bicubic100";
  setting[42] = "Bicubic125";
  setting[40] = "Bicubic150";
  setting[9] = "SoftCubic50";
  setting[10] = "SoftCubic60";
  setting[11] = "SoftCubic70";
  setting[12] = "SoftCubic80";
  setting[13] = "SoftCubic100";
  setting[14] = "Lanczos3";
  setting[15] = "Lanczos4";
  setting[16] = "Lanczos8";
  setting[17] = "Spline36";
  setting[18] = "Spline64";
  setting[41] = "SSIM1D25";
  setting[43] = "SSIM1D50";
  setting[44] = "SSIM1D75";
  setting[45] = "SSIM1D100";
  setting[46] = "SSIM2D25";
  setting[47] = "SSIM2D50";
  setting[48] = "SSIM2D75";
  setting[49] = "SSIM2D100";
  setting[19] = "Jinc3";
  setting[20] = "Jinc4";
  setting[21] = "Jinc8";
  setting[22] = "Bilateral";
  setting[29] = "Nedi";
  setting[35] = "ReconSoft";
  setting[36] = "ReconSharp";
  setting[37] = "ReconPlacebo";
  setting[38] = "ReconSharpDenoise";
  setting[39] = "ReconPlaceboDenoise";
  setting[23] = "SuperXbr25";
  setting[24] = "SuperXbr50";
  setting[25] = "SuperXbr75";
  setting[26] = "SuperXbr100";
  setting[27] = "SuperXbr125";
  setting[28] = "SuperXbr150";
  setting[30] = "Nnedi16";
  setting[31] = "Nnedi32";
  setting[32] = "Nnedi64";
  setting[33] = "Nnedi128";
  setting[34] = "Nnedi256";
  m_oldSettings["chromaUp"] = setting;
  m_oldSettings["lumaUp"] = setting;
  m_oldSettings["lumaDown"] = setting;

  // ContentType
  setting.clear();
  setting[0] = "auto";
  setting[1] = "film";
  setting[2] = "video";
  m_oldSettings["contentType"] = setting;

  // DL, DC Factor
  setting.clear();
  setting[0] = "2.0x";
  setting[1] = "1.5x";
  setting[2] = "1.2x";
  setting[3] = "always";
  m_oldSettings["nnediDLScalingFactor"] = setting;
  m_oldSettings["nnediDCScalingFactor"] = setting;

  // QL, QC Factor
  setting.clear();
  setting[0] = "4.0x";
  setting[1] = "3.0x";
  setting[2] = "2.4x";
  setting[3] = "always";
  m_oldSettings["nnediQLScalingFactor"] = setting;
  m_oldSettings["nnediQCScalingFactor"] = setting;

  // DL, DC, QL, DC
  setting.clear();
  setting[-1] = "-1";
  setting[0] = "NNEDI316";
  setting[1] = "NNEDI332";
  setting[2] = "NNEDI364";
  setting[3] = "NNEDI3128";
  setting[4] = "NNEDI3256";
  setting[5] = "SuperXbr25";
  setting[6] = "SuperXbr50";
  setting[7] = "SuperXbr75";
  setting[8] = "SuperXbr100";
  setting[9] = "SuperXbr125";
  setting[10] = "SuperXbr150";
  setting[11] = "NEDI";
  m_oldSettings["DL"] = setting;
  m_oldSettings["DC"] = setting;
  m_oldSettings["QL"] = setting;
  m_oldSettings["QC"] = setting;

  //donDither
  setting.clear();
  setting[-1] = "-1";
  setting[0] = "random";
  setting[1] = "ordered";
  setting[2] = "errorDifMedNoise";
  setting[3] = "errorDifLowNoise";
  m_oldSettings["dontDither"] = setting;

  //smoothMotion
  setting.clear();
  setting[-1] = "-1";
  setting[0] = "avoidJudder";
  setting[1] = "almostAlways";
  setting[2] = "always";
  m_oldSettings["smoothMotionEnabled"] = setting;
}

#endif