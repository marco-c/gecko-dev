/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaElementAudioSourceNode.h"
#include "mozilla/dom/MediaElementAudioSourceNodeBinding.h"
#include "AudioDestinationNode.h"
#include "nsIScriptError.h"
#include "AudioNodeStream.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(MediaElementAudioSourceNode)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(MediaElementAudioSourceNode)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mElement)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END_INHERITED(AudioNode)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(MediaElementAudioSourceNode,
                                                  MediaStreamAudioSourceNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaElementAudioSourceNode)
NS_INTERFACE_MAP_END_INHERITING(MediaStreamAudioSourceNode)

NS_IMPL_ADDREF_INHERITED(MediaElementAudioSourceNode,
                         MediaStreamAudioSourceNode)
NS_IMPL_RELEASE_INHERITED(MediaElementAudioSourceNode,
                          MediaStreamAudioSourceNode)

MediaElementAudioSourceNode::MediaElementAudioSourceNode(
    AudioContext* aContext, HTMLMediaElement* aElement)
    : MediaStreamAudioSourceNode(aContext, TrackChangeBehavior::FollowChanges),
      mElement(aElement) {
  MOZ_ASSERT(aElement);
}

/* static */
already_AddRefed<MediaElementAudioSourceNode>
MediaElementAudioSourceNode::Create(
    AudioContext& aAudioContext, const MediaElementAudioSourceOptions& aOptions,
    ErrorResult& aRv) {
  if (aAudioContext.IsOffline()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  RefPtr<MediaElementAudioSourceNode> node =
      new MediaElementAudioSourceNode(&aAudioContext, aOptions.mMediaElement);

  RefPtr<DOMMediaStream> stream = aOptions.mMediaElement->CaptureAudio(
      aRv, aAudioContext.Destination()->Stream()->Graph());
  if (aRv.Failed()) {
    return nullptr;
  }

  node->Init(stream, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  node->ListenForAllowedToPlay(aOptions);
  return node.forget();
}

JSObject* MediaElementAudioSourceNode::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return MediaElementAudioSourceNode_Binding::Wrap(aCx, this, aGivenProto);
}

void MediaElementAudioSourceNode::ListenForAllowedToPlay(
    const MediaElementAudioSourceOptions& aOptions) {
  aOptions.mMediaElement->GetAllowedToPlayPromise()
      ->Then(
          GetAbstractMainThread(), __func__,
          // Capture by reference to bypass the mozilla-refcounted-inside-lambda
          // static analysis. We capture a non-owning reference so as to allow
          // cycle collection of the node. The reference is cleared via
          // DisconnectIfExists() from Destroy() when the node is collected.
          [& self = *this]() {
            self.Context()->StartBlockedAudioContextIfAllowed();
            self.mAllowedToPlayRequest.Complete();
          })
      ->Track(mAllowedToPlayRequest);
}

void MediaElementAudioSourceNode::Destroy() {
  mAllowedToPlayRequest.DisconnectIfExists();
  MediaStreamAudioSourceNode::Destroy();
}

HTMLMediaElement* MediaElementAudioSourceNode::MediaElement() {
  return mElement;
}

}  // namespace dom
}  // namespace mozilla
