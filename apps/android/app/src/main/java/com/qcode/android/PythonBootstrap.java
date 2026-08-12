package com.qcode.android;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/** Extracts bundled aarch64 python into filesDir/bin (+ libs) for BashTool. */
public final class PythonBootstrap {
    private static final String TAG = "PythonBootstrap";
    // Bump when the asset layout / RUNPATH packaging changes so we re-extract.
    private static final String STAMP = "python-rpath-origin-1";

    private PythonBootstrap() {}

    public static void install(Context context) {
        File stamp = new File(context.getFilesDir(), "bin/.qcode-python-stamp");
        File python = new File(context.getFilesDir(), "bin/python3");
        if (python.exists() && python.canExecute() && stamp.exists()
                && STAMP.equals(readStamp(stamp))) {
            ensureExecutableTree(new File(context.getFilesDir(), "lib"));
            return;
        }
        File binDir = new File(context.getFilesDir(), "bin");
        File libDir = new File(context.getFilesDir(), "lib");
        binDir.mkdirs();
        libDir.mkdirs();
        try {
            copyAssetTree(context.getAssets(), "python", context.getFilesDir());
            File extracted = new File(binDir, "python3");
            if (extracted.exists()) {
                //noinspection ResultOfMethodCallIgnored
                extracted.setExecutable(true, false);
            }
            File alias = new File(binDir, "python");
            if (!alias.exists() && extracted.exists()) {
                try (InputStream in = new java.io.FileInputStream(extracted);
                     OutputStream out = new FileOutputStream(alias)) {
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) >= 0) {
                        out.write(buf, 0, n);
                    }
                }
                //noinspection ResultOfMethodCallIgnored
                alias.setExecutable(true, false);
            }
            ensureExecutableTree(binDir);
            ensureExecutableTree(libDir);
            writeStamp(stamp, STAMP);
            Log.i(TAG, "Python bootstrap complete: " + extracted.getAbsolutePath());
        } catch (Exception e) {
            Log.w(TAG, "Python bootstrap skipped/failed", e);
        }
    }

    private static String readStamp(File stamp) {
        try (InputStream in = new java.io.FileInputStream(stamp)) {
            byte[] buf = new byte[128];
            int n = in.read(buf);
            return n > 0 ? new String(buf, 0, n).trim() : "";
        } catch (Exception e) {
            return "";
        }
    }

    private static void writeStamp(File stamp, String value) throws Exception {
        File parent = stamp.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (OutputStream out = new FileOutputStream(stamp)) {
            out.write(value.getBytes());
        }
    }

    private static void ensureExecutableTree(File root) {
        if (root == null || !root.exists()) {
            return;
        }
        File[] children = root.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (child.isDirectory()) {
                ensureExecutableTree(child);
            } else {
                // Shared libs and interpreters need owner rwx for Android's linker.
                //noinspection ResultOfMethodCallIgnored
                child.setReadable(true, false);
                //noinspection ResultOfMethodCallIgnored
                child.setExecutable(true, false);
            }
        }
    }

    private static void copyAssetTree(AssetManager assets, String src, File destRoot)
            throws Exception {
        String[] children = assets.list(src);
        if (children == null) {
            return;
        }
        if (children.length == 0) {
            // leaf file
            File outFile = new File(destRoot, src);
            File parent = outFile.getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            try (InputStream in = assets.open(src);
                 OutputStream out = new FileOutputStream(outFile)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0) {
                    out.write(buf, 0, n);
                }
            }
            return;
        }
        for (String child : children) {
            String childPath = src + "/" + child;
            String[] grand = assets.list(childPath);
            if (grand != null && grand.length > 0) {
                copyAssetTree(assets, childPath, destRoot);
            } else {
                String relative = childPath.substring("python/".length());
                File outFile = new File(destRoot, relative);
                File parent = outFile.getParentFile();
                if (parent != null) {
                    parent.mkdirs();
                }
                try (InputStream in = assets.open(childPath);
                     OutputStream out = new FileOutputStream(outFile)) {
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) >= 0) {
                        out.write(buf, 0, n);
                    }
                }
                if (relative.startsWith("bin/") || relative.startsWith("lib/")) {
                    //noinspection ResultOfMethodCallIgnored
                    outFile.setReadable(true, false);
                    //noinspection ResultOfMethodCallIgnored
                    outFile.setExecutable(true, false);
                }
            }
        }
    }
}
