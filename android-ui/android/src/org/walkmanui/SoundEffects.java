package org.walkmanui;

import android.media.audiofx.AudioEffect;
import android.util.Log;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.UUID;

// The NW-A55 image runs libwalkmanfx (android-ui/audiofx) on every music
// session. Its settings get there through one more instance of it, on the
// output mix, that is never enabled: the library shares them between its
// instances. AudioEffect's (type, uuid, priority, session) constructor and
// setParameter(byte[], byte[]) are hidden API on Android 5.1.
public final class SoundEffects {
    private static final UUID TYPE = UUID.fromString("f0463e98-f8c3-412b-891d-a188b922e2a0");
    private static final UUID EFFECT = UUID.fromString("7c3e4afc-7189-4cf4-9ad2-5c37724cd6c7");
    // WALKMANFX_PARAM_SETTINGS, little endian.
    private static final byte[] KEY = {0x57, 0x4d, 0x58, 0x31};
    private static boolean checked;
    private static boolean installed;
    private static AudioEffect control;
    private static Method setParameter;

    public static synchronized boolean apply(byte[] settings) {
        if (!checked) {
            checked = true;
            installed = installed();
            if (!installed) Log.i("WalkmanUI", "This system has no Walkman sound effects; their settings are not heard");
        }
        if (!installed) return false;
        // A second try with a new instance: mediaserver may have restarted under the old one.
        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                if (control == null) open();
                int status = (Integer) setParameter.invoke(control, KEY, settings);
                if (status == AudioEffect.SUCCESS) return true;
                Log.w("WalkmanUI", "The sound effects refused their settings: " + status);
            } catch (ReflectiveOperationException | RuntimeException error) {
                Log.w("WalkmanUI", "The sound effects cannot be reached", error);
            }
            close();
        }
        return false;
    }

    private static boolean installed() {
        try {
            AudioEffect.Descriptor[] effects = AudioEffect.queryEffects();
            if (effects != null)
                for (AudioEffect.Descriptor effect : effects)
                    if (EFFECT.equals(effect.uuid)) return true;
        } catch (RuntimeException error) {
            Log.w("WalkmanUI", "Cannot list the audio effects", error);
        }
        return false;
    }

    private static void open() throws ReflectiveOperationException {
        Constructor<AudioEffect> make = AudioEffect.class.getDeclaredConstructor(
            UUID.class, UUID.class, int.class, int.class);
        make.setAccessible(true);
        setParameter = AudioEffect.class.getMethod("setParameter", byte[].class, byte[].class);
        control = make.newInstance(TYPE, EFFECT, 0, 0);
    }

    private static void close() {
        if (control != null) control.release();
        control = null;
    }
}
