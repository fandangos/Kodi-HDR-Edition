/*
 *  Copyright (C) 2012-2021 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "pvr/channels/PVRChannelNumber.h"

#include <memory>

namespace PVR
{

class CPVRChannel;

class CPVRChannelGroupMember
{
  friend class CPVRDatabase;

public:
  CPVRChannelGroupMember() = default;

  CPVRChannelGroupMember(const std::shared_ptr<CPVRChannel>& channel,
                         const CPVRChannelNumber& channelNumber,
                         int iClientPriority,
                         int iOrder,
                         const CPVRChannelNumber& clientChannelNumber)
    : m_channel(channel),
      m_channelNumber(channelNumber),
      m_clientChannelNumber(clientChannelNumber),
      m_iClientPriority(iClientPriority),
      m_iOrder(iOrder)
  {
  }

  virtual ~CPVRChannelGroupMember() = default;

  std::shared_ptr<CPVRChannel> Channel() const { return m_channel; }

  const CPVRChannelNumber& ChannelNumber() const { return m_channelNumber; }
  void SetChannelNumber(const CPVRChannelNumber& channelNumber);

  const CPVRChannelNumber& ClientChannelNumber() const { return m_clientChannelNumber; }
  void SetClientChannelNumber(const CPVRChannelNumber& clientChannelNumber);

  int ClientPriority() const { return m_iClientPriority; }
  void SetClientPriority(int iClientPriority);

  int Order() const { return m_iOrder; }
  void SetOrder(int iOrder);

private:
  std::shared_ptr<CPVRChannel> m_channel;
  CPVRChannelNumber m_channelNumber; // the channel number this channel has in the group
  CPVRChannelNumber
      m_clientChannelNumber; // the client channel number this channel has in the group
  int m_iClientPriority = 0;
  int m_iOrder = 0; // The value denoting the order of this member in the group
};

} // namespace PVR
