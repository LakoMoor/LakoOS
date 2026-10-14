package org.lakoos.vm;

import android.content.Context;
import java.io.*;
import java.util.*;
import java.util.zip.*;

public final class Qemu {
    public static final int VNC_PORT = 5900;
    static final int VERSION = 2;
    static Process proc;
    static final StringBuilder log = new StringBuilder();

    public static File home(Context c) { return new File(c.getFilesDir(), "qemu"); }

    public static boolean installed(Context c) { return new File(home(c), ".ok-" + VERSION).exists(); }

    public static void install(Context c, Runnable progress) throws IOException {
        File h = home(c);
        if (installed(c)) return;
        deleteTree(h);
        h.mkdirs();
        try (ZipInputStream z = new ZipInputStream(new BufferedInputStream(c.getAssets().open("qemu.zip"), 1 << 16))) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            int n = 0;
            while ((e = z.getNextEntry()) != null) {
                File f = new File(h, e.getName());
                if (e.isDirectory()) { f.mkdirs(); continue; }
                f.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(f)) {
                    int r;
                    while ((r = z.read(buf)) > 0) o.write(buf, 0, r);
                }
                if (e.getName().startsWith("bin/")) f.setExecutable(true, false);
                if ((++n % 10) == 0 && progress != null) progress.run();
            }
        }
        new File(h, ".ok-" + VERSION).createNewFile();
    }

    static void deleteTree(File f) {
        File[] k = f.listFiles();
        if (k != null) for (File x : k) deleteTree(x);
        f.delete();
    }

    public static synchronized boolean running() {
        if (proc == null) return false;
        try { proc.exitValue(); return false; } catch (IllegalThreadStateException e) { return true; }
    }

    public static synchronized void stop() {
        if (proc != null) { proc.destroy(); proc = null; }
    }

    public static synchronized String logText() { return log.toString(); }

    public static synchronized void start(Context c, String arch, String iso, int ramMb, int cpus, boolean useDisk, int xres, int yres) throws IOException {
        if (running()) return;
        File h = home(c);
        File data = c.getExternalFilesDir(null) != null ? c.getExternalFilesDir(null) : c.getFilesDir();
        File sh = new File(h, "share/qemu");
        List<String> a = new ArrayList<>();
        boolean arm = arch.equals("arm64");
        File vars = new File(data, arm ? "arm-vars.fd" : "x86-vars.fd");
        if (vars.exists() && vars.length() == 0) vars.delete();
        if (!vars.exists()) copy(new File(sh, arm ? "edk2-arm-vars.fd" : "edk2-i386-vars.fd"), vars);
        a.add(new File(h, arm ? "bin/qemu-system-aarch64" : "bin/qemu-system-x86_64").getPath());
        a.add("-L"); a.add(sh.getPath());
        if (arm) {
            a.addAll(Arrays.asList("-M", "virt", "-cpu", "max", "-accel", "tcg,thread=multi"));
            a.addAll(Arrays.asList("-drive", "if=pflash,format=raw,readonly=on,file=" + new File(sh, "edk2-aarch64-code.fd")));
            a.addAll(Arrays.asList("-drive", "if=pflash,format=raw,file=" + vars));
            a.addAll(Arrays.asList("-device", (xres <= 800 ? "ramfb" : "virtio-gpu-pci,xres=" + xres + ",yres=" + yres + ",romfile="), "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet"));
            a.addAll(Arrays.asList("-drive", "file=" + iso + ",if=none,id=cd,media=cdrom,readonly=on",
                    "-device", "usb-storage,drive=cd,bootindex=0"));
        } else {
            a.addAll(Arrays.asList("-machine", "q35", "-cpu", "max", "-accel", "tcg,thread=multi"));
            a.addAll(Arrays.asList("-drive", "if=pflash,format=raw,readonly=on,file=" + new File(sh, "edk2-x86_64-code.fd")));
            a.addAll(Arrays.asList("-drive", "if=pflash,format=raw,file=" + vars));
            a.addAll(Arrays.asList("-device", "VGA,xres=" + xres + ",yres=" + yres + ",romfile=", "-device", "qemu-xhci", "-device", "usb-kbd", "-device", "usb-tablet"));
            a.addAll(Arrays.asList("-drive", "file=" + iso + ",if=none,id=cd,media=cdrom,readonly=on",
                    "-device", "ide-cd,drive=cd,bus=ide.0,bootindex=0"));
        }
        if (useDisk) {
            File d = new File(data, "disk-" + arch + ".img");
            if (!d.exists()) try (RandomAccessFile r = new RandomAccessFile(d, "rw")) { r.setLength(16L << 30); }
            a.addAll(Arrays.asList("-drive", "file=" + d + ",if=none,id=hd,format=raw",
                    "-device", "nvme,drive=hd,serial=lako0,bootindex=1"));
        }
        a.addAll(Arrays.asList("-m", String.valueOf(ramMb), "-smp", String.valueOf(cpus),
                "-vnc", "127.0.0.1:0", "-nic", "user,model=e1000", "-rtc", "base=localtime"));
        ProcessBuilder pb = new ProcessBuilder(a);
        pb.environment().put("LD_LIBRARY_PATH", new File(h, "lib").getPath());
        pb.environment().put("TMPDIR", c.getCacheDir().getPath());
        pb.redirectErrorStream(true);
        log.setLength(0);
        log.append(a).append('\n');
        final Process p = pb.start();
        proc = p;
        new Thread(() -> {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String l;
                while ((l = r.readLine()) != null) synchronized (Qemu.class) {
                    if (log.length() < 20000) log.append(l).append('\n');
                }
            } catch (IOException ignored) {}
        }).start();
    }

    static void copy(File s, File d) throws IOException {
        try (InputStream i = new FileInputStream(s); OutputStream o = new FileOutputStream(d)) {
            byte[] b = new byte[1 << 16]; int n;
            while ((n = i.read(b)) > 0) o.write(b, 0, n);
        }
    }
}
