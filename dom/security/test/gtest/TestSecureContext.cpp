/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include <string.h>
#include <stdlib.h>

#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsScriptSecurityManager.h"
#include "mozilla/NullPrincipal.h"

using namespace mozilla;

static const uint32_t kURIMaxLength = 64;

struct TestExpectations {
  char uri[kURIMaxLength];
  bool expectedResult;
};

// ============================= TestDirectives ========================

TEST(SecureContext, IsOriginPotentiallyTrustworthyWithCodeBasePrincipal) {
  // boolean isOriginPotentiallyTrustworthy(in nsIPrincipal aPrincipal);

  static const TestExpectations uris[] = {
      {"http://example.com/", false},
      {"https://example.com/", true},
      {"ws://example.com/", false},
      {"wss://example.com/", true},
      {"file:///xyzzy", true},
      {"ftp://example.com", false},
      {"about:config", false},
      {"http://localhost", true},
      {"http://xyzzy.localhost", false},
      {"http://127.0.0.1", true},
      {"resource://xyzzy", true},
      {"moz-extension://xyzzy", true},
      {"data:data:text/plain;charset=utf-8;base64,eHl6enk=", false},
      {"blob://unique-id", false},
      {"mailto:foo@bar.com", false},
      {"moz-icon://example.com", false},
      {"javascript:42", false},
  };

  uint32_t numExpectations = sizeof(uris) / sizeof(TestExpectations);
  nsCOMPtr<nsIContentSecurityManager> csManager =
      do_GetService(NS_CONTENTSECURITYMANAGER_CONTRACTID);
  ASSERT_TRUE(!!csManager);

  nsresult rv;
  for (uint32_t i = 0; i < numExpectations; i++) {
    nsCOMPtr<nsIPrincipal> prin;
    nsAutoCString uri(uris[i].uri);
    rv = nsScriptSecurityManager::GetScriptSecurityManager()
             ->CreateCodebasePrincipalFromOrigin(uri, getter_AddRefs(prin));
    bool isPotentiallyTrustworthy = false;
    rv = csManager->IsOriginPotentiallyTrustworthy(prin,
                                                   &isPotentiallyTrustworthy);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(isPotentiallyTrustworthy, uris[i].expectedResult);
  }
}

TEST(SecureContext, IsOriginPotentiallyTrustworthyWithSystemPrincipal) {
  RefPtr<nsScriptSecurityManager> ssManager =
      nsScriptSecurityManager::GetScriptSecurityManager();
  ASSERT_TRUE(!!ssManager);
  nsCOMPtr<nsIContentSecurityManager> csManager =
      do_GetService(NS_CONTENTSECURITYMANAGER_CONTRACTID);
  ASSERT_TRUE(!!csManager);

  nsCOMPtr<nsIPrincipal> sysPrin = nsContentUtils::GetSystemPrincipal();
  bool isPotentiallyTrustworthy;
  nsresult rv = csManager->IsOriginPotentiallyTrustworthy(
      sysPrin, &isPotentiallyTrustworthy);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_TRUE(isPotentiallyTrustworthy);
}

TEST(SecureContext, IsOriginPotentiallyTrustworthyWithNullPrincipal) {
  RefPtr<nsScriptSecurityManager> ssManager =
      nsScriptSecurityManager::GetScriptSecurityManager();
  ASSERT_TRUE(!!ssManager);
  nsCOMPtr<nsIContentSecurityManager> csManager =
      do_GetService(NS_CONTENTSECURITYMANAGER_CONTRACTID);
  ASSERT_TRUE(!!csManager);

  RefPtr<NullPrincipal> nullPrin =
      NullPrincipal::CreateWithoutOriginAttributes();
  bool isPotentiallyTrustworthy;
  nsresult rv = csManager->IsOriginPotentiallyTrustworthy(
      nullPrin, &isPotentiallyTrustworthy);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_TRUE(!isPotentiallyTrustworthy);
}
