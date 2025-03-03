/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SocketProcessBridgeChild.h"
#include "SocketProcessLogging.h"

#include "mozilla/dom/ContentChild.h"
#include "mozilla/net/NeckoChild.h"
#include "nsIObserverService.h"
#include "nsThreadUtils.h"
#include "mozilla/Preferences.h"

namespace mozilla {

using dom::ContentChild;

namespace net {

StaticRefPtr<SocketProcessBridgeChild>
    SocketProcessBridgeChild::sSocketProcessBridgeChild;

NS_IMPL_ISUPPORTS(SocketProcessBridgeChild, nsIObserver)

// static
bool SocketProcessBridgeChild::Create(
    Endpoint<PSocketProcessBridgeChild>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());

  sSocketProcessBridgeChild =
      new SocketProcessBridgeChild(std::move(aEndpoint));
  if (sSocketProcessBridgeChild->Inited()) {
    return true;
  }

  sSocketProcessBridgeChild = nullptr;
  return false;
}

// static
already_AddRefed<SocketProcessBridgeChild>
SocketProcessBridgeChild::GetSingleton() {
  RefPtr<SocketProcessBridgeChild> child = sSocketProcessBridgeChild;
  return child.forget();
}

static bool SocketProcessEnabled() {
  static bool sInited = false;
  static bool sSocketProcessEnabled = false;
  if (!sInited) {
    sSocketProcessEnabled = Preferences::GetBool("network.process.enabled") &&
                            XRE_IsContentProcess();
    sInited = true;
  }

  return sSocketProcessEnabled;
}

// static
RefPtr<SocketProcessBridgeChild::GetPromise>
SocketProcessBridgeChild::GetSocketProcessBridge() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!SocketProcessEnabled()) {
    return GetPromise::CreateAndReject(nsCString("Socket process disabled!"),
                                       __func__);
  }

  if (!gNeckoChild) {
    return GetPromise::CreateAndReject(nsCString("No NeckoChild!"), __func__);
  }

  if (sSocketProcessBridgeChild) {
    return GetPromise::CreateAndResolve(sSocketProcessBridgeChild, __func__);
  }

  ContentChild* content = ContentChild::GetSingleton();
  if (!content || content->IsShuttingDown()) {
    return nullptr;
  }

  return gNeckoChild->SendInitSocketProcessBridge()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [](NeckoChild::InitSocketProcessBridgePromise::ResolveOrRejectValue&&
             aResult) {
        ContentChild* content = ContentChild::GetSingleton();
        if (!content || content->IsShuttingDown()) {
          return GetPromise::CreateAndReject(
              nsCString("ContentChild is shutting down."), __func__);
        }
        if (!sSocketProcessBridgeChild) {
          if (aResult.IsReject()) {
            return GetPromise::CreateAndReject(
                nsCString("SendInitSocketProcessBridge failed"), __func__);
          }

          if (!aResult.ResolveValue().IsValid()) {
            return GetPromise::CreateAndReject(
                nsCString(
                    "SendInitSocketProcessBridge resolved with an invalid "
                    "endpoint!"),
                __func__);
          }

          if (!SocketProcessBridgeChild::Create(
                  std::move(aResult.ResolveValue()))) {
            return GetPromise::CreateAndReject(
                nsCString("SendInitSocketProcessBridge resolved with a valid "
                          "endpoint, "
                          "but SocketProcessBridgeChild::Create failed!"),
                __func__);
          }
        }

        return GetPromise::CreateAndResolve(sSocketProcessBridgeChild,
                                            __func__);
      });
}

SocketProcessBridgeChild::SocketProcessBridgeChild(
    Endpoint<PSocketProcessBridgeChild>&& aEndpoint)
    : mShuttingDown(false) {
  LOG(("CONSTRUCT SocketProcessBridgeChild::SocketProcessBridgeChild\n"));

  mInited = aEndpoint.Bind(this);
  if (!mInited) {
    MOZ_ASSERT(false, "Bind failed!");
    return;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->AddObserver(this, "content-child-shutdown", false);
  }

  mSocketProcessPid = aEndpoint.OtherPid();
}

SocketProcessBridgeChild::~SocketProcessBridgeChild() {
  LOG(("DESTRUCT SocketProcessBridgeChild::SocketProcessBridgeChild\n"));
}

mozilla::ipc::IPCResult SocketProcessBridgeChild::RecvTest() {
  LOG(("SocketProcessBridgeChild::RecvTest\n"));
  return IPC_OK();
}

void SocketProcessBridgeChild::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("SocketProcessBridgeChild::ActorDestroy\n"));
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->RemoveObserver(this, "content-child-shutdown");
  }
  MessageLoop::current()->PostTask(
      NewRunnableMethod("net::SocketProcessBridgeChild::DeferredDestroy", this,
                        &SocketProcessBridgeChild::DeferredDestroy));
  mShuttingDown = true;
}

NS_IMETHODIMP
SocketProcessBridgeChild::Observe(nsISupports* aSubject, const char* aTopic,
                                  const char16_t* aData) {
  if (!strcmp(aTopic, "content-child-shutdown")) {
    PSocketProcessBridgeChild::Close();
  }
  return NS_OK;
}

void SocketProcessBridgeChild::DeferredDestroy() {
  MOZ_ASSERT(NS_IsMainThread());

  sSocketProcessBridgeChild = nullptr;
}

}  // namespace net
}  // namespace mozilla
