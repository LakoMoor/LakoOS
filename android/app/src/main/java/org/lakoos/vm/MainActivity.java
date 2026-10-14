package org.lakoos.vm;

import android.app.Activity;
import android.content.Intent;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.net.*;

public class MainActivity extends Activity {
    static final String BASE = "https://github.com/LakoMoor/LakoOS/releases/download/v0.1.0/";
    static final int BG = 0xFFF3F3F3, CARD = 0xFFFFFFFF, ACCENT = 0xFF0067C0, MUTED = 0xFF5D5D5D, INK = 0xFF1A1A1A, LINE = 0xFFE0E0E0, SEG = 0xFFFFFFFF;
    TextView status, isoName;
    ProgressBar bar;
    Button start;
    String isoPath;
    int archIdx = 0, ramIdx = 1, resIdx = 0;
    Button[] resBtn = new Button[3];
    final int[][] RES = {{800, 600}, {1280, 720}, {1920, 1080}};
    Button[] archBtn = new Button[2], ramBtn = new Button[4];
    final int[] RAM = {1024, 2048, 3072, 4096};
    CheckBox disk;
    Handler ui = new Handler(Looper.getMainLooper());
    float d;

    int dp(int v) { return (int) (v * d + 0.5f); }

    GradientDrawable shape(int color, int radius) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color); g.setCornerRadius(dp(radius));
        if (color == CARD || color == SEG) g.setStroke(Math.max(1, dp(1)), LINE);
        return g;
    }

    LinearLayout card(LinearLayout parent) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(shape(CARD, 8));
        c.setPadding(dp(16), dp(14), dp(16), dp(16));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(12);
        parent.addView(c, lp);
        return c;
    }

    TextView text(String s, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s); t.setTextSize(size); t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    Button button(String s, boolean primary) {
        Button b = new Button(this);
        b.setText(s); b.setAllCaps(false); b.setTextSize(15);
        b.setTextColor(primary ? 0xFFFFFFFF : INK);
        b.setBackground(shape(primary ? ACCENT : SEG, 6));
        b.setStateListAnimator(null);
        return b;
    }

    void segment(LinearLayout row, Button[] btns, String[] labels, int sel, final java.util.function.IntConsumer on) {
        for (int i = 0; i < labels.length; i++) {
            final int idx = i;
            Button b = button(labels[i], i == sel);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(44), 1);
            lp.rightMargin = i < labels.length - 1 ? dp(8) : 0;
            row.addView(b, lp);
            btns[i] = b;
            b.setOnClickListener(v -> {
                for (int k = 0; k < btns.length; k++) {
                    btns[k].setBackground(shape(k == idx ? ACCENT : SEG, 6));
                    btns[k].setTextColor(k == idx ? 0xFFFFFFFF : INK);
                }
                on.accept(idx);
            });
        }
    }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        d = getResources().getDisplayMetrics().density;
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | (Build.VERSION.SDK_INT >= 26 ? View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR : 0));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(20), dp(28), dp(20), dp(20));

        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        View logo = new View(this) {
            @Override protected void onDraw(Canvas c) {
                Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                float u = getWidth() / 256f;
                p.setColor(0xFF7A5CFF); c.drawRect(28 * u, 28 * u, 124 * u, 124 * u, p);
                p.setColor(0xFF2F7BFF); c.drawRect(28 * u, 132 * u, 124 * u, 228 * u, p);
                p.setColor(0xFF14C4E0); c.drawRect(132 * u, 132 * u, 228 * u, 228 * u, p);
            }
        };
        head.addView(logo, new LinearLayout.LayoutParams(dp(44), dp(44)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(14), 0, 0, 0);
        titles.addView(text("LakoOS", 26, INK, true));
        titles.addView(text("Free NT operating system, running on your phone", 13, MUTED, false));
        head.addView(titles);
        root.addView(head);

        LinearLayout c1 = card(root);
        c1.addView(text("1  Choose a system image", 14, INK, true));
        isoName = text("No image selected", 17, INK, true);
        isoName.setPadding(0, dp(4), 0, dp(10));
        c1.addView(isoName);
        LinearLayout r1 = new LinearLayout(this);
        Button pick = button("Choose ISO", false);
        Button dl = button("Download", true);
        r1.addView(pick, new LinearLayout.LayoutParams(0, dp(46), 1));
        LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(0, dp(46), 1);
        dlp.leftMargin = dp(8);
        r1.addView(dl, dlp);
        c1.addView(r1);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100); bar.setVisibility(View.GONE);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(-1, dp(6));
        blp.topMargin = dp(10);
        c1.addView(bar, blp);

        LinearLayout c2 = card(root);
        c2.addView(text("2  Set up the virtual machine", 14, INK, true));
        TextView at = text("Architecture", 12, MUTED, false);
        at.setPadding(0, dp(10), 0, 0);
        c2.addView(at);
        LinearLayout r2 = new LinearLayout(this);
        r2.setPadding(0, dp(8), 0, 0);
        segment(r2, archBtn, new String[]{"ARM64", "x86-64"}, archIdx, i -> archIdx = i);
        c2.addView(r2);
        TextView mt = text("Memory", 12, MUTED, false);
        mt.setPadding(0, dp(14), 0, 0);
        c2.addView(mt);
        LinearLayout r3 = new LinearLayout(this);
        r3.setPadding(0, dp(8), 0, 0);
        segment(r3, ramBtn, new String[]{"1 GB", "2 GB", "3 GB", "4 GB"}, ramIdx, i -> ramIdx = i);
        c2.addView(r3);
        TextView rt = text("Resolution", 12, MUTED, false);
        rt.setPadding(0, dp(14), 0, 0);
        c2.addView(rt);
        LinearLayout r4 = new LinearLayout(this);
        r4.setPadding(0, dp(8), 0, 0);
        segment(r4, resBtn, new String[]{"1024×768", "1280×720", "1920×1080"}, resIdx, i -> resIdx = i);
        c2.addView(r4);
        disk = new CheckBox(this);
        disk.setText("Add a 16 GB virtual disk (to install)");
        disk.setTextColor(INK);
        disk.setPadding(0, dp(12), 0, 0);
        c2.addView(disk);

        status = text("Preparing the emulator...", 14, MUTED, false);
        status.setPadding(dp(4), dp(14), dp(4), dp(10));
        root.addView(status);
        start = button("Start LakoOS", true);
        start.setTextSize(16);
        start.setEnabled(false);
        start.setAlpha(0.5f);
        root.addView(start, new LinearLayout.LayoutParams(-1, dp(52)));

        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.setType("*/*"); i.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(i, 1);
        });
        dl.setOnClickListener(v -> download());
        start.setOnClickListener(v -> launch());
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.addView(root);
        setContentView(sv);

        File f = new File(getExternalFilesDir(null), "lakoos.iso");
        if (f.exists()) { isoPath = f.getPath(); isoName.setText("lakoos.iso (" + (f.length() >> 20) + " MB)"); }
        new Thread(() -> {
            try {
                Qemu.install(this, () -> ui.post(() -> status.append(".")));
                ui.post(() -> { status.setText("Emulator ready."); check(); });
            } catch (IOException e) {
                ui.post(() -> status.setText("Could not unpack the emulator: " + e));
            }
        }).start();
    }

    void check() {
        boolean ok = isoPath != null && Qemu.installed(this);
        start.setEnabled(ok);
        start.setAlpha(ok ? 1f : 0.5f);
    }

    @Override protected void onActivityResult(int rq, int rc, Intent dt) {
        if (rq != 1 || rc != RESULT_OK || dt == null) return;
        Uri u = dt.getData();
        File dst = new File(getExternalFilesDir(null), "lakoos.iso");
        bar.setVisibility(View.VISIBLE);
        new Thread(() -> {
            try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(dst)) {
                byte[] buf = new byte[1 << 16]; int n; long t = 0;
                while ((n = in.read(buf)) > 0) {
                    o.write(buf, 0, n); t += n;
                    final long tt = t;
                    ui.post(() -> status.setText("Copying the image: " + (tt >> 20) + " MB"));
                }
                isoPath = dst.getPath();
                final long sz = dst.length();
                ui.post(() -> { status.setText("Image ready."); isoName.setText("lakoos.iso (" + (sz >> 20) + " MB)");
                    bar.setVisibility(View.GONE); check(); });
            } catch (IOException e) { ui.post(() -> status.setText("Copy failed: " + e)); }
        }).start();
    }

    void download() {
        final String name = "LakoOS-" + (archIdx == 0 ? "arm64" : "amd64") + "-livecd.iso";
        final File dst = new File(getExternalFilesDir(null), "lakoos.iso");
        bar.setVisibility(View.VISIBLE);
        new Thread(() -> {
            String[] urls = {BASE + name, "https://github.com/LakoMoor/LakoOS/releases/latest/download/" + name};
            IOException last = null;
            for (String url : urls) {
                try {
                    HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                    c.setInstanceFollowRedirects(true);
                    c.setConnectTimeout(20000); c.setReadTimeout(30000);
                    final long len = c.getContentLengthLong();
                    try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(dst)) {
                        byte[] buf = new byte[1 << 16]; int n; long t = 0;
                        while ((n = in.read(buf)) > 0) {
                            o.write(buf, 0, n); t += n;
                            final long tt = t;
                            ui.post(() -> { status.setText("Downloading: " + (tt >> 20) + (len > 0 ? " of " + (len >> 20) : "") + " MB");
                                if (len > 0) bar.setProgress((int) (tt * 100 / len)); });
                        }
                    }
                    isoPath = dst.getPath();
                    final long sz = dst.length();
                    ui.post(() -> { status.setText("Download complete."); isoName.setText(name + " (" + (sz >> 20) + " MB)");
                        bar.setVisibility(View.GONE); check(); });
                    return;
                } catch (IOException e) { last = e; }
            }
            final IOException le = last;
            ui.post(() -> { status.setText("Download failed: " + le); bar.setVisibility(View.GONE); });
        }).start();
    }

    void launch() {
        try {
            Qemu.start(this, archIdx == 0 ? "arm64" : "x86_64", isoPath, RAM[ramIdx],
                    Math.max(2, Runtime.getRuntime().availableProcessors()), disk.isChecked(), RES[resIdx][0], RES[resIdx][1]);
            Intent s = new Intent(this, QemuService.class);
            if (Build.VERSION.SDK_INT >= 26) startForegroundService(s); else startService(s);
            startActivity(new Intent(this, VncActivity.class));
        } catch (IOException e) { status.setText("Start failed: " + e); }
    }
}
