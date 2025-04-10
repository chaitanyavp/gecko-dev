/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsImageFrame.h"
#include "nsIFormControlFrame.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsCheckboxRadioFrame.h"
#include "nsLayoutUtils.h"
#include "mozilla/MouseEvents.h"
#include "nsIContent.h"

using namespace mozilla;

class nsImageControlFrame final : public nsImageFrame,
                                  public nsIFormControlFrame {
 public:
  explicit nsImageControlFrame(ComputedStyle* aStyle,
                               nsPresContext* aPresContext);
  ~nsImageControlFrame();

  virtual void DestroyFrom(nsIFrame* aDestructRoot,
                           PostDestroyData& aPostDestroyData) override;
  virtual void Init(nsIContent* aContent, nsContainerFrame* aParent,
                    nsIFrame* aPrevInFlow) override;

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsImageControlFrame)

  virtual void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
                      const ReflowInput& aReflowInput,
                      nsReflowStatus& aStatus) override;

  virtual nsresult HandleEvent(nsPresContext* aPresContext,
                               WidgetGUIEvent* aEvent,
                               nsEventStatus* aEventStatus) override;

#ifdef ACCESSIBILITY
  virtual mozilla::a11y::AccType AccessibleType() override;
#endif

#ifdef DEBUG_FRAME_DUMP
  virtual nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(NS_LITERAL_STRING("ImageControl"), aResult);
  }
#endif

  virtual nsresult GetCursor(const nsPoint& aPoint,
                             nsIFrame::Cursor& aCursor) override;
  // nsIFormContromFrame
  virtual void SetFocus(bool aOn, bool aRepaint) override;
  virtual nsresult SetFormProperty(nsAtom* aName,
                                   const nsAString& aValue) override;
};

nsImageControlFrame::nsImageControlFrame(ComputedStyle* aStyle,
                                         nsPresContext* aPresContext)
    : nsImageFrame(aStyle, aPresContext, kClassID) {}

nsImageControlFrame::~nsImageControlFrame() {}

void nsImageControlFrame::DestroyFrom(nsIFrame* aDestructRoot,
                                      PostDestroyData& aPostDestroyData) {
  if (!GetPrevInFlow()) {
    nsCheckboxRadioFrame::RegUnRegAccessKey(this, false);
  }
  nsImageFrame::DestroyFrom(aDestructRoot, aPostDestroyData);
}

nsIFrame* NS_NewImageControlFrame(nsIPresShell* aPresShell,
                                  ComputedStyle* aStyle) {
  return new (aPresShell)
      nsImageControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsImageControlFrame)

void nsImageControlFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                               nsIFrame* aPrevInFlow) {
  nsImageFrame::Init(aContent, aParent, aPrevInFlow);

  if (aPrevInFlow) {
    return;
  }

  mContent->SetProperty(nsGkAtoms::imageClickedPoint, new nsIntPoint(0, 0),
                        nsINode::DeleteProperty<nsIntPoint>);
}

NS_QUERYFRAME_HEAD(nsImageControlFrame)
  NS_QUERYFRAME_ENTRY(nsIFormControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsImageFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsImageControlFrame::AccessibleType() {
  if (mContent->IsAnyOfHTMLElements(nsGkAtoms::button, nsGkAtoms::input)) {
    return a11y::eHTMLButtonType;
  }

  return a11y::eNoType;
}
#endif

void nsImageControlFrame::Reflow(nsPresContext* aPresContext,
                                 ReflowOutput& aDesiredSize,
                                 const ReflowInput& aReflowInput,
                                 nsReflowStatus& aStatus) {
  DO_GLOBAL_REFLOW_COUNT("nsImageControlFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowInput, aDesiredSize, aStatus);
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  if (!GetPrevInFlow() && (mState & NS_FRAME_FIRST_REFLOW)) {
    nsCheckboxRadioFrame::RegUnRegAccessKey(this, true);
  }
  return nsImageFrame::Reflow(aPresContext, aDesiredSize, aReflowInput,
                              aStatus);
}

nsresult nsImageControlFrame::HandleEvent(nsPresContext* aPresContext,
                                          WidgetGUIEvent* aEvent,
                                          nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  // Don't do anything if the event has already been handled by someone
  if (nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  if (IsContentDisabled()) {
    return nsFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  }

  *aEventStatus = nsEventStatus_eIgnore;

  if (aEvent->mMessage == eMouseUp &&
      aEvent->AsMouseEvent()->button == WidgetMouseEvent::eLeftButton) {
    // Store click point for HTMLInputElement::SubmitNamesValues
    // Do this on MouseUp because the specs don't say and that's what IE does
    nsIntPoint* lastClickPoint = static_cast<nsIntPoint*>(
        mContent->GetProperty(nsGkAtoms::imageClickedPoint));
    if (lastClickPoint) {
      // normally lastClickedPoint is not null, as it's allocated in Init()
      nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, this);
      TranslateEventCoords(pt, *lastClickPoint);
    }
  }
  return nsImageFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

void nsImageControlFrame::SetFocus(bool aOn, bool aRepaint) {}

nsresult nsImageControlFrame::GetCursor(const nsPoint& aPoint,
                                        nsIFrame::Cursor& aCursor) {
  // Use style defined cursor if one is provided, otherwise when
  // the cursor style is "auto" we use the pointer cursor.
  FillCursorInformationFromStyle(StyleUI(), aCursor);

  if (StyleCursorKind::Auto == aCursor.mCursor) {
    aCursor.mCursor = StyleCursorKind::Pointer;
  }

  return NS_OK;
}

nsresult nsImageControlFrame::SetFormProperty(nsAtom* aName,
                                              const nsAString& aValue) {
  return NS_OK;
}
