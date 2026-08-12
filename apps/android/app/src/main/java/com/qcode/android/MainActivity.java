package com.qcode.android;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.webkit.JsPromptResult;
import android.webkit.JsResult;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {
  private final Handler mainHandler = new Handler(Looper.getMainLooper());
  private WebView webView;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);
    maybeRequestAllFilesAccess();
    // Creates Documents/notes + seeds tools/*.py and sample chapter when needed.
    QCodeBridge.init(getApplicationContext());
    webView = findViewById(R.id.webView);
    setupWebView();
    if (!QCodeBridge.isServerRunning()) {
      QCodeServerService.startService(this, 8080, NotesVaultBootstrap.DEFAULT_VAULT, "");
    }
    // Load UI once the local server is up.
    mainHandler.postDelayed(() -> {
      if (webView != null) {
        webView.loadUrl("http://127.0.0.1:8080");
      }
    }, 800L);
  }

  private void maybeRequestAllFilesAccess() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return;
    if (Environment.isExternalStorageManager()) return;
    try {
      Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
      intent.setData(Uri.parse("package:" + getPackageName()));
      startActivity(intent);
    } catch (Exception e) {
      try {
        Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
        startActivity(intent);
      } catch (Exception ignored) {
      }
    }
  }

  private void setupWebView() {
    WebSettings settings = webView.getSettings();
    settings.setJavaScriptEnabled(true);
    settings.setDomStorageEnabled(true);
    settings.setAllowFileAccess(true);
    settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
    settings.setAllowUniversalAccessFromFileURLs(false);
    // Needed so window.confirm / alert / prompt actually show (delete session, etc.).
    webView.setWebChromeClient(new WebChromeClient() {
      @Override
      public boolean onJsAlert(WebView view, String url, String message, JsResult result) {
        new AlertDialog.Builder(MainActivity.this)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, (d, w) -> result.confirm())
            .setOnCancelListener(d -> result.cancel())
            .show();
        return true;
      }

      @Override
      public boolean onJsConfirm(WebView view, String url, String message, JsResult result) {
        new AlertDialog.Builder(MainActivity.this)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, (d, w) -> result.confirm())
            .setNegativeButton(android.R.string.cancel, (d, w) -> result.cancel())
            .setOnCancelListener(d -> result.cancel())
            .show();
        return true;
      }

      @Override
      public boolean onJsPrompt(
          WebView view, String url, String message, String defaultValue, JsPromptResult result) {
        final EditText input = new EditText(MainActivity.this);
        input.setText(defaultValue == null ? "" : defaultValue);
        new AlertDialog.Builder(MainActivity.this)
            .setMessage(message)
            .setView(input)
            .setPositiveButton(android.R.string.ok, (d, w) -> result.confirm(input.getText().toString()))
            .setNegativeButton(android.R.string.cancel, (d, w) -> result.cancel())
            .setOnCancelListener(d -> result.cancel())
            .show();
        return true;
      }
    });
    webView.setWebViewClient(new WebViewClient());
    webView.addJavascriptInterface(new WebAppInterface(this), "QCodeAndroid");
  }

  @Override
  protected void onResume() {
    super.onResume();
    // Re-seed after the user may have granted All files access.
    NotesVaultBootstrap.ensure(getApplicationContext());
    if (QCodeBridge.isServerRunning() && webView != null) {
      final String url = webView.getUrl();
      if (url == null || url.equals("about:blank")) {
        webView.loadUrl("http://127.0.0.1:8080");
      }
    }
  }
}
