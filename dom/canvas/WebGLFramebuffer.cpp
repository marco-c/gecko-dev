/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLFramebuffer.h"

// You know it's going to be fun when these two show up:
#include <algorithm>
#include <iterator>

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "MozFramebuffer.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "nsPrintfCString.h"
#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "WebGLRenderbuffer.h"
#include "WebGLTexture.h"

namespace mozilla {

static bool ShouldDeferAttachment(const WebGLContext* const webgl,
                                  const GLenum attachPoint) {
  if (webgl->IsWebGL2()) return false;

  switch (attachPoint) {
    case LOCAL_GL_DEPTH_ATTACHMENT:
    case LOCAL_GL_STENCIL_ATTACHMENT:
    case LOCAL_GL_DEPTH_STENCIL_ATTACHMENT:
      return true;
    default:
      return false;
  }
}

WebGLFBAttachPoint::WebGLFBAttachPoint(const WebGLContext* const webgl,
                                       const GLenum attachmentPoint)
    : mAttachmentPoint(attachmentPoint),
      mDeferAttachment(ShouldDeferAttachment(webgl, mAttachmentPoint)) {}

WebGLFBAttachPoint::~WebGLFBAttachPoint() {
  MOZ_ASSERT(!mRenderbufferPtr);
  MOZ_ASSERT(!mTexturePtr);
}

bool WebGLFBAttachPoint::IsDeleteRequested() const {
  return Texture()
             ? Texture()->IsDeleteRequested()
             : Renderbuffer() ? Renderbuffer()->IsDeleteRequested() : false;
}

void WebGLFBAttachPoint::Clear() { Set(nullptr, {}); }

void WebGLFBAttachPoint::Set(gl::GLContext* const gl,
                             const webgl::FbAttachInfo& toAttach) {
  mRenderbufferPtr = toAttach.rb;
  mTexturePtr = toAttach.tex;
  mTexImageLayer = AssertedCast<uint32_t>(toAttach.zLayer);
  mTexImageZLayerCount = AssertedCast<uint8_t>(toAttach.zLayerCount);
  mTexImageLevel = AssertedCast<uint8_t>(toAttach.mipLevel);
  mIsMultiview = toAttach.isMultiview;

  if (gl && !mDeferAttachment) {
    DoAttachment(gl);
  }
}

const webgl::ImageInfo* WebGLFBAttachPoint::GetImageInfo() const {
  if (mTexturePtr) {
    const auto target = Texture()->Target();
    uint8_t face = 0;
    if (target == LOCAL_GL_TEXTURE_CUBE_MAP) {
      face = Layer() % 6;
    }
    return &mTexturePtr->ImageInfoAtFace(face, mTexImageLevel);
  }
  if (mRenderbufferPtr) return &mRenderbufferPtr->ImageInfo();
  return nullptr;
}

bool WebGLFBAttachPoint::IsComplete(WebGLContext* webgl,
                                    nsCString* const out_info) const {
  MOZ_ASSERT(HasAttachment());

  const auto fnWriteErrorInfo = [&](const char* const text) {
    WebGLContext::EnumName(mAttachmentPoint, out_info);
    out_info->AppendLiteral(": ");
    out_info->AppendASCII(text);
  };

  const auto& imageInfo = *GetImageInfo();
  if (!imageInfo.mWidth || !imageInfo.mHeight) {
    fnWriteErrorInfo("Attachment has no width or height.");
    return false;
  }
  MOZ_ASSERT(imageInfo.IsDefined());

  const auto& tex = Texture();
  if (tex) {
    // ES 3.0 spec, pg 213 has giant blocks of text that bake down to requiring
    // that attached tex images are within the valid mip-levels of the texture.
    // While it draws distinction to only test non-immutable textures, that's
    // because immutable textures are *always* texture-complete. We need to
    // check immutable textures though, because checking completeness is also
    // when we zero invalidated/no-data tex images.
    const auto attachedMipLevel = MipLevel();

    const bool withinValidMipLevels = [&]() {
      const bool ensureInit = false;
      const auto texCompleteness = tex->CalcCompletenessInfo(ensureInit);
      if (!texCompleteness)  // OOM
        return false;
      if (!texCompleteness->levels) return false;

      const auto baseLevel = tex->BaseMipmapLevel();
      const auto maxLevel = baseLevel + texCompleteness->levels - 1;
      return baseLevel <= attachedMipLevel && attachedMipLevel <= maxLevel;
    }();
    if (!withinValidMipLevels) {
      fnWriteErrorInfo("Attached mip level is invalid for texture.");
      return false;
    }

    const auto& levelInfo = tex->ImageInfoAtFace(0, attachedMipLevel);
    const auto faceDepth = levelInfo.mDepth * tex->FaceCount();
    const bool withinValidZLayers = Layer() + ZLayerCount() - 1 < faceDepth;
    if (!withinValidZLayers) {
      fnWriteErrorInfo("Attached z layer is invalid for texture.");
      return false;
    }
  }

  const auto& formatUsage = imageInfo.mFormat;
  if (!formatUsage->IsRenderable()) {
    const auto info = nsPrintfCString(
        "Attachment has an effective format of %s,"
        " which is not renderable.",
        formatUsage->format->name);
    fnWriteErrorInfo(info.BeginReading());
    return false;
  }
  if (!formatUsage->IsExplicitlyRenderable()) {
    webgl->WarnIfImplicit(formatUsage->GetExtensionID());
  }

  const auto format = formatUsage->format;

  bool hasRequiredBits;

  switch (mAttachmentPoint) {
    case LOCAL_GL_DEPTH_ATTACHMENT:
      hasRequiredBits = format->d;
      break;

    case LOCAL_GL_STENCIL_ATTACHMENT:
      hasRequiredBits = format->s;
      break;

    case LOCAL_GL_DEPTH_STENCIL_ATTACHMENT:
      MOZ_ASSERT(!webgl->IsWebGL2());
      hasRequiredBits = (format->d && format->s);
      break;

    default:
      MOZ_ASSERT(mAttachmentPoint >= LOCAL_GL_COLOR_ATTACHMENT0);
      hasRequiredBits = format->IsColorFormat();
      break;
  }

  if (!hasRequiredBits) {
    fnWriteErrorInfo(
        "Attachment's format is missing required color/depth/stencil"
        " bits.");
    return false;
  }

  if (!webgl->IsWebGL2()) {
    bool hasSurplusPlanes = false;

    switch (mAttachmentPoint) {
      case LOCAL_GL_DEPTH_ATTACHMENT:
        hasSurplusPlanes = format->s;
        break;

      case LOCAL_GL_STENCIL_ATTACHMENT:
        hasSurplusPlanes = format->d;
        break;
    }

    if (hasSurplusPlanes) {
      fnWriteErrorInfo(
          "Attachment has depth or stencil bits when it shouldn't.");
      return false;
    }
  }

  return true;
}

void WebGLFBAttachPoint::DoAttachment(gl::GLContext* const gl) const {
  if (Renderbuffer()) {
    Renderbuffer()->DoFramebufferRenderbuffer(mAttachmentPoint);
    return;
  }

  if (!Texture()) {
    MOZ_ASSERT(mAttachmentPoint != LOCAL_GL_DEPTH_STENCIL_ATTACHMENT);
    // WebGL 2 doesn't have a real attachment for this, and WebGL 1 is defered
    // and only DoAttachment if HasAttachment.

    gl->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER, mAttachmentPoint,
                                 LOCAL_GL_RENDERBUFFER, 0);
    return;
  }

  const auto& texName = Texture()->mGLName;

  switch (Texture()->Target().get()) {
    case LOCAL_GL_TEXTURE_2D:
    case LOCAL_GL_TEXTURE_CUBE_MAP: {
      TexImageTarget imageTarget = LOCAL_GL_TEXTURE_2D;
      if (Texture()->Target() == LOCAL_GL_TEXTURE_CUBE_MAP) {
        imageTarget = LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X + Layer();
      }

      if (mAttachmentPoint == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
        gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                  LOCAL_GL_DEPTH_ATTACHMENT, imageTarget.get(),
                                  texName, MipLevel());
        gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                  LOCAL_GL_STENCIL_ATTACHMENT,
                                  imageTarget.get(), texName, MipLevel());
      } else {
        gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER, mAttachmentPoint,
                                  imageTarget.get(), texName, MipLevel());
      }
      break;
    }

    case LOCAL_GL_TEXTURE_2D_ARRAY:
    case LOCAL_GL_TEXTURE_3D:
      if (ZLayerCount() != 1) {
        gl->fFramebufferTextureMultiview(LOCAL_GL_FRAMEBUFFER, mAttachmentPoint,
                                         texName, MipLevel(), Layer(),
                                         ZLayerCount());
      } else {
        gl->fFramebufferTextureLayer(LOCAL_GL_FRAMEBUFFER, mAttachmentPoint,
                                     texName, MipLevel(), Layer());
      }
      break;
  }
}

JS::Value WebGLFBAttachPoint::GetParameter(WebGLContext* webgl, JSContext* cx,
                                           GLenum target, GLenum attachment,
                                           GLenum pname,
                                           ErrorResult* const out_error) const {
  if (!HasAttachment()) {
    // Divergent between GLES 3 and 2.

    // GLES 2.0.25 p127:
    //   "If the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is NONE, then
    //   querying any other pname will generate INVALID_ENUM."

    // GLES 3.0.4 p240:
    //   "If the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is NONE, no
    //   framebuffer is bound to target. In this case querying pname
    //   FRAMEBUFFER_ATTACHMENT_OBJECT_NAME will return zero, and all other
    //   queries will generate an INVALID_OPERATION error."
    switch (pname) {
      case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        return JS::Int32Value(LOCAL_GL_NONE);

      case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
        if (webgl->IsWebGL2()) return JS::NullValue();

        break;

      default:
        break;
    }
    nsCString attachmentName;
    WebGLContext::EnumName(attachment, &attachmentName);
    if (webgl->IsWebGL2()) {
      webgl->ErrorInvalidOperation("No attachment at %s.",
                                   attachmentName.BeginReading());
    } else {
      webgl->ErrorInvalidEnum("No attachment at %s.",
                              attachmentName.BeginReading());
    }
    return JS::NullValue();
  }

  bool isPNameValid = false;
  switch (pname) {
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
      return JS::Int32Value(mTexturePtr ? LOCAL_GL_TEXTURE
                                        : LOCAL_GL_RENDERBUFFER);

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
      return (mTexturePtr ? webgl->WebGLObjectAsJSValue(cx, mTexturePtr.get(),
                                                        *out_error)
                          : webgl->WebGLObjectAsJSValue(
                                cx, mRenderbufferPtr.get(), *out_error));

      //////

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
      if (mTexturePtr) return JS::Int32Value(MipLevel());
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
      if (mTexturePtr) {
        GLenum face = 0;
        if (mTexturePtr->Target() == LOCAL_GL_TEXTURE_CUBE_MAP) {
          face = LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X + Layer();
        }
        return JS::Int32Value(face);
      }
      break;

      //////

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
      if (webgl->IsWebGL2()) {
        return JS::Int32Value(AssertedCast<int32_t>(Layer()));
      }
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR:
      if (webgl->IsExtensionEnabled(WebGLExtensionID::OVR_multiview2)) {
        return JS::Int32Value(AssertedCast<int32_t>(Layer()));
      }
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR:
      if (webgl->IsExtensionEnabled(WebGLExtensionID::OVR_multiview2)) {
        return JS::Int32Value(ZLayerCount());
      }
      break;

      //////

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      isPNameValid = webgl->IsWebGL2();
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      isPNameValid = (webgl->IsWebGL2() ||
                      webgl->IsExtensionEnabled(WebGLExtensionID::EXT_sRGB));
      break;
  }

  if (!isPNameValid) {
    webgl->ErrorInvalidEnum("Invalid pname: 0x%04x", pname);
    return JS::NullValue();
  }

  const auto& imageInfo = *GetImageInfo();
  const auto& usage = imageInfo.mFormat;
  if (!usage) {
    if (pname == LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING)
      return JS::NumberValue(LOCAL_GL_LINEAR);

    return JS::NullValue();
  }

  auto format = usage->format;

  GLint ret = 0;
  switch (pname) {
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
      ret = format->r;
      break;
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
      ret = format->g;
      break;
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
      ret = format->b;
      break;
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
      ret = format->a;
      break;
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      ret = format->d;
      break;
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      ret = format->s;
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      ret = (format->isSRGB ? LOCAL_GL_SRGB : LOCAL_GL_LINEAR);
      break;

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      MOZ_ASSERT(attachment != LOCAL_GL_DEPTH_STENCIL_ATTACHMENT);

      if (format->unsizedFormat == webgl::UnsizedFormat::DEPTH_STENCIL) {
        MOZ_ASSERT(attachment == LOCAL_GL_DEPTH_ATTACHMENT ||
                   attachment == LOCAL_GL_STENCIL_ATTACHMENT);

        if (attachment == LOCAL_GL_DEPTH_ATTACHMENT) {
          switch (format->effectiveFormat) {
            case webgl::EffectiveFormat::DEPTH24_STENCIL8:
              format =
                  webgl::GetFormat(webgl::EffectiveFormat::DEPTH_COMPONENT24);
              break;
            case webgl::EffectiveFormat::DEPTH32F_STENCIL8:
              format =
                  webgl::GetFormat(webgl::EffectiveFormat::DEPTH_COMPONENT32F);
              break;
            default:
              MOZ_ASSERT(false, "no matched DS format");
              break;
          }
        } else if (attachment == LOCAL_GL_STENCIL_ATTACHMENT) {
          switch (format->effectiveFormat) {
            case webgl::EffectiveFormat::DEPTH24_STENCIL8:
            case webgl::EffectiveFormat::DEPTH32F_STENCIL8:
              format = webgl::GetFormat(webgl::EffectiveFormat::STENCIL_INDEX8);
              break;
            default:
              MOZ_ASSERT(false, "no matched DS format");
              break;
          }
        }
      }

      switch (format->componentType) {
        case webgl::ComponentType::Int:
          ret = LOCAL_GL_INT;
          break;
        case webgl::ComponentType::UInt:
          ret = LOCAL_GL_UNSIGNED_INT;
          break;
        case webgl::ComponentType::NormInt:
          ret = LOCAL_GL_SIGNED_NORMALIZED;
          break;
        case webgl::ComponentType::NormUInt:
          ret = LOCAL_GL_UNSIGNED_NORMALIZED;
          break;
        case webgl::ComponentType::Float:
          ret = LOCAL_GL_FLOAT;
          break;
      }
      break;

    default:
      MOZ_ASSERT(false, "Missing case.");
      break;
  }

  return JS::Int32Value(ret);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// WebGLFramebuffer

WebGLFramebuffer::WebGLFramebuffer(WebGLContext* webgl, GLuint fbo)
    : WebGLRefCountedObject(webgl),
      mGLName(fbo),
      mDepthAttachment(webgl, LOCAL_GL_DEPTH_ATTACHMENT),
      mStencilAttachment(webgl, LOCAL_GL_STENCIL_ATTACHMENT),
      mDepthStencilAttachment(webgl, LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
  mContext->mFramebuffers.insertBack(this);

  mAttachments.push_back(&mDepthAttachment);
  mAttachments.push_back(&mStencilAttachment);

  if (!webgl->IsWebGL2()) {
    // Only WebGL1 has a separate depth+stencil attachment point.
    mAttachments.push_back(&mDepthStencilAttachment);
  }

  size_t i = 0;
  for (auto& cur : mColorAttachments) {
    new (&cur) WebGLFBAttachPoint(webgl, LOCAL_GL_COLOR_ATTACHMENT0 + i);
    i++;

    mAttachments.push_back(&cur);
  }

  mColorDrawBuffers.push_back(&mColorAttachments[0]);
  mColorReadBuffer = &mColorAttachments[0];
}

void WebGLFramebuffer::Delete() {
  InvalidateCaches();

  mDepthAttachment.Clear();
  mStencilAttachment.Clear();
  mDepthStencilAttachment.Clear();

  for (auto& cur : mColorAttachments) {
    cur.Clear();
  }

  mContext->gl->fDeleteFramebuffers(1, &mGLName);

  LinkedListElement<WebGLFramebuffer>::removeFrom(mContext->mFramebuffers);
}

////

Maybe<WebGLFBAttachPoint*> WebGLFramebuffer::GetColorAttachPoint(
    GLenum attachPoint) {
  if (attachPoint == LOCAL_GL_NONE) return Some<WebGLFBAttachPoint*>(nullptr);

  if (attachPoint < LOCAL_GL_COLOR_ATTACHMENT0) return Nothing();

  const size_t colorId = attachPoint - LOCAL_GL_COLOR_ATTACHMENT0;

  MOZ_ASSERT(mContext->mGLMaxColorAttachments <= kMaxColorAttachments);
  if (colorId >= mContext->mGLMaxColorAttachments) return Nothing();

  return Some(&mColorAttachments[colorId]);
}

Maybe<WebGLFBAttachPoint*> WebGLFramebuffer::GetAttachPoint(
    GLenum attachPoint) {
  switch (attachPoint) {
    case LOCAL_GL_DEPTH_STENCIL_ATTACHMENT:
      return Some(&mDepthStencilAttachment);

    case LOCAL_GL_DEPTH_ATTACHMENT:
      return Some(&mDepthAttachment);

    case LOCAL_GL_STENCIL_ATTACHMENT:
      return Some(&mStencilAttachment);

    default:
      return GetColorAttachPoint(attachPoint);
  }
}

void WebGLFramebuffer::DetachTexture(const WebGLTexture* tex) {
  for (const auto& attach : mAttachments) {
    if (attach->Texture() == tex) {
      attach->Clear();
    }
  }
  InvalidateCaches();
}

void WebGLFramebuffer::DetachRenderbuffer(const WebGLRenderbuffer* rb) {
  for (const auto& attach : mAttachments) {
    if (attach->Renderbuffer() == rb) {
      attach->Clear();
    }
  }
  InvalidateCaches();
}

////////////////////////////////////////////////////////////////////////////////
// Completeness

bool WebGLFramebuffer::HasDuplicateAttachments() const {
  std::set<WebGLFBAttachPoint::Ordered> uniqueAttachSet;

  for (const auto& attach : mColorAttachments) {
    if (!attach.HasAttachment()) continue;

    const WebGLFBAttachPoint::Ordered ordered(attach);

    const bool didInsert = uniqueAttachSet.insert(ordered).second;
    if (!didInsert) return true;
  }

  return false;
}

bool WebGLFramebuffer::HasDefinedAttachments() const {
  bool hasAttachments = false;
  for (const auto& attach : mAttachments) {
    hasAttachments |= attach->HasAttachment();
  }
  return hasAttachments;
}

bool WebGLFramebuffer::HasIncompleteAttachments(
    nsCString* const out_info) const {
  bool hasIncomplete = false;
  for (const auto& cur : mAttachments) {
    if (!cur->HasAttachment())
      continue;  // Not defined, so can't count as incomplete.

    hasIncomplete |= !cur->IsComplete(mContext, out_info);
  }
  return hasIncomplete;
}

bool WebGLFramebuffer::AllImageRectsMatch() const {
  MOZ_ASSERT(HasDefinedAttachments());
  DebugOnly<nsCString> fbStatusInfo;
  MOZ_ASSERT(!HasIncompleteAttachments(&fbStatusInfo));

  bool needsInit = true;
  uint32_t width = 0;
  uint32_t height = 0;

  bool hasMismatch = false;
  for (const auto& attach : mAttachments) {
    const auto& imageInfo = attach->GetImageInfo();
    if (!imageInfo) continue;

    const auto& curWidth = imageInfo->mWidth;
    const auto& curHeight = imageInfo->mHeight;

    if (needsInit) {
      needsInit = false;
      width = curWidth;
      height = curHeight;
      continue;
    }

    hasMismatch |= (curWidth != width || curHeight != height);
  }
  return !hasMismatch;
}

bool WebGLFramebuffer::AllImageSamplesMatch() const {
  MOZ_ASSERT(HasDefinedAttachments());
  DebugOnly<nsCString> fbStatusInfo;
  MOZ_ASSERT(!HasIncompleteAttachments(&fbStatusInfo));

  bool needsInit = true;
  uint32_t samples = 0;

  bool hasMismatch = false;
  for (const auto& attach : mAttachments) {
    const auto& imageInfo = attach->GetImageInfo();
    if (!imageInfo) continue;

    const auto& curSamples = imageInfo->mSamples;

    if (needsInit) {
      needsInit = false;
      samples = curSamples;
      continue;
    }

    hasMismatch |= (curSamples != samples);
  };
  return !hasMismatch;
}

FBStatus WebGLFramebuffer::PrecheckFramebufferStatus(
    nsCString* const out_info) const {
  MOZ_ASSERT(mContext->mBoundDrawFramebuffer == this ||
             mContext->mBoundReadFramebuffer == this);

  if (!HasDefinedAttachments())
    return LOCAL_GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;  // No
                                                                // attachments

  if (HasIncompleteAttachments(out_info))
    return LOCAL_GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;

  if (!AllImageRectsMatch())
    return LOCAL_GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;  // Inconsistent sizes

  if (!AllImageSamplesMatch())
    return LOCAL_GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;  // Inconsistent samples

  if (HasDuplicateAttachments()) return LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;

  if (mContext->IsWebGL2()) {
    MOZ_ASSERT(!mDepthStencilAttachment.HasAttachment());
    if (mDepthAttachment.HasAttachment() &&
        mStencilAttachment.HasAttachment()) {
      if (!mDepthAttachment.IsEquivalentForFeedback(mStencilAttachment))
        return LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;
    }
  } else {
    const auto depthOrStencilCount =
        int(mDepthAttachment.HasAttachment()) +
        int(mStencilAttachment.HasAttachment()) +
        int(mDepthStencilAttachment.HasAttachment());
    if (depthOrStencilCount > 1) return LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;
  }

  {
    const WebGLFBAttachPoint* example = nullptr;
    for (const auto& x : mAttachments) {
      if (!x->HasAttachment()) continue;
      if (!example) {
        example = x;
        continue;
      }
      if (x->ZLayerCount() != example->ZLayerCount()) {
        return LOCAL_GL_FRAMEBUFFER_INCOMPLETE_VIEW_TARGETS_OVR;
      }
    }
  }

  return LOCAL_GL_FRAMEBUFFER_COMPLETE;
}

////////////////////////////////////////
// Validation

bool WebGLFramebuffer::ValidateAndInitAttachments(
    const GLenum incompleteFbError) const {
  MOZ_ASSERT(mContext->mBoundDrawFramebuffer == this ||
             mContext->mBoundReadFramebuffer == this);

  const auto fbStatus = CheckFramebufferStatus();
  if (fbStatus == LOCAL_GL_FRAMEBUFFER_COMPLETE) return true;

  mContext->GenerateError(incompleteFbError, "Framebuffer must be complete.");
  return false;
}

bool WebGLFramebuffer::ValidateClearBufferType(GLenum buffer,
                                               uint32_t drawBuffer,
                                               GLenum funcType) const {
  if (buffer != LOCAL_GL_COLOR) return true;

  const auto& attach = mColorAttachments[drawBuffer];
  const auto& imageInfo = attach.GetImageInfo();
  if (!imageInfo) return true;

  if (!count(mColorDrawBuffers.begin(), mColorDrawBuffers.end(), &attach))
    return true;  // DRAW_BUFFERi set to NONE.

  GLenum attachType;
  switch (imageInfo->mFormat->format->componentType) {
    case webgl::ComponentType::Int:
      attachType = LOCAL_GL_INT;
      break;
    case webgl::ComponentType::UInt:
      attachType = LOCAL_GL_UNSIGNED_INT;
      break;
    default:
      attachType = LOCAL_GL_FLOAT;
      break;
  }

  if (attachType != funcType) {
    mContext->ErrorInvalidOperation(
        "This attachment is of type 0x%04x, but"
        " this function is of type 0x%04x.",
        attachType, funcType);
    return false;
  }

  return true;
}

bool WebGLFramebuffer::ValidateForColorRead(
    const webgl::FormatUsageInfo** const out_format, uint32_t* const out_width,
    uint32_t* const out_height) const {
  if (!mColorReadBuffer) {
    mContext->ErrorInvalidOperation("READ_BUFFER must not be NONE.");
    return false;
  }

  if (mColorReadBuffer->ZLayerCount() > 1) {
    mContext->GenerateError(LOCAL_GL_INVALID_FRAMEBUFFER_OPERATION,
                            "The READ_BUFFER attachment has multiple views.");
    return false;
  }

  const auto& imageInfo = mColorReadBuffer->GetImageInfo();
  if (!imageInfo) {
    mContext->ErrorInvalidOperation(
        "The READ_BUFFER attachment is not defined.");
    return false;
  }

  if (imageInfo->mSamples) {
    mContext->ErrorInvalidOperation(
        "The READ_BUFFER attachment is multisampled.");
    return false;
  }

  *out_format = imageInfo->mFormat;
  *out_width = imageInfo->mWidth;
  *out_height = imageInfo->mHeight;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Resolution and caching

void WebGLFramebuffer::DoDeferredAttachments() const {
  if (mContext->IsWebGL2()) return;

  const auto& gl = mContext->gl;
  gl->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_DEPTH_ATTACHMENT,
                               LOCAL_GL_RENDERBUFFER, 0);
  gl->fFramebufferRenderbuffer(LOCAL_GL_FRAMEBUFFER,
                               LOCAL_GL_STENCIL_ATTACHMENT,
                               LOCAL_GL_RENDERBUFFER, 0);

  const auto fn = [&](const WebGLFBAttachPoint& attach) {
    MOZ_ASSERT(attach.mDeferAttachment);
    if (attach.HasAttachment()) {
      attach.DoAttachment(gl);
    }
  };
  // Only one of these will have an attachment.
  fn(mDepthAttachment);
  fn(mStencilAttachment);
  fn(mDepthStencilAttachment);
}

void WebGLFramebuffer::ResolveAttachmentData() const {
  // GLES 3.0.5 p188:
  //   The result of clearing integer color buffers with `Clear` is undefined.

  // Two different approaches:
  // On WebGL 2, we have glClearBuffer, and *must* use it for integer buffers,
  // so let's just use it for all the buffers. One WebGL 1, we might not have
  // glClearBuffer,

  // WebGL 1 is easier, because we can just call glClear, possibly with
  // glDrawBuffers.

  const auto& gl = mContext->gl;

  const webgl::ScopedPrepForResourceClear scopedPrep(*mContext);

  if (mContext->IsWebGL2()) {
    const uint32_t uiZeros[4] = {};
    const int32_t iZeros[4] = {};
    const float fZeros[4] = {};
    const float fOne[] = {1.0f};

    for (const auto& cur : mAttachments) {
      const auto& imageInfo = cur->GetImageInfo();
      if (!imageInfo || imageInfo->mHasData)
        continue;  // Nothing attached, or already has data.

      const auto fnClearBuffer = [&]() {
        const auto& format = imageInfo->mFormat->format;
        MOZ_ASSERT(format->estimatedBytesPerPixel <= sizeof(uiZeros));
        MOZ_ASSERT(format->estimatedBytesPerPixel <= sizeof(iZeros));
        MOZ_ASSERT(format->estimatedBytesPerPixel <= sizeof(fZeros));

        switch (cur->mAttachmentPoint) {
          case LOCAL_GL_DEPTH_ATTACHMENT:
            gl->fClearBufferfv(LOCAL_GL_DEPTH, 0, fOne);
            break;
          case LOCAL_GL_STENCIL_ATTACHMENT:
            gl->fClearBufferiv(LOCAL_GL_STENCIL, 0, iZeros);
            break;
          default:
            MOZ_ASSERT(cur->mAttachmentPoint !=
                       LOCAL_GL_DEPTH_STENCIL_ATTACHMENT);
            const uint32_t drawBuffer =
                cur->mAttachmentPoint - LOCAL_GL_COLOR_ATTACHMENT0;
            MOZ_ASSERT(drawBuffer <= 100);
            switch (format->componentType) {
              case webgl::ComponentType::Int:
                gl->fClearBufferiv(LOCAL_GL_COLOR, drawBuffer, iZeros);
                break;
              case webgl::ComponentType::UInt:
                gl->fClearBufferuiv(LOCAL_GL_COLOR, drawBuffer, uiZeros);
                break;
              default:
                gl->fClearBufferfv(LOCAL_GL_COLOR, drawBuffer, fZeros);
                break;
            }
        }
      };

      if (imageInfo->mDepth > 1) {
        const auto& tex = cur->Texture();
        const gl::ScopedFramebuffer scopedFB(gl);
        const gl::ScopedBindFramebuffer scopedBindFB(gl, scopedFB.FB());
        for (uint32_t z = 0; z < imageInfo->mDepth; z++) {
          gl->fFramebufferTextureLayer(LOCAL_GL_FRAMEBUFFER,
                                       cur->mAttachmentPoint, tex->mGLName,
                                       cur->MipLevel(), z);
          fnClearBuffer();
        }
      } else {
        fnClearBuffer();
      }
      imageInfo->mHasData = true;
    }
    return;
  }

  uint32_t clearBits = 0;
  std::vector<GLenum> drawBufferForClear;

  const auto fnGather = [&](const WebGLFBAttachPoint& attach,
                            const uint32_t attachClearBits) {
    const auto& imageInfo = attach.GetImageInfo();
    if (!imageInfo || imageInfo->mHasData) return false;

    clearBits |= attachClearBits;
    imageInfo->mHasData = true;  // Just mark it now.
    return true;
  };

  //////

  for (const auto& cur : mColorAttachments) {
    if (fnGather(cur, LOCAL_GL_COLOR_BUFFER_BIT)) {
      const uint32_t id = cur.mAttachmentPoint - LOCAL_GL_COLOR_ATTACHMENT0;
      MOZ_ASSERT(id <= 100);
      drawBufferForClear.resize(id + 1);  // Pads with zeros!
      drawBufferForClear[id] = cur.mAttachmentPoint;
    }
  }

  (void)fnGather(mDepthAttachment, LOCAL_GL_DEPTH_BUFFER_BIT);
  (void)fnGather(mStencilAttachment, LOCAL_GL_STENCIL_BUFFER_BIT);
  (void)fnGather(mDepthStencilAttachment,
                 LOCAL_GL_DEPTH_BUFFER_BIT | LOCAL_GL_STENCIL_BUFFER_BIT);

  //////

  if (!clearBits) return;

  if (gl->IsSupported(gl::GLFeature::draw_buffers)) {
    gl->fDrawBuffers(drawBufferForClear.size(), drawBufferForClear.data());
  }

  gl->fClear(clearBits);

  RefreshDrawBuffers();
}

WebGLFramebuffer::CompletenessInfo::~CompletenessInfo() {
  if (!this->fb) return;
  const auto& fb = *this->fb;
  const auto& webgl = fb.mContext;
  fb.mNumFBStatusInvals++;
  if (fb.mNumFBStatusInvals > webgl->mMaxAcceptableFBStatusInvals) {
    webgl->GeneratePerfWarning(
        "FB was invalidated after being complete %u"
        " times. [webgl.perf.max-acceptable-fb-status-invals]",
        uint32_t(fb.mNumFBStatusInvals));
  }
}

////////////////////////////////////////////////////////////////////////////////
// Entrypoints

FBStatus WebGLFramebuffer::CheckFramebufferStatus() const {
  if (mCompletenessInfo) return LOCAL_GL_FRAMEBUFFER_COMPLETE;

  // Ok, let's try to resolve it!

  nsCString statusInfo;
  FBStatus ret = PrecheckFramebufferStatus(&statusInfo);
  do {
    if (ret != LOCAL_GL_FRAMEBUFFER_COMPLETE) break;

    // Looks good on our end. Let's ask the driver.
    gl::GLContext* const gl = mContext->gl;

    const ScopedFBRebinder autoFB(mContext);
    gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mGLName);

    ////

    DoDeferredAttachments();
    RefreshDrawBuffers();
    RefreshReadBuffer();

    ret = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);

    ////

    if (ret != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
      const nsPrintfCString text("Bad status according to the driver: 0x%04x",
                                 ret.get());
      statusInfo = text;
      break;
    }

    ResolveAttachmentData();

    // Sweet, let's cache that.
    auto info = CompletenessInfo{this, UINT32_MAX, UINT32_MAX};
    mCompletenessInfo.ResetInvalidators({});
    mCompletenessInfo.AddInvalidator(*this);

    const auto fnIsFloat32 = [](const webgl::FormatInfo& info) {
      if (info.componentType != webgl::ComponentType::Float) return false;
      return info.r == 32;
    };

    for (const auto& cur : mAttachments) {
      const auto& tex = cur->Texture();
      const auto& rb = cur->Renderbuffer();
      if (tex) {
        mCompletenessInfo.AddInvalidator(*tex);
        info.texAttachments.push_back(cur);
      } else if (rb) {
        mCompletenessInfo.AddInvalidator(*rb);
      } else {
        continue;
      }
      const auto& imageInfo = cur->GetImageInfo();
      MOZ_ASSERT(imageInfo);
      info.width = std::min(info.width, imageInfo->mWidth);
      info.height = std::min(info.height, imageInfo->mHeight);
      info.hasFloat32 |= fnIsFloat32(*imageInfo->mFormat->format);
      info.zLayerCount = cur->ZLayerCount();
      info.isMultiview = cur->IsMultiview();
    }
    mCompletenessInfo = Some(std::move(info));
    info.fb = nullptr;  // Don't trigger the invalidation warning.
    return LOCAL_GL_FRAMEBUFFER_COMPLETE;
  } while (false);

  MOZ_ASSERT(ret != LOCAL_GL_FRAMEBUFFER_COMPLETE);
  mContext->GenerateWarning("Framebuffer not complete. (status: 0x%04x) %s",
                            ret.get(), statusInfo.BeginReading());
  return ret;
}

////

void WebGLFramebuffer::RefreshDrawBuffers() const {
  const auto& gl = mContext->gl;
  if (!gl->IsSupported(gl::GLFeature::draw_buffers)) return;

  // Prior to GL4.1, having a no-image FB attachment that's selected by
  // DrawBuffers yields a framebuffer status of
  // FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER. We could workaround this only on
  // affected versions, but it's easier be unconditional.
  std::vector<GLenum> driverBuffers(mContext->mGLMaxDrawBuffers, LOCAL_GL_NONE);
  for (const auto& attach : mColorDrawBuffers) {
    if (attach->HasAttachment()) {
      const uint32_t index =
          attach->mAttachmentPoint - LOCAL_GL_COLOR_ATTACHMENT0;
      driverBuffers[index] = attach->mAttachmentPoint;
    }
  }

  gl->fDrawBuffers(driverBuffers.size(), driverBuffers.data());
}

void WebGLFramebuffer::RefreshReadBuffer() const {
  const auto& gl = mContext->gl;
  if (!gl->IsSupported(gl::GLFeature::read_buffer)) return;

  // Prior to GL4.1, having a no-image FB attachment that's selected by
  // ReadBuffer yields a framebuffer status of
  // FRAMEBUFFER_INCOMPLETE_READ_BUFFER. We could workaround this only on
  // affected versions, but it's easier be unconditional.
  GLenum driverBuffer = LOCAL_GL_NONE;
  if (mColorReadBuffer && mColorReadBuffer->HasAttachment()) {
    driverBuffer = mColorReadBuffer->mAttachmentPoint;
  }

  gl->fReadBuffer(driverBuffer);
}

////

void WebGLFramebuffer::DrawBuffers(const dom::Sequence<GLenum>& buffers) {
  if (buffers.Length() > mContext->mGLMaxDrawBuffers) {
    // "An INVALID_VALUE error is generated if `n` is greater than
    // MAX_DRAW_BUFFERS."
    mContext->ErrorInvalidValue(
        "`buffers` must have a length <="
        " MAX_DRAW_BUFFERS.");
    return;
  }

  std::vector<const WebGLFBAttachPoint*> newColorDrawBuffers;
  newColorDrawBuffers.reserve(buffers.Length());

  for (size_t i = 0; i < buffers.Length(); i++) {
    // "If the GL is bound to a draw framebuffer object, the `i`th buffer listed
    // in bufs must be COLOR_ATTACHMENTi or NONE. Specifying a buffer out of
    // order, BACK, or COLOR_ATTACHMENTm where `m` is greater than or equal to
    // the value of MAX_COLOR_ATTACHMENTS, will generate the error
    // INVALID_OPERATION.

    // WEBGL_draw_buffers:
    // "The value of the MAX_COLOR_ATTACHMENTS_WEBGL parameter must be greater
    // than or equal to that of the MAX_DRAW_BUFFERS_WEBGL parameter." This
    // means that if buffers.Length() isn't larger than MaxDrawBuffers, it won't
    // be larger than MaxColorAttachments.
    const auto& cur = buffers[i];
    if (cur == LOCAL_GL_COLOR_ATTACHMENT0 + i) {
      const auto& attach = mColorAttachments[i];
      newColorDrawBuffers.push_back(&attach);
    } else if (cur != LOCAL_GL_NONE) {
      const bool isColorEnum = (cur >= LOCAL_GL_COLOR_ATTACHMENT0 &&
                                cur < mContext->LastColorAttachmentEnum());
      if (cur != LOCAL_GL_BACK && !isColorEnum) {
        mContext->ErrorInvalidEnum("Unexpected enum in buffers.");
        return;
      }

      mContext->ErrorInvalidOperation(
          "`buffers[i]` must be NONE or"
          " COLOR_ATTACHMENTi.");
      return;
    }
  }

  ////

  mColorDrawBuffers.swap(newColorDrawBuffers);
  RefreshDrawBuffers();  // Calls glDrawBuffers.
}

void WebGLFramebuffer::ReadBuffer(GLenum attachPoint) {
  const auto& maybeAttach = GetColorAttachPoint(attachPoint);
  if (!maybeAttach) {
    const char text[] =
        "`mode` must be a COLOR_ATTACHMENTi, for 0 <= i <"
        " MAX_DRAW_BUFFERS.";
    if (attachPoint == LOCAL_GL_BACK) {
      mContext->ErrorInvalidOperation(text);
    } else {
      mContext->ErrorInvalidEnum(text);
    }
    return;
  }
  const auto& attach = maybeAttach.value();  // Might be nullptr.

  ////

  mColorReadBuffer = attach;
  RefreshReadBuffer();  // Calls glReadBuffer.
}

////

void WebGLFramebuffer::FramebufferAttach(const GLenum attachEnum,
                                         const webgl::FbAttachInfo& toAttach) {
  MOZ_ASSERT(mContext->mBoundDrawFramebuffer == this ||
             mContext->mBoundReadFramebuffer == this);

  // `attachment`
  const auto maybeAttach = GetAttachPoint(attachEnum);
  if (!maybeAttach || !maybeAttach.value()) {
    mContext->ErrorInvalidEnum("Bad `attachment`: 0x%x.", attachEnum);
    return;
  }
  const auto& attach = maybeAttach.value();

  const auto& gl = mContext->gl;
  gl->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mGLName);
  if (mContext->IsWebGL2() && attachEnum == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
    mDepthAttachment.Set(gl, toAttach);
    mStencilAttachment.Set(gl, toAttach);
  } else {
    attach->Set(gl, toAttach);
  }
  InvalidateCaches();
}

JS::Value WebGLFramebuffer::GetAttachmentParameter(
    JSContext* cx, GLenum target, GLenum attachEnum, GLenum pname,
    ErrorResult* const out_error) {
  const auto maybeAttach = GetAttachPoint(attachEnum);
  if (!maybeAttach || attachEnum == LOCAL_GL_NONE) {
    mContext->ErrorInvalidEnum(
        "Can only query COLOR_ATTACHMENTi,"
        " DEPTH_ATTACHMENT, DEPTH_STENCIL_ATTACHMENT, or"
        " STENCIL_ATTACHMENT for a framebuffer.");
    return JS::NullValue();
  }
  auto attach = maybeAttach.value();

  if (mContext->IsWebGL2() && attachEnum == LOCAL_GL_DEPTH_STENCIL_ATTACHMENT) {
    // There are a couple special rules for this one.

    if (pname == LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE) {
      mContext->ErrorInvalidOperation(
          "Querying"
          " FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE"
          " against DEPTH_STENCIL_ATTACHMENT is an"
          " error.");
      return JS::NullValue();
    }

    if (mDepthAttachment.Renderbuffer() != mStencilAttachment.Renderbuffer() ||
        mDepthAttachment.Texture() != mStencilAttachment.Texture()) {
      mContext->ErrorInvalidOperation(
          "DEPTH_ATTACHMENT and STENCIL_ATTACHMENT"
          " have different objects bound.");
      return JS::NullValue();
    }

    attach = &mDepthAttachment;
  }

  return attach->GetParameter(mContext, cx, target, attachEnum, pname,
                              out_error);
}

////////////////////

static void GetBackbufferFormats(const WebGLContext* webgl,
                                 const webgl::FormatInfo** const out_color,
                                 const webgl::FormatInfo** const out_depth,
                                 const webgl::FormatInfo** const out_stencil) {
  const auto& options = webgl->Options();

  const auto effFormat = (options.alpha ? webgl::EffectiveFormat::RGBA8
                                        : webgl::EffectiveFormat::RGB8);
  *out_color = webgl::GetFormat(effFormat);

  *out_depth = nullptr;
  *out_stencil = nullptr;
  if (options.depth && options.stencil) {
    *out_depth = webgl::GetFormat(webgl::EffectiveFormat::DEPTH24_STENCIL8);
    *out_stencil = *out_depth;
  } else {
    if (options.depth) {
      *out_depth = webgl::GetFormat(webgl::EffectiveFormat::DEPTH_COMPONENT16);
    }
    if (options.stencil) {
      *out_stencil = webgl::GetFormat(webgl::EffectiveFormat::STENCIL_INDEX8);
    }
  }
}

/*static*/
void WebGLFramebuffer::BlitFramebuffer(WebGLContext* webgl, GLint srcX0,
                                       GLint srcY0, GLint srcX1, GLint srcY1,
                                       GLint dstX0, GLint dstY0, GLint dstX1,
                                       GLint dstY1, GLbitfield mask,
                                       GLenum filter) {
  const GLbitfield depthAndStencilBits =
      LOCAL_GL_DEPTH_BUFFER_BIT | LOCAL_GL_STENCIL_BUFFER_BIT;
  if (bool(mask & depthAndStencilBits) && filter == LOCAL_GL_LINEAR) {
    webgl->ErrorInvalidOperation(
        "DEPTH_BUFFER_BIT and STENCIL_BUFFER_BIT can"
        " only be used with NEAREST filtering.");
    return;
  }

  const auto& srcFB = webgl->mBoundReadFramebuffer;
  const auto& dstFB = webgl->mBoundDrawFramebuffer;

  ////
  // Collect data

  const auto fnGetFormat =
      [](const WebGLFBAttachPoint& cur,
         bool* const out_hasSamples) -> const webgl::FormatInfo* {
    const auto& imageInfo = cur.GetImageInfo();
    if (!imageInfo) return nullptr;  // No attachment.
    *out_hasSamples = bool(imageInfo->mSamples);
    return imageInfo->mFormat->format;
  };

  bool srcHasSamples = false;
  bool srcIsFilterable = true;
  const webgl::FormatInfo* srcColorFormat;
  const webgl::FormatInfo* srcDepthFormat;
  const webgl::FormatInfo* srcStencilFormat;
  gfx::IntSize srcSize;

  if (srcFB) {
    const auto& info = *srcFB->GetCompletenessInfo();
    if (info.zLayerCount != 1) {
      webgl->GenerateError(LOCAL_GL_INVALID_FRAMEBUFFER_OPERATION,
                           "Source framebuffer cannot have multiple views.");
      return;
    }
    srcColorFormat = nullptr;
    if (srcFB->mColorReadBuffer) {
      const auto& imageInfo = srcFB->mColorReadBuffer->GetImageInfo();
      if (imageInfo) {
        srcIsFilterable &= imageInfo->mFormat->isFilterable;
      }
      srcColorFormat = fnGetFormat(*(srcFB->mColorReadBuffer), &srcHasSamples);
    }
    srcDepthFormat = fnGetFormat(srcFB->DepthAttachment(), &srcHasSamples);
    srcStencilFormat = fnGetFormat(srcFB->StencilAttachment(), &srcHasSamples);
    MOZ_ASSERT(!srcFB->DepthStencilAttachment().HasAttachment());
    srcSize = {info.width, info.height};
  } else {
    srcHasSamples = false;  // Always false.

    GetBackbufferFormats(webgl, &srcColorFormat, &srcDepthFormat,
                         &srcStencilFormat);
    srcSize = webgl->DrawingBufferSize();
  }

  ////

  bool dstHasSamples = false;
  const webgl::FormatInfo* dstDepthFormat;
  const webgl::FormatInfo* dstStencilFormat;
  bool dstHasColor = false;
  bool colorFormatsMatch = true;
  bool colorTypesMatch = true;
  bool colorSrgbMatches = true;
  gfx::IntSize dstSize;

  const auto fnCheckColorFormat = [&](const webgl::FormatInfo* dstFormat) {
    MOZ_ASSERT(dstFormat->r || dstFormat->g || dstFormat->b || dstFormat->a);
    dstHasColor = true;
    colorFormatsMatch &= (dstFormat == srcColorFormat);
    colorTypesMatch &=
        srcColorFormat && (dstFormat->baseType == srcColorFormat->baseType);
    colorSrgbMatches &=
        srcColorFormat && (dstFormat->isSRGB == srcColorFormat->isSRGB);
  };

  if (dstFB) {
    for (const auto& cur : dstFB->mColorDrawBuffers) {
      const auto& format = fnGetFormat(*cur, &dstHasSamples);
      if (!format) continue;

      fnCheckColorFormat(format);
    }

    dstDepthFormat = fnGetFormat(dstFB->DepthAttachment(), &dstHasSamples);
    dstStencilFormat = fnGetFormat(dstFB->StencilAttachment(), &dstHasSamples);
    MOZ_ASSERT(!dstFB->DepthStencilAttachment().HasAttachment());

    const auto& info = *dstFB->GetCompletenessInfo();
    dstSize = {info.width, info.height};
  } else {
    dstHasSamples = webgl->Options().antialias;

    const webgl::FormatInfo* dstColorFormat;
    GetBackbufferFormats(webgl, &dstColorFormat, &dstDepthFormat,
                         &dstStencilFormat);

    fnCheckColorFormat(dstColorFormat);

    dstSize = webgl->DrawingBufferSize();
  }

  ////
  // Clear unused buffer bits

  if (mask & LOCAL_GL_COLOR_BUFFER_BIT && !srcColorFormat && !dstHasColor) {
    mask ^= LOCAL_GL_COLOR_BUFFER_BIT;
  }

  if (mask & LOCAL_GL_DEPTH_BUFFER_BIT && !srcDepthFormat && !dstDepthFormat) {
    mask ^= LOCAL_GL_DEPTH_BUFFER_BIT;
  }

  if (mask & LOCAL_GL_STENCIL_BUFFER_BIT && !srcStencilFormat &&
      !dstStencilFormat) {
    mask ^= LOCAL_GL_STENCIL_BUFFER_BIT;
  }

  ////
  // Validation

  if (dstHasSamples) {
    webgl->ErrorInvalidOperation(
        "DRAW_FRAMEBUFFER may not have multiple"
        " samples.");
    return;
  }

  bool requireFilterable = (filter == LOCAL_GL_LINEAR);
  if (srcHasSamples) {
    requireFilterable = false;  // It picks one.

    if (mask & LOCAL_GL_COLOR_BUFFER_BIT && dstHasColor && !colorFormatsMatch) {
      webgl->ErrorInvalidOperation(
          "Color buffer formats must match if"
          " selected, when reading from a multisampled"
          " source.");
      return;
    }

    if (dstX0 != srcX0 || dstX1 != srcX1 || dstY0 != srcY0 || dstY1 != srcY1) {
      webgl->ErrorInvalidOperation(
          "If the source is multisampled, then the"
          " source and dest regions must match exactly.");
      return;
    }
  }

  // -

  if (mask & LOCAL_GL_COLOR_BUFFER_BIT) {
    if (requireFilterable && !srcIsFilterable) {
      webgl->ErrorInvalidOperation(
          "`filter` is LINEAR and READ_BUFFER"
          " contains integer data.");
      return;
    }

    if (!colorTypesMatch) {
      webgl->ErrorInvalidOperation(
          "Color component types (float/uint/"
          "int) must match.");
      return;
    }
  }

  /* GLES 3.0.4, p199:
   *   Calling BlitFramebuffer will result in an INVALID_OPERATION error if
   *   mask includes DEPTH_BUFFER_BIT or STENCIL_BUFFER_BIT, and the source
   *   and destination depth and stencil buffer formats do not match.
   *
   * jgilbert: The wording is such that if only DEPTH_BUFFER_BIT is specified,
   * the stencil formats must match. This seems wrong. It could be a spec bug,
   * or I could be missing an interaction in one of the earlier paragraphs.
   */
  if (mask & LOCAL_GL_DEPTH_BUFFER_BIT && dstDepthFormat &&
      dstDepthFormat != srcDepthFormat) {
    webgl->ErrorInvalidOperation(
        "Depth buffer formats must match if selected.");
    return;
  }

  if (mask & LOCAL_GL_STENCIL_BUFFER_BIT && dstStencilFormat &&
      dstStencilFormat != srcStencilFormat) {
    webgl->ErrorInvalidOperation(
        "Stencil buffer formats must match if selected.");
    return;
  }

  ////
  // Check for feedback

  if (srcFB && dstFB) {
    const WebGLFBAttachPoint* feedback = nullptr;

    if (mask & LOCAL_GL_COLOR_BUFFER_BIT) {
      MOZ_ASSERT(srcFB->mColorReadBuffer->HasAttachment());
      for (const auto& cur : dstFB->mColorDrawBuffers) {
        if (srcFB->mColorReadBuffer->IsEquivalentForFeedback(*cur)) {
          feedback = cur;
          break;
        }
      }
    }

    if (mask & LOCAL_GL_DEPTH_BUFFER_BIT &&
        srcFB->DepthAttachment().IsEquivalentForFeedback(
            dstFB->DepthAttachment())) {
      feedback = &dstFB->DepthAttachment();
    }

    if (mask & LOCAL_GL_STENCIL_BUFFER_BIT &&
        srcFB->StencilAttachment().IsEquivalentForFeedback(
            dstFB->StencilAttachment())) {
      feedback = &dstFB->StencilAttachment();
    }

    if (feedback) {
      webgl->ErrorInvalidOperation(
          "Feedback detected into DRAW_FRAMEBUFFER's"
          " 0x%04x attachment.",
          feedback->mAttachmentPoint);
      return;
    }
  } else if (!srcFB && !dstFB) {
    webgl->ErrorInvalidOperation("Feedback with default framebuffer.");
    return;
  }

  // -

  const auto& gl = webgl->gl;
  const ScopedDrawCallWrapper wrapper(*webgl);
  gl->fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                       mask, filter);

  // -

  if (mask & LOCAL_GL_COLOR_BUFFER_BIT && !colorSrgbMatches && !gl->IsGLES() &&
      gl->Version() < 440) {
    // Mostly for Mac.
    // Remember, we have to filter in the *linear* format blit.

    // src -Blit-> fbB -DrawBlit-> fbC -Blit-> dst

    const auto fbB = gl::MozFramebuffer::Create(gl, {1, 1}, 0, false);
    const auto fbC = gl::MozFramebuffer::Create(gl, {1, 1}, 0, false);

    // -

    auto sizeBC = srcSize;
    GLenum formatC = LOCAL_GL_RGBA8;
    if (srcColorFormat->isSRGB) {
      // srgb -> linear
    } else {
      // linear -> srgb
      sizeBC = dstSize;
      formatC = LOCAL_GL_SRGB8_ALPHA8;
    }

    const auto fnSetTex = [&](const gl::MozFramebuffer& fb,
                              const GLenum format) {
      const gl::ScopedBindTexture bindTex(gl, fb.ColorTex());
      gl->fTexStorage2D(LOCAL_GL_TEXTURE_2D, 1, format, sizeBC.width,
                        sizeBC.height);
    };
    fnSetTex(*fbB, srcColorFormat->sizedFormat);
    fnSetTex(*fbC, formatC);

    // -

    {
      const gl::ScopedBindFramebuffer bindFb(gl);
      gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fbB->mFB);

      if (srcColorFormat->isSRGB) {
        // srgb -> linear
        gl->fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, srcX0, srcY0, srcX1,
                             srcY1, LOCAL_GL_COLOR_BUFFER_BIT,
                             LOCAL_GL_NEAREST);
      } else {
        // linear -> srgb
        gl->fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1,
                             dstY1, LOCAL_GL_COLOR_BUFFER_BIT, filter);
      }

      const auto& blitHelper = *gl->BlitHelper();
      gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fbC->mFB);
      blitHelper.DrawBlitTextureToFramebuffer(fbB->ColorTex(), sizeBC, sizeBC);
    }

    {
      const gl::ScopedBindFramebuffer bindFb(gl);
      gl->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, fbC->mFB);

      if (srcColorFormat->isSRGB) {
        // srgb -> linear
        gl->fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1,
                             dstY1, LOCAL_GL_COLOR_BUFFER_BIT, filter);
      } else {
        // linear -> srgb
        gl->fBlitFramebuffer(dstX0, dstY0, dstX1, dstY1, dstX0, dstY0, dstX1,
                             dstY1, LOCAL_GL_COLOR_BUFFER_BIT,
                             LOCAL_GL_NEAREST);
      }
    }
  }

  // -
  // glBlitFramebuffer ignores glColorMask!

  if (!webgl->mBoundDrawFramebuffer && webgl->mNeedsFakeNoAlpha) {
    if (!webgl->mScissorTestEnabled) {
      gl->fEnable(LOCAL_GL_SCISSOR_TEST);
    }
    if (webgl->mRasterizerDiscardEnabled) {
      gl->fDisable(LOCAL_GL_RASTERIZER_DISCARD);
    }
    const WebGLContext::ScissorRect dstRect = {
        std::min(dstX0, dstX1), std::min(dstY0, dstY1), abs(dstX1 - dstX0),
        abs(dstY1 - dstY0)};
    dstRect.Apply(*gl);
    gl->fClearColor(0, 0, 0, 1);

    webgl->DoColorMask(0x8);
    gl->fClear(LOCAL_GL_COLOR_BUFFER_BIT);

    if (!webgl->mScissorTestEnabled) {
      gl->fDisable(LOCAL_GL_SCISSOR_TEST);
    }
    if (webgl->mRasterizerDiscardEnabled) {
      gl->fEnable(LOCAL_GL_RASTERIZER_DISCARD);
    }
    webgl->mScissorRect.Apply(*gl);
    gl->fClearColor(webgl->mColorClearValue[0], webgl->mColorClearValue[1],
                    webgl->mColorClearValue[2], webgl->mColorClearValue[3]);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Goop.

JSObject* WebGLFramebuffer::WrapObject(JSContext* cx,
                                       JS::Handle<JSObject*> givenProto) {
  return dom::WebGLFramebuffer_Binding::Wrap(cx, this, givenProto);
}

inline void ImplCycleCollectionUnlink(mozilla::WebGLFBAttachPoint& field) {
  field.Unlink();
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback,
    const mozilla::WebGLFBAttachPoint& field, const char* name,
    uint32_t flags = 0) {
  CycleCollectionNoteChild(callback, field.Texture(), name, flags);
  CycleCollectionNoteChild(callback, field.Renderbuffer(), name, flags);
}

template <typename C>
inline void ImplCycleCollectionUnlink(C& field) {
  for (auto& cur : field) {
    cur.Unlink();
  }
}

template <typename C>
inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& callback, const C& field,
    const char* name, uint32_t flags = 0) {
  for (auto& cur : field) {
    ImplCycleCollectionTraverse(callback, cur, name, flags);
  }
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLFramebuffer, mDepthAttachment,
                                      mStencilAttachment,
                                      mDepthStencilAttachment,
                                      mColorAttachments)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(WebGLFramebuffer, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(WebGLFramebuffer, Release)

}  // namespace mozilla
