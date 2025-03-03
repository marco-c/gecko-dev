/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* the interface (to internal code) for retrieving computed style data */

#ifndef _ComputedStyle_h_
#define _ComputedStyle_h_

#include "nsIMemoryReporter.h"
#include <algorithm>
#include "mozilla/Assertions.h"
#include "mozilla/CachedInheritingStyles.h"
#include "mozilla/Maybe.h"
#include "mozilla/PseudoStyleType.h"
#include "mozilla/ServoComputedData.h"
#include "mozilla/ServoTypes.h"
#include "mozilla/ServoUtils.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#include "nsColor.h"

#include "nsStyleStructFwd.h"

enum nsChangeHint : uint32_t;
class nsPresContext;
class nsWindowSizes;

#define STYLE_STRUCT(name_) struct nsStyle##name_;
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

extern "C" {
void Gecko_ComputedStyle_Destroy(mozilla::ComputedStyle*);
}

namespace mozilla {

namespace dom {
class Document;
}

class ComputedStyle;

/**
 * A ComputedStyle represents the computed style data for an element.
 *
 * The computed style data are stored in a set of reference counted structs
 * (see nsStyleStruct.h) that are stored directly on the ComputedStyle.
 *
 * Style structs are immutable once they have been produced, so when any change
 * is made that needs a restyle, we create a new ComputedStyle.
 *
 * ComputedStyles are reference counted. References are generally held by:
 *
 *  1. nsIFrame::mComputedStyle, for every frame
 *  2. Element::mServoData, for every element not inside a display:none subtree
 *  3. nsComputedDOMStyle, when created for elements in display:none subtrees
 *  4. media_queries::Device, which holds the initial value of every property
 */

// Various bits used by both Servo and Gecko.
//
// Please add an assert that this matches the Servo bit in
// computed_value_flags::assert_match().
//
// FIXME(emilio): Would be nice to use cbindgen when it can handle bitflags!
// without macro expansion, see https://github.com/eqrion/cbindgen/issues/100.
enum class ComputedStyleBit : uint16_t {
  HasTextDecorationLines = 1 << 0,
  SuppressLineBreak = 1 << 1,
  IsTextCombined = 1 << 2,
  RelevantLinkVisited = 1 << 3,
  HasPseudoElementData = 1 << 4,
  DependsOnFontMetrics = 1 << 9,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(ComputedStyleBit)

class ComputedStyle {
  using Bit = ComputedStyleBit;

 public:
  ComputedStyle(PseudoStyleType aPseudoType,
                ServoComputedDataForgotten aComputedValues);

  Bit Bits() const { return static_cast<Bit>(mSource.flags.mFlags); }

  // Return the ComputedStyle whose style data should be used for the R,
  // G, and B components of color, background-color, and border-*-color
  // if RelevantLinkIsVisited().
  //
  // GetPseudo() and GetPseudoType() on this ComputedStyle return the
  // same as on |this|, and its depth in the tree (number of GetParent()
  // calls until null is returned) is the same as |this|, since its
  // parent is either |this|'s parent or |this|'s parent's
  // style-if-visited.
  //
  // Structs on this context should never be examined without also
  // examining the corresponding struct on |this|.  Doing so will likely
  // both (1) lead to a privacy leak and (2) lead to dynamic change bugs
  // related to the Peek code in ComputedStyle::CalcStyleDifference.
  ComputedStyle* GetStyleIfVisited() const {
    return mSource.visited_style.mPtr;
  }

  bool IsLazilyCascadedPseudoElement() const {
    return IsPseudoElement() &&
           !nsCSSPseudoElements::IsEagerlyCascadedInServo(GetPseudoType());
  }

  PseudoStyleType GetPseudoType() const { return mPseudoType; }

  bool IsPseudoElement() const {
    return PseudoStyle::IsPseudoElement(mPseudoType);
  }

  bool IsInheritingAnonBox() const {
    return PseudoStyle::IsInheritingAnonBox(mPseudoType);
  }

  bool IsNonInheritingAnonBox() const {
    return PseudoStyle::IsNonInheritingAnonBox(mPseudoType);
  }

  bool IsWrapperAnonBox() const {
    return PseudoStyle::IsWrapperAnonBox(mPseudoType);
  }

  bool IsAnonBox() const { return PseudoStyle::IsAnonBox(mPseudoType); }

  bool IsPseudoOrAnonBox() const {
    return mPseudoType != PseudoStyleType::NotPseudo;
  }

  // Does this ComputedStyle or any of its ancestors have text
  // decoration lines?
  // Differs from nsStyleTextReset::HasTextDecorationLines, which tests
  // only the data for a single context.
  bool HasTextDecorationLines() const {
    return bool(Bits() & Bit::HasTextDecorationLines);
  }

  // Whether any line break inside should be suppressed? If this returns
  // true, the line should not be broken inside, which means inlines act
  // as if nowrap is set, <br> is suppressed, and blocks are inlinized.
  // This bit is propogated to all children of line partitipants. It is
  // currently used by ruby to make its content frames unbreakable.
  // NOTE: for nsTextFrame, use nsTextFrame::ShouldSuppressLineBreak()
  // instead of this method.
  bool ShouldSuppressLineBreak() const {
    return bool(Bits() & Bit::SuppressLineBreak);
  }

  // Is this horizontal-in-vertical (tate-chu-yoko) text? This flag is
  // only set on ComputedStyles whose pseudo is nsCSSAnonBoxes::mozText().
  bool IsTextCombined() const { return bool(Bits() & Bit::IsTextCombined); }

  // Is this horizontal-in-vertical (tate-chu-yoko) text? This flag is
  // only set on ComputedStyles whose pseudo is nsCSSAnonBoxes::mozText().
  bool DependsOnFontMetrics() const {
    return bool(Bits() & Bit::DependsOnFontMetrics);
  }

  // Does this ComputedStyle represent the style for a pseudo-element or
  // inherit data from such a ComputedStyle?  Whether this returns true
  // is equivalent to whether it or any of its ancestors returns
  // non-null for IsPseudoElement().
  bool HasPseudoElementData() const {
    return bool(Bits() & Bit::HasPseudoElementData);
  }

  // Is the only link whose visitedness is allowed to influence the
  // style of the node this ComputedStyle is for (which is that element
  // or its nearest ancestor that is a link) visited?
  bool RelevantLinkVisited() const {
    return bool(Bits() & Bit::RelevantLinkVisited);
  }

  ComputedStyle* GetCachedInheritingAnonBoxStyle(
      PseudoStyleType aPseudoType) const {
    MOZ_ASSERT(PseudoStyle::IsInheritingAnonBox(aPseudoType));
    return mCachedInheritingStyles.Lookup(aPseudoType);
  }

  void SetCachedInheritedAnonBoxStyle(ComputedStyle* aStyle) {
    mCachedInheritingStyles.Insert(aStyle);
  }

  ComputedStyle* GetCachedLazyPseudoStyle(PseudoStyleType aPseudo) const;

  void SetCachedLazyPseudoStyle(ComputedStyle* aStyle) {
    MOZ_ASSERT(aStyle->IsPseudoElement());
    MOZ_ASSERT(!GetCachedLazyPseudoStyle(aStyle->GetPseudoType()));
    MOZ_ASSERT(aStyle->IsLazilyCascadedPseudoElement());

    // Since we're caching lazy pseudo styles on the ComputedValues of the
    // originating element, we can assume that we either have the same
    // originating element, or that they were at least similar enough to share
    // the same ComputedValues, which means that they would match the same
    // pseudo rules. This allows us to avoid matching selectors and checking
    // the rule node before deciding to share.
    //
    // The one place this optimization breaks is with pseudo-elements that
    // support state (like :hover). So we just avoid sharing in those cases.
    if (nsCSSPseudoElements::PseudoElementSupportsUserActionState(
            aStyle->GetPseudoType())) {
      return;
    }

    mCachedInheritingStyles.Insert(aStyle);
  }

#define STYLE_STRUCT(name_)                                              \
  inline const nsStyle##name_* Style##name_() const MOZ_NONNULL_RETURN { \
    return mSource.GetStyle##name_();                                    \
  }
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

  /**
   * Compute the style changes needed during restyling when this style
   * context is being replaced by aNewContext.  (This is nonsymmetric since
   * we optimize by skipping comparison for styles that have never been
   * requested.)
   *
   * This method returns a change hint (see nsChangeHint.h).  All change
   * hints apply to the frame and its later continuations or ib-split
   * siblings.  Most (all of those except the "NotHandledForDescendants"
   * hints) also apply to all descendants.
   *
   * aEqualStructs must not be null.  Into it will be stored a bitfield
   * representing which structs were compared to be non-equal.
   *
   * CSS Variables are not compared here. Instead, the caller is responsible for
   * that when needed (basically only for elements).
   */
  nsChangeHint CalcStyleDifference(const ComputedStyle& aNewContext,
                                   uint32_t* aEqualStructs) const;

#ifdef DEBUG
  bool EqualForCachedAnonymousContentStyle(const ComputedStyle&) const;
#endif

 public:
  /**
   * Get a color that depends on link-visitedness using this and
   * this->GetStyleIfVisited().
   *
   * @param aField A pointer to a member variable in a style struct.
   *               The member variable and its style struct must have
   *               been listed in nsCSSVisitedDependentPropList.h.
   */
  template <typename T, typename S>
  nscolor GetVisitedDependentColor(T S::*aField);

  /**
   * aColors should be a two element array of nscolor in which the first
   * color is the unvisited color and the second is the visited color.
   *
   * Combine the R, G, and B components of whichever of aColors should
   * be used based on aLinkIsVisited with the A component of aColors[0].
   */
  static nscolor CombineVisitedColors(nscolor* aColors, bool aLinkIsVisited);

  /**
   * Start image loads for this style.
   *
   * The Document is used to get a hand on the image loader. The old style is a
   * hack for bug 1439285.
   */
  inline void StartImageLoads(dom::Document&,
                              const ComputedStyle* aOldStyle = nullptr);

#ifdef DEBUG
  void List(FILE* out, int32_t aIndent);
  static const char* StructName(StyleStructID aSID);
  static Maybe<StyleStructID> LookupStruct(const nsACString& aName);
#endif

  // The |aCVsSize| outparam on this function is where the actual CVs size
  // value is added. It's done that way because the callers know which value
  // the size should be added to.
  void AddSizeOfIncludingThis(nsWindowSizes& aSizes, size_t* aCVsSize) const;

 protected:
  // Needs to be friend so that it can call the destructor without making it
  // public.
  friend void ::Gecko_ComputedStyle_Destroy(ComputedStyle*);

  ~ComputedStyle() = default;

  ServoComputedData mSource;

  // A cache of anonymous box and lazy pseudo styles inheriting from this style.
  CachedInheritingStyles mCachedInheritingStyles;

  const PseudoStyleType mPseudoType;
};

}  // namespace mozilla

#endif
