// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.webview_shell.test;

import android.os.Bundle;

import org.chromium.base.test.BaseInstrumentationTestRunner;

/**
 * Customized test runner for running instrumentation tests in WebViewBrowserTests.
 */
public class WebViewLayoutTestRunner extends BaseInstrumentationTestRunner {
    private String mModeArgument;
    private static final String MODE_REBASELINE = "rebaseline";

    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        if (arguments != null) {
            mModeArgument = arguments.getString("mode");
        }
    }

    public boolean isRebaseline() {
        return mModeArgument != null ? mModeArgument.equals(MODE_REBASELINE) : false;
    }
}
