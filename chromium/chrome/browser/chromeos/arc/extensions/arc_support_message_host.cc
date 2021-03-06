// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/extensions/arc_support_message_host.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/arc/arc_auth_service.h"
#include "chrome/browser/chromeos/arc/arc_support_host.h"

namespace arc {

// static
const char ArcSupportMessageHost::kHostName[] = "com.google.arc_support";

// static
const char* const ArcSupportMessageHost::kHostOrigin[] = {
    "chrome-extension://cnbgggchhmkkdmeppjobngjoejnihlei/"};

// static
std::unique_ptr<extensions::NativeMessageHost> ArcSupportMessageHost::Create() {
  return std::unique_ptr<NativeMessageHost>(new ArcSupportMessageHost());
}

ArcSupportMessageHost::ArcSupportMessageHost() = default;

ArcSupportMessageHost::~ArcSupportMessageHost() {
  // On shutdown, ArcAuthService may be already deleted. In that case,
  // ArcAuthService::Get() returns nullptr. Note that, ArcSupportHost
  // disconnects to this instance on shutdown already.
  ArcAuthService* auth_service = ArcAuthService::Get();
  if (auth_service) {
    DCHECK(auth_service->support_host());
    auth_service->support_host()->UnsetMessageHost(this);
  }
}

void ArcSupportMessageHost::SendMessage(const base::Value& message) {
  if (!client_)
    return;

  std::string message_string;
  base::JSONWriter::Write(message, &message_string);
  client_->PostMessageFromNativeHost(message_string);
}

void ArcSupportMessageHost::SetObserver(Observer* observer) {
  // We assume that the observer instance is only ArcSupportHost, which is
  // currently system unique. This is also used to reset the observere,
  // so |observer| xor |observer_| needs to be nullptr.
  DCHECK(!observer != !observer_);
  observer_ = observer;
}

void ArcSupportMessageHost::Start(Client* client) {
  DCHECK(!client_);
  client_ = client;

  ArcAuthService* auth_service = ArcAuthService::Get();
  DCHECK(auth_service);
  DCHECK(auth_service->support_host());
  auth_service->support_host()->SetMessageHost(this);
}

void ArcSupportMessageHost::OnMessage(const std::string& message_string) {
  if (!observer_)
    return;

  std::unique_ptr<base::Value> message_value =
      base::JSONReader::Read(message_string);
  base::DictionaryValue* message;
  if (!message_value || !message_value->GetAsDictionary(&message)) {
    NOTREACHED();
    return;
  }

  observer_->OnMessage(*message);
}

scoped_refptr<base::SingleThreadTaskRunner> ArcSupportMessageHost::task_runner()
    const {
  return base::ThreadTaskRunnerHandle::Get();
}

}  // namespace arc
