/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputData.h"

#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/TextEvents.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsThreadUtils.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/TouchEvents.h"
#include "UnitTransforms.h"

namespace mozilla {

using namespace dom;

InputData::~InputData() {}

InputData::InputData(InputType aInputType)
    : mInputType(aInputType),
      mTime(0),
      mFocusSequenceNumber(0),
      mLayersId{0},
      modifiers(0) {}

InputData::InputData(InputType aInputType, uint32_t aTime, TimeStamp aTimeStamp,
                     Modifiers aModifiers)
    : mInputType(aInputType),
      mTime(aTime),
      mTimeStamp(aTimeStamp),
      mFocusSequenceNumber(0),
      mLayersId{0},
      modifiers(aModifiers) {}

SingleTouchData::SingleTouchData(int32_t aIdentifier,
                                 ScreenIntPoint aScreenPoint,
                                 ScreenSize aRadius, float aRotationAngle,
                                 float aForce)
    : mIdentifier(aIdentifier),
      mScreenPoint(aScreenPoint),
      mRadius(aRadius),
      mRotationAngle(aRotationAngle),
      mForce(aForce) {}

SingleTouchData::SingleTouchData(int32_t aIdentifier,
                                 ParentLayerPoint aLocalScreenPoint,
                                 ScreenSize aRadius, float aRotationAngle,
                                 float aForce)
    : mIdentifier(aIdentifier),
      mLocalScreenPoint(aLocalScreenPoint),
      mRadius(aRadius),
      mRotationAngle(aRotationAngle),
      mForce(aForce) {}

SingleTouchData::SingleTouchData()
    : mIdentifier(0), mRotationAngle(0.0), mForce(0.0) {}

already_AddRefed<Touch> SingleTouchData::ToNewDOMTouch() const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only create dom::Touch instances on main thread");
  RefPtr<Touch> touch =
      new Touch(mIdentifier,
                LayoutDeviceIntPoint::Truncate(mScreenPoint.x, mScreenPoint.y),
                LayoutDeviceIntPoint::Truncate(mRadius.width, mRadius.height),
                mRotationAngle, mForce);
  return touch.forget();
}

MultiTouchInput::MultiTouchInput(MultiTouchType aType, uint32_t aTime,
                                 TimeStamp aTimeStamp, Modifiers aModifiers)
    : InputData(MULTITOUCH_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mHandledByAPZ(false) {}

MultiTouchInput::MultiTouchInput()
    : InputData(MULTITOUCH_INPUT),
      mType(MULTITOUCH_START),
      mHandledByAPZ(false) {}

MultiTouchInput::MultiTouchInput(const MultiTouchInput& aOther)
    : InputData(MULTITOUCH_INPUT, aOther.mTime, aOther.mTimeStamp,
                aOther.modifiers),
      mType(aOther.mType),
      mScreenOffset(aOther.mScreenOffset),
      mHandledByAPZ(aOther.mHandledByAPZ) {
  mTouches.AppendElements(aOther.mTouches);
}

MultiTouchInput::MultiTouchInput(const WidgetTouchEvent& aTouchEvent)
    : InputData(MULTITOUCH_INPUT, aTouchEvent.mTime, aTouchEvent.mTimeStamp,
                aTouchEvent.mModifiers),
      mHandledByAPZ(aTouchEvent.mFlags.mHandledByAPZ) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only copy from WidgetTouchEvent on main thread");

  switch (aTouchEvent.mMessage) {
    case eTouchStart:
      mType = MULTITOUCH_START;
      break;
    case eTouchMove:
      mType = MULTITOUCH_MOVE;
      break;
    case eTouchEnd:
      mType = MULTITOUCH_END;
      break;
    case eTouchCancel:
      mType = MULTITOUCH_CANCEL;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Did not assign a type to a MultiTouchInput");
      break;
  }

  mScreenOffset = ViewAs<ExternalPixel>(
      aTouchEvent.mWidget->WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

  for (size_t i = 0; i < aTouchEvent.mTouches.Length(); i++) {
    const Touch* domTouch = aTouchEvent.mTouches[i];

    // Extract data from weird interfaces.
    int32_t identifier = domTouch->Identifier();
    int32_t radiusX = domTouch->RadiusX(CallerType::System);
    int32_t radiusY = domTouch->RadiusY(CallerType::System);
    float rotationAngle = domTouch->RotationAngle(CallerType::System);
    float force = domTouch->Force(CallerType::System);

    SingleTouchData data(
        identifier,
        ViewAs<ScreenPixel>(
            domTouch->mRefPoint,
            PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent),
        ScreenSize(radiusX, radiusY), rotationAngle, force);

    mTouches.AppendElement(data);
  }
}

void MultiTouchInput::Translate(const ScreenPoint& aTranslation) {
  const int32_t xTranslation = (int32_t)(aTranslation.x + 0.5f);
  const int32_t yTranslation = (int32_t)(aTranslation.y + 0.5f);

  for (auto iter = mTouches.begin(); iter != mTouches.end(); iter++) {
    iter->mScreenPoint.MoveBy(xTranslation, yTranslation);
  }
}

WidgetTouchEvent MultiTouchInput::ToWidgetTouchEvent(nsIWidget* aWidget) const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only convert To WidgetTouchEvent on main thread");

  EventMessage touchEventMessage = eVoidEvent;
  switch (mType) {
    case MULTITOUCH_START:
      touchEventMessage = eTouchStart;
      break;
    case MULTITOUCH_MOVE:
      touchEventMessage = eTouchMove;
      break;
    case MULTITOUCH_END:
      touchEventMessage = eTouchEnd;
      break;
    case MULTITOUCH_CANCEL:
      touchEventMessage = eTouchCancel;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Did not assign a type to WidgetTouchEvent in MultiTouchInput");
      break;
  }

  WidgetTouchEvent event(true, touchEventMessage, aWidget);
  if (touchEventMessage == eVoidEvent) {
    return event;
  }

  event.mModifiers = this->modifiers;
  event.mTime = this->mTime;
  event.mTimeStamp = this->mTimeStamp;
  event.mFlags.mHandledByAPZ = mHandledByAPZ;
  event.mFocusSequenceNumber = mFocusSequenceNumber;

  for (size_t i = 0; i < mTouches.Length(); i++) {
    *event.mTouches.AppendElement() = mTouches[i].ToNewDOMTouch();
  }

  return event;
}

WidgetMouseEvent MultiTouchInput::ToWidgetMouseEvent(nsIWidget* aWidget) const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only convert To WidgetMouseEvent on main thread");

  EventMessage mouseEventMessage = eVoidEvent;
  switch (mType) {
    case MultiTouchInput::MULTITOUCH_START:
      mouseEventMessage = eMouseDown;
      break;
    case MultiTouchInput::MULTITOUCH_MOVE:
      mouseEventMessage = eMouseMove;
      break;
    case MultiTouchInput::MULTITOUCH_CANCEL:
    case MultiTouchInput::MULTITOUCH_END:
      mouseEventMessage = eMouseUp;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Did not assign a type to WidgetMouseEvent");
      break;
  }

  WidgetMouseEvent event(true, mouseEventMessage, aWidget,
                         WidgetMouseEvent::eReal, WidgetMouseEvent::eNormal);

  const SingleTouchData& firstTouch = mTouches[0];
  event.mRefPoint.x = firstTouch.mScreenPoint.x;
  event.mRefPoint.y = firstTouch.mScreenPoint.y;

  event.mTime = mTime;
  event.button = WidgetMouseEvent::eLeftButton;
  event.inputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;
  event.mModifiers = modifiers;
  event.mFlags.mHandledByAPZ = mHandledByAPZ;
  event.mFocusSequenceNumber = mFocusSequenceNumber;

  if (mouseEventMessage != eMouseMove) {
    event.mClickCount = 1;
  }

  return event;
}

int32_t MultiTouchInput::IndexOfTouch(int32_t aTouchIdentifier) {
  for (size_t i = 0; i < mTouches.Length(); i++) {
    if (mTouches[i].mIdentifier == aTouchIdentifier) {
      return (int32_t)i;
    }
  }
  return -1;
}

bool MultiTouchInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  for (size_t i = 0; i < mTouches.Length(); i++) {
    Maybe<ParentLayerIntPoint> point =
        UntransformBy(aTransform, mTouches[i].mScreenPoint);
    if (!point) {
      return false;
    }
    mTouches[i].mLocalScreenPoint = *point;
  }
  return true;
}

MouseInput::MouseInput()
    : InputData(MOUSE_INPUT),
      mType(MOUSE_NONE),
      mButtonType(NONE),
      mInputSource(0),
      mButtons(0),
      mHandledByAPZ(false) {}

MouseInput::MouseInput(MouseType aType, ButtonType aButtonType,
                       uint16_t aInputSource, int16_t aButtons,
                       const ScreenPoint& aPoint, uint32_t aTime,
                       TimeStamp aTimeStamp, Modifiers aModifiers)
    : InputData(MOUSE_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mButtonType(aButtonType),
      mInputSource(aInputSource),
      mButtons(aButtons),
      mOrigin(aPoint),
      mHandledByAPZ(false) {}

MouseInput::MouseInput(const WidgetMouseEventBase& aMouseEvent)
    : InputData(MOUSE_INPUT, aMouseEvent.mTime, aMouseEvent.mTimeStamp,
                aMouseEvent.mModifiers),
      mType(MOUSE_NONE),
      mButtonType(NONE),
      mInputSource(aMouseEvent.inputSource),
      mButtons(aMouseEvent.buttons),
      mHandledByAPZ(aMouseEvent.mFlags.mHandledByAPZ) {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only copy from WidgetTouchEvent on main thread");

  mButtonType = NONE;

  switch (aMouseEvent.button) {
    case WidgetMouseEventBase::eLeftButton:
      mButtonType = MouseInput::LEFT_BUTTON;
      break;
    case WidgetMouseEventBase::eMiddleButton:
      mButtonType = MouseInput::MIDDLE_BUTTON;
      break;
    case WidgetMouseEventBase::eRightButton:
      mButtonType = MouseInput::RIGHT_BUTTON;
      break;
  }

  switch (aMouseEvent.mMessage) {
    case eMouseMove:
      mType = MOUSE_MOVE;
      break;
    case eMouseUp:
      mType = MOUSE_UP;
      break;
    case eMouseDown:
      mType = MOUSE_DOWN;
      break;
    case eDragStart:
      mType = MOUSE_DRAG_START;
      break;
    case eDragEnd:
      mType = MOUSE_DRAG_END;
      break;
    case eMouseEnterIntoWidget:
      mType = MOUSE_WIDGET_ENTER;
      break;
    case eMouseExitFromWidget:
      mType = MOUSE_WIDGET_EXIT;
      break;
    case eMouseHitTest:
      mType = MOUSE_HITTEST;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Mouse event type not supported");
      break;
  }

  mOrigin = ScreenPoint(ViewAs<ScreenPixel>(
      aMouseEvent.mRefPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
}

bool MouseInput::IsLeftButton() const { return mButtonType == LEFT_BUTTON; }

bool MouseInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mOrigin);
  if (!point) {
    return false;
  }
  mLocalOrigin = *point;

  return true;
}

WidgetMouseEvent MouseInput::ToWidgetMouseEvent(nsIWidget* aWidget) const {
  MOZ_ASSERT(NS_IsMainThread(),
             "Can only convert To WidgetTouchEvent on main thread");

  EventMessage msg = eVoidEvent;
  uint32_t clickCount = 0;
  switch (mType) {
    case MOUSE_MOVE:
      msg = eMouseMove;
      break;
    case MOUSE_UP:
      msg = eMouseUp;
      clickCount = 1;
      break;
    case MOUSE_DOWN:
      msg = eMouseDown;
      clickCount = 1;
      break;
    case MOUSE_DRAG_START:
      msg = eDragStart;
      break;
    case MOUSE_DRAG_END:
      msg = eDragEnd;
      break;
    case MOUSE_WIDGET_ENTER:
      msg = eMouseEnterIntoWidget;
      break;
    case MOUSE_WIDGET_EXIT:
      msg = eMouseExitFromWidget;
      break;
    case MOUSE_HITTEST:
      msg = eMouseHitTest;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Did not assign a type to WidgetMouseEvent in MouseInput");
      break;
  }

  WidgetMouseEvent event(true, msg, aWidget, WidgetMouseEvent::eReal,
                         WidgetMouseEvent::eNormal);

  if (msg == eVoidEvent) {
    return event;
  }

  switch (mButtonType) {
    case MouseInput::LEFT_BUTTON:
      event.button = WidgetMouseEventBase::eLeftButton;
      break;
    case MouseInput::MIDDLE_BUTTON:
      event.button = WidgetMouseEventBase::eMiddleButton;
      break;
    case MouseInput::RIGHT_BUTTON:
      event.button = WidgetMouseEventBase::eRightButton;
      break;
    case MouseInput::NONE:
    default:
      break;
  }

  event.buttons = mButtons;
  event.mModifiers = modifiers;
  event.mTime = mTime;
  event.mTimeStamp = mTimeStamp;
  event.mFlags.mHandledByAPZ = mHandledByAPZ;
  event.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mOrigin,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  event.mClickCount = clickCount;
  event.inputSource = mInputSource;
  event.mIgnoreRootScrollFrame = true;
  event.mFocusSequenceNumber = mFocusSequenceNumber;

  return event;
}

PanGestureInput::PanGestureInput()
    : InputData(PANGESTURE_INPUT),
      mType(PANGESTURE_MAYSTART),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mHandledByAPZ(false),
      mFollowedByMomentum(false),
      mRequiresContentResponseIfCannotScrollHorizontallyInStartDirection(false),
      mOverscrollBehaviorAllowsSwipe(false) {}

PanGestureInput::PanGestureInput(PanGestureType aType, uint32_t aTime,
                                 TimeStamp aTimeStamp,
                                 const ScreenPoint& aPanStartPoint,
                                 const ScreenPoint& aPanDisplacement,
                                 Modifiers aModifiers)
    : InputData(PANGESTURE_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mPanStartPoint(aPanStartPoint),
      mPanDisplacement(aPanDisplacement),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mHandledByAPZ(false),
      mFollowedByMomentum(false),
      mRequiresContentResponseIfCannotScrollHorizontallyInStartDirection(false),
      mOverscrollBehaviorAllowsSwipe(false) {}

bool PanGestureInput::IsMomentum() const {
  switch (mType) {
    case PanGestureInput::PANGESTURE_MOMENTUMSTART:
    case PanGestureInput::PANGESTURE_MOMENTUMPAN:
    case PanGestureInput::PANGESTURE_MOMENTUMEND:
      return true;
    default:
      return false;
  }
}

WidgetWheelEvent PanGestureInput::ToWidgetWheelEvent(nsIWidget* aWidget) const {
  WidgetWheelEvent wheelEvent(true, eWheel, aWidget);
  wheelEvent.mModifiers = this->modifiers;
  wheelEvent.mTime = mTime;
  wheelEvent.mTimeStamp = mTimeStamp;
  wheelEvent.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mPanStartPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  wheelEvent.buttons = 0;
  wheelEvent.mDeltaMode = WheelEvent_Binding::DOM_DELTA_PIXEL;
  wheelEvent.mMayHaveMomentum = true;  // pan inputs may have momentum
  wheelEvent.mIsMomentum = IsMomentum();
  wheelEvent.mLineOrPageDeltaX = mLineOrPageDeltaX;
  wheelEvent.mLineOrPageDeltaY = mLineOrPageDeltaY;
  wheelEvent.mDeltaX = mPanDisplacement.x;
  wheelEvent.mDeltaY = mPanDisplacement.y;
  wheelEvent.mFlags.mHandledByAPZ = mHandledByAPZ;
  wheelEvent.mFocusSequenceNumber = mFocusSequenceNumber;
  return wheelEvent;
}

bool PanGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> panStartPoint =
      UntransformBy(aTransform, mPanStartPoint);
  if (!panStartPoint) {
    return false;
  }
  mLocalPanStartPoint = *panStartPoint;

  Maybe<ParentLayerPoint> panDisplacement =
      UntransformVector(aTransform, mPanDisplacement, mPanStartPoint);
  if (!panDisplacement) {
    return false;
  }
  mLocalPanDisplacement = *panDisplacement;
  return true;
}

ScreenPoint PanGestureInput::UserMultipliedPanDisplacement() const {
  return ScreenPoint(mPanDisplacement.x * mUserDeltaMultiplierX,
                     mPanDisplacement.y * mUserDeltaMultiplierY);
}

ParentLayerPoint PanGestureInput::UserMultipliedLocalPanDisplacement() const {
  return ParentLayerPoint(mLocalPanDisplacement.x * mUserDeltaMultiplierX,
                          mLocalPanDisplacement.y * mUserDeltaMultiplierY);
}

PinchGestureInput::PinchGestureInput()
    : InputData(PINCHGESTURE_INPUT), mType(PINCHGESTURE_START) {}

PinchGestureInput::PinchGestureInput(
    PinchGestureType aType, uint32_t aTime, TimeStamp aTimeStamp,
    const ExternalPoint& aScreenOffset, const ScreenPoint& aFocusPoint,
    ScreenCoord aCurrentSpan, ScreenCoord aPreviousSpan, Modifiers aModifiers)
    : InputData(PINCHGESTURE_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mFocusPoint(aFocusPoint),
      mScreenOffset(aScreenOffset),
      mCurrentSpan(aCurrentSpan),
      mPreviousSpan(aPreviousSpan) {}

bool PinchGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  if (mFocusPoint == BothFingersLifted<ScreenPixel>()) {
    // Special value, no transform required.
    mLocalFocusPoint = BothFingersLifted();
    return true;
  }
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mFocusPoint);
  if (!point) {
    return false;
  }
  mLocalFocusPoint = *point;
  return true;
}

TapGestureInput::TapGestureInput()
    : InputData(TAPGESTURE_INPUT), mType(TAPGESTURE_LONG) {}

TapGestureInput::TapGestureInput(TapGestureType aType, uint32_t aTime,
                                 TimeStamp aTimeStamp,
                                 const ScreenIntPoint& aPoint,
                                 Modifiers aModifiers)
    : InputData(TAPGESTURE_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mPoint(aPoint) {}

TapGestureInput::TapGestureInput(TapGestureType aType, uint32_t aTime,
                                 TimeStamp aTimeStamp,
                                 const ParentLayerPoint& aLocalPoint,
                                 Modifiers aModifiers)
    : InputData(TAPGESTURE_INPUT, aTime, aTimeStamp, aModifiers),
      mType(aType),
      mLocalPoint(aLocalPoint) {}

bool TapGestureInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerIntPoint> point = UntransformBy(aTransform, mPoint);
  if (!point) {
    return false;
  }
  mLocalPoint = *point;
  return true;
}

ScrollWheelInput::ScrollWheelInput()
    : InputData(SCROLLWHEEL_INPUT),
      mDeltaType(SCROLLDELTA_LINE),
      mScrollMode(SCROLLMODE_INSTANT),
      mHandledByAPZ(false),
      mDeltaX(0.0),
      mDeltaY(0.0),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(false),
      mIsMomentum(false),
      mAPZAction(APZWheelAction::Scroll) {}

ScrollWheelInput::ScrollWheelInput(
    uint32_t aTime, TimeStamp aTimeStamp, Modifiers aModifiers,
    ScrollMode aScrollMode, ScrollDeltaType aDeltaType,
    const ScreenPoint& aOrigin, double aDeltaX, double aDeltaY,
    bool aAllowToOverrideSystemScrollSpeed,
    WheelDeltaAdjustmentStrategy aWheelDeltaAdjustmentStrategy)
    : InputData(SCROLLWHEEL_INPUT, aTime, aTimeStamp, aModifiers),
      mDeltaType(aDeltaType),
      mScrollMode(aScrollMode),
      mOrigin(aOrigin),
      mHandledByAPZ(false),
      mDeltaX(aDeltaX),
      mDeltaY(aDeltaY),
      mLineOrPageDeltaX(0),
      mLineOrPageDeltaY(0),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(false),
      mIsMomentum(false),
      mAllowToOverrideSystemScrollSpeed(aAllowToOverrideSystemScrollSpeed),
      mWheelDeltaAdjustmentStrategy(aWheelDeltaAdjustmentStrategy),
      mAPZAction(APZWheelAction::Scroll) {}

ScrollWheelInput::ScrollWheelInput(const WidgetWheelEvent& aWheelEvent)
    : InputData(SCROLLWHEEL_INPUT, aWheelEvent.mTime, aWheelEvent.mTimeStamp,
                aWheelEvent.mModifiers),
      mDeltaType(DeltaTypeForDeltaMode(aWheelEvent.mDeltaMode)),
      mScrollMode(SCROLLMODE_INSTANT),
      mHandledByAPZ(aWheelEvent.mFlags.mHandledByAPZ),
      mDeltaX(aWheelEvent.mDeltaX),
      mDeltaY(aWheelEvent.mDeltaY),
      mLineOrPageDeltaX(aWheelEvent.mLineOrPageDeltaX),
      mLineOrPageDeltaY(aWheelEvent.mLineOrPageDeltaY),
      mScrollSeriesNumber(0),
      mUserDeltaMultiplierX(1.0),
      mUserDeltaMultiplierY(1.0),
      mMayHaveMomentum(aWheelEvent.mMayHaveMomentum),
      mIsMomentum(aWheelEvent.mIsMomentum),
      mAllowToOverrideSystemScrollSpeed(
          aWheelEvent.mAllowToOverrideSystemScrollSpeed),
      mWheelDeltaAdjustmentStrategy(WheelDeltaAdjustmentStrategy::eNone),
      mAPZAction(APZWheelAction::Scroll) {
  mOrigin = ScreenPoint(ViewAs<ScreenPixel>(
      aWheelEvent.mRefPoint,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
}

ScrollWheelInput::ScrollDeltaType ScrollWheelInput::DeltaTypeForDeltaMode(
    uint32_t aDeltaMode) {
  switch (aDeltaMode) {
    case WheelEvent_Binding::DOM_DELTA_LINE:
      return SCROLLDELTA_LINE;
    case WheelEvent_Binding::DOM_DELTA_PAGE:
      return SCROLLDELTA_PAGE;
    case WheelEvent_Binding::DOM_DELTA_PIXEL:
      return SCROLLDELTA_PIXEL;
    default:
      MOZ_CRASH();
  }
  return SCROLLDELTA_LINE;
}

uint32_t ScrollWheelInput::DeltaModeForDeltaType(ScrollDeltaType aDeltaType) {
  switch (aDeltaType) {
    case ScrollWheelInput::SCROLLDELTA_LINE:
      return WheelEvent_Binding::DOM_DELTA_LINE;
    case ScrollWheelInput::SCROLLDELTA_PAGE:
      return WheelEvent_Binding::DOM_DELTA_PAGE;
    case ScrollWheelInput::SCROLLDELTA_PIXEL:
    default:
      return WheelEvent_Binding::DOM_DELTA_PIXEL;
  }
}

nsIScrollableFrame::ScrollUnit ScrollWheelInput::ScrollUnitForDeltaType(
    ScrollDeltaType aDeltaType) {
  switch (aDeltaType) {
    case SCROLLDELTA_LINE:
      return nsIScrollableFrame::LINES;
    case SCROLLDELTA_PAGE:
      return nsIScrollableFrame::PAGES;
    case SCROLLDELTA_PIXEL:
      return nsIScrollableFrame::DEVICE_PIXELS;
    default:
      MOZ_CRASH();
  }
  return nsIScrollableFrame::LINES;
}

WidgetWheelEvent ScrollWheelInput::ToWidgetWheelEvent(
    nsIWidget* aWidget) const {
  WidgetWheelEvent wheelEvent(true, eWheel, aWidget);
  wheelEvent.mModifiers = this->modifiers;
  wheelEvent.mTime = mTime;
  wheelEvent.mTimeStamp = mTimeStamp;
  wheelEvent.mRefPoint = RoundedToInt(ViewAs<LayoutDevicePixel>(
      mOrigin,
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent));
  wheelEvent.buttons = 0;
  wheelEvent.mDeltaMode = DeltaModeForDeltaType(mDeltaType);
  wheelEvent.mMayHaveMomentum = mMayHaveMomentum;
  wheelEvent.mIsMomentum = mIsMomentum;
  wheelEvent.mDeltaX = mDeltaX;
  wheelEvent.mDeltaY = mDeltaY;
  wheelEvent.mLineOrPageDeltaX = mLineOrPageDeltaX;
  wheelEvent.mLineOrPageDeltaY = mLineOrPageDeltaY;
  wheelEvent.mAllowToOverrideSystemScrollSpeed =
      mAllowToOverrideSystemScrollSpeed;
  wheelEvent.mFlags.mHandledByAPZ = mHandledByAPZ;
  wheelEvent.mFocusSequenceNumber = mFocusSequenceNumber;
  return wheelEvent;
}

bool ScrollWheelInput::TransformToLocal(
    const ScreenToParentLayerMatrix4x4& aTransform) {
  Maybe<ParentLayerPoint> point = UntransformBy(aTransform, mOrigin);
  if (!point) {
    return false;
  }
  mLocalOrigin = *point;
  return true;
}

bool ScrollWheelInput::IsCustomizedByUserPrefs() const {
  return mUserDeltaMultiplierX != 1.0 || mUserDeltaMultiplierY != 1.0;
}

KeyboardInput::KeyboardInput(const WidgetKeyboardEvent& aEvent)
    : InputData(KEYBOARD_INPUT, aEvent.mTime, aEvent.mTimeStamp,
                aEvent.mModifiers),
      mKeyCode(aEvent.mKeyCode),
      mCharCode(aEvent.mCharCode),
      mHandledByAPZ(false) {
  switch (aEvent.mMessage) {
    case eKeyPress: {
      mType = KeyboardInput::KEY_PRESS;
      break;
    }
    case eKeyUp: {
      mType = KeyboardInput::KEY_UP;
      break;
    }
    case eKeyDown: {
      mType = KeyboardInput::KEY_DOWN;
      break;
    }
    default:
      mType = KeyboardInput::KEY_OTHER;
      break;
  }

  aEvent.GetShortcutKeyCandidates(mShortcutCandidates);
}

KeyboardInput::KeyboardInput()
    : InputData(KEYBOARD_INPUT),
      mType(KEY_DOWN),
      mKeyCode(0),
      mCharCode(0),
      mHandledByAPZ(false) {}

}  // namespace mozilla
