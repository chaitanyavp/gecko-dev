/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SMILIntegerType_h
#define mozilla_SMILIntegerType_h

#include "mozilla/Attributes.h"
#include "mozilla/SMILType.h"

namespace mozilla {

class SMILIntegerType : public SMILType {
 public:
  virtual void Init(SMILValue& aValue) const override;
  virtual void Destroy(SMILValue& aValue) const override;
  virtual nsresult Assign(SMILValue& aDest,
                          const SMILValue& aSrc) const override;
  virtual bool IsEqual(const SMILValue& aLeft,
                       const SMILValue& aRight) const override;
  virtual nsresult Add(SMILValue& aDest, const SMILValue& aValueToAdd,
                       uint32_t aCount) const override;
  virtual nsresult ComputeDistance(const SMILValue& aFrom, const SMILValue& aTo,
                                   double& aDistance) const override;
  virtual nsresult Interpolate(const SMILValue& aStartVal,
                               const SMILValue& aEndVal, double aUnitDistance,
                               SMILValue& aResult) const override;

  static SMILIntegerType* Singleton() {
    static SMILIntegerType sSingleton;
    return &sSingleton;
  }

 private:
  constexpr SMILIntegerType() {}
};

}  // namespace mozilla

#endif  // mozilla_SMILIntegerType_h
