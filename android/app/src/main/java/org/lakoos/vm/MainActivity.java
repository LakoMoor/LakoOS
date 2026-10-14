package org.lakoos.vm;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.net.*;

public class MainActivity extends Activity {
    static final String BASE = "https://github.com/LakoMoor/LakoOS/releases/latest/download/";
    TextView status;
    ProgressBar bar;
    Spinner arch, ram;
    CheckBox disk;
    Button start;
    String isoPath;
    Handler ui = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = (int) (16 * getResources().getDisplayMetrics().density);
        l.setPadding(p, p, p, p);
        l.setBackgroundColor(0xFF0A0043);
        TextView t = new TextView(this);
        t.setText("LakoOS VM");
        t.setTextSize(28); t.setTextColor(0xFFFFFFFF);
        l.addView(t);
        status = new TextView(this);
        status.setTextColor(0xFFCCCCFF);
        l.addView(status);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        l.addView(bar);
        arch = new Spinner(this);
        arch.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                new String[]{"arm64", "x86_64"}));
        l.addView(label("Architecture")); l.addView(arch);
        ram = new Spinner(this);
        ram.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                new String[]{"1024", "2048", "3072", "4096"}));
        ram.setSelection(1);
        l.addView(label("Memory (MB)")); l.addView(ram);
        disk = new CheckBox(this);
        disk.setText("Attach a 16 GB virtual disk (for installing)");
        disk.setTextColor(0xFFFFFFFF);
        l.addView(disk);
        Button pick = new Button(this);
        pick.setText("Choose an ISO file");
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.setType("*/*"); i.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(i, 1);
        });
        l.addView(pick);
        Button dl = new Button(this);
        dl.setText("Download the live image from GitHub");
        dl.setOnClickListener(v -> download());
        l.addView(dl);
        start = new Button(this);
        start.setText("Start LakoOS");
        start.setEnabled(false);
        start.setOnClickListener(v -> launch());
        l.addView(start);
        setContentView(new ScrollView(this) {{ addView(l); }});
        status.setText("Preparing the emulator...");
        new Thread(() -> {
            try {
                Qemu.install(this, () -> ui.post(() -> status.append(".")));
                ui.post(() -> { status.setText("Emulator ready. Choose an ISO."); check(); });
            } catch (IOException e) {
                ui.post(() -> status.setText("Could not unpack the emulator: " + e));
            }
        }).start();
        File f = new File(getExternalFilesDir(null), "lakoos.iso");
        if (f.exists()) { isoPath = f.getPath(); }
    }

    TextView label(String s) {
        TextView t = new TextView(this);
        t.setText(s); t.setTextColor(0xFFFFFFFF);
        return t;
    }

    void check() { start.setEnabled(isoPath != null && Qemu.installed(this)); }

    @Override protected void onActivityResult(int rq, int rc, Intent d) {
        if (rq != 1 || rc != RESULT_OK || d == null) return;
        Uri u = d.getData();
        File dst = new File(getExternalFilesDir(null), "lakoos.iso");
        status.setText("Copying the ISO...");
        new Thread(() -> {
            try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(dst)) {
                byte[] buf = new byte[1 << 16]; int n; long t = 0;
                while ((n = in.read(buf)) > 0) { o.write(buf, 0, n); t += n;
                    final long tt = t; ui.post(() -> status.setText("Copying the ISO: " + (tt >> 20) + " MB")); }
                isoPath = dst.getPath();
                ui.post(() -> { status.setText("ISO ready."); check(); });
            } catch (IOException e) { ui.post(() -> status.setText("Copy failed: " + e)); }
        }).start();
    }

    void download() {
        final String name = "LakoOS-" + (arch.getSelectedItemPosition() == 0 ? "arm64" : "amd64") + "-livecd.iso";
        File dst = new File(getExternalFilesDir(null), "lakoos.iso");
        new Thread(() -> {
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(BASE + name).openConnection();
                c.setInstanceFollowRedirects(true);
                c.setConnectTimeout(20000); c.setReadTimeout(30000);
                long len = c.getContentLengthLong();
                try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(dst)) {
                    byte[] buf = new byte[1 << 16]; int n; long t = 0;
                    while ((n = in.read(buf)) > 0) {
                        o.write(buf, 0, n); t += n;
                        final long tt = t; final long ll = len;
                        ui.post(() -> { status.setText("Downloading: " + (tt >> 20) + " MB"
                                + (ll > 0 ? " of " + (ll >> 20) : "")); if (ll > 0) bar.setProgress((int) (tt * 100 / ll)); });
                    }
                }
                isoPath = dst.getPath();
                ui.post(() -> { status.setText("Download complete."); check(); });
            } catch (IOException e) { ui.post(() -> status.setText("Download failed: " + e)); }
        }).start();
    }

    void launch() {
        try {
            String a = (String) arch.getSelectedItem();
            int mb = Integer.parseInt((String) ram.getSelectedItem());
            Qemu.start(this, a, isoPath, mb, Math.max(2, Runtime.getRuntime().availableProcessors()), disk.isChecked());
            Intent s = new Intent(this, QemuService.class);
            if (Build.VERSION.SDK_INT >= 26) startForegroundService(s); else startService(s);
            startActivity(new Intent(this, VncActivity.class));
        } catch (IOException e) { status.setText("Start failed: " + e); }
    }
}
