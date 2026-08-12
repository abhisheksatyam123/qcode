package com.qcode.android;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Ensures the notes vault exists and seeds local Python tools + sample study
 * materials under {@code Documents/notes/}.
 *
 * Important (Android scoped storage): files must be created by this app process
 * to be reliably listable/readable in the Files tab. External {@code adb push}
 * into the vault is often invisible to the app.
 */
public final class NotesVaultBootstrap {
  private static final String TAG = "NotesVault";
  public static final String DEFAULT_VAULT =
      Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
          + "/notes";

  private NotesVaultBootstrap() {}

  public static String ensure(Context context) {
    File vault = new File(DEFAULT_VAULT);
    if (!vault.exists() && !vault.mkdirs()) {
      Log.w(TAG, "Could not create vault at " + vault.getAbsolutePath());
    }
    File tools = new File(vault, "tools");
    if (!tools.exists() && !tools.mkdirs()) {
      Log.w(TAG, "Could not create tools dir at " + tools.getAbsolutePath());
    }
    // Always refresh tool scripts from assets so app updates ship new helpers.
    copyAssetDir(context, "notes_tools", tools);

    // Legacy sample (keep if already present / first install).
    File legacySample = new File(vault, "ganita-manjari/ch01/quiz.html");
    if (!legacySample.exists()) {
      copyAssetDir(context, "notes_sample/ganita-manjari",
          new File(vault, "ganita-manjari"));
      Log.i(TAG, "Seeded legacy sample at " + legacySample.getAbsolutePath());
    }

    // Canonical curriculum tree: classes/class-9/mathematics/...
    File curriculumChapter = new File(vault,
        "classes/class-9/mathematics/ch01-coordinates/quiz.json");
    if (!curriculumChapter.exists()) {
      copyAssetDir(context, "notes_sample/classes", new File(vault, "classes"));
      Log.i(TAG, "Seeded curriculum sample at " + curriculumChapter.getAbsolutePath());
    }

    File readme = new File(vault, "_home.md");
    if (!readme.exists()) {
      writeText(readme,
          "# Notes\n\n"
              + "This is your QCode study vault.\n\n"
              + "## Curriculum layout\n\n"
              + "Study material lives under `classes/`:\n\n"
              + "```text\n"
              + "classes/<class>/<subject>/<chapter>/\n"
              + "  content.md  quiz.json  quiz.html  _meta.json  _progress.json\n"
              + "classes/<class>/<subject>/quizzes/<overall-quiz>/\n"
              + "```\n\n"
              + "- Sample: `classes/class-9/mathematics/ch01-coordinates/`\n"
              + "- Status: `python3 tools/curriculum_status.py .`\n"
              + "- Scaffold: `python3 tools/scaffold_chapter.py --class 9 "
              + "--subject mathematics --slug ch02 --title \"...\"`\n"
              + "- Record attempt: `python3 tools/record_attempt.py "
              + "--path classes/.../ch01 --question-id q1 --correct 1 --topic ...`\n\n"
              + "Personal notes (`atomic/`, `journal/`, …) stay outside `classes/`.\n"
              + "`.md` opens as Markdown; `.html` opens interactively; `.pdf` uses the PDF viewer.\n");
    } else {
      // Soft-upgrade hint once: if curriculum exists but _home never mentions it.
      try {
        String existing = readText(readme);
        if (existing != null && !existing.contains("classes/")
            && new File(vault, "classes").isDirectory()) {
          writeText(readme, existing
              + "\n\n## Curriculum\n\n"
              + "Class/subject/chapter study tree: `classes/`. "
              + "Run `python3 tools/curriculum_status.py .` for gaps.\n");
        }
      } catch (Exception ignored) {
      }
    }
    Log.i(TAG, "Vault ready at " + vault.getAbsolutePath()
        + " (legacyExternal=" + Environment.isExternalStorageLegacy()
        + ", sdk=" + Build.VERSION.SDK_INT + ")");
    return vault.getAbsolutePath();
  }

  private static String readText(File file) {
    try (InputStream in = new java.io.FileInputStream(file)) {
      byte[] buf = new byte[(int) Math.min(file.length(), 200_000L)];
      int n = in.read(buf);
      return n > 0 ? new String(buf, 0, n, java.nio.charset.StandardCharsets.UTF_8) : "";
    } catch (Exception e) {
      return null;
    }
  }

  private static void copyAssetDir(Context context, String assetDir, File destDir) {
    try {
      String[] names = context.getAssets().list(assetDir);
      if (names == null) return;
      //noinspection ResultOfMethodCallIgnored
      destDir.mkdirs();
      for (String name : names) {
        String assetPath = assetDir + "/" + name;
        String[] children = context.getAssets().list(assetPath);
        if (children != null && children.length > 0) {
          copyAssetDir(context, assetPath, new File(destDir, name));
          continue;
        }
        File out = new File(destDir, name);
        try (InputStream in = context.getAssets().open(assetPath);
             OutputStream os = new FileOutputStream(out)) {
          byte[] buf = new byte[8192];
          int n;
          while ((n = in.read(buf)) >= 0) {
            os.write(buf, 0, n);
          }
        }
      }
    } catch (Exception e) {
      Log.w(TAG, "Failed copying assets from " + assetDir, e);
    }
  }

  private static void writeText(File file, String text) {
    try {
      File parent = file.getParentFile();
      if (parent != null) {
        //noinspection ResultOfMethodCallIgnored
        parent.mkdirs();
      }
      try (FileOutputStream out = new FileOutputStream(file)) {
        out.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
      }
    } catch (Exception e) {
      Log.w(TAG, "Failed writing " + file, e);
    }
  }
}
