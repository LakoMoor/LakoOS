package org.lakoos.vm;

import android.app.Activity;
import android.content.Context;
import android.graphics.*;
import android.os.*;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import java.io.*;
import java.net.Socket;

public class VncActivity extends Activity {
    VncView view;
    TextView info;
    Handler ui = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        FrameLayout root = new FrameLayout(this);
        view = new VncView(this);
        root.addView(view);
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        Button kb = new Button(this); kb.setText("Keyboard"); kb.setAllCaps(false);
        kb.setTextColor(0xFFFFFFFF);
        android.graphics.drawable.GradientDrawable g1 = new android.graphics.drawable.GradientDrawable();
        g1.setColor(0xCC202020); g1.setCornerRadius(24 * getResources().getDisplayMetrics().density);
        kb.setBackground(g1);
        kb.setOnClickListener(v -> {
            view.requestFocus();
            ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE)).toggleSoftInput(InputMethodManager.SHOW_FORCED, 0);
        });
        Button st = new Button(this); st.setText("Stop"); st.setAllCaps(false);
        st.setTextColor(0xFFFFFFFF);
        android.graphics.drawable.GradientDrawable g2 = new android.graphics.drawable.GradientDrawable();
        g2.setColor(0xCCC42B1C); g2.setCornerRadius(24 * getResources().getDisplayMetrics().density);
        st.setBackground(g2);
        st.setOnClickListener(v -> { Qemu.stop(); stopService(new android.content.Intent(this, QemuService.class)); finish(); });
        Button cap = new Button(this); cap.setText("Mouse"); cap.setAllCaps(false);
        cap.setTextColor(0xFFFFFFFF);
        android.graphics.drawable.GradientDrawable g3 = new android.graphics.drawable.GradientDrawable();
        g3.setColor(0xCC0067C0); g3.setCornerRadius(24 * getResources().getDisplayMetrics().density);
        cap.setBackground(g3);
        cap.setOnClickListener(v -> view.toggleCapture());
        Button tp = new Button(this); tp.setText("Touchpad"); tp.setAllCaps(false);
        tp.setTextColor(0xFFFFFFFF);
        android.graphics.drawable.GradientDrawable g4 = new android.graphics.drawable.GradientDrawable();
        g4.setColor(0xCC202020); g4.setCornerRadius(24 * getResources().getDisplayMetrics().density);
        tp.setBackground(g4);
        tp.setOnClickListener(v -> {
            view.touchpad = !view.touchpad;
            g4.setColor(view.touchpad ? 0xCC0067C0 : 0xCC202020);
            info.setVisibility(View.VISIBLE);
            info.setText(view.touchpad ? "Touchpad mode: drag to move, tap to click, two fingers to scroll or right-click"
                    : "Direct touch mode");
            ui.postDelayed(() -> info.setVisibility(View.GONE), 2500);
        });
        bar.addView(kb); bar.addView(tp); bar.addView(cap); bar.addView(st);
        root.addView(bar, new FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
        info = new TextView(this);
        info.setTextColor(0xFFFFFFFF);
        info.setText("Starting the virtual machine...");
        root.addView(info, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));
        setContentView(root);
        view.owner = this;
        view.connect();
        immersive();
    }

    void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override public void onWindowFocusChanged(boolean f) {
        super.onWindowFocusChanged(f);
        if (f) immersive();
        view.focusChanged(f);
    }

    void shown() { ui.post(() -> info.setVisibility(View.GONE)); }
    void message(String s) { ui.post(() -> { info.setVisibility(View.VISIBLE); info.setText(s); }); }

    @Override protected void onDestroy() { view.close(); super.onDestroy(); }

    static class VncView extends View {
        VncActivity owner;
        Bitmap bmp;
        int[] px;
        int fw, fh;
        volatile Socket sock;
        DataOutputStream out;
        volatile boolean alive = true;
        final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
        int buttons;

        VncView(Context c) { super(c); setFocusable(true); setFocusableInTouchMode(true); }

        void close() { alive = false; sender.shutdownNow(); try { if (sock != null) sock.close(); } catch (IOException ignored) {} }

        void connect() {
            new Thread(() -> {
                long t0 = SystemClock.uptimeMillis();
                while (alive) {
                    try {
                        Socket s = new Socket("127.0.0.1", Qemu.VNC_PORT);
                        sock = s;
                        run(s);
                    } catch (IOException e) {
                        if (!Qemu.running() && SystemClock.uptimeMillis() - t0 > 4000) {
                            owner.message("The virtual machine stopped.\n\n" + Qemu.logText());
                            return;
                        }
                    }
                    try { Thread.sleep(1000); } catch (InterruptedException ignored) {}
                }
            }).start();
        }

        void run(Socket s) throws IOException {
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream(), 1 << 16));
            out = new DataOutputStream(new BufferedOutputStream(s.getOutputStream()));
            byte[] ver = new byte[12];
            in.readFully(ver);
            out.write("RFB 003.008\n".getBytes()); out.flush();
            int n = in.readUnsignedByte();
            if (n == 0) return;
            byte[] types = new byte[n]; in.readFully(types);
            out.writeByte(1); out.flush();
            if (in.readInt() != 0) return;
            out.writeByte(1); out.flush();
            fw = in.readUnsignedShort(); fh = in.readUnsignedShort();
            in.skipBytes(16);
            int nl = in.readInt(); in.skipBytes(nl);
            out.writeByte(0); out.write(new byte[3]);
            out.writeByte(32); out.writeByte(24); out.writeByte(0); out.writeByte(1);
            out.writeShort(255); out.writeShort(255); out.writeShort(255);
            out.writeByte(16); out.writeByte(8); out.writeByte(0); out.write(new byte[3]);
            out.writeByte(2); out.writeByte(0); out.writeShort(1); out.writeInt(0);
            synchronized (this) { bmp = Bitmap.createBitmap(fw, fh, Bitmap.Config.ARGB_8888); px = new int[fw * fh]; }
            request(false);
            byte[] buf = new byte[0];
            while (alive) {
                int type = in.readUnsignedByte();
                if (type == 0) {
                    in.readUnsignedByte();
                    int rects = in.readUnsignedShort();
                    for (int r = 0; r < rects; r++) {
                        int x = in.readUnsignedShort(), y = in.readUnsignedShort();
                        int w = in.readUnsignedShort(), h = in.readUnsignedShort();
                        int enc = in.readInt();
                        if (enc != 0) throw new IOException("encoding " + enc);
                        int bytes = w * h * 4;
                        if (buf.length < bytes) buf = new byte[bytes];
                        in.readFully(buf, 0, bytes);
                        synchronized (this) {
                            for (int j = 0; j < h; j++) {
                                int bi = j * w * 4, di = (y + j) * fw + x;
                                for (int i = 0; i < w; i++, bi += 4)
                                    px[di + i] = 0xFF000000 | ((buf[bi + 2] & 0xFF) << 16) | ((buf[bi + 1] & 0xFF) << 8) | (buf[bi] & 0xFF);
                            }
                            bmp.setPixels(px, y * fw + x, fw, x, y, w, h);
                        }
                    }
                    owner.shown();
                    postInvalidate();
                    request(true);
                } else if (type == 2) {
                } else if (type == 3) {
                    in.skipBytes(3); int l = in.readInt(); in.skipBytes(l);
                } else if (type == 1) {
                    in.skipBytes(1); int first = in.readUnsignedShort(); int cnt = in.readUnsignedShort();
                    in.skipBytes(cnt * 6);
                } else throw new IOException("message " + type);
            }
        }

        final java.util.concurrent.ExecutorService sender = java.util.concurrent.Executors.newSingleThreadExecutor();

        void send(final byte[] b) {
            sender.execute(() -> {
                try {
                    DataOutputStream o = out;
                    if (o != null) synchronized (o) { o.write(b); o.flush(); }
                } catch (Exception ignored) {}
            });
        }

        void request(boolean inc) {
            byte[] m = new byte[10];
            m[0] = 3; m[1] = (byte) (inc ? 1 : 0);
            m[6] = (byte) (fw >> 8); m[7] = (byte) fw; m[8] = (byte) (fh >> 8); m[9] = (byte) fh;
            send(m);
        }

        RectF dest() {
            float s = Math.min((float) getWidth() / fw, (float) getHeight() / fh);
            float w = fw * s, h = fh * s;
            return new RectF((getWidth() - w) / 2, (getHeight() - h) / 2, (getWidth() + w) / 2, (getHeight() + h) / 2);
        }

        @Override protected void onDraw(Canvas c) {
            c.drawColor(0xFF000000);
            synchronized (this) {
                if (bmp != null) c.drawBitmap(bmp, null, dest(), paint);
            }
        }

        float cx, cy;       // pointer position in framebuffer pixels
        boolean captured;

        void pointerFb(int mask) {
            if (fw == 0) return;
            int fx = (int) Math.max(0, Math.min(fw - 1, cx));
            int fy = (int) Math.max(0, Math.min(fh - 1, cy));
            send(new byte[]{5, (byte) mask, (byte) (fx >> 8), (byte) fx, (byte) (fy >> 8), (byte) fy});
        }

        void pointer(int mask, float x, float y) {
            if (fw == 0) return;
            RectF d = dest();
            cx = (x - d.left) * fw / d.width();
            cy = (y - d.top) * fh / d.height();
            pointerFb(mask);
        }

        void toggleCapture() {
            if (captured) { releasePointerCapture(); captured = false; }
            else { requestFocus(); requestPointerCapture(); captured = true; }
        }

        void focusChanged(boolean f) {
            if (!f) captured = false;
        }

        int maskOf(MotionEvent e) {
            int b = e.getButtonState(), m = 0;
            if ((b & MotionEvent.BUTTON_PRIMARY) != 0) m |= 1;
            if ((b & MotionEvent.BUTTON_TERTIARY) != 0) m |= 2;
            if ((b & MotionEvent.BUTTON_SECONDARY) != 0) m |= 4;
            return m;
        }

        @Override public boolean onGenericMotionEvent(MotionEvent e) {
            if ((e.getSource() & InputDevice.SOURCE_CLASS_POINTER) != 0) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_HOVER_MOVE:
                    case MotionEvent.ACTION_HOVER_ENTER:
                        pointer(maskOf(e), e.getX(), e.getY());
                        return true;
                    case MotionEvent.ACTION_SCROLL: {
                        float v = e.getAxisValue(MotionEvent.AXIS_VSCROLL);
                        int wheel = v > 0 ? 8 : 16;
                        pointer(maskOf(e) | wheel, e.getX(), e.getY());
                        pointer(maskOf(e), e.getX(), e.getY());
                        return true;
                    }
                }
            }
            return super.onGenericMotionEvent(e);
        }

        @Override public boolean onCapturedPointerEvent(MotionEvent e) {
            if (e.getActionMasked() == MotionEvent.ACTION_SCROLL) {
                int wheel = e.getAxisValue(MotionEvent.AXIS_VSCROLL) > 0 ? 8 : 16;
                pointerFb(maskOf(e) | wheel);
                pointerFb(maskOf(e));
                return true;
            }
            cx += e.getX();
            cy += e.getY();
            cx = Math.max(0, Math.min(fw - 1, cx));
            cy = Math.max(0, Math.min(fh - 1, cy));
            pointerFb(maskOf(e));
            return true;
        }

        boolean touchpad;
        float tx, ty, tdx, tdy, scrollAcc;
        long tdown;
        boolean tmoved, tdrag, tpair;
        int tmax;
        final Runnable holdLeft = () -> {
            if (!tmoved && !tpair && tmax == 1) { tdrag = true; pointerFb(1); }
        };

        boolean touchpadEvent(MotionEvent e) {
            float gain = 1.6f * fw / Math.max(1f, getWidth());
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    tx = e.getX(); ty = e.getY(); tdx = tdy = 0; tmoved = tdrag = tpair = false;
                    tmax = 1; tdown = SystemClock.uptimeMillis(); scrollAcc = 0;
                    if (cx == 0 && cy == 0) { cx = fw / 2f; cy = fh / 2f; }
                    postDelayed(holdLeft, 450);
                    return true;
                case MotionEvent.ACTION_POINTER_DOWN:
                    tmax = Math.max(tmax, e.getPointerCount());
                    tpair = true;
                    tx = e.getX(); ty = e.getY();
                    removeCallbacks(holdLeft);
                    return true;
                case MotionEvent.ACTION_MOVE: {
                    float dx = e.getX() - tx, dy = e.getY() - ty;
                    tx = e.getX(); ty = e.getY();
                    tdx += dx; tdy += dy;
                    float slop = 8 * getResources().getDisplayMetrics().density;
                    if (Math.abs(tdx) + Math.abs(tdy) > slop) { tmoved = true; removeCallbacks(holdLeft); }
                    if (e.getPointerCount() >= 2) {
                        scrollAcc += dy;
                        float step = 24 * getResources().getDisplayMetrics().density;
                        while (scrollAcc > step) { pointerFb(16); pointerFb(0); scrollAcc -= step; }
                        while (scrollAcc < -step) { pointerFb(8); pointerFb(0); scrollAcc += step; }
                    } else {
                        cx = Math.max(0, Math.min(fw - 1, cx + dx * gain));
                        cy = Math.max(0, Math.min(fh - 1, cy + dy * gain));
                        pointerFb(tdrag ? 1 : 0);
                    }
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    removeCallbacks(holdLeft);
                    if (tdrag) {
                        pointerFb(0);
                    } else if (!tmoved && SystemClock.uptimeMillis() - tdown < 350
                            && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        int m = tmax >= 2 ? 4 : 1;
                        pointerFb(m); pointerFb(0);
                    }
                    tdrag = false;
                    return true;
            }
            return true;
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            if (touchpad && e.getToolType(0) != MotionEvent.TOOL_TYPE_MOUSE) return touchpadEvent(e);
            boolean mouse = (e.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                    || e.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE;
            if (mouse) {
                if (!captured && e.getActionMasked() == MotionEvent.ACTION_DOWN) { /* free pointer: absolute */ }
                pointer(maskOf(e), e.getX(), e.getY());
                return true;
            }
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) buttons = (e.getPointerCount() > 1) ? 4 : 1;
            if (a == MotionEvent.ACTION_POINTER_DOWN) buttons = 4;
            if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
                pointer(buttons, e.getX(), e.getY());
                pointer(0, e.getX(), e.getY());
                buttons = 0;
            } else pointer(buttons, e.getX(), e.getY());
            return true;
        }

        void key(int sym, boolean down) {
            send(new byte[]{4, (byte) (down ? 1 : 0), 0, 0, (byte) (sym >> 24), (byte) (sym >> 16), (byte) (sym >> 8), (byte) sym});
        }

        int keysym(KeyEvent e) {
            int k = e.getKeyCode();
            switch (k) {
                case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 0xFF0D;
                case KeyEvent.KEYCODE_DEL: return 0xFF08;
                case KeyEvent.KEYCODE_TAB: return 0xFF09;
                case KeyEvent.KEYCODE_ESCAPE: return 0xFF1B;
                case KeyEvent.KEYCODE_DPAD_LEFT: return 0xFF51;
                case KeyEvent.KEYCODE_DPAD_UP: return 0xFF52;
                case KeyEvent.KEYCODE_DPAD_RIGHT: return 0xFF53;
                case KeyEvent.KEYCODE_DPAD_DOWN: return 0xFF54;
                case KeyEvent.KEYCODE_PAGE_UP: return 0xFF55;
                case KeyEvent.KEYCODE_PAGE_DOWN: return 0xFF56;
                case KeyEvent.KEYCODE_MOVE_HOME: return 0xFF50;
                case KeyEvent.KEYCODE_MOVE_END: return 0xFF57;
                case KeyEvent.KEYCODE_INSERT: return 0xFF63;
                case KeyEvent.KEYCODE_FORWARD_DEL: return 0xFFFF;
                case KeyEvent.KEYCODE_SHIFT_LEFT: return 0xFFE1;
                case KeyEvent.KEYCODE_SHIFT_RIGHT: return 0xFFE2;
                case KeyEvent.KEYCODE_CTRL_LEFT: return 0xFFE3;
                case KeyEvent.KEYCODE_CTRL_RIGHT: return 0xFFE4;
                case KeyEvent.KEYCODE_CAPS_LOCK: return 0xFFE5;
                case KeyEvent.KEYCODE_ALT_LEFT: return 0xFFE9;
                case KeyEvent.KEYCODE_ALT_RIGHT: return 0xFFEA;
                case KeyEvent.KEYCODE_META_LEFT: return 0xFFEB;
                case KeyEvent.KEYCODE_META_RIGHT: return 0xFFEC;
                case KeyEvent.KEYCODE_MENU: return 0xFF67;
                case KeyEvent.KEYCODE_SYSRQ: return 0xFF61;
                case KeyEvent.KEYCODE_SCROLL_LOCK: return 0xFF14;
                case KeyEvent.KEYCODE_NUM_LOCK: return 0xFF7F;
            }
            if (k >= KeyEvent.KEYCODE_F1 && k <= KeyEvent.KEYCODE_F12) return 0xFFBE + (k - KeyEvent.KEYCODE_F1);
            int u = e.getUnicodeChar(e.getMetaState() & KeyEvent.META_SHIFT_MASK);
            return u > 0 ? u : 0;
        }

        boolean hw(KeyEvent e) {
            return e.getSource() != InputDevice.SOURCE_UNKNOWN || e.getDevice() != null && !e.getDevice().isVirtual();
        }

        @Override public boolean onKeyDown(int k, KeyEvent e) {
            int s = keysym(e);
            if (s != 0) { key(s, true); return true; }
            return k != KeyEvent.KEYCODE_BACK && k != KeyEvent.KEYCODE_HOME && super.onKeyDown(k, e);
        }

        @Override public boolean onKeyUp(int k, KeyEvent e) {
            int s = keysym(e);
            if (s != 0) { key(s, false); return true; }
            return k != KeyEvent.KEYCODE_BACK && k != KeyEvent.KEYCODE_HOME && super.onKeyUp(k, e);
        }

        @Override public boolean onCheckIsTextEditor() { return true; }

        @Override public android.view.inputmethod.InputConnection onCreateInputConnection(android.view.inputmethod.EditorInfo ei) {
            ei.inputType = android.text.InputType.TYPE_NULL;
            return new android.view.inputmethod.BaseInputConnection(this, false) {
                @Override public boolean commitText(CharSequence t, int p) {
                    for (int i = 0; i < t.length(); i++) { key(t.charAt(i), true); key(t.charAt(i), false); }
                    return true;
                }
            };
        }
    }
}
