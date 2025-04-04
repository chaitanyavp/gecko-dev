/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * structs that contain the data provided by ComputedStyle, the
 * internal API for computed style data for an element
 */

#ifndef nsStyleStruct_h___
#define nsStyleStruct_h___

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/SheetType.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StyleComplexColor.h"
#include "mozilla/UniquePtr.h"
#include "nsColor.h"
#include "nsCoord.h"
#include "nsMargin.h"
#include "nsFont.h"
#include "nsStyleAutoArray.h"
#include "nsStyleCoord.h"
#include "nsStyleConsts.h"
#include "nsChangeHint.h"
#include "nsTimingFunction.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsCSSValue.h"
#include "imgRequestProxy.h"
#include "Orientation.h"
#include "CounterStyleManager.h"
#include <cstddef>  // offsetof()
#include <utility>
#include "X11UndefineNone.h"

class nsIFrame;
class nsIURI;
class nsTextFrame;
class imgIContainer;
class nsPresContext;
struct nsStyleDisplay;
struct nsStyleVisibility;
namespace mozilla {
class ComputedStyle;
namespace dom {
class ImageTracker;
}  // namespace dom
}  // namespace mozilla

namespace mozilla {

using Position = StylePosition;

/**
 * True if the effective background image position described by this depends on
 * the size of the corresponding frame.
 */
template <>
inline bool StylePosition::DependsOnPositioningAreaSize() const {
  return horizontal.HasPercent() || vertical.HasPercent();
}

template <>
inline Position Position::FromPercentage(float aPercent) {
  return {LengthPercentage::FromPercentage(aPercent),
          LengthPercentage::FromPercentage(aPercent)};
}

}  // namespace mozilla

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleFont {
  nsStyleFont(const nsStyleFont& aStyleFont);
  explicit nsStyleFont(const mozilla::dom::Document&);
  ~nsStyleFont() { MOZ_COUNT_DTOR(nsStyleFont); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleFont*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleFont& aNewData) const;

  /**
   * Return aSize multiplied by the current text zoom factor (in aPresContext).
   * aSize is allowed to be negative, but the caller is expected to deal with
   * negative results.  The result is clamped to nscoord_MIN .. nscoord_MAX.
   */
  static nscoord ZoomText(const mozilla::dom::Document&, nscoord aSize);

  nsFont mFont;
  nscoord mSize;  // Our "computed size". Can be different
                  // from mFont.size which is our "actual size" and is
                  // enforced to be >= the user's preferred min-size.
                  // mFont.size should be used for display purposes
                  // while mSize is the value to return in
                  // getComputedStyle() for example.

  // In stylo these three track whether the size is keyword-derived
  // and if so if it has been modified by a factor/offset
  float mFontSizeFactor;
  nscoord mFontSizeOffset;
  uint8_t mFontSizeKeyword;  // NS_STYLE_FONT_SIZE_*, is
                             // NS_STYLE_FONT_SIZE_NO_KEYWORD when not
                             // keyword-derived

  uint8_t mGenericID;  // generic CSS font family, if any;
                       // value is a kGenericFont_* constant, see nsFont.h.

  // MathML scriptlevel support
  int8_t mScriptLevel;
  // MathML  mathvariant support
  uint8_t mMathVariant;
  // MathML displaystyle support
  uint8_t mMathDisplay;

  // allow different min font-size for certain cases
  uint8_t mMinFontSizeRatio;  // percent * 100

  // was mLanguage set based on a lang attribute in the document?
  bool mExplicitLanguage;

  // should calls to ZoomText() and UnZoomText() be made to the font
  // size on this nsStyleFont?
  bool mAllowZoom;

  // The value mSize would have had if scriptminsize had never been applied
  nscoord mScriptUnconstrainedSize;
  nscoord mScriptMinSize;  // length
  float mScriptSizeMultiplier;
  RefPtr<nsAtom> mLanguage;
};

struct nsStyleGradientStop {
  nsStyleCoord mLocation;  // percent, coord, calc, none
  mozilla::StyleComplexColor mColor;
  bool mIsInterpolationHint;

  // Use ==/!= on nsStyleGradient instead of on the gradient stop.
  bool operator==(const nsStyleGradientStop&) const = delete;
  bool operator!=(const nsStyleGradientStop&) const = delete;
};

class nsStyleGradient final {
 public:
  nsStyleGradient();
  uint8_t mShape;  // NS_STYLE_GRADIENT_SHAPE_*
  uint8_t mSize;   // NS_STYLE_GRADIENT_SIZE_*;
                   // not used (must be FARTHEST_CORNER) for linear shape
  bool mRepeating;
  bool mLegacySyntax;  // If true, serialization should use a vendor prefix.
  // XXXdholbert This will hopefully be going away soon, if bug 1337655 sticks:
  bool mMozLegacySyntax;  // (Only makes sense when mLegacySyntax is true.)
                          // If true, serialization should use -moz prefix.
                          // Else, serialization should use -webkit prefix.

  nsStyleCoord mBgPosX;  // percent, coord, calc, none
  nsStyleCoord mBgPosY;  // percent, coord, calc, none
  nsStyleCoord mAngle;   // none, angle

  nsStyleCoord mRadiusX;  // percent, coord, calc, none
  nsStyleCoord mRadiusY;  // percent, coord, calc, none

  // stops are in the order specified in the stylesheet
  nsTArray<nsStyleGradientStop> mStops;

  bool operator==(const nsStyleGradient& aOther) const;
  bool operator!=(const nsStyleGradient& aOther) const {
    return !(*this == aOther);
  }

  bool IsOpaque();
  bool HasCalc();
  uint32_t Hash(PLDHashNumber aHash);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsStyleGradient)

 private:
  // Private destructor, to discourage deletion outside of Release():
  ~nsStyleGradient() {}

  nsStyleGradient(const nsStyleGradient& aOther) = delete;
  nsStyleGradient& operator=(const nsStyleGradient& aOther) = delete;
};

/**
 * A wrapper for an imgRequestProxy that supports off-main-thread creation
 * and equality comparison.
 *
 * An nsStyleImageRequest can be created using the constructor that takes the
 * URL, base URI, referrer and principal that can be used to initiate an image
 * load and produce an imgRequestProxy later.
 *
 * This can be called from any thread.  The nsStyleImageRequest is not
 * considered "resolved" at this point, and the Resolve() method must be called
 * later to initiate the image load and make calls to get() valid.
 *
 * Calls to TrackImage(), UntrackImage(), LockImage(), UnlockImage() and
 * RequestDiscard() are made to the imgRequestProxy and ImageTracker as
 * appropriate, according to the mode flags passed in to the constructor.
 *
 * The constructor receives a css::URLValue to represent the url()
 * information, which is held on to for the comparisons done in
 * DefinitelyEquals().
 */
class nsStyleImageRequest {
 public:
  // Flags describing whether the imgRequestProxy must be tracked in the
  // ImageTracker, whether LockImage/UnlockImage calls will be made
  // when obtaining and releasing the imgRequestProxy, and whether
  // RequestDiscard will be called on release.
  enum class Mode : uint8_t {
    // The imgRequestProxy will be added to the ImageTracker when resolved
    // Without this flag, the nsStyleImageRequest itself will call LockImage/
    // UnlockImage on the imgRequestProxy, rather than leaving locking to the
    // ImageTracker to manage.
    //
    // This flag is currently used by all nsStyleImageRequests except
    // those for list-style-image and cursor.
    Track = 0x1,

    // The imgRequestProxy will have its RequestDiscard method called when
    // the nsStyleImageRequest is going away.
    //
    // This is currently used only for cursor images.
    Discard = 0x2,
  };

  // Can be called from any thread, but Resolve() must be called later
  // on the main thread before get() can be used.
  nsStyleImageRequest(Mode aModeFlags, mozilla::css::URLValue* aImageValue);

  bool Resolve(mozilla::dom::Document&,
               const nsStyleImageRequest* aOldImageRequest);
  bool IsResolved() const { return mResolved; }

  imgRequestProxy* get() {
    MOZ_ASSERT(IsResolved(), "Resolve() must be called first");
    MOZ_ASSERT(NS_IsMainThread());
    return mRequestProxy.get();
  }
  const imgRequestProxy* get() const {
    return const_cast<nsStyleImageRequest*>(this)->get();
  }

  // Returns whether the URLValue objects in the two nsStyleImageRequests
  // return true from URLValue::DefinitelyEqualURIs.
  bool DefinitelyEquals(const nsStyleImageRequest& aOther) const;

  mozilla::css::URLValue* GetImageValue() const { return mImageValue; }

  already_AddRefed<nsIURI> GetImageURI() const;
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsStyleImageRequest);

 private:
  ~nsStyleImageRequest();
  nsStyleImageRequest& operator=(const nsStyleImageRequest& aOther) = delete;

  void MaybeTrackAndLock();

  RefPtr<imgRequestProxy> mRequestProxy;
  RefPtr<mozilla::css::URLValue> mImageValue;
  RefPtr<mozilla::dom::ImageTracker> mImageTracker;

  // Cache DocGroup for dispatching events in the destructor.
  RefPtr<mozilla::dom::DocGroup> mDocGroup;

  Mode mModeFlags;
  bool mResolved;
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsStyleImageRequest::Mode)

enum nsStyleImageType {
  eStyleImageType_Null,
  eStyleImageType_Image,
  eStyleImageType_Gradient,
  eStyleImageType_Element,
  eStyleImageType_URL
};

struct CachedBorderImageData {
  ~CachedBorderImageData() { PurgeCachedImages(); }

  // Caller are expected to ensure that the value of aSVGViewportSize is
  // different from the cached one since the method won't do the check.
  void SetCachedSVGViewportSize(const mozilla::Maybe<nsSize>& aSVGViewportSize);
  const mozilla::Maybe<nsSize>& GetCachedSVGViewportSize();
  void PurgeCachedImages();
  void SetSubImage(uint8_t aIndex, imgIContainer* aSubImage);
  imgIContainer* GetSubImage(uint8_t aIndex);

 private:
  // If this is a SVG border-image, we save the size of the SVG viewport that
  // we used when rasterizing any cached border-image subimages. (The viewport
  // size matters for percent-valued sizes & positions in inner SVG doc).
  mozilla::Maybe<nsSize> mCachedSVGViewportSize;
  nsCOMArray<imgIContainer> mSubImages;
};

/**
 * Represents a paintable image of one of the following types.
 * (1) A real image loaded from an external source.
 * (2) A CSS linear or radial gradient.
 * (3) An element within a document, or an <img>, <video>, or <canvas> element
 *     not in a document.
 * (*) Optionally a crop rect can be set to paint a partial (rectangular)
 * region of an image. (Currently, this feature is only supported with an
 * image of type (1)).
 */
struct nsStyleImage {
  typedef mozilla::css::URLValue URLValue;

  nsStyleImage();
  ~nsStyleImage();
  nsStyleImage(const nsStyleImage& aOther);
  nsStyleImage& operator=(const nsStyleImage& aOther);

  void SetNull();
  void SetImageRequest(already_AddRefed<nsStyleImageRequest> aImage);
  void SetGradientData(nsStyleGradient* aGradient);
  void SetElementId(already_AddRefed<nsAtom> aElementId);
  void SetCropRect(mozilla::UniquePtr<nsStyleSides> aCropRect);
  void SetURLValue(already_AddRefed<const URLValue> aURLValue);

  void ResolveImage(mozilla::dom::Document& aDocument,
                    const nsStyleImage* aOldImage) {
    MOZ_ASSERT(mType != eStyleImageType_Image || mImage);
    if (mType == eStyleImageType_Image && !mImage->IsResolved()) {
      const nsStyleImageRequest* oldRequest =
          (aOldImage && aOldImage->GetType() == eStyleImageType_Image)
              ? aOldImage->ImageRequest()
              : nullptr;
      mImage->Resolve(aDocument, oldRequest);
    }
  }

  nsStyleImageType GetType() const { return mType; }
  nsStyleImageRequest* ImageRequest() const {
    MOZ_ASSERT(mType == eStyleImageType_Image, "Data is not an image!");
    MOZ_ASSERT(mImage);
    return mImage;
  }
  imgRequestProxy* GetImageData() const { return ImageRequest()->get(); }
  nsStyleGradient* GetGradientData() const {
    NS_ASSERTION(mType == eStyleImageType_Gradient, "Data is not a gradient!");
    return mGradient;
  }
  bool IsResolved() const {
    return mType != eStyleImageType_Image || ImageRequest()->IsResolved();
  }
  const nsAtom* GetElementId() const {
    NS_ASSERTION(mType == eStyleImageType_Element, "Data is not an element!");
    return mElementId;
  }
  const mozilla::UniquePtr<nsStyleSides>& GetCropRect() const {
    NS_ASSERTION(mType == eStyleImageType_Image,
                 "Only image data can have a crop rect");
    return mCropRect;
  }

  already_AddRefed<nsIURI> GetImageURI() const;

  const URLValue* GetURLValue() const;

  /**
   * Compute the actual crop rect in pixels, using the source image bounds.
   * The computation involves converting percentage unit to pixel unit and
   * clamping each side value to fit in the source image bounds.
   * @param aActualCropRect the computed actual crop rect.
   * @param aIsEntireImage true iff |aActualCropRect| is identical to the
   * source image bounds.
   * @return true iff |aActualCropRect| holds a meaningful value.
   */
  bool ComputeActualCropRect(nsIntRect& aActualCropRect,
                             bool* aIsEntireImage = nullptr) const;

  /**
   * Starts the decoding of a image. Returns true if the current frame of the
   * image is complete. The return value is intended to be used instead of
   * IsComplete because IsComplete may not be up to date if notifications
   * from decoding are pending because they are being sent async.
   */
  bool StartDecoding() const;
  /**
   * @return true if the item is definitely opaque --- i.e., paints every
   * pixel within its bounds opaquely, and the bounds contains at least a pixel.
   */
  bool IsOpaque() const;
  /**
   * @return true if this image is fully loaded, and its size is calculated;
   * always returns true if |mType| is |eStyleImageType_Gradient| or
   * |eStyleImageType_Element|.
   */
  bool IsComplete() const;
  /**
   * @return true if this image is loaded without error;
   * always returns true if |mType| is |eStyleImageType_Gradient| or
   * |eStyleImageType_Element|.
   */
  bool IsLoaded() const;
  /**
   * @return true if it is 100% confident that this image contains no pixel
   * to draw.
   */
  bool IsEmpty() const {
    // There are some other cases when the image will be empty, for example
    // when the crop rect is empty. However, checking the emptiness of crop
    // rect is non-trivial since each side value can be specified with
    // percentage unit, which can not be evaluated until the source image size
    // is available. Therefore, we currently postpone the evaluation of crop
    // rect until the actual rendering time --- alternatively until
    // GetOpaqueRegion() is called.
    return mType == eStyleImageType_Null;
  }

  bool operator==(const nsStyleImage& aOther) const;
  bool operator!=(const nsStyleImage& aOther) const {
    return !(*this == aOther);
  }

  bool ImageDataEquals(const nsStyleImage& aOther) const {
    return GetType() == eStyleImageType_Image &&
           aOther.GetType() == eStyleImageType_Image &&
           GetImageData() == aOther.GetImageData();
  }

  // These methods are used for the caller to caches the sub images created
  // during a border-image paint operation
  inline void SetSubImage(uint8_t aIndex, imgIContainer* aSubImage) const;
  inline imgIContainer* GetSubImage(uint8_t aIndex) const;
  void PurgeCacheForViewportChange(
      const mozilla::Maybe<nsSize>& aSVGViewportSize,
      const bool aHasIntrinsicRatio) const;

 private:
  void DoCopy(const nsStyleImage& aOther);
  void EnsureCachedBIData() const;

  // This variable keeps some cache data for border image and is lazily
  // allocated since it is only used in border image case.
  mozilla::UniquePtr<CachedBorderImageData> mCachedBIData;

  nsStyleImageType mType;
  union {
    nsStyleImageRequest* mImage;
    nsStyleGradient* mGradient;
    const URLValue* mURLValue;  // See the comment in SetStyleImage's 'case
                                // eCSSUnit_URL' section to know why we need to
                                // store URLValues separately from mImage.
    nsAtom* mElementId;
  };

  // This is _currently_ used only in conjunction with eStyleImageType_Image.
  mozilla::UniquePtr<nsStyleSides> mCropRect;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleColor {
  explicit nsStyleColor(const mozilla::dom::Document&);
  nsStyleColor(const nsStyleColor& aOther);
  ~nsStyleColor() { MOZ_COUNT_DTOR(nsStyleColor); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleColor*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleColor& aNewData) const;

  // Don't add ANY members to this struct!  We can achieve caching in the rule
  // tree (rather than the style tree) by letting color stay by itself! -dwh
  nscolor mColor;
};

struct nsStyleImageLayers {
  // Indices into kBackgroundLayerTable and kMaskLayerTable
  enum {
    shorthand = 0,
    color,
    image,
    repeat,
    positionX,
    positionY,
    clip,
    origin,
    size,
    attachment,
    maskMode,
    composite
  };

  enum class LayerType : uint8_t { Background = 0, Mask };

  explicit nsStyleImageLayers(LayerType aType);
  nsStyleImageLayers(const nsStyleImageLayers& aSource);
  ~nsStyleImageLayers() { MOZ_COUNT_DTOR(nsStyleImageLayers); }

  static bool IsInitialPositionForLayerType(mozilla::Position aPosition,
                                            LayerType aType);

  struct Repeat {
    mozilla::StyleImageLayerRepeat mXRepeat, mYRepeat;

    // Initialize nothing
    Repeat() {}

    bool IsInitialValue() const {
      return mXRepeat == mozilla::StyleImageLayerRepeat::Repeat &&
             mYRepeat == mozilla::StyleImageLayerRepeat::Repeat;
    }

    bool DependsOnPositioningAreaSize() const {
      return mXRepeat == mozilla::StyleImageLayerRepeat::Space ||
             mYRepeat == mozilla::StyleImageLayerRepeat::Space;
    }

    // Initialize to initial values
    void SetInitialValues() {
      mXRepeat = mozilla::StyleImageLayerRepeat::Repeat;
      mYRepeat = mozilla::StyleImageLayerRepeat::Repeat;
    }

    bool operator==(const Repeat& aOther) const {
      return mXRepeat == aOther.mXRepeat && mYRepeat == aOther.mYRepeat;
    }
    bool operator!=(const Repeat& aOther) const { return !(*this == aOther); }
  };

  struct Layer {
    typedef mozilla::StyleGeometryBox StyleGeometryBox;
    typedef mozilla::StyleImageLayerAttachment StyleImageLayerAttachment;
    typedef mozilla::StyleBackgroundSize StyleBackgroundSize;

    nsStyleImage mImage;
    mozilla::Position mPosition;
    StyleBackgroundSize mSize;
    StyleGeometryBox mClip;
    MOZ_INIT_OUTSIDE_CTOR StyleGeometryBox mOrigin;

    // This property is used for background layer only.
    // For a mask layer, it should always be the initial value, which is
    // StyleImageLayerAttachment::Scroll.
    StyleImageLayerAttachment mAttachment;

    // This property is used for background layer only.
    // For a mask layer, it should always be the initial value, which is
    // NS_STYLE_BLEND_NORMAL.
    uint8_t mBlendMode;  // NS_STYLE_BLEND_*

    // This property is used for mask layer only.
    // For a background layer, it should always be the initial value, which is
    // NS_STYLE_COMPOSITE_MODE_ADD.
    uint8_t mComposite;  // NS_STYLE_MASK_COMPOSITE_*

    // mask-only property. This property is used for mask layer only. For a
    // background layer, it should always be the initial value, which is
    // StyleMaskMode::MatchSource.
    mozilla::StyleMaskMode mMaskMode;

    Repeat mRepeat;

    // This constructor does not initialize mRepeat or mOrigin and Initialize()
    // must be called to do that.
    Layer();
    ~Layer();

    // Initialize mRepeat and mOrigin by specified layer type
    void Initialize(LayerType aType);

    void ResolveImage(mozilla::dom::Document& aDocument,
                      const Layer* aOldLayer) {
      mImage.ResolveImage(aDocument, aOldLayer ? &aOldLayer->mImage : nullptr);
    }

    // True if the rendering of this layer might change when the size
    // of the background positioning area changes.  This is true for any
    // non-solid-color background whose position or size depends on
    // the size of the positioning area.  It's also true for SVG images
    // whose root <svg> node has a viewBox.
    bool RenderingMightDependOnPositioningAreaSizeChange() const;

    // Compute the change hint required by changes in just this layer.
    nsChangeHint CalcDifference(const Layer& aNewLayer) const;

    // An equality operator that compares the images using URL-equality
    // rather than pointer-equality.
    bool operator==(const Layer& aOther) const;
    bool operator!=(const Layer& aOther) const { return !(*this == aOther); }
  };

  // The (positive) number of computed values of each property, since
  // the lengths of the lists are independent.
  uint32_t mAttachmentCount;
  uint32_t mClipCount;
  uint32_t mOriginCount;
  uint32_t mRepeatCount;
  uint32_t mPositionXCount;
  uint32_t mPositionYCount;
  uint32_t mImageCount;
  uint32_t mSizeCount;
  uint32_t mMaskModeCount;
  uint32_t mBlendModeCount;
  uint32_t mCompositeCount;

  // Layers are stored in an array, matching the top-to-bottom order in
  // which they are specified in CSS.  The number of layers to be used
  // should come from the background-image property.  We create
  // additional |Layer| objects for *any* property, not just
  // background-image.  This means that the bottommost layer that
  // callers in layout care about (which is also the one whose
  // background-clip applies to the background-color) may not be last
  // layer.  In layers below the bottom layer, properties will be
  // uninitialized unless their count, above, indicates that they are
  // present.
  nsStyleAutoArray<Layer> mLayers;

  const Layer& BottomLayer() const { return mLayers[mImageCount - 1]; }

  void ResolveImages(mozilla::dom::Document& aDocument,
                     const nsStyleImageLayers* aOldLayers) {
    for (uint32_t i = 0; i < mImageCount; ++i) {
      const Layer* oldLayer = (aOldLayers && aOldLayers->mLayers.Length() > i)
                                  ? &aOldLayers->mLayers[i]
                                  : nullptr;
      mLayers[i].ResolveImage(aDocument, oldLayer);
    }
  }

  // Fill unspecified layers by cycling through their values
  // till they all are of length aMaxItemCount
  void FillAllLayers(uint32_t aMaxItemCount);

  nsChangeHint CalcDifference(const nsStyleImageLayers& aNewLayers,
                              nsStyleImageLayers::LayerType aType) const;

  nsStyleImageLayers& operator=(const nsStyleImageLayers& aOther);
  nsStyleImageLayers& operator=(nsStyleImageLayers&& aOther);
  bool operator==(const nsStyleImageLayers& aOther) const;

  static const nsCSSPropertyID kBackgroundLayerTable[];
  static const nsCSSPropertyID kMaskLayerTable[];

#define NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(var_, layers_) \
  for (uint32_t var_ = (layers_).mImageCount; var_-- != 0;)
#define NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT_WITH_RANGE(var_, layers_,  \
                                                             start_, count_) \
  NS_ASSERTION(                                                              \
      (int32_t)(start_) >= 0 && (uint32_t)(start_) < (layers_).mImageCount,  \
      "Invalid layer start!");                                               \
  NS_ASSERTION((count_) > 0 && (count_) <= (start_) + 1,                     \
               "Invalid layer range!");                                      \
  for (uint32_t var_ = (start_) + 1;                                         \
       var_-- != (uint32_t)((start_) + 1 - (count_));)
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleBackground {
  explicit nsStyleBackground(const mozilla::dom::Document&);
  nsStyleBackground(const nsStyleBackground& aOther);
  ~nsStyleBackground();

  // Resolves and tracks the images in mImage.  Only called with a Servo-backed
  // style system, where those images must be resolved later than the OMT
  // nsStyleBackground constructor call.
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleBackground*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleBackground& aNewData) const;

  // Return the background color as nscolor.
  nscolor BackgroundColor(const nsIFrame* aFrame) const;
  nscolor BackgroundColor(mozilla::ComputedStyle* aStyle) const;

  // True if this background is completely transparent.
  bool IsTransparent(const nsIFrame* aFrame) const;
  bool IsTransparent(mozilla::ComputedStyle* aStyle) const;

  // We have to take slower codepaths for fixed background attachment,
  // but we don't want to do that when there's no image.
  // Not inline because it uses an nsCOMPtr<imgIRequest>
  // FIXME: Should be in nsStyleStructInlines.h.
  bool HasFixedBackground(nsIFrame* aFrame) const;

  // Checks to see if this has a non-empty image with "local" attachment.
  // This is defined in nsStyleStructInlines.h.
  inline bool HasLocalBackground() const;

  const nsStyleImageLayers::Layer& BottomLayer() const {
    return mImage.BottomLayer();
  }

  nsStyleImageLayers mImage;
  mozilla::StyleComplexColor mBackgroundColor;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleMargin {
  explicit nsStyleMargin(const mozilla::dom::Document&);
  nsStyleMargin(const nsStyleMargin& aMargin);
  ~nsStyleMargin() { MOZ_COUNT_DTOR(nsStyleMargin); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleMargin*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleMargin& aNewData) const;

  bool GetMargin(nsMargin& aMargin) const {
    bool convertsToLength = mMargin.All(
        [](const auto& aLength) { return aLength.ConvertsToLength(); });

    if (!convertsToLength) {
      return false;
    }

    NS_FOR_CSS_SIDES(side) {
      aMargin.Side(side) = mMargin.Get(side).AsLengthPercentage().ToLength();
    }
    return true;
  }

  // Return true if either the start or end side in the axis is 'auto'.
  // (defined in WritingModes.h since we need the full WritingMode type)
  inline bool HasBlockAxisAuto(mozilla::WritingMode aWM) const;
  inline bool HasInlineAxisAuto(mozilla::WritingMode aWM) const;

  mozilla::StyleRect<mozilla::LengthPercentageOrAuto> mMargin;
  mozilla::StyleRect<mozilla::StyleLength> mScrollMargin;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStylePadding {
  explicit nsStylePadding(const mozilla::dom::Document&);
  nsStylePadding(const nsStylePadding& aPadding);
  ~nsStylePadding() { MOZ_COUNT_DTOR(nsStylePadding); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStylePadding*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStylePadding& aNewData) const;

  mozilla::StyleRect<mozilla::NonNegativeLengthPercentage> mPadding;
  mozilla::StyleRect<mozilla::NonNegativeLengthPercentageOrAuto> mScrollPadding;

  inline bool IsWidthDependent() const {
    return !mPadding.All(
        [](const auto& aLength) { return aLength.ConvertsToLength(); });
  }

  bool GetPadding(nsMargin& aPadding) const {
    if (IsWidthDependent()) {
      return false;
    }

    NS_FOR_CSS_SIDES(side) {
      // Clamp negative calc() to 0.
      aPadding.Side(side) = std::max(mPadding.Get(side).ToLength(), 0);
    }
    return true;
  }
};

struct nsCSSShadowItem {
  nscoord mXOffset;
  nscoord mYOffset;
  nscoord mRadius;
  nscoord mSpread;

  mozilla::StyleComplexColor mColor;
  bool mInset;

  nsCSSShadowItem()
      : mXOffset(0),
        mYOffset(0),
        mRadius(0),
        mSpread(0),
        mColor(mozilla::StyleComplexColor::CurrentColor()),
        mInset(false) {
    MOZ_COUNT_CTOR(nsCSSShadowItem);
  }
  ~nsCSSShadowItem() { MOZ_COUNT_DTOR(nsCSSShadowItem); }

  bool operator==(const nsCSSShadowItem& aOther) const {
    return (mXOffset == aOther.mXOffset && mYOffset == aOther.mYOffset &&
            mRadius == aOther.mRadius && mSpread == aOther.mSpread &&
            mInset == aOther.mInset && mColor == aOther.mColor);
  }
  bool operator!=(const nsCSSShadowItem& aOther) const {
    return !(*this == aOther);
  }
};

class nsCSSShadowArray final {
 public:
  void* operator new(size_t aBaseSize, uint32_t aArrayLen) {
    // We can allocate both this nsCSSShadowArray and the
    // actual array in one allocation. The amount of memory to
    // allocate is equal to the class's size + the number of bytes for all
    // but the first array item (because aBaseSize includes one
    // item, see the private declarations)
    return ::operator new(aBaseSize +
                          (aArrayLen - 1) * sizeof(nsCSSShadowItem));
  }

  void operator delete(void* aPtr) { ::operator delete(aPtr); }

  explicit nsCSSShadowArray(uint32_t aArrayLen) : mLength(aArrayLen) {
    for (uint32_t i = 1; i < mLength; ++i) {
      // Make sure we call the constructors of each nsCSSShadowItem
      // (the first one is called for us because we declared it under private)
      new (&mArray[i]) nsCSSShadowItem();
    }
  }

 private:
  // Private destructor, to discourage deletion outside of Release():
  ~nsCSSShadowArray() {
    for (uint32_t i = 1; i < mLength; ++i) {
      mArray[i].~nsCSSShadowItem();
    }
  }

 public:
  uint32_t Length() const { return mLength; }
  nsCSSShadowItem* ShadowAt(uint32_t i) {
    MOZ_ASSERT(i < mLength,
               "Accessing too high an index in the text shadow array!");
    return &mArray[i];
  }
  const nsCSSShadowItem* ShadowAt(uint32_t i) const {
    MOZ_ASSERT(i < mLength,
               "Accessing too high an index in the text shadow array!");
    return &mArray[i];
  }

  bool HasShadowWithInset(bool aInset) {
    for (uint32_t i = 0; i < mLength; ++i) {
      if (mArray[i].mInset == aInset) {
        return true;
      }
    }
    return false;
  }

  bool operator==(const nsCSSShadowArray& aOther) const {
    if (mLength != aOther.Length()) {
      return false;
    }

    for (uint32_t i = 0; i < mLength; ++i) {
      if (ShadowAt(i) != aOther.ShadowAt(i)) {
        return false;
      }
    }

    return true;
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsCSSShadowArray)

 private:
  uint32_t mLength;
  nsCSSShadowItem mArray[1];  // This MUST be the last item
};

// Border widths are rounded to the nearest-below integer number of pixels,
// but values between zero and one device pixels are always rounded up to
// one device pixel.
#define NS_ROUND_BORDER_TO_PIXELS(l, tpp) \
  ((l) == 0) ? 0 : std::max((tpp), (l) / (tpp) * (tpp))

// Returns if the given border style type is visible or not
static bool IsVisibleBorderStyle(mozilla::StyleBorderStyle aStyle) {
  return (aStyle != mozilla::StyleBorderStyle::None &&
          aStyle != mozilla::StyleBorderStyle::Hidden);
}

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleBorder {
  explicit nsStyleBorder(const mozilla::dom::Document&);
  nsStyleBorder(const nsStyleBorder& aBorder);
  ~nsStyleBorder();

  // Resolves and tracks mBorderImageSource.  Only called with a Servo-backed
  // style system, where those images must be resolved later than the OMT
  // nsStyleBorder constructor call.
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleBorder*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleBorder& aNewData) const;

  // Return whether aStyle is a visible style.  Invisible styles cause
  // the relevant computed border width to be 0.
  // Note that this does *not* consider the effects of 'border-image':
  // if border-style is none, but there is a loaded border image,
  // HasVisibleStyle will be false even though there *is* a border.
  bool HasVisibleStyle(mozilla::Side aSide) const {
    return IsVisibleBorderStyle(mBorderStyle[aSide]);
  }

  // aBorderWidth is in twips
  void SetBorderWidth(mozilla::Side aSide, nscoord aBorderWidth) {
    nscoord roundedWidth =
        NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, mTwipsPerPixel);
    mBorder.Side(aSide) = roundedWidth;
    if (HasVisibleStyle(aSide)) {
      mComputedBorder.Side(aSide) = roundedWidth;
    }
  }

  // Get the computed border (plus rounding).  This does consider the
  // effects of 'border-style: none', but does not consider
  // 'border-image'.
  const nsMargin& GetComputedBorder() const { return mComputedBorder; }

  bool HasBorder() const {
    return mComputedBorder != nsMargin(0, 0, 0, 0) ||
           !mBorderImageSource.IsEmpty();
  }

  // Get the actual border width for a particular side, in appunits.  Note that
  // this is zero if and only if there is no border to be painted for this
  // side.  That is, this value takes into account the border style and the
  // value is rounded to the nearest device pixel by NS_ROUND_BORDER_TO_PIXELS.
  nscoord GetComputedBorderWidth(mozilla::Side aSide) const {
    return GetComputedBorder().Side(aSide);
  }

  mozilla::StyleBorderStyle GetBorderStyle(mozilla::Side aSide) const {
    NS_ASSERTION(aSide <= mozilla::eSideLeft, "bad side");
    return mBorderStyle[aSide];
  }

  void SetBorderStyle(mozilla::Side aSide, mozilla::StyleBorderStyle aStyle) {
    NS_ASSERTION(aSide <= mozilla::eSideLeft, "bad side");
    mBorderStyle[aSide] = aStyle;
    mComputedBorder.Side(aSide) =
        (HasVisibleStyle(aSide) ? mBorder.Side(aSide) : 0);
  }

  inline bool IsBorderImageLoaded() const {
    return mBorderImageSource.IsLoaded();
  }

  nsMargin GetImageOutset() const;

  imgIRequest* GetBorderImageRequest() const {
    if (mBorderImageSource.GetType() == eStyleImageType_Image) {
      return mBorderImageSource.GetImageData();
    }
    return nullptr;
  }

 public:
  mozilla::StyleBorderRadius mBorderRadius;  // coord, percent
  nsStyleImage mBorderImageSource;
  nsStyleSides mBorderImageWidth;  // length, factor, percent, auto
  mozilla::StyleNonNegativeLengthOrNumberRect mBorderImageOutset;
  mozilla::StyleBorderImageSlice mBorderImageSlice;  // factor, percent
  mozilla::StyleBorderImageRepeat mBorderImageRepeatH;
  mozilla::StyleBorderImageRepeat mBorderImageRepeatV;
  mozilla::StyleFloatEdge mFloatEdge;
  mozilla::StyleBoxDecorationBreak mBoxDecorationBreak;

 protected:
  mozilla::StyleBorderStyle mBorderStyle[4];  // StyleBorderStyle::*

 public:
  // the colors to use for a simple border.
  // not used for -moz-border-colors
  mozilla::StyleComplexColor mBorderTopColor;
  mozilla::StyleComplexColor mBorderRightColor;
  mozilla::StyleComplexColor mBorderBottomColor;
  mozilla::StyleComplexColor mBorderLeftColor;

  mozilla::StyleComplexColor& BorderColorFor(mozilla::Side aSide) {
    switch (aSide) {
      case mozilla::eSideTop:
        return mBorderTopColor;
      case mozilla::eSideRight:
        return mBorderRightColor;
      case mozilla::eSideBottom:
        return mBorderBottomColor;
      case mozilla::eSideLeft:
        return mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return mBorderTopColor;
  }

  const mozilla::StyleComplexColor& BorderColorFor(mozilla::Side aSide) const {
    switch (aSide) {
      case mozilla::eSideTop:
        return mBorderTopColor;
      case mozilla::eSideRight:
        return mBorderRightColor;
      case mozilla::eSideBottom:
        return mBorderBottomColor;
      case mozilla::eSideLeft:
        return mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return mBorderTopColor;
  }

  static mozilla::StyleComplexColor nsStyleBorder::*BorderColorFieldFor(
      mozilla::Side aSide) {
    switch (aSide) {
      case mozilla::eSideTop:
        return &nsStyleBorder::mBorderTopColor;
      case mozilla::eSideRight:
        return &nsStyleBorder::mBorderRightColor;
      case mozilla::eSideBottom:
        return &nsStyleBorder::mBorderBottomColor;
      case mozilla::eSideLeft:
        return &nsStyleBorder::mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return nullptr;
  }

 protected:
  // mComputedBorder holds the CSS2.1 computed border-width values.
  // In particular, these widths take into account the border-style
  // for the relevant side, and the values are rounded to the nearest
  // device pixel (which is not part of the definition of computed
  // values). The presence or absence of a border-image does not
  // affect border-width values.
  nsMargin mComputedBorder;

  // mBorder holds the nscoord values for the border widths as they
  // would be if all the border-style values were visible (not hidden
  // or none).  This member exists so that when we create structs
  // using the copy constructor during style resolution the new
  // structs will know what the specified values of the border were in
  // case they have more specific rules setting the border style.
  //
  // Note that this isn't quite the CSS specified value, since this
  // has had the enumerated border widths converted to lengths, and
  // all lengths converted to twips.  But it's not quite the computed
  // value either. The values are rounded to the nearest device pixel.
  nsMargin mBorder;

 private:
  nscoord mTwipsPerPixel;

  nsStyleBorder& operator=(const nsStyleBorder& aOther) = delete;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleOutline {
  explicit nsStyleOutline(const mozilla::dom::Document&);
  nsStyleOutline(const nsStyleOutline& aOutline);
  ~nsStyleOutline() { MOZ_COUNT_DTOR(nsStyleOutline); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleOutline*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleOutline& aNewData) const;

  mozilla::StyleBorderRadius mOutlineRadius;

  // This is the specified value of outline-width, but with length values
  // computed to absolute.  mActualOutlineWidth stores the outline-width
  // value used by layout.  (We must store mOutlineWidth for the same
  // style struct resolution reasons that we do nsStyleBorder::mBorder;
  // see that field's comment.)
  nscoord mOutlineWidth;
  nscoord mOutlineOffset;
  mozilla::StyleComplexColor mOutlineColor;
  mozilla::StyleOutlineStyle mOutlineStyle;

  nscoord GetOutlineWidth() const { return mActualOutlineWidth; }

  bool ShouldPaintOutline() const {
    if (mOutlineStyle.IsAuto()) {
      return true;
    }
    if (GetOutlineWidth() > 0) {
      MOZ_ASSERT(
          mOutlineStyle.border_style._0 != mozilla::StyleBorderStyle::None,
          "outline-style: none implies outline-width of zero");
      return true;
    }
    return false;
  }

 protected:
  // The actual value of outline-width is the computed value (an absolute
  // length, forced to zero when outline-style is none) rounded to device
  // pixels.  This is the value used by layout.
  nscoord mActualOutlineWidth;
  nscoord mTwipsPerPixel;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleList {
  explicit nsStyleList(const mozilla::dom::Document&);
  nsStyleList(const nsStyleList& aStyleList);
  ~nsStyleList();

  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleList*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleList& aNewData,
                              const nsStyleDisplay& aOldDisplay) const;

  imgRequestProxy* GetListStyleImage() const {
    return mListStyleImage ? mListStyleImage->get() : nullptr;
  }

  already_AddRefed<nsIURI> GetListStyleImageURI() const;

  uint8_t mListStylePosition;
  RefPtr<nsStyleImageRequest> mListStyleImage;

  mozilla::CounterStylePtr mCounterStyle;

 private:
  nsStyleList& operator=(const nsStyleList& aOther) = delete;

 public:
  RefPtr<RawServoQuotes> mQuotes;
  nsRect mImageRegion;  // the rect to use within an image
};

struct nsStyleGridLine {
  // http://dev.w3.org/csswg/css-grid/#typedef-grid-line
  // XXXmats we could optimize memory size here
  bool mHasSpan;
  int32_t mInteger;    // 0 means not provided
  nsString mLineName;  // Empty string means not provided.

  // These are the limits that we choose to clamp grid line numbers to.
  // http://dev.w3.org/csswg/css-grid/#overlarge-grids
  // mInteger is clamped to this range:
  static const int32_t kMinLine = -10000;
  static const int32_t kMaxLine = 10000;

  nsStyleGridLine()
      : mHasSpan(false),
        mInteger(0)
  // mLineName get its default constructor, the empty string
  {}

  nsStyleGridLine(const nsStyleGridLine& aOther) { (*this) = aOther; }

  void operator=(const nsStyleGridLine& aOther) {
    mHasSpan = aOther.mHasSpan;
    mInteger = aOther.mInteger;
    mLineName = aOther.mLineName;
  }

  bool operator!=(const nsStyleGridLine& aOther) const {
    return mHasSpan != aOther.mHasSpan || mInteger != aOther.mInteger ||
           mLineName != aOther.mLineName;
  }

  bool IsAuto() const {
    bool haveInitialValues = mInteger == 0 && mLineName.IsEmpty();
    MOZ_ASSERT(!(haveInitialValues && mHasSpan),
               "should not have 'span' when other components are "
               "at their initial values");
    return haveInitialValues;
  }
};

// Computed value of the grid-template-columns or grid-template-rows property
// (but *not* grid-template-areas.)
// http://dev.w3.org/csswg/css-grid/#track-sizing
//
// This represents either:
// * none:
//   mIsSubgrid is false, all three arrays are empty
// * <track-list>:
//   mIsSubgrid is false,
//   mMinTrackSizingFunctions and mMaxTrackSizingFunctions
//   are of identical non-zero size,
//   and mLineNameLists is one element longer than that.
//   (Delimiting N columns requires N+1 lines:
//   one before each track, plus one at the very end.)
//
//   An omitted <line-names> is still represented in mLineNameLists,
//   as an empty sub-array.
//
//   A <track-size> specified as a single <track-breadth> is represented
//   as identical min and max sizing functions.
//   A 'fit-content(size)' <track-size> is represented as eStyleUnit_None
//   in the min sizing function and 'size' in the max sizing function.
//
//   The units for nsStyleCoord are:
//   * eStyleUnit_Percent represents a <percentage>
//   * eStyleUnit_FlexFraction represents a <flex> flexible fraction
//   * eStyleUnit_Coord represents a <length>
//   * eStyleUnit_Enumerated represents min-content or max-content
// * subgrid <line-name-list>?:
//   mIsSubgrid is true,
//   mLineNameLists may or may not be empty,
//   mMinTrackSizingFunctions and mMaxTrackSizingFunctions are empty.
//
// If mRepeatAutoIndex != -1 then that index is an <auto-repeat> and
// mIsAutoFill == true means it's an 'auto-fill', otherwise 'auto-fit'.
// mRepeatAutoLineNameListBefore is the list of line names before the track
// size, mRepeatAutoLineNameListAfter the names after.  (They are empty
// when there is no <auto-repeat> track, i.e. when mRepeatAutoIndex == -1).
// When mIsSubgrid is true, mRepeatAutoLineNameListBefore contains the line
// names and mRepeatAutoLineNameListAfter is empty.
struct nsStyleGridTemplate {
  nsTArray<nsTArray<nsString>> mLineNameLists;
  nsTArray<nsStyleCoord> mMinTrackSizingFunctions;
  nsTArray<nsStyleCoord> mMaxTrackSizingFunctions;
  nsTArray<nsString> mRepeatAutoLineNameListBefore;
  nsTArray<nsString> mRepeatAutoLineNameListAfter;
  int16_t mRepeatAutoIndex;  // -1 or the track index for an auto-fill/fit track
  bool mIsAutoFill : 1;
  bool mIsSubgrid : 1;

  nsStyleGridTemplate()
      : mRepeatAutoIndex(-1), mIsAutoFill(false), mIsSubgrid(false) {}

  inline bool operator==(const nsStyleGridTemplate& aOther) const {
    return mIsSubgrid == aOther.mIsSubgrid &&
           mLineNameLists == aOther.mLineNameLists &&
           mMinTrackSizingFunctions == aOther.mMinTrackSizingFunctions &&
           mMaxTrackSizingFunctions == aOther.mMaxTrackSizingFunctions &&
           mIsAutoFill == aOther.mIsAutoFill &&
           mRepeatAutoIndex == aOther.mRepeatAutoIndex &&
           mRepeatAutoLineNameListBefore ==
               aOther.mRepeatAutoLineNameListBefore &&
           mRepeatAutoLineNameListAfter == aOther.mRepeatAutoLineNameListAfter;
  }

  bool HasRepeatAuto() const { return mRepeatAutoIndex != -1; }

  bool IsRepeatAutoIndex(uint32_t aIndex) const {
    MOZ_ASSERT(aIndex < uint32_t(2 * nsStyleGridLine::kMaxLine));
    return int32_t(aIndex) == mRepeatAutoIndex;
  }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStylePosition {
  using LengthPercentageOrAuto = mozilla::LengthPercentageOrAuto;
  using Position = mozilla::Position;
  template <typename T>
  using StyleRect = mozilla::StyleRect<T>;
  using StyleSize = mozilla::StyleSize;
  using StyleMaxSize = mozilla::StyleMaxSize;
  using StyleFlexBasis = mozilla::StyleFlexBasis;
  using WritingMode = mozilla::WritingMode;

  explicit nsStylePosition(const mozilla::dom::Document&);
  nsStylePosition(const nsStylePosition& aOther);
  ~nsStylePosition();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStylePosition*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(
      const nsStylePosition& aNewData,
      const nsStyleVisibility& aOldStyleVisibility) const;

  /**
   * Return the used value for 'align-self' given our parent ComputedStyle
   * aParent (or null for the root).
   */
  uint8_t UsedAlignSelf(mozilla::ComputedStyle* aParent) const;

  /**
   * Return the used value for 'justify-self' given our parent ComputedStyle
   * aParent (or null for the root).
   */
  uint8_t UsedJustifySelf(mozilla::ComputedStyle* aParent) const;

  Position mObjectPosition;
  StyleRect<LengthPercentageOrAuto> mOffset;
  StyleSize mWidth;
  StyleSize mMinWidth;
  StyleMaxSize mMaxWidth;
  StyleSize mHeight;
  StyleSize mMinHeight;
  StyleMaxSize mMaxHeight;
  StyleFlexBasis mFlexBasis;
  nsStyleCoord mGridAutoColumnsMin;  // coord, percent, enum, calc, flex
  nsStyleCoord mGridAutoColumnsMax;  // coord, percent, enum, calc, flex
  nsStyleCoord mGridAutoRowsMin;     // coord, percent, enum, calc, flex
  nsStyleCoord mGridAutoRowsMax;     // coord, percent, enum, calc, flex
  uint8_t mGridAutoFlow;             // NS_STYLE_GRID_AUTO_FLOW_*
  mozilla::StyleBoxSizing mBoxSizing;

  // All align/justify properties here take NS_STYLE_ALIGN_* values.
  uint16_t mAlignContent;  // fallback value in the high byte
  uint8_t mAlignItems;
  uint8_t mAlignSelf;
  uint16_t mJustifyContent;  // fallback value in the high byte
  // We cascade mSpecifiedJustifyItems, to handle the auto value, but store the
  // computed value in mJustifyItems.
  //
  // They're effectively only different in this regard: mJustifyItems is set to
  // mSpecifiedJustifyItems, except when the latter is AUTO -- in that case,
  // mJustifyItems is set to NORMAL, or to the parent ComputedStyle's
  // mJustifyItems if it has the legacy flag.
  //
  // This last part happens in ComputedStyle::ApplyStyleFixups.
  uint8_t mSpecifiedJustifyItems;
  uint8_t mJustifyItems;
  uint8_t mJustifySelf;
  mozilla::StyleFlexDirection mFlexDirection;
  uint8_t mFlexWrap;   // NS_STYLE_FLEX_WRAP_*
  uint8_t mObjectFit;  // NS_STYLE_OBJECT_FIT_*
  int32_t mOrder;
  float mFlexGrow;
  float mFlexShrink;
  mozilla::StyleZIndex mZIndex;
  mozilla::UniquePtr<nsStyleGridTemplate> mGridTemplateColumns;
  mozilla::UniquePtr<nsStyleGridTemplate> mGridTemplateRows;

  // nullptr for 'none'
  RefPtr<mozilla::css::GridTemplateAreasValue> mGridTemplateAreas;

  nsStyleGridLine mGridColumnStart;
  nsStyleGridLine mGridColumnEnd;
  nsStyleGridLine mGridRowStart;
  nsStyleGridLine mGridRowEnd;
  nsStyleCoord mColumnGap;  // normal, coord, percent, calc
  nsStyleCoord mRowGap;     // normal, coord, percent, calc

  bool OffsetHasPercent(mozilla::Side aSide) const {
    return mOffset.Get(aSide).HasPercent();
  }

  // Logical-coordinate accessors for width and height properties,
  // given a WritingMode value. The definitions of these methods are
  // found in WritingModes.h (after the WritingMode class is fully
  // declared).
  inline const StyleSize& ISize(WritingMode) const;
  inline const StyleSize& MinISize(WritingMode) const;
  inline const StyleMaxSize& MaxISize(WritingMode) const;
  inline const StyleSize& BSize(WritingMode) const;
  inline const StyleSize& MinBSize(WritingMode) const;
  inline const StyleMaxSize& MaxBSize(WritingMode) const;
  inline bool ISizeDependsOnContainer(WritingMode) const;
  inline bool MinISizeDependsOnContainer(WritingMode) const;
  inline bool MaxISizeDependsOnContainer(WritingMode) const;
  inline bool BSizeDependsOnContainer(WritingMode) const;
  inline bool MinBSizeDependsOnContainer(WritingMode) const;
  inline bool MaxBSizeDependsOnContainer(WritingMode) const;

  const nsStyleGridTemplate& GridTemplateColumns() const;
  const nsStyleGridTemplate& GridTemplateRows() const;

 private:
  template <typename SizeOrMaxSize>
  static bool ISizeCoordDependsOnContainer(const SizeOrMaxSize& aCoord) {
    if (aCoord.IsLengthPercentage()) {
      return aCoord.AsLengthPercentage().HasPercent();
    }

    if (!aCoord.IsExtremumLength()) {
      return false;
    }

    auto keyword = aCoord.AsExtremumLength();
    return keyword == mozilla::StyleExtremumLength::MozFitContent ||
           keyword == mozilla::StyleExtremumLength::MozAvailable;
  }

  template <typename SizeOrMaxSize>
  static bool BSizeCoordDependsOnContainer(const SizeOrMaxSize& aCoord) {
    return aCoord.IsLengthPercentage() &&
           aCoord.AsLengthPercentage().HasPercent();
  }
};

struct nsStyleTextOverflowSide {
  nsStyleTextOverflowSide() : mType(NS_STYLE_TEXT_OVERFLOW_CLIP) {}

  bool operator==(const nsStyleTextOverflowSide& aOther) const {
    return mType == aOther.mType && (mType != NS_STYLE_TEXT_OVERFLOW_STRING ||
                                     mString == aOther.mString);
  }
  bool operator!=(const nsStyleTextOverflowSide& aOther) const {
    return !(*this == aOther);
  }

  nsString mString;
  uint8_t mType;
};

struct nsStyleTextOverflow {
  nsStyleTextOverflow() : mLogicalDirections(true) {}
  bool operator==(const nsStyleTextOverflow& aOther) const {
    return mLeft == aOther.mLeft && mRight == aOther.mRight;
  }
  bool operator!=(const nsStyleTextOverflow& aOther) const {
    return !(*this == aOther);
  }

  // Returns the value to apply on the left side.
  const nsStyleTextOverflowSide& GetLeft(uint8_t aDirection) const {
    NS_ASSERTION(aDirection == NS_STYLE_DIRECTION_LTR ||
                     aDirection == NS_STYLE_DIRECTION_RTL,
                 "bad direction");
    return !mLogicalDirections || aDirection == NS_STYLE_DIRECTION_LTR ? mLeft
                                                                       : mRight;
  }

  // Returns the value to apply on the right side.
  const nsStyleTextOverflowSide& GetRight(uint8_t aDirection) const {
    NS_ASSERTION(aDirection == NS_STYLE_DIRECTION_LTR ||
                     aDirection == NS_STYLE_DIRECTION_RTL,
                 "bad direction");
    return !mLogicalDirections || aDirection == NS_STYLE_DIRECTION_LTR ? mRight
                                                                       : mLeft;
  }

  // Returns the first value that was specified.
  const nsStyleTextOverflowSide* GetFirstValue() const {
    return mLogicalDirections ? &mRight : &mLeft;
  }

  // Returns the second value, or null if there was only one value specified.
  const nsStyleTextOverflowSide* GetSecondValue() const {
    return mLogicalDirections ? nullptr : &mRight;
  }

  nsStyleTextOverflowSide mLeft;   // start side when mLogicalDirections is true
  nsStyleTextOverflowSide mRight;  // end side when mLogicalDirections is true
  bool mLogicalDirections;         // true when only one value was specified
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTextReset {
  explicit nsStyleTextReset(const mozilla::dom::Document&);
  nsStyleTextReset(const nsStyleTextReset& aOther);
  ~nsStyleTextReset();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleTextReset*) {}
  const static bool kHasTriggerImageLoads = false;

  // Note the difference between this and
  // ComputedStyle::HasTextDecorationLines.
  bool HasTextDecorationLines() const {
    return mTextDecorationLine != NS_STYLE_TEXT_DECORATION_LINE_NONE &&
           mTextDecorationLine != NS_STYLE_TEXT_DECORATION_LINE_OVERRIDE_ALL;
  }

  nsChangeHint CalcDifference(const nsStyleTextReset& aNewData) const;

  nsStyleTextOverflow mTextOverflow;  // enum, string

  uint8_t mTextDecorationLine;   // NS_STYLE_TEXT_DECORATION_LINE_*
  uint8_t mTextDecorationStyle;  // NS_STYLE_TEXT_DECORATION_STYLE_*
  uint8_t mUnicodeBidi;          // NS_STYLE_UNICODE_BIDI_*
  nscoord mInitialLetterSink;    // 0 means normal
  float mInitialLetterSize;      // 0.0f means normal
  mozilla::StyleComplexColor mTextDecorationColor;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleText {
  explicit nsStyleText(const mozilla::dom::Document&);
  nsStyleText(const nsStyleText& aOther);
  ~nsStyleText();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleText*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleText& aNewData) const;

  uint8_t mTextAlign;      // NS_STYLE_TEXT_ALIGN_*
  uint8_t mTextAlignLast;  // NS_STYLE_TEXT_ALIGN_*
  mozilla::StyleTextJustify mTextJustify;
  uint8_t mTextTransform;  // NS_STYLE_TEXT_TRANSFORM_*
  mozilla::StyleWhiteSpace mWhiteSpace;

 private:
  mozilla::StyleWordBreak mWordBreak = mozilla::StyleWordBreak::Normal;
  mozilla::StyleOverflowWrap mOverflowWrap = mozilla::StyleOverflowWrap::Normal;

 public:
  mozilla::StyleHyphens mHyphens;
  uint8_t mRubyAlign;           // NS_STYLE_RUBY_ALIGN_*
  uint8_t mRubyPosition;        // NS_STYLE_RUBY_POSITION_*
  uint8_t mTextSizeAdjust;      // NS_STYLE_TEXT_SIZE_ADJUST_*
  uint8_t mTextCombineUpright;  // NS_STYLE_TEXT_COMBINE_UPRIGHT_*
  uint8_t
      mControlCharacterVisibility;  // NS_STYLE_CONTROL_CHARACTER_VISIBILITY_*
  uint8_t mTextEmphasisPosition;    // NS_STYLE_TEXT_EMPHASIS_POSITION_*
  uint8_t mTextEmphasisStyle;       // NS_STYLE_TEXT_EMPHASIS_STYLE_*
  mozilla::StyleTextRendering mTextRendering;
  mozilla::StyleComplexColor mTextEmphasisColor;
  mozilla::StyleComplexColor mWebkitTextFillColor;
  mozilla::StyleComplexColor mWebkitTextStrokeColor;

  mozilla::StyleNonNegativeLengthOrNumber mMozTabSize;
  nsStyleCoord mWordSpacing;    // coord, percent, calc
  nsStyleCoord mLetterSpacing;  // coord, normal
  nsStyleCoord mLineHeight;     // coord, factor, normal
  mozilla::LengthPercentage mTextIndent;
  nscoord mWebkitTextStrokeWidth;  // coord

  RefPtr<nsCSSShadowArray> mTextShadow;  // nullptr in case of a zero-length

  nsString mTextEmphasisStyleString;

  mozilla::StyleWordBreak EffectiveWordBreak() const {
    if (mWordBreak == mozilla::StyleWordBreak::BreakWord) {
      return mozilla::StyleWordBreak::Normal;
    }
    return mWordBreak;
  }

  mozilla::StyleOverflowWrap EffectiveOverflowWrap() const {
    if (mWordBreak == mozilla::StyleWordBreak::BreakWord) {
      return mozilla::StyleOverflowWrap::Anywhere;
    }
    return mOverflowWrap;
  }

  bool WhiteSpaceIsSignificant() const {
    return mWhiteSpace == mozilla::StyleWhiteSpace::Pre ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreWrap ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreSpace;
  }

  bool NewlineIsSignificantStyle() const {
    return mWhiteSpace == mozilla::StyleWhiteSpace::Pre ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreWrap ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreLine;
  }

  bool WhiteSpaceOrNewlineIsSignificant() const {
    return mWhiteSpace == mozilla::StyleWhiteSpace::Pre ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreWrap ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreLine ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreSpace;
  }

  bool TabIsSignificant() const {
    return mWhiteSpace == mozilla::StyleWhiteSpace::Pre ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreWrap;
  }

  bool WhiteSpaceCanWrapStyle() const {
    return mWhiteSpace == mozilla::StyleWhiteSpace::Normal ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreWrap ||
           mWhiteSpace == mozilla::StyleWhiteSpace::PreLine;
  }

  bool WordCanWrapStyle() const {
    if (!WhiteSpaceCanWrapStyle()) {
      return false;
    }
    auto owrap = EffectiveOverflowWrap();
    return owrap == mozilla::StyleOverflowWrap::BreakWord ||
           owrap == mozilla::StyleOverflowWrap::Anywhere;
  }

  bool HasTextEmphasis() const { return !mTextEmphasisStyleString.IsEmpty(); }

  bool HasWebkitTextStroke() const { return mWebkitTextStrokeWidth > 0; }

  // These are defined in nsStyleStructInlines.h.
  inline bool HasTextShadow() const;
  inline nsCSSShadowArray* GetTextShadow() const;

  // The aContextFrame argument on each of these is the frame this
  // style struct is for.  If the frame is for SVG text or inside ruby,
  // the return value will be massaged to be something that makes sense
  // for those cases.
  inline bool NewlineIsSignificant(const nsTextFrame* aContextFrame) const;
  inline bool WhiteSpaceCanWrap(const nsIFrame* aContextFrame) const;
  inline bool WordCanWrap(const nsIFrame* aContextFrame) const;

  mozilla::LogicalSide TextEmphasisSide(mozilla::WritingMode aWM) const;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleVisibility {
  explicit nsStyleVisibility(const mozilla::dom::Document&);
  nsStyleVisibility(const nsStyleVisibility& aVisibility);
  ~nsStyleVisibility() { MOZ_COUNT_DTOR(nsStyleVisibility); }
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleVisibility*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleVisibility& aNewData) const;

  mozilla::StyleImageOrientation mImageOrientation;
  uint8_t mDirection;        // NS_STYLE_DIRECTION_*
  uint8_t mVisible;          // NS_STYLE_VISIBILITY_VISIBLE_*
  uint8_t mImageRendering;   // NS_STYLE_IMAGE_RENDERING_*
  uint8_t mWritingMode;      // NS_STYLE_WRITING_MODE_*
  uint8_t mTextOrientation;  // NS_STYLE_TEXT_ORIENTATION_MIXED_*
  mozilla::StyleColorAdjust mColorAdjust;

  bool IsVisible() const { return (mVisible == NS_STYLE_VISIBILITY_VISIBLE); }

  bool IsVisibleOrCollapsed() const {
    return ((mVisible == NS_STYLE_VISIBILITY_VISIBLE) ||
            (mVisible == NS_STYLE_VISIBILITY_COLLAPSE));
  }
};

namespace mozilla {

struct StyleTransition {
  StyleTransition() { /* leaves uninitialized; see also SetInitialValues */
  }
  explicit StyleTransition(const StyleTransition& aCopy);

  void SetInitialValues();

  // Delay and Duration are in milliseconds

  const nsTimingFunction& GetTimingFunction() const { return mTimingFunction; }
  float GetDelay() const { return mDelay; }
  float GetDuration() const { return mDuration; }
  nsCSSPropertyID GetProperty() const { return mProperty; }
  nsAtom* GetUnknownProperty() const { return mUnknownProperty; }

  bool operator==(const StyleTransition& aOther) const;
  bool operator!=(const StyleTransition& aOther) const {
    return !(*this == aOther);
  }

 private:
  nsTimingFunction mTimingFunction;
  float mDuration;
  float mDelay;
  nsCSSPropertyID mProperty;
  RefPtr<nsAtom> mUnknownProperty;  // used when mProperty is
                                    // eCSSProperty_UNKNOWN or
                                    // eCSSPropertyExtra_variable
};

struct StyleAnimation {
  StyleAnimation() { /* leaves uninitialized; see also SetInitialValues */
  }
  explicit StyleAnimation(const StyleAnimation& aCopy);

  void SetInitialValues();

  // Delay and Duration are in milliseconds

  const nsTimingFunction& GetTimingFunction() const { return mTimingFunction; }
  float GetDelay() const { return mDelay; }
  float GetDuration() const { return mDuration; }
  nsAtom* GetName() const { return mName; }
  dom::PlaybackDirection GetDirection() const { return mDirection; }
  dom::FillMode GetFillMode() const { return mFillMode; }
  StyleAnimationPlayState GetPlayState() const { return mPlayState; }
  float GetIterationCount() const { return mIterationCount; }

  void SetName(already_AddRefed<nsAtom> aName) { mName = aName; }
  void SetName(nsAtom* aName) { mName = aName; }

  bool operator==(const StyleAnimation& aOther) const;
  bool operator!=(const StyleAnimation& aOther) const {
    return !(*this == aOther);
  }

 private:
  nsTimingFunction mTimingFunction;
  float mDuration;
  float mDelay;
  RefPtr<nsAtom> mName;  // nsGkAtoms::_empty for 'none'
  dom::PlaybackDirection mDirection;
  dom::FillMode mFillMode;
  StyleAnimationPlayState mPlayState;
  float mIterationCount;  // mozilla::PositiveInfinity<float>() means infinite
};

class StyleBasicShape final {
 public:
  explicit StyleBasicShape(StyleBasicShapeType);

  StyleBasicShapeType GetShapeType() const { return mType; }
  nsCSSKeyword GetShapeTypeName() const;

  StyleFillRule GetFillRule() const { return mFillRule; }

  const mozilla::Position& GetPosition() const {
    MOZ_ASSERT(mType == StyleBasicShapeType::Circle ||
                   mType == StyleBasicShapeType::Ellipse,
               "expected circle or ellipse");
    return mPosition;
  }

  bool HasRadius() const {
    MOZ_ASSERT(mType == StyleBasicShapeType::Inset, "expected inset");
    NS_FOR_CSS_HALF_CORNERS(corner) {
      auto& radius = mRadius.Get(corner);
      if (radius.HasPercent() || radius.LengthInCSSPixels() != 0.0f) {
        return true;
      }
    }
    return false;
  }

  const mozilla::StyleBorderRadius& GetRadius() const {
    MOZ_ASSERT(mType == StyleBasicShapeType::Inset, "expected inset");
    return mRadius;
  }

  // mCoordinates has coordinates for polygon or radii for
  // ellipse and circle.
  const nsTArray<nsStyleCoord>& Coordinates() const { return mCoordinates; }

  bool operator==(const StyleBasicShape& aOther) const {
    return mType == aOther.mType && mFillRule == aOther.mFillRule &&
           mCoordinates == aOther.mCoordinates &&
           mPosition == aOther.mPosition && mRadius == aOther.mRadius;
  }
  bool operator!=(const StyleBasicShape& aOther) const {
    return !(*this == aOther);
  }

 private:
  StyleBasicShapeType mType;
  StyleFillRule mFillRule;

  // mCoordinates has coordinates for polygon or radii for
  // ellipse and circle.
  // (top, right, bottom, left) for inset
  nsTArray<nsStyleCoord> mCoordinates;
  // position of center for ellipse or circle
  mozilla::Position mPosition;
  // corner radii for inset (0 if not set)
  mozilla::StyleBorderRadius mRadius;
};

struct StyleSVGPath final {
  const nsTArray<StylePathCommand>& Path() const { return mPath; }

  StyleFillRule FillRule() const { return mFillRule; }

  bool operator==(const StyleSVGPath& aOther) const {
    return mPath == aOther.mPath && mFillRule == aOther.mFillRule;
  }

  bool operator!=(const StyleSVGPath& aOther) const {
    return !(*this == aOther);
  }

 private:
  nsTArray<StylePathCommand> mPath;
  StyleFillRule mFillRule = StyleFillRule::Nonzero;
};

struct StyleShapeSource final {
  StyleShapeSource();

  StyleShapeSource(const StyleShapeSource& aSource);

  ~StyleShapeSource();

  StyleShapeSource& operator=(const StyleShapeSource& aOther);

  bool operator==(const StyleShapeSource& aOther) const;

  bool operator!=(const StyleShapeSource& aOther) const {
    return !(*this == aOther);
  }

  StyleShapeSourceType GetType() const { return mType; }

  const css::URLValue& URL() const {
    MOZ_ASSERT(mType == StyleShapeSourceType::URL, "Wrong shape source type!");
    MOZ_ASSERT(mShapeImage && mShapeImage->GetURLValue());
    return *mShapeImage->GetURLValue();
  }

  void SetURL(const css::URLValue& aURLValue);

  const nsStyleImage& ShapeImage() const {
    MOZ_ASSERT(mType == StyleShapeSourceType::Image,
               "Wrong shape source type!");
    MOZ_ASSERT(mShapeImage);
    return *mShapeImage;
  }

  // Iff we have "shape-outside:<image>" with an image URI (not a gradient),
  // this method returns the corresponding imgIRequest*. Else, returns
  // null.
  imgIRequest* GetShapeImageData() const;

  void SetShapeImage(UniquePtr<nsStyleImage> aShapeImage);

  const StyleBasicShape& BasicShape() const {
    MOZ_ASSERT(mType == StyleShapeSourceType::Shape,
               "Wrong shape source type!");
    MOZ_ASSERT(mBasicShape);
    return *mBasicShape;
  }

  void SetBasicShape(UniquePtr<StyleBasicShape> aBasicShape,
                     StyleGeometryBox aReferenceBox);

  StyleGeometryBox GetReferenceBox() const {
    MOZ_ASSERT(mType == StyleShapeSourceType::Box ||
                   mType == StyleShapeSourceType::Shape,
               "Wrong shape source type!");
    return mReferenceBox;
  }

  void SetReferenceBox(StyleGeometryBox aReferenceBox);

  const StyleSVGPath& Path() const {
    MOZ_ASSERT(mType == StyleShapeSourceType::Path, "Wrong shape source type!");
    MOZ_ASSERT(mSVGPath);
    return *mSVGPath;
  }
  void SetPath(UniquePtr<StyleSVGPath> aPath);

  void TriggerImageLoads(mozilla::dom::Document&,
                         const StyleShapeSource* aOldShapeSource);

 private:
  void* operator new(size_t) = delete;

  void DoCopy(const StyleShapeSource& aOther);
  void DoDestroy();

  union {
    mozilla::UniquePtr<StyleBasicShape> mBasicShape;
    mozilla::UniquePtr<nsStyleImage> mShapeImage;
    mozilla::UniquePtr<StyleSVGPath> mSVGPath;
    // TODO: Bug 1480665, implement ray() function.
  };
  StyleShapeSourceType mType = StyleShapeSourceType::None;
  StyleGeometryBox mReferenceBox = StyleGeometryBox::NoBox;
};

struct StyleMotion final {
  bool operator==(const StyleMotion& aOther) const {
    return mOffsetPath == aOther.mOffsetPath;
  }

  bool operator!=(const StyleMotion& aOther) const {
    return !(*this == aOther);
  }

  const StyleShapeSource& OffsetPath() const { return mOffsetPath; }

  bool HasPath() const {
    // Bug 1186329: We have to check other acceptable types after supporting
    // different values of offset-path. e.g. basic-shapes, ray.
    return mOffsetPath.GetType() == StyleShapeSourceType::Path;
  }

 private:
  StyleShapeSource mOffsetPath;
};

}  // namespace mozilla

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleDisplay {
  typedef mozilla::StyleGeometryBox StyleGeometryBox;

  explicit nsStyleDisplay(const mozilla::dom::Document&);
  nsStyleDisplay(const nsStyleDisplay& aOther);
  ~nsStyleDisplay();

  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleDisplay*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleDisplay& aNewData) const;

  bool TransformChanged(const nsStyleDisplay& aNewData) const;

  // We guarantee that if mBinding is non-null, so are mBinding->GetURI() and
  // mBinding->mOriginPrincipal.
  RefPtr<mozilla::css::URLValue> mBinding;
  mozilla::StyleDisplay mDisplay;
  mozilla::StyleDisplay mOriginalDisplay;  // saved mDisplay for
                                           //         position:absolute/fixed
                                           //         and float:left/right;
                                           //         otherwise equal to
                                           //         mDisplay
  uint8_t mContain;                        // NS_STYLE_CONTAIN_*
  mozilla::StyleAppearance mAppearance;
  uint8_t mPosition;  // NS_STYLE_POSITION_*

  mozilla::StyleFloat mFloat;
  // Save mFloat for position:absolute/fixed; otherwise equal to mFloat.
  mozilla::StyleFloat mOriginalFloat;

  mozilla::StyleClear mBreakType;
  mozilla::StyleBreakWithin mBreakInside;
  mozilla::StyleBreakBetween mBreakBefore;
  mozilla::StyleBreakBetween mBreakAfter;
  mozilla::StyleOverflow mOverflowX;
  mozilla::StyleOverflow mOverflowY;
  mozilla::StyleOverflowClipBox mOverflowClipBoxBlock;
  mozilla::StyleOverflowClipBox mOverflowClipBoxInline;
  mozilla::StyleResize mResize;
  mozilla::StyleOrient mOrient;
  uint8_t mIsolation;           // NS_STYLE_ISOLATION_*
  uint8_t mTopLayer;            // NS_STYLE_TOP_LAYER_*
  uint8_t mWillChangeBitField;  // NS_STYLE_WILL_CHANGE_*
                                // Stores a bitfield representation of the
                                // properties that are frequently queried. This
                                // should match mWillChange. Also tracks if any
                                // of the properties in the will-change list
                                // require a stacking context.
  nsTArray<RefPtr<nsAtom>> mWillChange;

  uint8_t mTouchAction;     // NS_STYLE_TOUCH_ACTION_*
  uint8_t mScrollBehavior;  // NS_STYLE_SCROLL_BEHAVIOR_*
  mozilla::StyleOverscrollBehavior mOverscrollBehaviorX;
  mozilla::StyleOverscrollBehavior mOverscrollBehaviorY;
  mozilla::StyleOverflowAnchor mOverflowAnchor;
  mozilla::StyleScrollSnapAlign mScrollSnapAlign;
  mozilla::StyleScrollSnapType mScrollSnapTypeX;
  mozilla::StyleScrollSnapType mScrollSnapTypeY;
  nsStyleCoord mScrollSnapPointsX;
  nsStyleCoord mScrollSnapPointsY;
  mozilla::Position mScrollSnapDestination;
  nsTArray<mozilla::Position> mScrollSnapCoordinate;

  // mSpecifiedTransform is the list of transform functions as
  // specified, or null to indicate there is no transform.  (inherit or
  // initial are replaced by an actual list of transform functions, or
  // null, as appropriate.)
  uint8_t mBackfaceVisibility;
  uint8_t mTransformStyle;
  StyleGeometryBox mTransformBox;
  RefPtr<nsCSSValueSharedList> mSpecifiedTransform;
  RefPtr<nsCSSValueSharedList> mSpecifiedRotate;
  RefPtr<nsCSSValueSharedList> mSpecifiedTranslate;
  RefPtr<nsCSSValueSharedList> mSpecifiedScale;
  // Used to store the final combination of mSpecifiedRotate,
  // mSpecifiedTranslate, and mSpecifiedScale.
  RefPtr<nsCSSValueSharedList> mIndividualTransform;
  mozilla::UniquePtr<mozilla::StyleMotion> mMotion;

  mozilla::StyleTransformOrigin mTransformOrigin;
  mozilla::StylePerspective mChildPerspective;
  mozilla::Position mPerspectiveOrigin;

  nsStyleCoord mVerticalAlign;  // coord, percent, calc, enum
                                // (NS_STYLE_VERTICAL_ALIGN_*)

  nsStyleAutoArray<mozilla::StyleTransition> mTransitions;

  // The number of elements in mTransitions that are not from repeating
  // a list due to another property being longer.
  uint32_t mTransitionTimingFunctionCount;
  uint32_t mTransitionDurationCount;
  uint32_t mTransitionDelayCount;
  uint32_t mTransitionPropertyCount;

  nsCSSPropertyID GetTransitionProperty(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionPropertyCount].GetProperty();
  }
  float GetTransitionDelay(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionDelayCount].GetDelay();
  }
  float GetTransitionDuration(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionDurationCount].GetDuration();
  }
  const nsTimingFunction& GetTransitionTimingFunction(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionTimingFunctionCount]
        .GetTimingFunction();
  }
  float GetTransitionCombinedDuration(uint32_t aIndex) const {
    // https://drafts.csswg.org/css-transitions/#transition-combined-duration
    return std::max(
               mTransitions[aIndex % mTransitionDurationCount].GetDuration(),
               0.0f) +
           mTransitions[aIndex % mTransitionDelayCount].GetDelay();
  }

  nsStyleAutoArray<mozilla::StyleAnimation> mAnimations;

  // The number of elements in mAnimations that are not from repeating
  // a list due to another property being longer.
  uint32_t mAnimationTimingFunctionCount;
  uint32_t mAnimationDurationCount;
  uint32_t mAnimationDelayCount;
  uint32_t mAnimationNameCount;
  uint32_t mAnimationDirectionCount;
  uint32_t mAnimationFillModeCount;
  uint32_t mAnimationPlayStateCount;
  uint32_t mAnimationIterationCountCount;

  nsAtom* GetAnimationName(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationNameCount].GetName();
  }
  float GetAnimationDelay(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDelayCount].GetDelay();
  }
  float GetAnimationDuration(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDurationCount].GetDuration();
  }
  mozilla::dom::PlaybackDirection GetAnimationDirection(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDirectionCount].GetDirection();
  }
  mozilla::dom::FillMode GetAnimationFillMode(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationFillModeCount].GetFillMode();
  }
  mozilla::StyleAnimationPlayState GetAnimationPlayState(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationPlayStateCount].GetPlayState();
  }
  float GetAnimationIterationCount(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationIterationCountCount]
        .GetIterationCount();
  }
  const nsTimingFunction& GetAnimationTimingFunction(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationTimingFunctionCount]
        .GetTimingFunction();
  }

  // The threshold used for extracting a shape from shape-outside: <image>.
  float mShapeImageThreshold = 0.0f;

  // The margin around a shape-outside: <image>.
  mozilla::NonNegativeLengthPercentage mShapeMargin;

  mozilla::StyleShapeSource mShapeOutside;

  bool HasAppearance() const {
    return mAppearance != mozilla::StyleAppearance::None;
  }

  bool IsBlockInsideStyle() const {
    return mozilla::StyleDisplay::Block == mDisplay ||
           mozilla::StyleDisplay::ListItem == mDisplay ||
           mozilla::StyleDisplay::InlineBlock == mDisplay ||
           mozilla::StyleDisplay::TableCaption == mDisplay ||
           mozilla::StyleDisplay::FlowRoot == mDisplay;
    // Should TABLE_CELL be included here?  They have
    // block frames nested inside of them.
    // (But please audit all callers before changing.)
  }

  bool IsBlockOutsideStyle() const {
    return mozilla::StyleDisplay::Block == mDisplay ||
           mozilla::StyleDisplay::Flex == mDisplay ||
           mozilla::StyleDisplay::WebkitBox == mDisplay ||
           mozilla::StyleDisplay::Grid == mDisplay ||
           mozilla::StyleDisplay::ListItem == mDisplay ||
           mozilla::StyleDisplay::Table == mDisplay ||
           mozilla::StyleDisplay::FlowRoot == mDisplay;
  }

  static bool IsDisplayTypeInlineOutside(mozilla::StyleDisplay aDisplay) {
    return mozilla::StyleDisplay::Inline == aDisplay ||
           mozilla::StyleDisplay::InlineBlock == aDisplay ||
           mozilla::StyleDisplay::InlineTable == aDisplay ||
           mozilla::StyleDisplay::MozInlineBox == aDisplay ||
           mozilla::StyleDisplay::InlineFlex == aDisplay ||
           mozilla::StyleDisplay::WebkitInlineBox == aDisplay ||
           mozilla::StyleDisplay::InlineGrid == aDisplay ||
           mozilla::StyleDisplay::MozInlineGrid == aDisplay ||
           mozilla::StyleDisplay::MozInlineStack == aDisplay ||
           mozilla::StyleDisplay::Ruby == aDisplay ||
           mozilla::StyleDisplay::RubyBase == aDisplay ||
           mozilla::StyleDisplay::RubyBaseContainer == aDisplay ||
           mozilla::StyleDisplay::RubyText == aDisplay ||
           mozilla::StyleDisplay::RubyTextContainer == aDisplay ||
           mozilla::StyleDisplay::Contents == aDisplay;
  }

  bool IsInlineOutsideStyle() const {
    return IsDisplayTypeInlineOutside(mDisplay);
  }

  bool IsOriginalDisplayInlineOutsideStyle() const {
    return IsDisplayTypeInlineOutside(mOriginalDisplay);
  }

  bool IsInnerTableStyle() const {
    return mozilla::StyleDisplay::TableCell == mDisplay ||
           IsInternalTableStyleExceptCell();
  }

  bool IsInternalTableStyleExceptCell() const {
    return mozilla::StyleDisplay::TableRow == mDisplay ||
           mozilla::StyleDisplay::TableRowGroup == mDisplay ||
           mozilla::StyleDisplay::TableHeaderGroup == mDisplay ||
           mozilla::StyleDisplay::TableFooterGroup == mDisplay ||
           mozilla::StyleDisplay::TableColumn == mDisplay ||
           mozilla::StyleDisplay::TableColumnGroup == mDisplay;
  }

  bool IsFloatingStyle() const { return mozilla::StyleFloat::None != mFloat; }

  bool IsAbsolutelyPositionedStyle() const {
    return NS_STYLE_POSITION_ABSOLUTE == mPosition ||
           NS_STYLE_POSITION_FIXED == mPosition;
  }

  bool IsRelativelyPositionedStyle() const {
    return NS_STYLE_POSITION_RELATIVE == mPosition ||
           NS_STYLE_POSITION_STICKY == mPosition;
  }
  bool IsStickyPositionedStyle() const {
    return NS_STYLE_POSITION_STICKY == mPosition;
  }
  bool IsPositionForcingStackingContext() const {
    return NS_STYLE_POSITION_STICKY == mPosition ||
           NS_STYLE_POSITION_FIXED == mPosition;
  }

  static bool IsRubyDisplayType(mozilla::StyleDisplay aDisplay) {
    return mozilla::StyleDisplay::Ruby == aDisplay ||
           IsInternalRubyDisplayType(aDisplay);
  }

  static bool IsInternalRubyDisplayType(mozilla::StyleDisplay aDisplay) {
    return mozilla::StyleDisplay::RubyBase == aDisplay ||
           mozilla::StyleDisplay::RubyBaseContainer == aDisplay ||
           mozilla::StyleDisplay::RubyText == aDisplay ||
           mozilla::StyleDisplay::RubyTextContainer == aDisplay;
  }

  bool IsRubyDisplayType() const { return IsRubyDisplayType(mDisplay); }

  bool IsInternalRubyDisplayType() const {
    return IsInternalRubyDisplayType(mDisplay);
  }

  bool IsOutOfFlowStyle() const {
    return (IsAbsolutelyPositionedStyle() || IsFloatingStyle());
  }

  bool IsScrollableOverflow() const {
    // mOverflowX and mOverflowY always match when one of them is
    // Visible or MozHiddenUnscrollable.
    return mOverflowX != mozilla::StyleOverflow::Visible &&
           mOverflowX != mozilla::StyleOverflow::MozHiddenUnscrollable;
  }

  bool IsContainPaint() const {
    return (NS_STYLE_CONTAIN_PAINT & mContain) &&
           !IsInternalRubyDisplayType() && !IsInternalTableStyleExceptCell();
  }

  bool IsContainLayout() const {
    // Note: The spec for layout containment says it should
    // have no effect on non-atomic, inline-level boxes. We
    // don't check for these here because we don't know
    // what type of element is involved. Callers are
    // responsible for checking if the box in question is
    // non-atomic and inline-level, and creating an
    // exemption as necessary.
    return (NS_STYLE_CONTAIN_LAYOUT & mContain) &&
           !IsInternalRubyDisplayType() && !IsInternalTableStyleExceptCell();
  }

  bool IsContainSize() const {
    // Note: The spec for size containment says it should
    // have no effect on non-atomic, inline-level boxes. We
    // don't check for these here because we don't know
    // what type of element is involved. Callers are
    // responsible for checking if the box in question is
    // non-atomic and inline-level, and creating an
    // exemption as necessary.
    return (NS_STYLE_CONTAIN_SIZE & mContain) && !IsInternalRubyDisplayType() &&
           (mozilla::StyleDisplay::Table != mDisplay) && !IsInnerTableStyle();
  }

  /* Returns whether the element has the -moz-transform property
   * or a related property. */
  bool HasTransformStyle() const {
    return mSpecifiedTransform || mSpecifiedRotate || mSpecifiedTranslate ||
           mSpecifiedScale ||
           mTransformStyle == NS_STYLE_TRANSFORM_STYLE_PRESERVE_3D ||
           (mWillChangeBitField & NS_STYLE_WILL_CHANGE_TRANSFORM) ||
           (mMotion && mMotion->HasPath());
  }

  bool HasIndividualTransform() const {
    return mSpecifiedRotate || mSpecifiedTranslate || mSpecifiedScale;
  }

  bool HasPerspectiveStyle() const { return !mChildPerspective.IsNone(); }

  bool BackfaceIsHidden() const {
    return mBackfaceVisibility == NS_STYLE_BACKFACE_VISIBILITY_HIDDEN;
  }

  // FIXME(emilio): This should be more fine-grained on each caller to
  // BreakBefore() / BreakAfter().
  static bool ShouldBreak(mozilla::StyleBreakBetween aBreak) {
    switch (aBreak) {
      case mozilla::StyleBreakBetween::Left:
      case mozilla::StyleBreakBetween::Right:
      case mozilla::StyleBreakBetween::Page:
      case mozilla::StyleBreakBetween::Always:
        return true;
      case mozilla::StyleBreakBetween::Auto:
      case mozilla::StyleBreakBetween::Avoid:
        return false;
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown break kind");
        return false;
    }
  }

  bool BreakBefore() const { return ShouldBreak(mBreakBefore); }

  bool BreakAfter() const { return ShouldBreak(mBreakAfter); }

  // These are defined in nsStyleStructInlines.h.

  // The aContextFrame argument on each of these is the frame this
  // style struct is for.  If the frame is for SVG text, the return
  // value will be massaged to be something that makes sense for
  // SVG text.
  inline bool IsBlockInside(const nsIFrame* aContextFrame) const;
  inline bool IsBlockOutside(const nsIFrame* aContextFrame) const;
  inline bool IsInlineOutside(const nsIFrame* aContextFrame) const;
  inline bool IsOriginalDisplayInlineOutside(
      const nsIFrame* aContextFrame) const;
  inline mozilla::StyleDisplay GetDisplay(const nsIFrame* aContextFrame) const;
  inline bool IsFloating(const nsIFrame* aContextFrame) const;
  inline bool IsRelativelyPositioned(const nsIFrame* aContextFrame) const;
  inline bool IsStickyPositioned(const nsIFrame* aContextFrame) const;
  inline bool IsAbsolutelyPositioned(const nsIFrame* aContextFrame) const;

  // These methods are defined in nsStyleStructInlines.h.

  /**
   * Returns true when the element has the transform property
   * or a related property, and supports CSS transforms.
   * aContextFrame is the frame for which this is the nsStyleDisplay.
   */
  inline bool HasTransform(const nsIFrame* aContextFrame) const;

  /**
   * Returns true when the element has the perspective property,
   * and supports CSS transforms. aContextFrame is the frame for
   * which this is the nsStyleDisplay.
   */
  inline bool HasPerspective(const nsIFrame* aContextFrame) const;

  /**
   * Returns whether the element is a containing block for its
   * absolutely positioned descendants.
   * aContextFrame is the frame for which this is the nsStyleDisplay.
   */
  inline bool IsAbsPosContainingBlock(const nsIFrame* aContextFrame) const;

  /**
   * Tests for only the sub-parts of IsAbsPosContainingBlock that apply
   * to nearly all frames, except those that are SVG text frames.
   *
   * This should be used only when the caller has the style but not the
   * frame (i.e., when calculating style changes).
   *
   * NOTE: This (unlike IsAbsPosContainingBlock) does not include
   * IsFixPosContainingBlockForNonSVGTextFrames.
   */
  inline bool IsAbsPosContainingBlockForNonSVGTextFrames() const;

  /**
   * Returns true when the element is a containing block for its fixed-pos
   * descendants.
   * aContextFrame is the frame for which this is the nsStyleDisplay.
   */
  inline bool IsFixedPosContainingBlock(const nsIFrame* aContextFrame) const;

  /**
   * Tests for only the sub-parts of IsFixedPosContainingBlock that apply
   * to:
   *  - nearly all frames, except those that are SVG text frames.
   *  - frames that support CSS contain:layout and contain:paint and are not
   *    SVG text frames.
   *  - frames that support CSS transforms and are not SVG text frames.
   *
   * This should be used only when the caller has the style but not the
   * frame (i.e., when calculating style changes).
   */
  inline bool IsFixedPosContainingBlockForNonSVGTextFrames(
      const mozilla::ComputedStyle&) const;
  inline bool
  IsFixedPosContainingBlockForContainLayoutAndPaintSupportingFrames() const;
  inline bool IsFixedPosContainingBlockForTransformSupportingFrames() const;

  /**
   * Returns the final combined individual transform.
   **/
  already_AddRefed<nsCSSValueSharedList> GetCombinedTransform() const {
    return mIndividualTransform ? do_AddRef(mIndividualTransform) : nullptr;
  }

  void GenerateCombinedIndividualTransform();
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTable {
  explicit nsStyleTable(const mozilla::dom::Document&);
  nsStyleTable(const nsStyleTable& aOther);
  ~nsStyleTable();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleTable*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleTable& aNewData) const;

  uint8_t mLayoutStrategy;  // NS_STYLE_TABLE_LAYOUT_*
  int32_t mSpan;  // -x-span; the number of columns spanned by a colgroup or col
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTableBorder {
  explicit nsStyleTableBorder(const mozilla::dom::Document&);
  nsStyleTableBorder(const nsStyleTableBorder& aOther);
  ~nsStyleTableBorder();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleTableBorder*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleTableBorder& aNewData) const;

  nscoord mBorderSpacingCol;
  nscoord mBorderSpacingRow;
  uint8_t mBorderCollapse;
  uint8_t mCaptionSide;
  uint8_t mEmptyCells;
};

struct nsStyleContentAttr {
  RefPtr<nsAtom> mName;          // Non-null.
  RefPtr<nsAtom> mNamespaceURL;  // May be null.

  bool operator==(const nsStyleContentAttr& aOther) const {
    return mName == aOther.mName && mNamespaceURL == aOther.mNamespaceURL;
  }
};

class nsStyleContentData {
  using StyleContentType = mozilla::StyleContentType;

 public:
  nsStyleContentData() : mType(StyleContentType::Uninitialized) {
    MOZ_COUNT_CTOR(nsStyleContentData);
    mContent.mString = nullptr;
  }
  nsStyleContentData(const nsStyleContentData&);

  ~nsStyleContentData();
  nsStyleContentData& operator=(const nsStyleContentData& aOther);
  bool operator==(const nsStyleContentData& aOther) const;

  bool operator!=(const nsStyleContentData& aOther) const {
    return !(*this == aOther);
  }

  StyleContentType GetType() const { return mType; }

  char16_t* GetString() const {
    MOZ_ASSERT(mType == StyleContentType::String);
    return mContent.mString;
  }

  const nsStyleContentAttr* GetAttr() const {
    MOZ_ASSERT(mType == StyleContentType::Attr);
    MOZ_ASSERT(mContent.mAttr);
    return mContent.mAttr;
  }

  struct CounterFunction {
    nsString mIdent;
    // This is only used when it is a counters() function.
    nsString mSeparator;
    mozilla::CounterStylePtr mCounterStyle;

    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CounterFunction)

    bool operator==(const CounterFunction& aOther) const;
    bool operator!=(const CounterFunction& aOther) const {
      return !(*this == aOther);
    }

   private:
    ~CounterFunction() {}
  };

  CounterFunction* GetCounters() const {
    MOZ_ASSERT(mType == StyleContentType::Counter ||
               mType == StyleContentType::Counters);
    return mContent.mCounters;
  }

  nsStyleImageRequest* ImageRequest() const {
    MOZ_ASSERT(mType == StyleContentType::Image);
    MOZ_ASSERT(mContent.mImage);
    return mContent.mImage;
  }

  imgRequestProxy* GetImage() const { return ImageRequest()->get(); }

  void SetCounters(StyleContentType aType,
                   already_AddRefed<CounterFunction> aCounterFunction) {
    MOZ_ASSERT(aType == StyleContentType::Counter ||
               aType == StyleContentType::Counters);
    MOZ_ASSERT(mType == StyleContentType::Uninitialized,
               "should only initialize nsStyleContentData once");
    mType = aType;
    mContent.mCounters = aCounterFunction.take();
    MOZ_ASSERT(mContent.mCounters);
  }

  void SetImageRequest(already_AddRefed<nsStyleImageRequest> aRequest) {
    MOZ_ASSERT(mType == StyleContentType::Uninitialized,
               "should only initialize nsStyleContentData once");
    mType = StyleContentType::Image;
    mContent.mImage = aRequest.take();
    MOZ_ASSERT(mContent.mImage);
  }

  void Resolve(mozilla::dom::Document&, const nsStyleContentData*);

 private:
  StyleContentType mType;
  union {
    char16_t* mString;
    nsStyleContentAttr* mAttr;
    nsStyleImageRequest* mImage;
    CounterFunction* mCounters;
  } mContent;
};

struct nsStyleCounterData {
  nsString mCounter;
  int32_t mValue;

  bool operator==(const nsStyleCounterData& aOther) const {
    return mValue == aOther.mValue && mCounter == aOther.mCounter;
  }

  bool operator!=(const nsStyleCounterData& aOther) const {
    return !(*this == aOther);
  }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleContent {
  explicit nsStyleContent(const mozilla::dom::Document&);
  nsStyleContent(const nsStyleContent& aContent);
  ~nsStyleContent();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleContent*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleContent& aNewData) const;

  uint32_t ContentCount() const { return mContents.Length(); }

  const nsStyleContentData& ContentAt(uint32_t aIndex) const {
    return mContents[aIndex];
  }

  nsStyleContentData& ContentAt(uint32_t aIndex) { return mContents[aIndex]; }

  void AllocateContents(uint32_t aCount) {
    // We need to run the destructors of the elements of mContents, so we
    // delete and reallocate even if aCount == mContentCount.  (If
    // nsStyleContentData had its members private and managed their
    // ownership on setting, we wouldn't need this, but that seems
    // unnecessary at this point.)
    mContents.Clear();
    mContents.SetLength(aCount);
  }

  uint32_t CounterIncrementCount() const { return mIncrements.Length(); }
  const nsStyleCounterData& CounterIncrementAt(uint32_t aIndex) const {
    return mIncrements[aIndex];
  }

  void AllocateCounterIncrements(uint32_t aCount) {
    mIncrements.Clear();
    mIncrements.SetLength(aCount);
  }

  void SetCounterIncrementAt(uint32_t aIndex, const nsString& aCounter,
                             int32_t aIncrement) {
    mIncrements[aIndex].mCounter = aCounter;
    mIncrements[aIndex].mValue = aIncrement;
  }

  uint32_t CounterResetCount() const { return mResets.Length(); }
  const nsStyleCounterData& CounterResetAt(uint32_t aIndex) const {
    return mResets[aIndex];
  }

  void AllocateCounterResets(uint32_t aCount) {
    mResets.Clear();
    mResets.SetLength(aCount);
  }

  void SetCounterResetAt(uint32_t aIndex, const nsString& aCounter,
                         int32_t aValue) {
    mResets[aIndex].mCounter = aCounter;
    mResets[aIndex].mValue = aValue;
  }

 protected:
  nsTArray<nsStyleContentData> mContents;
  nsTArray<nsStyleCounterData> mIncrements;
  nsTArray<nsStyleCounterData> mResets;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleUIReset {
  explicit nsStyleUIReset(const mozilla::dom::Document&);
  nsStyleUIReset(const nsStyleUIReset& aOther);
  ~nsStyleUIReset();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleUIReset*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleUIReset& aNewData) const;

  mozilla::StyleUserSelect mUserSelect;  // [reset](selection-style)
  mozilla::StyleScrollbarWidth mScrollbarWidth;
  uint8_t mForceBrokenImageIcon;  // (0 if not forcing, otherwise forcing)
  uint8_t mIMEMode;
  mozilla::StyleWindowDragging mWindowDragging;
  uint8_t mWindowShadow;
  float mWindowOpacity;
  RefPtr<nsCSSValueSharedList> mSpecifiedWindowTransform;
  mozilla::StyleTransformOrigin mWindowTransformOrigin;
};

struct nsCursorImage {
  bool mHaveHotspot;
  float mHotspotX, mHotspotY;
  RefPtr<nsStyleImageRequest> mImage;

  nsCursorImage();
  nsCursorImage(const nsCursorImage& aOther);

  nsCursorImage& operator=(const nsCursorImage& aOther);

  bool operator==(const nsCursorImage& aOther) const;
  bool operator!=(const nsCursorImage& aOther) const {
    return !(*this == aOther);
  }

  imgRequestProxy* GetImage() const { return mImage->get(); }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleUI {
  explicit nsStyleUI(const mozilla::dom::Document&);
  nsStyleUI(const nsStyleUI& aOther);
  ~nsStyleUI();

  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleUI*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleUI& aNewData) const;

  mozilla::StyleUserInput mUserInput;
  mozilla::StyleUserModify mUserModify;  // (modify-content)
  mozilla::StyleUserFocus mUserFocus;    // (auto-select)
  uint8_t mPointerEvents;                // NS_STYLE_POINTER_EVENTS_*

  mozilla::StyleCursorKind mCursor;
  nsTArray<nsCursorImage> mCursorImages;  // images and coords
  mozilla::StyleComplexColor mCaretColor;

  mozilla::StyleComplexColor mScrollbarFaceColor;
  mozilla::StyleComplexColor mScrollbarTrackColor;

  inline uint8_t GetEffectivePointerEvents(nsIFrame* aFrame) const;

  bool HasCustomScrollbars() const {
    return !mScrollbarFaceColor.IsAuto() || !mScrollbarTrackColor.IsAuto();
  }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleXUL {
  explicit nsStyleXUL(const mozilla::dom::Document&);
  nsStyleXUL(const nsStyleXUL& aSource);
  ~nsStyleXUL();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleXUL*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleXUL& aNewData) const;

  float mBoxFlex;
  uint32_t mBoxOrdinal;
  mozilla::StyleBoxAlign mBoxAlign;
  mozilla::StyleBoxDirection mBoxDirection;
  mozilla::StyleBoxOrient mBoxOrient;
  mozilla::StyleBoxPack mBoxPack;
  mozilla::StyleStackSizing mStackSizing;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleColumn {
  explicit nsStyleColumn(const mozilla::dom::Document&);
  nsStyleColumn(const nsStyleColumn& aSource);
  ~nsStyleColumn();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleColumn*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleColumn& aNewData) const;

  // This is the maximum number of columns we can process. It's used in
  // nsColumnSetFrame.
  static const uint32_t kMaxColumnCount = 1000;

  // This represents the value of column-count: auto.
  static const uint32_t kColumnCountAuto = 0;

  uint32_t mColumnCount = kColumnCountAuto;
  nsStyleCoord mColumnWidth;  // coord, auto

  mozilla::StyleComplexColor mColumnRuleColor;
  mozilla::StyleBorderStyle mColumnRuleStyle;  // StyleborderStyle::*
  mozilla::StyleColumnFill mColumnFill = mozilla::StyleColumnFill::Balance;
  mozilla::StyleColumnSpan mColumnSpan = mozilla::StyleColumnSpan::None;

  nscoord GetComputedColumnRuleWidth() const {
    return (IsVisibleBorderStyle(mColumnRuleStyle) ? mColumnRuleWidth : 0);
  }

  bool IsColumnContainerStyle() const {
    return (mColumnCount != kColumnCountAuto ||
            mColumnWidth.GetUnit() != eStyleUnit_Auto);
  }

  bool IsColumnSpanStyle() const {
    return mColumnSpan == mozilla::StyleColumnSpan::All;
  }

 protected:
  nscoord mColumnRuleWidth;  // coord
  nscoord mTwipsPerPixel;
};

enum nsStyleSVGPaintType : uint8_t {
  eStyleSVGPaintType_None = 1,
  eStyleSVGPaintType_Color,
  eStyleSVGPaintType_Server,
  eStyleSVGPaintType_ContextFill,
  eStyleSVGPaintType_ContextStroke
};

enum nsStyleSVGFallbackType : uint8_t {
  eStyleSVGFallbackType_NotSet,
  eStyleSVGFallbackType_None,
  eStyleSVGFallbackType_Color,
};

enum nsStyleSVGOpacitySource : uint8_t {
  eStyleSVGOpacitySource_Normal,
  eStyleSVGOpacitySource_ContextFillOpacity,
  eStyleSVGOpacitySource_ContextStrokeOpacity
};

class nsStyleSVGPaint {
 public:
  explicit nsStyleSVGPaint(nsStyleSVGPaintType aType = nsStyleSVGPaintType(0));
  nsStyleSVGPaint(const nsStyleSVGPaint& aSource);
  ~nsStyleSVGPaint();

  nsStyleSVGPaint& operator=(const nsStyleSVGPaint& aOther);

  nsStyleSVGPaintType Type() const { return mType; }

  void SetNone();
  void SetColor(mozilla::StyleComplexColor aColor);
  void SetPaintServer(mozilla::css::URLValue* aPaintServer,
                      nsStyleSVGFallbackType aFallbackType,
                      mozilla::StyleComplexColor aFallbackColor);
  void SetPaintServer(mozilla::css::URLValue* aPaintServer) {
    SetPaintServer(aPaintServer, eStyleSVGFallbackType_NotSet,
                   mozilla::StyleComplexColor::Black());
  }
  void SetContextValue(nsStyleSVGPaintType aType,
                       nsStyleSVGFallbackType aFallbackType,
                       mozilla::StyleComplexColor aFallbackColor);
  void SetContextValue(nsStyleSVGPaintType aType) {
    SetContextValue(aType, eStyleSVGFallbackType_NotSet,
                    mozilla::StyleComplexColor::Black());
  }

  nscolor GetColor(mozilla::ComputedStyle* aComputedStyle) const {
    MOZ_ASSERT(mType == eStyleSVGPaintType_Color);
    return mPaint.mColor.CalcColor(aComputedStyle);
  }

  mozilla::css::URLValue* GetPaintServer() const {
    MOZ_ASSERT(mType == eStyleSVGPaintType_Server);
    return mPaint.mPaintServer;
  }

  nsStyleSVGFallbackType GetFallbackType() const { return mFallbackType; }

  nscolor GetFallbackColor(mozilla::ComputedStyle* aComputedStyle) const {
    MOZ_ASSERT(mType == eStyleSVGPaintType_Server ||
               mType == eStyleSVGPaintType_ContextFill ||
               mType == eStyleSVGPaintType_ContextStroke);
    return mFallbackColor.CalcColor(aComputedStyle);
  }

  bool operator==(const nsStyleSVGPaint& aOther) const;
  bool operator!=(const nsStyleSVGPaint& aOther) const {
    return !(*this == aOther);
  }

 private:
  void Reset();
  void Assign(const nsStyleSVGPaint& aOther);

  union ColorOrPaintServer {
    mozilla::StyleComplexColor mColor;
    mozilla::css::URLValue* mPaintServer;
    explicit ColorOrPaintServer(mozilla::StyleComplexColor c) : mColor(c) {}
  };
  ColorOrPaintServer mPaint;
  nsStyleSVGPaintType mType;
  nsStyleSVGFallbackType mFallbackType;
  mozilla::StyleComplexColor mFallbackColor;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleSVG {
  explicit nsStyleSVG(const mozilla::dom::Document&);
  nsStyleSVG(const nsStyleSVG& aSource);
  ~nsStyleSVG();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleSVG*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleSVG& aNewData) const;

  nsStyleSVGPaint mFill;
  nsStyleSVGPaint mStroke;
  RefPtr<mozilla::css::URLValue> mMarkerEnd;
  RefPtr<mozilla::css::URLValue> mMarkerMid;
  RefPtr<mozilla::css::URLValue> mMarkerStart;
  nsTArray<mozilla::NonNegativeLengthPercentage> mStrokeDasharray;
  nsTArray<RefPtr<nsAtom>> mContextProps;

  mozilla::LengthPercentage mStrokeDashoffset;
  mozilla::NonNegativeLengthPercentage mStrokeWidth;

  float mFillOpacity;
  float mStrokeMiterlimit;
  float mStrokeOpacity;

  mozilla::StyleFillRule mClipRule;
  uint8_t mColorInterpolation;         // NS_STYLE_COLOR_INTERPOLATION_*
  uint8_t mColorInterpolationFilters;  // NS_STYLE_COLOR_INTERPOLATION_*
  mozilla::StyleFillRule mFillRule;
  uint8_t mPaintOrder;        // bitfield of NS_STYLE_PAINT_ORDER_* values
  uint8_t mShapeRendering;    // NS_STYLE_SHAPE_RENDERING_*
  uint8_t mStrokeLinecap;     // NS_STYLE_STROKE_LINECAP_*
  uint8_t mStrokeLinejoin;    // NS_STYLE_STROKE_LINEJOIN_*
  uint8_t mTextAnchor;        // NS_STYLE_TEXT_ANCHOR_*
  uint8_t mContextPropsBits;  // bitfield of
                              // NS_STYLE_CONTEXT_PROPERTY_FILL_* values

  /// Returns true if style has been set to expose the computed values of
  /// certain properties (such as 'fill') to the contents of any linked images.
  bool ExposesContextProperties() const { return bool(mContextPropsBits); }

  nsStyleSVGOpacitySource FillOpacitySource() const {
    uint8_t value =
        (mContextFlags & FILL_OPACITY_SOURCE_MASK) >> FILL_OPACITY_SOURCE_SHIFT;
    return nsStyleSVGOpacitySource(value);
  }
  nsStyleSVGOpacitySource StrokeOpacitySource() const {
    uint8_t value = (mContextFlags & STROKE_OPACITY_SOURCE_MASK) >>
                    STROKE_OPACITY_SOURCE_SHIFT;
    return nsStyleSVGOpacitySource(value);
  }
  bool StrokeDasharrayFromObject() const {
    return mContextFlags & STROKE_DASHARRAY_CONTEXT;
  }
  bool StrokeDashoffsetFromObject() const {
    return mContextFlags & STROKE_DASHOFFSET_CONTEXT;
  }
  bool StrokeWidthFromObject() const {
    return mContextFlags & STROKE_WIDTH_CONTEXT;
  }

  bool HasMarker() const { return mMarkerStart || mMarkerMid || mMarkerEnd; }

  /**
   * Returns true if the stroke is not "none" and the stroke-opacity is greater
   * than zero. This ignores stroke-widths as that depends on the context.
   */
  bool HasStroke() const {
    return mStroke.Type() != eStyleSVGPaintType_None && mStrokeOpacity > 0;
  }

  /**
   * Returns true if the fill is not "none" and the fill-opacity is greater
   * than zero.
   */
  bool HasFill() const {
    return mFill.Type() != eStyleSVGPaintType_None && mFillOpacity > 0;
  }

 private:
  // Flags to represent the use of context-fill and context-stroke
  // for fill-opacity or stroke-opacity, and context-value for stroke-dasharray,
  // stroke-dashoffset and stroke-width.

  // fill-opacity: context-{fill,stroke}
  static const uint8_t FILL_OPACITY_SOURCE_MASK = 0x03;
  // stroke-opacity: context-{fill,stroke}
  static const uint8_t STROKE_OPACITY_SOURCE_MASK = 0x0C;
  // stroke-dasharray: context-value
  static const uint8_t STROKE_DASHARRAY_CONTEXT = 0x10;
  // stroke-dashoffset: context-value
  static const uint8_t STROKE_DASHOFFSET_CONTEXT = 0x20;
  // stroke-width: context-value
  static const uint8_t STROKE_WIDTH_CONTEXT = 0x40;
  static const uint8_t FILL_OPACITY_SOURCE_SHIFT = 0;
  static const uint8_t STROKE_OPACITY_SOURCE_SHIFT = 2;

  uint8_t mContextFlags;
};

struct nsStyleFilter {
  nsStyleFilter();
  nsStyleFilter(const nsStyleFilter& aSource);
  ~nsStyleFilter();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleFilter*) {}
  const static bool kHasTriggerImageLoads = false;

  nsStyleFilter& operator=(const nsStyleFilter& aOther);

  bool operator==(const nsStyleFilter& aOther) const;
  bool operator!=(const nsStyleFilter& aOther) const {
    return !(*this == aOther);
  }

  uint32_t GetType() const { return mType; }

  const nsStyleCoord& GetFilterParameter() const {
    NS_ASSERTION(mType != NS_STYLE_FILTER_DROP_SHADOW &&
                     mType != NS_STYLE_FILTER_URL &&
                     mType != NS_STYLE_FILTER_NONE,
                 "wrong filter type");
    return mFilterParameter;
  }
  void SetFilterParameter(const nsStyleCoord& aFilterParameter, int32_t aType);

  mozilla::css::URLValue* GetURL() const {
    MOZ_ASSERT(mType == NS_STYLE_FILTER_URL, "wrong filter type");
    return mURL;
  }

  bool SetURL(mozilla::css::URLValue* aValue);

  nsCSSShadowArray* GetDropShadow() const {
    NS_ASSERTION(mType == NS_STYLE_FILTER_DROP_SHADOW, "wrong filter type");
    return mDropShadow;
  }
  void SetDropShadow(nsCSSShadowArray* aDropShadow);

 private:
  void ReleaseRef();

  uint32_t mType;                 // NS_STYLE_FILTER_*
  nsStyleCoord mFilterParameter;  // coord, percent, factor, angle
  union {
    mozilla::css::URLValue* mURL;
    nsCSSShadowArray* mDropShadow;
  };
};

template <>
struct nsTArray_CopyChooser<nsStyleFilter> {
  typedef nsTArray_CopyWithConstructors<nsStyleFilter> Type;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleSVGReset {
  explicit nsStyleSVGReset(const mozilla::dom::Document&);
  nsStyleSVGReset(const nsStyleSVGReset& aSource);
  ~nsStyleSVGReset();

  // Resolves and tracks the images in mMask.  Only called with a Servo-backed
  // style system, where those images must be resolved later than the OMT
  // nsStyleSVGReset constructor call.
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleSVGReset*);
  const static bool kHasTriggerImageLoads = true;

  nsChangeHint CalcDifference(const nsStyleSVGReset& aNewData) const;

  bool HasClipPath() const {
    return mClipPath.GetType() != mozilla::StyleShapeSourceType::None;
  }

  bool HasMask() const;

  bool HasNonScalingStroke() const {
    return mVectorEffect == NS_STYLE_VECTOR_EFFECT_NON_SCALING_STROKE;
  }

  nsStyleImageLayers mMask;
  mozilla::StyleShapeSource mClipPath;
  mozilla::StyleComplexColor mStopColor;
  mozilla::StyleComplexColor mFloodColor;
  mozilla::StyleComplexColor mLightingColor;

  float mStopOpacity;
  float mFloodOpacity;

  uint8_t mDominantBaseline;  // NS_STYLE_DOMINANT_BASELINE_*
  uint8_t mVectorEffect;      // NS_STYLE_VECTOR_EFFECT_*
  uint8_t mMaskType;          // NS_STYLE_MASK_TYPE_*
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleEffects {
  explicit nsStyleEffects(const mozilla::dom::Document&);
  nsStyleEffects(const nsStyleEffects& aSource);
  ~nsStyleEffects();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleEffects*) {}
  const static bool kHasTriggerImageLoads = false;

  nsChangeHint CalcDifference(const nsStyleEffects& aNewData) const;

  bool HasFilters() const { return !mFilters.IsEmpty(); }

  nsTArray<nsStyleFilter> mFilters;
  RefPtr<nsCSSShadowArray> mBoxShadow;  // nullptr for 'none'
  nsRect mClip;                         // offsets from UL border edge
  float mOpacity;
  uint8_t mClipFlags;     // bitfield of NS_STYLE_CLIP_* values
  uint8_t mMixBlendMode;  // NS_STYLE_BLEND_*
};

#define STATIC_ASSERT_TYPE_LAYOUTS_MATCH(T1, T2)           \
  static_assert(sizeof(T1) == sizeof(T2),                  \
                "Size mismatch between " #T1 " and " #T2); \
  static_assert(alignof(T1) == alignof(T2),                \
                "Align mismatch between " #T1 " and " #T2);

#define STATIC_ASSERT_FIELD_OFFSET_MATCHES(T1, T2, field)          \
  static_assert(offsetof(T1, field) == offsetof(T2, field),        \
                "Field offset mismatch of " #field " between " #T1 \
                " and " #T2);

/**
 * These *_Simple types are used to map Gecko types to layout-equivalent but
 * simpler Rust types, to aid Rust binding generation.
 *
 * If something in this types or the assertions below needs to change, ask
 * bholley, heycam or emilio before!
 *
 * <div rustbindgen="true" replaces="nsPoint">
 */
struct nsPoint_Simple {
  nscoord x, y;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsPoint, nsPoint_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsPoint, nsPoint_Simple, x);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsPoint, nsPoint_Simple, y);

/**
 * <div rustbindgen="true" replaces="nsMargin">
 */
struct nsMargin_Simple {
  nscoord top, right, bottom, left;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsMargin, nsMargin_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, top);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, right);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, bottom);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, left);

/**
 * <div rustbindgen="true" replaces="nsRect">
 */
struct nsRect_Simple {
  nscoord x, y, width, height;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsRect, nsRect_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, x);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, y);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, width);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, height);

/**
 * <div rustbindgen="true" replaces="nsSize">
 */
struct nsSize_Simple {
  nscoord width, height;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsSize, nsSize_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsSize, nsSize_Simple, width);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsSize, nsSize_Simple, height);

/**
 * <div rustbindgen="true" replaces="mozilla::UniquePtr">
 *
 * TODO(Emilio): This is a workaround and we should be able to get rid of this
 * one.
 */
template <typename T>
struct UniquePtr_Simple {
  T* mPtr;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(mozilla::UniquePtr<int>,
                                 UniquePtr_Simple<int>);

/**
 * <div rustbindgen replaces="nsTArray"></div>
 */
template <typename T>
class nsTArray_Simple {
  T* mBuffer;

 public:
  // The existence of a destructor here prevents bindgen from deriving the Clone
  // trait via a simple memory copy.
  ~nsTArray_Simple(){};
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsTArray<nsStyleImageLayers::Layer>,
                                 nsTArray_Simple<nsStyleImageLayers::Layer>);
STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsTArray<mozilla::StyleTransition>,
                                 nsTArray_Simple<mozilla::StyleTransition>);
STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsTArray<mozilla::StyleAnimation>,
                                 nsTArray_Simple<mozilla::StyleAnimation>);

/**
 * <div rustbindgen replaces="nsCOMArray"></div>
 *
 * mozilla::ArrayIterator doesn't work well with bindgen.
 */
template <typename T>
class nsCOMArray_Simple {
  nsTArray<nsISupports*> mBuffer;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsCOMArray<nsIContent>,
                                 nsCOMArray_Simple<nsIContent>);
STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsCOMArray<nsINode>,
                                 nsCOMArray_Simple<nsINode>);
STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsCOMArray<imgIContainer>,
                                 nsCOMArray_Simple<imgIContainer>);

#endif /* nsStyleStruct_h___ */
