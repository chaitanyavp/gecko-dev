/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SVGFECompositeElement_h
#define mozilla_dom_SVGFECompositeElement_h

#include "SVGEnum.h"
#include "SVGFilters.h"
#include "nsSVGNumber2.h"

nsresult NS_NewSVGFECompositeElement(
    nsIContent** aResult, already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

namespace mozilla {
namespace dom {

typedef SVGFE SVGFECompositeElementBase;

class SVGFECompositeElement : public SVGFECompositeElementBase {
  friend nsresult(::NS_NewSVGFECompositeElement(
      nsIContent** aResult,
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo));

 protected:
  explicit SVGFECompositeElement(
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
      : SVGFECompositeElementBase(std::move(aNodeInfo)) {}
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

 public:
  virtual FilterPrimitiveDescription GetPrimitiveDescription(
      nsSVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
      const nsTArray<bool>& aInputsAreTainted,
      nsTArray<RefPtr<SourceSurface>>& aInputImages) override;
  virtual bool AttributeAffectsRendering(int32_t aNameSpaceID,
                                         nsAtom* aAttribute) const override;
  virtual SVGString& GetResultImageName() override {
    return mStringAttributes[RESULT];
  }
  virtual void GetSourceImageNames(nsTArray<SVGStringInfo>& aSources) override;

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  // WebIDL
  already_AddRefed<SVGAnimatedString> In1();
  already_AddRefed<SVGAnimatedString> In2();
  already_AddRefed<SVGAnimatedEnumeration> Operator();
  already_AddRefed<SVGAnimatedNumber> K1();
  already_AddRefed<SVGAnimatedNumber> K2();
  already_AddRefed<SVGAnimatedNumber> K3();
  already_AddRefed<SVGAnimatedNumber> K4();
  void SetK(float k1, float k2, float k3, float k4);

 protected:
  virtual NumberAttributesInfo GetNumberInfo() override;
  virtual EnumAttributesInfo GetEnumInfo() override;
  virtual StringAttributesInfo GetStringInfo() override;

  enum { ATTR_K1, ATTR_K2, ATTR_K3, ATTR_K4 };
  nsSVGNumber2 mNumberAttributes[4];
  static NumberInfo sNumberInfo[4];

  enum { OPERATOR };
  SVGEnum mEnumAttributes[1];
  static SVGEnumMapping sOperatorMap[];
  static EnumInfo sEnumInfo[1];

  enum { RESULT, IN1, IN2 };
  SVGString mStringAttributes[3];
  static StringInfo sStringInfo[3];
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_SVGFECompositeElement_h
