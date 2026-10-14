package org.lakoos.vm;

import android.app.*;
import android.content.Intent;
import android.os.IBinder;

public class QemuService extends Service {
    @Override public int onStartCommand(Intent i, int f, int id) {
        Notification.Builder b;
        if (android.os.Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel("vm", "LakoOS VM", NotificationManager.IMPORTANCE_LOW);
            ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(ch);
            b = new Notification.Builder(this, "vm");
        } else b = new Notification.Builder(this);
        PendingIntent pi = PendingIntent.getActivity(this, 0, new Intent(this, VncActivity.class), 0);
        startForeground(1, b.setContentTitle("LakoOS is running").setSmallIcon(android.R.drawable.ic_media_play)
                .setContentIntent(pi).build());
        return START_NOT_STICKY;
    }
    @Override public void onDestroy() { Qemu.stop(); }
    @Override public IBinder onBind(Intent i) { return null; }
}
