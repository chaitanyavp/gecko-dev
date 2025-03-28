/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nr_socket_proxy_config.h"

#include "mozilla/dom/PBrowserOrId.h"

namespace mozilla {

class NrSocketProxyConfig::Private {
 public:
  dom::PBrowserOrId mBrowser;
  nsCString mAlpn;
};

NrSocketProxyConfig::NrSocketProxyConfig(const dom::PBrowserOrId& aBrowser,
                                         const nsCString& aAlpn)
    : mPrivate(new Private({aBrowser, aAlpn})) {}

NrSocketProxyConfig::NrSocketProxyConfig(NrSocketProxyConfig&& aOrig)
    : mPrivate(std::move(aOrig.mPrivate)) {}

NrSocketProxyConfig::~NrSocketProxyConfig() {}

const dom::PBrowserOrId& NrSocketProxyConfig::GetBrowser() const {
  return mPrivate->mBrowser;
}

const nsCString& NrSocketProxyConfig::GetAlpn() const {
  return mPrivate->mAlpn;
}

}  // namespace mozilla
