package com.qcode.android;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.webkit.JavascriptInterface;

public class WebAppInterface {
  private final Context mContext;

  public WebAppInterface(Context context) {
    mContext = context;
  }

  @JavascriptInterface
  public void openSettings() {
    Intent intent = new Intent(mContext, SettingsActivity.class);
    if (!(mContext instanceof Activity)) {
      intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    }
    mContext.startActivity(intent);
  }

  @JavascriptInterface
  public boolean isServerRunning() {
    return QCodeBridge.isServerRunning();
  }

  /** Start or stop the embedded HTTP server. Returns true if running after toggle. */
  @JavascriptInterface
  public boolean toggleServer() {
    if (QCodeBridge.isServerRunning()) {
      QCodeServerService.stopService(mContext);
      return false;
    }
    QCodeServerService.startService(mContext, 8080, NotesVaultBootstrap.DEFAULT_VAULT, "");
    return true;
  }

  @JavascriptInterface
  public void startServer() {
    if (!QCodeBridge.isServerRunning()) {
      QCodeServerService.startService(mContext, 8080, NotesVaultBootstrap.DEFAULT_VAULT, "");
    }
  }
}
