/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MediaImportSourceJobBase.h"

#include "ServiceBroker.h"
#include "utils/StringUtils.h"
#include "utils/log.h"

CMediaImportSourceJobBase::CMediaImportSourceJobBase(const std::string& name,
                                                     const CMediaImportSource& source,
                                                     const IMediaImporterManager* importerManager)
  : m_logger(CServiceBroker::GetLogging().GetLogger(
        StringUtils::Format("{}[{}]", name, source.GetIdentifier()))),
    m_source(source),
    m_importerManager(importerManager)
{
}

bool CMediaImportSourceJobBase::operator==(const CJob* other) const
{
  if (strcmp(other->GetType(), GetType()) != 0)
    return false;

  const CMediaImportSourceJobBase* otherSourceJob =
      dynamic_cast<const CMediaImportSourceJobBase*>(other);
  if (otherSourceJob == nullptr)
    return false;

  return m_source == otherSourceJob->m_source;
}
