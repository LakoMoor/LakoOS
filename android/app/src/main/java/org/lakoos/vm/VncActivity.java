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
        Button kb = new Button(this); kb.setText("Keyboard");
        kb.setOnClickListener(v -> {
            view.requestFocus();
            ((InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE)).toggleSoftInput(InputMethodManager.SHOW_FORCED, 0);
        });
        Button st = new Button(this); st.setText("Stop");
        st.setOnClickListener(v -> { Qemu.stop(); stopService(new android.content.Intent(this, QemuService.class)); finish(); });
        bar.addView(kb); bar.addView(st);
        root.addView(bar, new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.END));
        info = new TextView(this);
        info.setTextColor(0xFFFFFFFF);
        info.setText("Starting the virtual machine...");
        root.addView(info, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));
        setContentView(root);
        view.owner = this;
        view.connect();
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

        void close() { alive = false; try { if (sock != null) sock.close(); } catch (IOException ignored) {} }

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

        synchronized void send(byte[] b) {
            try { if (out != null) { out.write(b); out.flush(); } } catch (IOException ignored) {}
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

        void pointer(int mask, float x, float y) {
            if (fw == 0) return;
            RectF d = dest();
            int fx = (int) Math.max(0, Math.min(fw - 1, (x - d.left) * fw / d.width()));
            int fy = (int) Math.max(0, Math.min(fh - 1, (y - d.top) * fh / d.height()));
            send(new byte[]{5, (byte) mask, (byte) (fx >> 8), (byte) fx, (byte) (fy >> 8), (byte) fy});
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
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
            switch (e.getKeyCode()) {
                case KeyEvent.KEYCODE_ENTER: return 0xFF0D;
                case KeyEvent.KEYCODE_DEL: return 0xFF08;
                case KeyEvent.KEYCODE_TAB: return 0xFF09;
                case KeyEvent.KEYCODE_ESCAPE: return 0xFF1B;
                case KeyEvent.KEYCODE_DPAD_LEFT: return 0xFF51;
                case KeyEvent.KEYCODE_DPAD_UP: return 0xFF52;
                case KeyEvent.KEYCODE_DPAD_RIGHT: return 0xFF53;
                case KeyEvent.KEYCODE_DPAD_DOWN: return 0xFF54;
                case KeyEvent.KEYCODE_FORWARD_DEL: return 0xFFFF;
                case KeyEvent.KEYCODE_SHIFT_LEFT: return 0xFFE1;
                case KeyEvent.KEYCODE_CTRL_LEFT: return 0xFFE3;
                case KeyEvent.KEYCODE_ALT_LEFT: return 0xFFE9;
            }
            int u = e.getUnicodeChar();
            return u > 0 ? u : 0;
        }

        @Override public boolean onKeyDown(int k, KeyEvent e) {
            if (k == KeyEvent.KEYCODE_BACK) return false;
            int s = keysym(e);
            if (s != 0) { key(s, true); return true; }
            return super.onKeyDown(k, e);
        }

        @Override public boolean onKeyUp(int k, KeyEvent e) {
            if (k == KeyEvent.KEYCODE_BACK) return false;
            int s = keysym(e);
            if (s != 0) { key(s, false); return true; }
            return super.onKeyUp(k, e);
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
