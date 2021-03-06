// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/mac/coremedia_glue.h"

#include <dlfcn.h>
#import <Foundation/Foundation.h>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"

namespace {

// This class is used to retrieve some CoreMedia library functions. It must be
// used as a LazyInstance so that it is initialised once and in a thread-safe
// way. Normally no work is done in constructors: LazyInstance is an exception.
class CoreMediaLibraryInternal {
 public:
  typedef CoreMediaGlue::CMTime (*CMTimeMakeMethod)(int64_t, int32_t);
  typedef Float64 (*CMTimeGetSecondsMethod)(CoreMediaGlue::CMTime);

  typedef CoreMediaGlue::CMTimeRange (
      *CMTimeRangeMakeMethod)(CoreMediaGlue::CMTime, CoreMediaGlue::CMTime);

  typedef OSStatus (*CMBlockBufferCreateContiguousMethod)(
      CFAllocatorRef,
      CoreMediaGlue::CMBlockBufferRef,
      CFAllocatorRef,
      const CoreMediaGlue::CMBlockBufferCustomBlockSource*,
      size_t,
      size_t,
      CoreMediaGlue::CMBlockBufferFlags,
      CoreMediaGlue::CMBlockBufferRef*);
  typedef size_t (*CMBlockBufferGetDataLengthMethod)(
      CoreMediaGlue::CMBlockBufferRef);
  typedef OSStatus (*CMBlockBufferGetDataPointerMethod)(
      CoreMediaGlue::CMBlockBufferRef,
      size_t,
      size_t*,
      size_t*,
      char**);
  typedef Boolean (*CMBlockBufferIsRangeContiguousMethod)(
      CoreMediaGlue::CMBlockBufferRef,
      size_t,
      size_t);

  typedef OSStatus (*CMBlockBufferCopyDataBytesMethod)(
      CoreMediaGlue::CMBlockBufferRef theSourceBuffer,
      size_t offsetToData,
      size_t dataLength,
      void* destination);
  typedef CoreMediaGlue::CMBlockBufferRef (*CMSampleBufferGetDataBufferMethod)(
      CoreMediaGlue::CMSampleBufferRef);
  typedef CoreMediaGlue::CMTime (*CMSampleBufferGetDurationMethod)(
      CoreMediaGlue::CMSampleBufferRef);
  typedef CoreMediaGlue::CMFormatDescriptionRef (
      *CMSampleBufferGetFormatDescriptionMethod)(
      CoreMediaGlue::CMSampleBufferRef);
  typedef CVImageBufferRef (*CMSampleBufferGetImageBufferMethod)(
      CoreMediaGlue::CMSampleBufferRef);
  typedef CoreMediaGlue::CMTime (*CMSampleBufferGetPresentationTimestampMethod)(
      CoreMediaGlue::CMSampleBufferRef);
  typedef CFArrayRef (*CMSampleBufferGetSampleAttachmentsArrayMethod)(
      CoreMediaGlue::CMSampleBufferRef,
      Boolean);
  typedef CoreMediaGlue::CMTime (*CMSampleBufferGetPresentationTimeStampMethod)(
      CoreMediaGlue::CMSampleBufferRef);

  typedef FourCharCode (*CMFormatDescriptionGetMediaSubTypeMethod)(
      CoreMediaGlue::CMFormatDescriptionRef desc);
  typedef CoreMediaGlue::CMVideoDimensions
      (*CMVideoFormatDescriptionGetDimensionsMethod)(
          CoreMediaGlue::CMVideoFormatDescriptionRef videoDesc);
  typedef const AudioFormatListItem* (
      *CMAudioFormatDescriptionGetRichestDecodableFormatMethod)(
      CoreMediaGlue::CMAudioFormatDescriptionRef desc);
  typedef CGRect (*CMVideoFormatDescriptionGetCleanApertureMethod)(
      CoreMediaGlue::CMVideoFormatDescriptionRef videoDesc,
      Boolean originIsAtTopLeft);
  typedef CGSize (*CMVideoFormatDescriptionGetPresentationDimensionsMethod)(
      CoreMediaGlue::CMVideoFormatDescriptionRef videoDesc,
      Boolean usePixelAspectRatio,
      Boolean useCleanAperture);
  typedef OSStatus (*CMVideoFormatDescriptionGetH264ParameterSetAtIndexMethod)(
      CoreMediaGlue::CMFormatDescriptionRef,
      size_t,
      const uint8_t**,
      size_t*,
      size_t*,
      int*);

  CoreMediaLibraryInternal() {
    NSBundle* bundle = [NSBundle
        bundleWithPath:@"/System/Library/Frameworks/CoreMedia.framework"];

    const char* path = [[bundle executablePath] fileSystemRepresentation];
    CHECK(path);
    void* library_handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    CHECK(library_handle) << dlerror();

    // Now extract the methods.
    cm_time_make_ = reinterpret_cast<CMTimeMakeMethod>(
        dlsym(library_handle, "CMTimeMake"));
    CHECK(cm_time_make_) << dlerror();

    cm_time_get_seconds_ = reinterpret_cast<CMTimeGetSecondsMethod>(
        dlsym(library_handle, "CMTimeGetSeconds"));
    CHECK(cm_time_get_seconds_) << dlerror();

    cm_time_range_make_ = reinterpret_cast<CMTimeRangeMakeMethod>(
        dlsym(library_handle, "CMTimeRangeMake"));
    CHECK(cm_time_range_make_) << dlerror();

    cm_block_buffer_copy_data_bytes_method_ =
        reinterpret_cast<CMBlockBufferCopyDataBytesMethod>(
            dlsym(library_handle, "CMBlockBufferCopyDataBytes"));
    CHECK(cm_block_buffer_copy_data_bytes_method_) << dlerror();
    cm_block_buffer_create_contiguous_method_ =
        reinterpret_cast<CMBlockBufferCreateContiguousMethod>(
            dlsym(library_handle, "CMBlockBufferCreateContiguous"));
    CHECK(cm_block_buffer_create_contiguous_method_) << dlerror();
    cm_block_buffer_get_data_length_method_ =
        reinterpret_cast<CMBlockBufferGetDataLengthMethod>(
            dlsym(library_handle, "CMBlockBufferGetDataLength"));
    CHECK(cm_block_buffer_get_data_length_method_) << dlerror();
    cm_block_buffer_get_data_pointer_method_ =
        reinterpret_cast<CMBlockBufferGetDataPointerMethod>(
            dlsym(library_handle, "CMBlockBufferGetDataPointer"));
    CHECK(cm_block_buffer_get_data_pointer_method_) << dlerror();
    cm_block_buffer_is_range_contiguous_method_ =
        reinterpret_cast<CMBlockBufferIsRangeContiguousMethod>(
            dlsym(library_handle, "CMBlockBufferIsRangeContiguous"));
    CHECK(cm_block_buffer_is_range_contiguous_method_) << dlerror();

    cm_sample_buffer_get_data_buffer_method_ =
        reinterpret_cast<CMSampleBufferGetDataBufferMethod>(
            dlsym(library_handle, "CMSampleBufferGetDataBuffer"));
    CHECK(cm_sample_buffer_get_data_buffer_method_) << dlerror();
    cm_sample_buffer_get_duration_method_ =
        reinterpret_cast<CMSampleBufferGetDurationMethod>(
            dlsym(library_handle, "CMSampleBufferGetDuration"));
    CHECK(cm_sample_buffer_get_duration_method_) << dlerror();
    cm_sample_buffer_get_format_description_method_ =
        reinterpret_cast<CMSampleBufferGetFormatDescriptionMethod>(
            dlsym(library_handle, "CMSampleBufferGetFormatDescription"));
    CHECK(cm_sample_buffer_get_format_description_method_) << dlerror();
    cm_sample_buffer_get_image_buffer_method_ =
        reinterpret_cast<CMSampleBufferGetImageBufferMethod>(
            dlsym(library_handle, "CMSampleBufferGetImageBuffer"));
    CHECK(cm_sample_buffer_get_image_buffer_method_) << dlerror();
    cm_sample_buffer_get_presentation_timestamp_method_ =
        reinterpret_cast<CMSampleBufferGetPresentationTimestampMethod>(
            dlsym(library_handle, "CMSampleBufferGetPresentationTimeStamp"));
    CHECK(cm_sample_buffer_get_presentation_timestamp_method_) << dlerror();
    cm_sample_buffer_get_sample_attachments_array_method_ =
        reinterpret_cast<CMSampleBufferGetSampleAttachmentsArrayMethod>(
            dlsym(library_handle, "CMSampleBufferGetSampleAttachmentsArray"));
    CHECK(cm_sample_buffer_get_sample_attachments_array_method_) << dlerror();
    cm_sample_buffer_get_presentation_timestamp_method_ =
        reinterpret_cast<CMSampleBufferGetPresentationTimeStampMethod>(
            dlsym(library_handle, "CMSampleBufferGetPresentationTimeStamp"));
    CHECK(cm_sample_buffer_get_presentation_timestamp_method_) << dlerror();
    k_cm_sample_attachment_key_not_sync_ = reinterpret_cast<CFStringRef*>(
        dlsym(library_handle, "kCMSampleAttachmentKey_NotSync"));
    CHECK(k_cm_sample_attachment_key_not_sync_) << dlerror();

    cm_format_description_get_media_sub_type_method_ =
        reinterpret_cast<CMFormatDescriptionGetMediaSubTypeMethod>(
            dlsym(library_handle, "CMFormatDescriptionGetMediaSubType"));
    CHECK(cm_format_description_get_media_sub_type_method_) << dlerror();
    cm_video_format_description_get_dimensions_method_ =
        reinterpret_cast<CMVideoFormatDescriptionGetDimensionsMethod>(
            dlsym(library_handle, "CMVideoFormatDescriptionGetDimensions"));
    CHECK(cm_video_format_description_get_dimensions_method_) << dlerror();

    cm_audio_format_description_get_richest_decodable_format_method_ =
        reinterpret_cast<
            CMAudioFormatDescriptionGetRichestDecodableFormatMethod>(
            dlsym(library_handle,
                  "CMAudioFormatDescriptionGetRichestDecodableFormat"));
    CHECK(cm_audio_format_description_get_richest_decodable_format_method_)
        << dlerror();

    cm_video_format_description_get_clean_aperture_method_ =
        reinterpret_cast<CMVideoFormatDescriptionGetCleanApertureMethod>(
            dlsym(library_handle, "CMVideoFormatDescriptionGetCleanAperture"));
    CHECK(cm_video_format_description_get_clean_aperture_method_) << dlerror();

    cm_video_format_description_get_presentation_dimensions_method_ =
        reinterpret_cast<
            CMVideoFormatDescriptionGetPresentationDimensionsMethod>(
            dlsym(library_handle,
                  "CMVideoFormatDescriptionGetPresentationDimensions"));
    CHECK(cm_video_format_description_get_presentation_dimensions_method_)
        << dlerror();

    // Available starting (OS X 10.9, iOS 7), allow to be null.
    cm_video_format_description_get_h264_parameter_set_at_index_method_ =
        reinterpret_cast<
            CMVideoFormatDescriptionGetH264ParameterSetAtIndexMethod>(
            dlsym(library_handle,
                  "CMVideoFormatDescriptionGetH264ParameterSetAtIndex"));
  }

  const CMTimeMakeMethod& cm_time_make() const { return cm_time_make_; }
  const CMTimeGetSecondsMethod& cm_time_get_seconds() const {
    return cm_time_get_seconds_;
  }

  const CMTimeRangeMakeMethod& cm_time_range_make() const {
    return cm_time_range_make_;
  }

  const CMBlockBufferCopyDataBytesMethod&
  cm_block_buffer_copy_data_bytes_method() const {
    return cm_block_buffer_copy_data_bytes_method_;
  }
  const CMBlockBufferCreateContiguousMethod&
  cm_block_buffer_create_contiguous_method() const {
    return cm_block_buffer_create_contiguous_method_;
  }
  const CMBlockBufferGetDataLengthMethod&
  cm_block_buffer_get_data_length_method() const {
    return cm_block_buffer_get_data_length_method_;
  }
  const CMBlockBufferGetDataPointerMethod&
  cm_block_buffer_get_data_pointer_method() const {
    return cm_block_buffer_get_data_pointer_method_;
  }
  const CMBlockBufferIsRangeContiguousMethod&
  cm_block_buffer_is_range_contiguous_method() const {
    return cm_block_buffer_is_range_contiguous_method_;
  }

  const CMSampleBufferGetDataBufferMethod&
  cm_sample_buffer_get_data_buffer_method() const {
    return cm_sample_buffer_get_data_buffer_method_;
  }
  const CMSampleBufferGetDurationMethod& cm_sample_buffer_get_duration_method()
      const {
    return cm_sample_buffer_get_duration_method_;
  }
  const CMSampleBufferGetFormatDescriptionMethod&
  cm_sample_buffer_get_format_description_method() const {
    return cm_sample_buffer_get_format_description_method_;
  }
  const CMSampleBufferGetImageBufferMethod&
      cm_sample_buffer_get_image_buffer_method() const {
    return cm_sample_buffer_get_image_buffer_method_;
  }
  const CMSampleBufferGetSampleAttachmentsArrayMethod&
  cm_sample_buffer_get_sample_attachments_array_method() const {
    return cm_sample_buffer_get_sample_attachments_array_method_;
  }
  const CMSampleBufferGetPresentationTimeStampMethod&
  cm_sample_buffer_get_presentation_timestamp_method() const {
    return cm_sample_buffer_get_presentation_timestamp_method_;
  }
  CFStringRef* const& k_cm_sample_attachment_key_not_sync() const {
    return k_cm_sample_attachment_key_not_sync_;
  }

  const CMFormatDescriptionGetMediaSubTypeMethod&
      cm_format_description_get_media_sub_type_method() const {
    return cm_format_description_get_media_sub_type_method_;
  }
  const CMVideoFormatDescriptionGetDimensionsMethod&
      cm_video_format_description_get_dimensions_method() const {
    return cm_video_format_description_get_dimensions_method_;
  }
  const CMAudioFormatDescriptionGetRichestDecodableFormatMethod&
  cm_audio_format_description_get_richest_decodable_format_method() const {
    return cm_audio_format_description_get_richest_decodable_format_method_;
  }
  const CMVideoFormatDescriptionGetCleanApertureMethod&
      cm_video_format_description_get_clean_aperture_method() const {
    return cm_video_format_description_get_clean_aperture_method_;
  }
  const CMVideoFormatDescriptionGetPresentationDimensionsMethod&
      cm_video_format_description_get_presentation_dimensions_method() const {
    return cm_video_format_description_get_presentation_dimensions_method_;
  }
  const CMVideoFormatDescriptionGetH264ParameterSetAtIndexMethod&
  cm_video_format_description_get_h264_parameter_set_at_index_method() const {
    return cm_video_format_description_get_h264_parameter_set_at_index_method_;
  }

 private:
  CMTimeMakeMethod cm_time_make_;
  CMTimeGetSecondsMethod cm_time_get_seconds_;

  CMTimeRangeMakeMethod cm_time_range_make_;

  CMBlockBufferCopyDataBytesMethod cm_block_buffer_copy_data_bytes_method_;
  CMBlockBufferCreateContiguousMethod cm_block_buffer_create_contiguous_method_;
  CMBlockBufferGetDataLengthMethod cm_block_buffer_get_data_length_method_;
  CMBlockBufferGetDataPointerMethod cm_block_buffer_get_data_pointer_method_;
  CMBlockBufferIsRangeContiguousMethod
      cm_block_buffer_is_range_contiguous_method_;

  CMSampleBufferGetDataBufferMethod cm_sample_buffer_get_data_buffer_method_;
  CMSampleBufferGetDurationMethod cm_sample_buffer_get_duration_method_;
  CMSampleBufferGetFormatDescriptionMethod
      cm_sample_buffer_get_format_description_method_;
  CMSampleBufferGetImageBufferMethod cm_sample_buffer_get_image_buffer_method_;
  CMSampleBufferGetSampleAttachmentsArrayMethod
      cm_sample_buffer_get_sample_attachments_array_method_;
  CFStringRef* k_cm_sample_attachment_key_not_sync_;

  CMFormatDescriptionGetMediaSubTypeMethod
      cm_format_description_get_media_sub_type_method_;
  CMVideoFormatDescriptionGetDimensionsMethod
      cm_video_format_description_get_dimensions_method_;
  CMAudioFormatDescriptionGetRichestDecodableFormatMethod
      cm_audio_format_description_get_richest_decodable_format_method_;
  CMVideoFormatDescriptionGetCleanApertureMethod
      cm_video_format_description_get_clean_aperture_method_;
  CMVideoFormatDescriptionGetPresentationDimensionsMethod
      cm_video_format_description_get_presentation_dimensions_method_;
  CMVideoFormatDescriptionGetH264ParameterSetAtIndexMethod
      cm_video_format_description_get_h264_parameter_set_at_index_method_;
  CMSampleBufferGetPresentationTimeStampMethod
      cm_sample_buffer_get_presentation_timestamp_method_;

  DISALLOW_COPY_AND_ASSIGN(CoreMediaLibraryInternal);
};

const CoreMediaGlue::CMTimeFlags kCMTimeFlags_Valid = 1UL << 0;
const CoreMediaGlue::CMTimeFlags kCMTimeFlags_PositiveInfinity = 1UL << 2;

}  // namespace

static base::LazyInstance<CoreMediaLibraryInternal> g_coremedia_handle =
    LAZY_INSTANCE_INITIALIZER;

// static
const CoreMediaGlue::CMTime CoreMediaGlue::kCMTimeZero = {0,
                                                          1,
                                                          kCMTimeFlags_Valid,
                                                          0};

// static
const CoreMediaGlue::CMTime CoreMediaGlue::kCMTimePositiveInfinity =
    {0, 0, kCMTimeFlags_Valid | kCMTimeFlags_PositiveInfinity, 0};

// static
CoreMediaGlue::CMTime CoreMediaGlue::CMTimeMake(int64_t value,
                                                int32_t timescale) {
  return g_coremedia_handle.Get().cm_time_make()(value, timescale);
}

// static
CoreMediaGlue::CMTimeRange CoreMediaGlue::CMTimeRangeMake(CMTime start,
                                                          CMTime duration) {
  return g_coremedia_handle.Get().cm_time_range_make()(start, duration);
}

// static
OSStatus CoreMediaGlue::CMBlockBufferCreateContiguous(
    CFAllocatorRef structureAllocator,
    CMBlockBufferRef sourceBuffer,
    CFAllocatorRef blockAllocator,
    const CMBlockBufferCustomBlockSource* customBlockSource,
    size_t offsetToData,
    size_t dataLength,
    CMBlockBufferFlags flags,
    CMBlockBufferRef* newBBufOut) {
  return g_coremedia_handle.Get().cm_block_buffer_create_contiguous_method()(
      structureAllocator,
      sourceBuffer,
      blockAllocator,
      customBlockSource,
      offsetToData,
      dataLength,
      flags,
      newBBufOut);
}

// static
size_t CoreMediaGlue::CMBlockBufferGetDataLength(CMBlockBufferRef theBuffer) {
  return g_coremedia_handle.Get().cm_block_buffer_get_data_length_method()(
      theBuffer);
}

// static
OSStatus CoreMediaGlue::CMBlockBufferGetDataPointer(CMBlockBufferRef theBuffer,
                                                    size_t offset,
                                                    size_t* lengthAtOffset,
                                                    size_t* totalLength,
                                                    char** dataPointer) {
  return g_coremedia_handle.Get().cm_block_buffer_get_data_pointer_method()(
      theBuffer, offset, lengthAtOffset, totalLength, dataPointer);
}

// static
Boolean CoreMediaGlue::CMBlockBufferIsRangeContiguous(
    CMBlockBufferRef theBuffer,
    size_t offset,
    size_t length) {
  return g_coremedia_handle.Get().cm_block_buffer_is_range_contiguous_method()(
      theBuffer, offset, length);
}

// static
OSStatus CoreMediaGlue::CMBlockBufferCopyDataBytes(
    CMBlockBufferRef theSourceBuffer,
    size_t offsetToData,
    size_t dataLength,
    void* destination) {
  return g_coremedia_handle.Get().cm_block_buffer_copy_data_bytes_method()(
      theSourceBuffer, offsetToData, dataLength, destination);
}

// static
CoreMediaGlue::CMBlockBufferRef CoreMediaGlue::CMSampleBufferGetDataBuffer(
    CMSampleBufferRef sbuf) {
  return g_coremedia_handle.Get().cm_sample_buffer_get_data_buffer_method()(
      sbuf);
}

// static
CoreMediaGlue::CMTime CoreMediaGlue::CMSampleBufferGetDuration(
    CMSampleBufferRef sbuf) {
  return g_coremedia_handle.Get().cm_sample_buffer_get_duration_method()(sbuf);
}

// static
CoreMediaGlue::CMFormatDescriptionRef
CoreMediaGlue::CMSampleBufferGetFormatDescription(
    CoreMediaGlue::CMSampleBufferRef sbuf) {
  return g_coremedia_handle.Get()
      .cm_sample_buffer_get_format_description_method()(sbuf);
}

// static
Float64 CoreMediaGlue::CMTimeGetSeconds(CoreMediaGlue::CMTime time) {
  return g_coremedia_handle.Get().cm_time_get_seconds()(time);
}

// static
CVImageBufferRef CoreMediaGlue::CMSampleBufferGetImageBuffer(
    CMSampleBufferRef buffer) {
  return g_coremedia_handle.Get().cm_sample_buffer_get_image_buffer_method()(
      buffer);
}

// static
CFArrayRef CoreMediaGlue::CMSampleBufferGetSampleAttachmentsArray(
    CMSampleBufferRef sbuf,
    Boolean createIfNecessary) {
  return g_coremedia_handle.Get()
      .cm_sample_buffer_get_sample_attachments_array_method()(
          sbuf, createIfNecessary);
}

// static
CoreMediaGlue::CMTime CoreMediaGlue::CMSampleBufferGetPresentationTimeStamp(
    CMSampleBufferRef sbuf) {
  return g_coremedia_handle.Get()
      .cm_sample_buffer_get_presentation_timestamp_method()(sbuf);
}

// static
CFStringRef CoreMediaGlue::kCMSampleAttachmentKey_NotSync() {
  return *g_coremedia_handle.Get().k_cm_sample_attachment_key_not_sync();
}

// static
FourCharCode CoreMediaGlue::CMFormatDescriptionGetMediaSubType(
      CMFormatDescriptionRef desc) {
  return g_coremedia_handle.Get()
      .cm_format_description_get_media_sub_type_method()(desc);
}

// static
CoreMediaGlue::CMVideoDimensions
    CoreMediaGlue::CMVideoFormatDescriptionGetDimensions(
        CMVideoFormatDescriptionRef videoDesc) {
  return g_coremedia_handle.Get()
      .cm_video_format_description_get_dimensions_method()(videoDesc);
}

// static
const AudioFormatListItem*
CoreMediaGlue::CMAudioFormatDescriptionGetRichestDecodableFormat(
    CMAudioFormatDescriptionRef desc) {
  return g_coremedia_handle.Get()
      .cm_audio_format_description_get_richest_decodable_format_method()(desc);
}

// static
CGRect CoreMediaGlue::CMVideoFormatDescriptionGetCleanAperture(
    CMVideoFormatDescriptionRef videoDesc,
    Boolean originIsAtTopLeft) {
  return g_coremedia_handle.Get()
      .cm_video_format_description_get_clean_aperture_method()(
          videoDesc, originIsAtTopLeft);
}

// static
CGSize CoreMediaGlue::CMVideoFormatDescriptionGetPresentationDimensions(
    CMVideoFormatDescriptionRef videoDesc,
    Boolean usePixelAspectRatio,
    Boolean useCleanAperture) {
  return g_coremedia_handle.Get()
      .cm_video_format_description_get_presentation_dimensions_method()(
          videoDesc, usePixelAspectRatio, useCleanAperture);
}

// static
OSStatus CoreMediaGlue::CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
    CMFormatDescriptionRef videoDesc,
    size_t parameterSetIndex,
    const uint8_t** parameterSetPointerOut,
    size_t* parameterSetSizeOut,
    size_t* parameterSetCountOut,
    int* NALUnitHeaderLengthOut) {
  return g_coremedia_handle.Get()
      .cm_video_format_description_get_h264_parameter_set_at_index_method()(
          videoDesc,
          parameterSetIndex,
          parameterSetPointerOut,
          parameterSetSizeOut,
          parameterSetCountOut,
          NALUnitHeaderLengthOut);
}
