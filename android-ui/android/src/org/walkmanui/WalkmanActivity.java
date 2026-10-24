package org.walkmanui;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import org.qtproject.qt5.android.bindings.QtActivity;

// The Sony header takes the place of Android's status bar, as on the
// Walkman: the status bar stays hidden and a swipe down from the top edge
// shows it over the header for a moment (immersive sticky). The navigation
// bar stays, drawn black. Qt's showFullScreen() would hide it as well, so
// main.cpp shows the window maximized and the flags are set here, again on
// every focus gain because a dialog or the shade can clear them.
public final class WalkmanActivity extends QtActivity {
    private static final int SYSTEM_UI = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

    // vold mounts the card on its own time: its check takes seconds, longer on
    // an exFAT volume left dirty, and until then /storage/sdcard1 is an empty
    // folder that main.cpp's storage list leaves out. Not MEDIA_EJECT: a scan
    // started then would hold the card's files while vold unmounts it.
    private final BroadcastReceiver storages = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            try {
                nativeStoragesChanged();
            } catch (UnsatisfiedLinkError e) {
                // Qt's library is not loaded yet; main() reads /storage itself.
            }
        }
    };

    private static native void nativeStoragesChanged();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.setNavigationBarColor(Color.BLACK);
        window.getDecorView().setSystemUiVisibility(SYSTEM_UI);
        IntentFilter filter = new IntentFilter(Intent.ACTION_MEDIA_MOUNTED);
        filter.addAction(Intent.ACTION_MEDIA_UNMOUNTED);
        filter.addAction(Intent.ACTION_MEDIA_REMOVED);
        filter.addAction(Intent.ACTION_MEDIA_BAD_REMOVAL);
        filter.addDataScheme("file");
        registerReceiver(storages, filter);
    }

    @Override public void onDestroy() {
        unregisterReceiver(storages);
        super.onDestroy();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) getWindow().getDecorView().setSystemUiVisibility(SYSTEM_UI);
    }

    // The player's buttons arrive as media keys (probe/android/keylayout). Qt
    // turns KEYCODE_MEDIA_PLAY_PAUSE into Qt::Key_MediaPlay (androidjniinput.cpp),
    // so through Qt the play button could never pause; these go straight to the
    // playback commands instead, as PlaybackService's session would send them.
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        int command = mediaCommand(event.getKeyCode());
        if (command < 0) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) PlaybackService.send(command);
        return true;
    }

    private static int mediaCommand(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE: return 0;
            case KeyEvent.KEYCODE_MEDIA_NEXT: return 1;
            case KeyEvent.KEYCODE_MEDIA_PREVIOUS: return 2;
            case KeyEvent.KEYCODE_MEDIA_STOP: return 3;
            case KeyEvent.KEYCODE_MEDIA_PAUSE: return 4;
            case KeyEvent.KEYCODE_MEDIA_PLAY: return 5;
            default: return -1;
        }
    }
}
