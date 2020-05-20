/*
 *  Copyright (C) 2013-2019 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "events/UniqueEvent.h"
#include "media/import/MediaImport.h"
#include "media/import/MediaImportSource.h"

class CMediaImportSourceEvent : public CUniqueEvent
{
public:
  CMediaImportSourceEvent(const CMediaImportSource& source,
                          const CVariant& description,
                          EventLevel level = EventLevel::Information);
  CMediaImportSourceEvent(const CMediaImportSource& source,
                          const CVariant& description,
                          bool removed,
                          EventLevel level = EventLevel::Information);
  virtual ~CMediaImportSourceEvent() {}

  virtual const char* GetType() const override { return "MediaImportSourceEvent"; }
  virtual std::string GetExecutionLabel() const override;

  virtual bool CanExecute() const override;
  virtual bool Execute() const override;

protected:
  CMediaImportSource m_source;
};

class CMediaImportEvent : public CUniqueEvent
{
public:
  CMediaImportEvent(const CMediaImport& import,
                    const CVariant& description,
                    EventLevel level = EventLevel::Information);
  CMediaImportEvent(const CMediaImport& import,
                    const CVariant& description,
                    bool removed,
                    EventLevel level = EventLevel::Information);
  virtual ~CMediaImportEvent() {}

  virtual const char* GetType() const override { return "MediaImportEvent"; }
  virtual std::string GetExecutionLabel() const override;

  virtual bool CanExecute() const override;
  virtual bool Execute() const override;

protected:
  CMediaImport m_import;
};
