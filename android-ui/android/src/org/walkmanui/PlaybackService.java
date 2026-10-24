package org.walkmanui;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

public final class PlaybackService extends Service {
    private static final String CHANNEL = "walkmanui_playback";
    private static final int NOTIFICATION = 41;
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static volatile boolean nativeReady;
    private static volatile boolean playing;
    private static volatile PlaybackService instance;
    private static volatile SessionData latest = new SessionData(false, false, "WalkmanUI", "", 0, 0);
    private static AudioManager audioManager;
    private static AudioFocusRequest focusRequest;
    private static boolean hasFocus;
    private static boolean resumeOnGain;
    private MediaSession session;
    private PowerManager.WakeLock wakeLock;
    private String notificationKey = "";
    private boolean receiverRegistered;

    private static native void nativeCommand(int command, long position);
    private static native void nativeFailure(String message);

    private static final class SessionData {
        final boolean active;
        final boolean playing;
        final String title;
        final String artist;
        final long duration;
        final long position;

        SessionData(boolean active, boolean playing, String title, String artist, long duration, long position) {
            this.active = active;
            this.playing = playing;
            this.title = title;
            this.artist = artist;
            this.duration = duration;
            this.position = position;
        }
    }

    private static final AudioManager.OnAudioFocusChangeListener FOCUS = change -> {
        synchronized (PlaybackService.class) {
            if (change == AudioManager.AUDIOFOCUS_GAIN) {
                hasFocus = true;
                if (resumeOnGain && nativeReady) nativeCommand(5, 0);
                resumeOnGain = false;
                return;
            }
            hasFocus = false;
            resumeOnGain = playing && change != AudioManager.AUDIOFOCUS_LOSS;
            if (nativeReady && playing) nativeCommand(4, 0);
        }
    };

    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction()) && nativeReady) {
                resumeOnGain = false;
                nativeCommand(4, 0);
            }
        }
    };

    public static void setNativeReady(boolean ready) { nativeReady = ready; }

    static void send(int command) { if (nativeReady) nativeCommand(command, 0); }

    public static synchronized boolean requestFocus(Context context) {
        if (hasFocus) return true;
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (audioManager == null) return false;
        int result;
        if (Build.VERSION.SDK_INT >= 26) {
            focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(FOCUS, MAIN).setWillPauseWhenDucked(true).build();
            result = audioManager.requestAudioFocus(focusRequest);
        } else {
            result = audioManager.requestAudioFocus(FOCUS, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        }
        hasFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        return hasFocus;
    }

    public static synchronized void releaseFocus() {
        resumeOnGain = false;
        if (audioManager != null) {
            if (Build.VERSION.SDK_INT >= 26 && focusRequest != null)
                audioManager.abandonAudioFocusRequest(focusRequest);
            else audioManager.abandonAudioFocus(FOCUS);
        }
        hasFocus = false;
    }

    public static void update(Context context, boolean active, boolean isPlaying, String title,
                              String artist, long duration, long position) {
        latest = new SessionData(active, isPlaying, title, artist, duration, position);
        playing = isPlaying;
        MAIN.post(() -> {
            try {
                if (!latest.active) {
                    releaseFocus();
                    if (instance != null) instance.stopSelf();
                } else if (instance != null) {
                    instance.applySession();
                } else {
                    Intent intent = new Intent(context, PlaybackService.class);
                    if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(intent);
                    else context.startService(intent);
                }
            } catch (RuntimeException error) {
                reportFailure(error);
            }
        });
    }

    private static void reportFailure(RuntimeException error) {
        Log.e("WalkmanUI", "Playback service failed", error);
        releaseFocus();
        if (nativeReady) nativeFailure("Android playback service failed: " + error.getMessage());
        if (instance != null) instance.stopSelf();
    }

    public static void shutdown(Context context) {
        nativeReady = false;
        playing = false;
        releaseFocus();
        context.stopService(new Intent(context, PlaybackService.class));
    }

    @Override public void onCreate() {
        super.onCreate();
        instance = this;
        try {
            initializeSession();
        } catch (RuntimeException error) {
            reportFailure(error);
        }
    }

    private void initializeSession() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager == null) throw new IllegalStateException("Notification service is unavailable");
            manager.createNotificationChannel(new NotificationChannel(CHANNEL, "Music playback", NotificationManager.IMPORTANCE_LOW));
        }
        session = new MediaSession(this, "WalkmanUI");
        session.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS | MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS);
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { command(5, 0); }
            @Override public void onPause() { command(4, 0); }
            @Override public void onSkipToNext() { command(1, 0); }
            @Override public void onSkipToPrevious() { command(2, 0); }
            @Override public void onStop() { command(3, 0); }
            @Override public void onSeekTo(long position) { command(6, position); }
        });
        session.setActive(true);
        PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
        if (power == null) throw new IllegalStateException("Power service is unavailable");
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "WalkmanUI:Playback");
        wakeLock.setReferenceCounted(false);
        registerReceiver(noisy, new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
        receiverRegistered = true;
        startForeground(NOTIFICATION, notification());
        applySession();
    }

    private void command(int value, long position) {
        if (nativeReady) nativeCommand(value, position);
    }

    private void applySession() {
        SessionData data = latest;
        if (!data.active) { stopSelf(); return; }
        long actions = PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
            | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS
            | PlaybackState.ACTION_SEEK_TO | PlaybackState.ACTION_STOP;
        session.setPlaybackState(new PlaybackState.Builder().setActions(actions)
            .setState(data.playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED,
                data.position, data.playing ? 1.0f : 0.0f).build());
        session.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE, data.title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, data.artist)
            .putLong(MediaMetadata.METADATA_KEY_DURATION, data.duration).build());
        if (data.playing && !wakeLock.isHeld()) wakeLock.acquire();
        if (!data.playing && wakeLock.isHeld()) wakeLock.release();
        String key = data.title + "\n" + data.artist + "\n" + data.playing;
        if (!key.equals(notificationKey)) {
            notificationKey = key;
            NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager != null) manager.notify(NOTIFICATION, notification());
        }
    }

    private PendingIntent actionIntent(int command) {
        Intent intent = new Intent(this, PlaybackService.class).setAction("command").putExtra("command", command);
        return PendingIntent.getService(this, command, intent, pendingFlags());
    }

    private int pendingFlags() {
        return PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0);
    }

    private Notification notification() {
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
            ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        builder.setSmallIcon(R.drawable.ic_stat_music).setContentTitle(latest.title).setContentText(latest.artist)
            .setVisibility(Notification.VISIBILITY_PUBLIC).setCategory(Notification.CATEGORY_TRANSPORT)
            .setOngoing(latest.playing).setShowWhen(false)
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_previous, "Previous", actionIntent(2)).build())
            .addAction(new Notification.Action.Builder(latest.playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play,
                latest.playing ? "Pause" : "Play", actionIntent(0)).build())
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_next, "Next", actionIntent(1)).build())
            .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1, 2));
        if (open != null) builder.setContentIntent(PendingIntent.getActivity(this, 0, open, pendingFlags()));
        return builder.build();
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        try {
            if (intent != null && "command".equals(intent.getAction())) {
                int value = intent.getIntExtra("command", -1);
                if (value >= 0 && value <= 5) command(value, 0);
                else Log.w("WalkmanUI", "Rejected invalid playback command");
            }
            if (session != null) applySession();
        } catch (RuntimeException error) {
            reportFailure(error);
        }
        return START_NOT_STICKY;
    }

    @Override public void onTaskRemoved(Intent rootIntent) {
        command(3, 0);
        stopSelf();
    }

    @Override public void onDestroy() {
        instance = null;
        if (receiverRegistered) unregisterReceiver(noisy);
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        if (session != null) session.release();
        stopForeground(true);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
