package com.qcode.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;
import androidx.core.app.NotificationCompat;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class QCodeServerService extends Service {
    public static final String ACTION_START_SERVER = "com.qcode.android.START_SERVER";
    public static final String ACTION_STOP_SERVER = "com.qcode.android.STOP_SERVER";
    private static final String CHANNEL_ID = "qcode_server_channel";
    public static final String EXTRA_PORT = "extra_port";
    public static final String EXTRA_WEBUI_DIR = "extra_webui_dir";
    public static final String EXTRA_WORKSPACE = "extra_workspace";
    private static final int NOTIFICATION_ID = 1001;
    private static final String TAG = "QCodeServerService";

    public static void startService(Context context, int port, String workspaceDir, String webuiDir) {
        Intent intent = new Intent(context, QCodeServerService.class);
        intent.setAction(ACTION_START_SERVER);
        intent.putExtra(EXTRA_PORT, port);
        intent.putExtra(EXTRA_WORKSPACE, workspaceDir);
        intent.putExtra(EXTRA_WEBUI_DIR, webuiDir);
        if (Build.VERSION.SDK_INT >= 26) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stopService(Context context) {
        Intent intent = new Intent(context, QCodeServerService.class);
        intent.setAction(ACTION_STOP_SERVER);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        QCodeBridge.init(getApplicationContext());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            return START_STICKY;
        }
        String action = intent.getAction();
        if (ACTION_START_SERVER.equals(action)) {
            final int port = intent.getIntExtra(EXTRA_PORT, 8080);
            String workspace = intent.getStringExtra(EXTRA_WORKSPACE);
            if (workspace == null || workspace.isEmpty()) {
                workspace = NotesVaultBootstrap.ensure(getApplicationContext());
            }
            String webui = intent.getStringExtra(EXTRA_WEBUI_DIR);
            if (webui == null || webui.isEmpty()) {
                webui = prepareWebUiAssets();
            }
            startForeground(NOTIFICATION_ID, buildNotification("QCode Server running on port " + port));
            final String finalWorkspace = workspace;
            final String finalWebui = webui;
            new Thread(() -> {
                boolean ok = QCodeBridge.startServer(port, finalWorkspace, finalWebui);
                Log.i(TAG, "QCode Server start result: " + ok);
            }, "qcode-server-start").start();
        } else if (ACTION_STOP_SERVER.equals(action)) {
            QCodeBridge.stopServer();
            stopForeground(true);
            stopSelf();
        }
        return START_STICKY;
    }

    private String prepareWebUiAssets() {
        File targetDir = new File(getFilesDir(), "webui");
        if (!targetDir.exists()) {
            targetDir.mkdirs();
        }
        copyAssetFolder("webui", targetDir);
        return targetDir.getAbsolutePath();
    }

    private void copyAssetFolder(String srcName, File targetDir) {
        AssetManager assetManager = getAssets();
        try {
            String[] files = assetManager.list(srcName);
            if (files == null) {
                return;
            }
            for (String filename : files) {
                try (InputStream in = assetManager.open(srcName + "/" + filename);
                     OutputStream out = new FileOutputStream(new File(targetDir, filename))) {
                    byte[] buffer = new byte[8192];
                    int read;
                    while ((read = in.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to copy assets", e);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "QCode Agent Server", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Runs local QCode agent HTTP service");
            NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification(String contentText) {
        Intent notificationIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this, 0, notificationIntent, PendingIntent.FLAG_IMMUTABLE);
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("QCode Android Service")
                .setContentText(contentText)
                .setSmallIcon(android.R.drawable.ic_menu_manage)
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .build();
    }
}
