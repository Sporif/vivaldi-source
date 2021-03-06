// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/permission_context_base.h"

#include <stddef.h>
#include <utility>

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker.h"
#include "chrome/browser/permissions/permission_request.h"
#include "chrome/browser/permissions/permission_request_id.h"
#include "chrome/browser/permissions/permission_request_impl.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/permissions/permission_uma_util.h"
#include "chrome/browser/permissions/permission_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/origin_util.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "chrome/browser/permissions/permission_queue_controller.h"
#endif

#include "app/vivaldi_apptools.h"
#include "extensions/browser/guest_view/web_view/web_view_constants.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/guest_view/web_view/web_view_permission_helper.h"

using extensions::WebViewGuest;

// static
const char PermissionContextBase::kPermissionsKillSwitchFieldStudy[] =
    "PermissionsKillSwitch";
// static
const char PermissionContextBase::kPermissionsKillSwitchBlockedValue[] =
    "blocked";

PermissionContextBase::PermissionContextBase(
    Profile* profile,
    const content::PermissionType permission_type,
    const ContentSettingsType content_settings_type)
    : profile_(profile),
      permission_type_(permission_type),
      content_settings_type_(content_settings_type),
      weak_factory_(this) {
#if defined(OS_ANDROID)
  permission_queue_controller_.reset(new PermissionQueueController(
      profile_, permission_type_, content_settings_type_));
#endif
  PermissionDecisionAutoBlocker::UpdateFromVariations();
}

PermissionContextBase::~PermissionContextBase() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void PermissionContextBase::RequestPermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_frame,
    bool user_gesture,
    const BrowserPermissionCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // First check if this permission has been disabled.
  if (IsPermissionKillSwitchOn()) {
    // Log to the developer console.
    web_contents->GetMainFrame()->AddMessageToConsole(
        content::CONSOLE_MESSAGE_LEVEL_LOG,
        base::StringPrintf(
            "%s permission has been blocked.",
            PermissionUtil::GetPermissionString(permission_type_).c_str()));
    // The kill switch is enabled for this permission; Block all requests.
    callback.Run(CONTENT_SETTING_BLOCK);
    return;
  }

  GURL requesting_origin = requesting_frame.GetOrigin();
  GURL embedding_origin = web_contents->GetLastCommittedURL().GetOrigin();

  if (!requesting_origin.is_valid() || !embedding_origin.is_valid()) {
    std::string type_name =
        content_settings::WebsiteSettingsRegistry::GetInstance()
            ->Get(content_settings_type_)
            ->name();

    DVLOG(1) << "Attempt to use " << type_name
             << " from an invalid URL: " << requesting_origin << ","
             << embedding_origin << " (" << type_name
             << " is not supported in popups)";
    NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                        false /* persist */, CONTENT_SETTING_BLOCK);
    return;
  }

  ContentSetting content_setting =
      GetPermissionStatus(requesting_origin, embedding_origin);
  if (content_setting == CONTENT_SETTING_ALLOW) {
    HostContentSettingsMapFactory::GetForProfile(profile_)->UpdateLastUsage(
        requesting_origin, embedding_origin, content_settings_type_);
  }
  if (content_setting == CONTENT_SETTING_ALLOW ||
      content_setting == CONTENT_SETTING_BLOCK) {
    NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                        false /* persist */, content_setting);
    return;
  }

  PermissionUmaUtil::PermissionRequested(permission_type_, requesting_origin,
                                         embedding_origin, profile_);

  DecidePermission(web_contents, id, requesting_origin, embedding_origin,
                   user_gesture, callback);
}

ContentSetting PermissionContextBase::GetPermissionStatus(
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {

  // If the permission has been disabled through Finch, block all requests.
  if (IsPermissionKillSwitchOn())
    return CONTENT_SETTING_BLOCK;

  if (IsRestrictedToSecureOrigins() &&
      !content::IsOriginSecure(requesting_origin)) {
    return CONTENT_SETTING_BLOCK;
  }

  return HostContentSettingsMapFactory::GetForProfile(profile_)
      ->GetContentSetting(requesting_origin, embedding_origin,
                          content_settings_type_, std::string());
}

void PermissionContextBase::ResetPermission(
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetContentSettingDefaultScope(requesting_origin, embedding_origin,
                                      content_settings_type_, std::string(),
                                      CONTENT_SETTING_DEFAULT);
}

void PermissionContextBase::CancelPermissionRequest(
    content::WebContents* web_contents,
    const PermissionRequestID& id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  RemoveBridgeID(id.request_id());

  if (PermissionRequestManager::IsEnabled()) {
    PermissionRequest* cancelling = pending_requests_.get(id.ToString());
    if (cancelling != nullptr && web_contents != nullptr &&
        PermissionRequestManager::FromWebContents(web_contents) != nullptr) {
      PermissionRequestManager::FromWebContents(web_contents)
          ->CancelRequest(cancelling);
    }
  } else {
#if defined(OS_ANDROID)
    GetQueueController()->CancelInfoBarRequest(id);
#else
    NOTREACHED();
#endif
  }
}

void PermissionContextBase::DecidePermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // NOTE(yngve) extensions are not allowed to create webviews when running as
  // Vivaldi, so this is only non-null for Vivaldi, but adding check of Vivaldi
  // just to be on the safe side
  WebViewGuest *guest = WebViewGuest::FromWebContents(web_contents);
  if (guest && vivaldi::IsVivaldiRunning()) {
    extensions::WebViewPermissionHelper* web_view_permission_helper =
        extensions::WebViewPermissionHelper::FromWebContents(web_contents);
    WebViewPermissionType helper_permission_type =
        WEB_VIEW_PERMISSION_TYPE_UNKNOWN;
    switch(permission_type_) {
      case content::PermissionType::GEOLOCATION:
        helper_permission_type = WEB_VIEW_PERMISSION_TYPE_GEOLOCATION;
        break;
      case content::PermissionType::NOTIFICATIONS:
        helper_permission_type = WEB_VIEW_PERMISSION_TYPE_NOTIFICATION;
        break;
      default:
        break;
    }
    if (web_view_permission_helper &&
        helper_permission_type != WEB_VIEW_PERMISSION_TYPE_UNKNOWN) {
      base::DictionaryValue request_info;
      request_info.SetString(guest_view::kUrl, requesting_origin.spec());
      const extensions::WebViewPermissionHelper::PermissionResponseCallback
          permission_callback =
            base::Bind(&PermissionContextBase::OnPermissionRequestResponse,
                     weak_factory_.GetWeakPtr(), id, requesting_origin,
                     embedding_origin, user_gesture,
                     callback);
      int request_id = web_view_permission_helper->RequestPermission(
          helper_permission_type,
          request_info,
          permission_callback,
          false
          );

      bridge_id_to_request_id_map_[id.request_id()] = request_id;
    } else {
      NotifyPermissionSet(id,
                      requesting_origin,
                      web_contents->GetLastCommittedURL().GetOrigin(),
                      callback,
                      false /* persist */,
                      GetPermissionStatus(requesting_origin, embedding_origin));
    }
    return;
  }
  if (PermissionRequestManager::IsEnabled()) {
    PermissionRequestManager* permission_request_manager =
        PermissionRequestManager::FromWebContents(web_contents);
    // TODO(felt): sometimes |permission_request_manager| is null. This check is
    // meant to prevent crashes. See crbug.com/457091.
    if (!permission_request_manager)
      return;

    std::unique_ptr<PermissionRequest> request_ptr =
        base::MakeUnique<PermissionRequestImpl>(
            requesting_origin, permission_type_, profile_, user_gesture,
            base::Bind(&PermissionContextBase::PermissionDecided,
                       weak_factory_.GetWeakPtr(), id, requesting_origin,
                       embedding_origin, user_gesture, callback),
            base::Bind(&PermissionContextBase::CleanUpRequest,
                       weak_factory_.GetWeakPtr(), id));
    PermissionRequest* request = request_ptr.get();

    bool inserted =
        pending_requests_.add(id.ToString(), std::move(request_ptr)).second;
    DCHECK(inserted) << "Duplicate id " << id.ToString();
    permission_request_manager->AddRequest(request);
  } else {
#if defined(OS_ANDROID)
    GetQueueController()->CreateInfoBarRequest(
        id, requesting_origin, embedding_origin, user_gesture,
        base::Bind(&PermissionContextBase::PermissionDecided,
                   weak_factory_.GetWeakPtr(), id, requesting_origin,
                   embedding_origin, user_gesture, callback,
                   // the queue controller takes care of persisting the
                   // permission
                   false));
#else
    NOTREACHED();
#endif
  }
}

int PermissionContextBase::RemoveBridgeID(int bridge_id) {
  std::map<int, int>::iterator bridge_itr =
      bridge_id_to_request_id_map_.find(bridge_id);
  if (bridge_itr == bridge_id_to_request_id_map_.end())
    return webview::kInvalidPermissionRequestID;

  int request_id = bridge_itr->second;
  bridge_id_to_request_id_map_.erase(bridge_itr);
  return request_id;
}

void PermissionContextBase::OnPermissionRequestResponse(
        const PermissionRequestID& id,
        const GURL& requesting_origin,
        const GURL& embedding_origin,
        bool user_gesture,
        const BrowserPermissionCallback& callback,
        bool allowed,
        const std::string &user_input
    ) {
  RemoveBridgeID(id.request_id());
  PermissionDecided(id,
                    requesting_origin,
                    embedding_origin,
                    user_gesture,
                    callback,
                    true,
                    allowed ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
}

void PermissionContextBase::PermissionDecided(
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback,
    bool persist,
    ContentSetting content_setting) {
#if !defined(OS_ANDROID)
  // Infobar persistence and its related UMA is tracked on the infobar
  // controller directly.
  PermissionRequestGestureType gesture_type =
      user_gesture ? PermissionRequestGestureType::GESTURE
                   : PermissionRequestGestureType::NO_GESTURE;
  DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
         content_setting == CONTENT_SETTING_BLOCK ||
         content_setting == CONTENT_SETTING_DEFAULT);
  if (content_setting == CONTENT_SETTING_ALLOW) {
    PermissionUmaUtil::PermissionGranted(permission_type_, gesture_type,
                                         requesting_origin, profile_);
  } else if (content_setting == CONTENT_SETTING_BLOCK) {
    PermissionUmaUtil::PermissionDenied(permission_type_, gesture_type,
                                        requesting_origin, profile_);
  } else {
    PermissionUmaUtil::PermissionDismissed(permission_type_, gesture_type,
                                           requesting_origin, profile_);
  }
#endif

  // Check if we should convert a dismiss decision into a block decision. This
  // is gated on enabling the kBlockPromptsIfDismissedOften feature.
  if (content_setting == CONTENT_SETTING_DEFAULT &&
      PermissionDecisionAutoBlocker::ShouldChangeDismissalToBlock(
          requesting_origin, permission_type_, profile_)) {
    persist = true;
    content_setting = CONTENT_SETTING_BLOCK;
  }

  NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                      persist, content_setting);
}

#if defined(OS_ANDROID)
PermissionQueueController* PermissionContextBase::GetQueueController() {
  return permission_queue_controller_.get();
}
#endif

Profile* PermissionContextBase::profile() const {
  return profile_;
}

void PermissionContextBase::NotifyPermissionSet(
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    const BrowserPermissionCallback& callback,
    bool persist,
    ContentSetting content_setting) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (persist)
    UpdateContentSetting(requesting_origin, embedding_origin, content_setting);

  UpdateTabContext(id, requesting_origin,
                   content_setting == CONTENT_SETTING_ALLOW);

  if (content_setting == CONTENT_SETTING_DEFAULT)
    content_setting = CONTENT_SETTING_ASK;

  callback.Run(content_setting);
}

void PermissionContextBase::CleanUpRequest(const PermissionRequestID& id) {
  size_t success = pending_requests_.erase(id.ToString());
  DCHECK(success == 1) << "Missing request " << id.ToString();
}

void PermissionContextBase::UpdateContentSetting(
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    ContentSetting content_setting) {
  DCHECK_EQ(requesting_origin, requesting_origin.GetOrigin());
  DCHECK_EQ(embedding_origin, embedding_origin.GetOrigin());
  DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
         content_setting == CONTENT_SETTING_BLOCK);
  DCHECK(!requesting_origin.SchemeIsFile());
  DCHECK(!embedding_origin.SchemeIsFile());

  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetContentSettingDefaultScope(requesting_origin, embedding_origin,
                                      content_settings_type_, std::string(),
                                      content_setting);
}

bool PermissionContextBase::IsPermissionKillSwitchOn() const {
  const std::string param = variations::GetVariationParamValue(
      kPermissionsKillSwitchFieldStudy,
      PermissionUtil::GetPermissionString(permission_type_));

  return param == kPermissionsKillSwitchBlockedValue;
}
