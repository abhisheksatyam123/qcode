package com.qcode.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

public class QCodeBridge {
    public static final String DEFAULT_OPENROUTER_KEY = "";
    private static final String TAG = "QCodeBridge";
    private static boolean isLoaded;

    private static native boolean nativeInitWithNativeLibDir(String filesDir, String nativeLibDir,
                                                             String notesVault);
    private static native boolean nativeIsServerRunning();
    private static native String nativeRunPrompt(String prompt, String workspace,
                                                 String provider, String model);
    private static native void nativeSetEnvironmentKeys(String openrouter, String openai,
                                                        String anthropic, String groq);
    private static native boolean nativeStartServer(int port, String workspace, String webui);
    private static native boolean nativeStopServer();
    private static native boolean nativeWriteProviderConfig(
            String openrouter, String opencode, String openai,
            String anthropic, String groq, String ollamaHost);

    static {
        isLoaded = false;
        try {
            System.loadLibrary("qcode_jni");
            isLoaded = true;
            Log.i(TAG, "Native library libqcode_jni.so loaded successfully.");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load libqcode_jni.so", e);
        }
    }

    public static boolean init(Context context) {
        if (!isLoaded) {
            return false;
        }
        String filesDir = context.getFilesDir().getAbsolutePath();
        SharedPreferences prefs = context.getSharedPreferences("qcode_settings", 0);
        String openrouterKey = prefs.getString("OPENROUTER_API_KEY", DEFAULT_OPENROUTER_KEY);
        String opencodeKey = prefs.getString("OPENCODE_API_KEY", "");
        String openaiKey = prefs.getString("OPENAI_API_KEY", "");
        String anthropicKey = prefs.getString("ANTHROPIC_API_KEY", "");
        String groqKey = prefs.getString("GROQ_API_KEY", "");
        String ollamaHost = prefs.getString("OLLAMA_HOST", "http://localhost:11434");
        // Extract CA bundle + python stdlib before nativeInit so SSL/PYTHONHOME work.
        installAssetFile(context, "cacert.pem", new java.io.File(context.getFilesDir(), "cacert.pem"));
        PythonBootstrap.install(context);
        String notesVault = NotesVaultBootstrap.ensure(context);
        String nativeLibDir = context.getApplicationInfo().nativeLibraryDir;
        boolean ok = nativeInitWithNativeLibDir(
                filesDir, nativeLibDir != null ? nativeLibDir : "",
                notesVault != null ? notesVault : "");
        nativeWriteProviderConfig(openrouterKey, opencodeKey, openaiKey, anthropicKey, groqKey,
                ollamaHost);
        saveEnvironmentKeys(openrouterKey, openaiKey, anthropicKey, groqKey);
        return ok;
    }

    private static void installAssetFile(Context context, String assetName, java.io.File dest) {
        if (dest.exists() && dest.length() > 0) {
            return;
        }
        try {
            java.io.File parent = dest.getParentFile();
            if (parent != null) {
                //noinspection ResultOfMethodCallIgnored
                parent.mkdirs();
            }
            try (java.io.InputStream in = context.getAssets().open(assetName);
                 java.io.OutputStream out = new java.io.FileOutputStream(dest)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0) {
                    out.write(buf, 0, n);
                }
            }
            Log.i(TAG, "Installed asset " + assetName + " -> " + dest.getAbsolutePath());
        } catch (Exception e) {
            Log.w(TAG, "Failed to install asset " + assetName, e);
        }
    }

    public static void saveEnvironmentKeys(String openrouterKey, String openaiKey,
                                           String anthropicKey, String groqKey) {
        if (!isLoaded) {
            return;
        }
        nativeSetEnvironmentKeys(
                openrouterKey != null ? openrouterKey : "",
                openaiKey != null ? openaiKey : "",
                anthropicKey != null ? anthropicKey : "",
                groqKey != null ? groqKey : "");
    }

    public static boolean writeProviderConfig(Context context) {
        if (!isLoaded) {
            return false;
        }
        SharedPreferences prefs = context.getSharedPreferences("qcode_settings", 0);
        return nativeWriteProviderConfig(
                prefs.getString("OPENROUTER_API_KEY", DEFAULT_OPENROUTER_KEY),
                prefs.getString("OPENCODE_API_KEY", ""),
                prefs.getString("OPENAI_API_KEY", ""),
                prefs.getString("ANTHROPIC_API_KEY", ""),
                prefs.getString("GROQ_API_KEY", ""),
                prefs.getString("OLLAMA_HOST", "http://localhost:11434"));
    }

    public static boolean startServer(int port, String workspaceDir, String webuiDir) {
        return isLoaded && nativeStartServer(port, workspaceDir, webuiDir);
    }

    public static boolean stopServer() {
        return isLoaded && nativeStopServer();
    }

    public static boolean isServerRunning() {
        return isLoaded && nativeIsServerRunning();
    }

    public static String runPrompt(String prompt, String workspaceDir, String provider,
                                   String model) {
        if (!isLoaded) {
            return "{\"error\":\"Native library not loaded\"}";
        }
        return nativeRunPrompt(prompt, workspaceDir, provider, model);
    }
}
