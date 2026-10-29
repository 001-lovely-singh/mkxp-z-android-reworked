package com.hatkid.mkxpz;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputDevice;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Vibrator;
import android.os.VibrationEffect;
import android.os.storage.StorageManager;
import android.os.storage.OnObbStateChangeListener;
import android.net.Uri;
import android.provider.Settings;
import android.util.Log;
import android.widget.Toast;
import java.util.Locale;
import java.io.File;

import org.libsdl.app.SDLActivity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import com.anyplay.overlay.AnyPlayUi;

public class MainActivity extends SDLActivity
{
    // This activity inherits from SDLActivity activity.
    // Put your Java-side stuff here.

    private static final String TAG = "mkxp-z[Activity]";
    private static final String GAME_PATH_DEFAULT = Environment.getExternalStorageDirectory() + "/mkxp-z";
    private static String GAME_PATH = GAME_PATH_DEFAULT;
    private static String OBB_MAIN_FILENAME;
    private static boolean DEBUG = false;

    // ---- AnyPlay integration -------------------------------------------
    // AnyPlay starts this activity with action "com.anyplay.rgss.LAUNCH" and the
    // full game folder in the string extra "game_dir". When it is present we use
    // that folder INSTEAD of the fixed /storage/emulated/0/mkxp-z one. Opening
    // the app normally (launcher icon) still uses the default folder.
    private static final String EXTRA_GAME_DIR = "game_dir";
    private boolean mCustomGameDir = false;

    protected boolean mStarted = false;

    private StorageManager mStorageManager;

    // AnyPlay overlay (gamepad, keyboard, loading spinner, Close-game dialog)
    private AnyPlayUi mUi;
    private static MainActivity sInstance;

    private void runSDLThread()
    {
        if (!mStarted) {
            Log.i(TAG, "Game path: " + GAME_PATH);
        }

        mStarted = true;

        // Run (resume) native SDL thread
        if (mHasMultiWindow) {
            resumeNativeThread();
        }
    }

    OnObbStateChangeListener obbListener = new OnObbStateChangeListener()
    {
        @Override
        public void onObbStateChange(String path, int state)
        {
            super.onObbStateChange(path, state);

            Log.v(TAG, "OBB state of " + path + " changed to " + state);

            switch (state)
            {
                case OnObbStateChangeListener.MOUNTED:
                    String obbPath = mStorageManager.getMountedObbPath(path);
                    Log.v(TAG, "OBB " + path + " is mounted to " + obbPath);
                    GAME_PATH = obbPath;
                    break;

                case OnObbStateChangeListener.UNMOUNTED:
                    Log.v(TAG, "OBB " + path + " is unmounted");
                    GAME_PATH = GAME_PATH_DEFAULT;
                    break;

                default:
                    Log.e(TAG, "Failed to mount OBB " + path + ": Got state " + state);
                    break;
            }

            runSDLThread();
        }
    };

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
        if (requestCode == 110) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
                // Close the App because the User did not allow the all files access permission to be used.
                mSingleton.finish();
            }
        }
    }

    /**
     * Reads "game_dir" from the launching intent and, if valid, makes it the
     * game folder. The native side reads the static GAME_PATH field when the SDL
     * thread starts (see main.cpp), so this must run before that (onCreate).
     */
    private void applyGameDirFromIntent(Intent intent)
    {
        if (intent == null) return;

        String dir = intent.getStringExtra(EXTRA_GAME_DIR);
        if (dir == null || dir.isEmpty()) return;

        File f = new File(dir);
        if (!f.isDirectory()) {
            Log.e(TAG, "game_dir does not exist or is not a folder: " + dir);
            return; // fall back to the default folder; native side shows an error if that is missing too
        }

        GAME_PATH = f.getAbsolutePath();
        mCustomGameDir = true;
        Log.i(TAG, "Game folder from AnyPlay: " + GAME_PATH);
    }

    /**
     * The activity is singleTask. If it is still alive (user pressed Home) and
     * AnyPlay asks for a DIFFERENT game, Ruby cannot be re-initialised in the same
     * process, so we close and the next launch starts a fresh process.
     */
    @Override
    protected void onNewIntent(Intent intent)
    {
        super.onNewIntent(intent);

        String dir = intent.getStringExtra(EXTRA_GAME_DIR);
        if (dir != null && !dir.isEmpty() && !new File(dir).getAbsolutePath().equals(GAME_PATH)) {
            Toast.makeText(this, "Closing the running game. Open the new game again.", Toast.LENGTH_LONG).show();
            finish(); // onDestroy() exits the process, so the next launch is clean
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        // Must be before super.onCreate(): picks the game folder before SDL starts.
        applyGameDirFromIntent(getIntent());

        super.onCreate(savedInstanceState);

        mStorageManager = (StorageManager) getSystemService(STORAGE_SERVICE);

        // Get main OBB filepath
        final String obbPrefix = "main"; // "main", "patch"
        final int obbVersion = 1;
        OBB_MAIN_FILENAME = getObbDir() + "/" + obbPrefix + "." + obbVersion + "." + getPackageName() + ".obb";

        // Get Debug flag
        try {
            ActivityInfo actInfo = getPackageManager().getActivityInfo(this.getComponentName(), PackageManager.GET_META_DATA);
            DEBUG = actInfo.metaData.getBoolean("mkxp_debug");
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "Failed to set debug flag: " + e);
            e.printStackTrace();
        }

        // Check for all files access permission (Android 11+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                // Request all files access permission
                // TODO: AlertDialog: polite notice that mkxp-z requires All Files Access permission.
                Uri uri = Uri.parse("package:" + getPackageName());
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, uri);
                startActivityForResult(intent, 110);
            }
        }

        hideSystemUi();

        // AnyPlay overlay on top of the game surface
        sInstance = this;
        if (mLayout != null) {
            mUi = new AnyPlayUi(this, mLayout, GAME_PATH);
        }
    }

    /**
     * Called from native code (graphics.cpp) when the game draws its first frame.
     * Removes the loading spinner.
     */
    @SuppressWarnings("unused")
    public static void onGameFirstFrame()
    {
        MainActivity a = sInstance;
        if (a != null && a.mUi != null) a.mUi.onFirstFrame();
    }

    /**
     * Real full screen: hides the status bar and the gesture/navigation bar
     * (they reappear for a moment when the user swipes from the edge) and draws
     * under the camera notch. Without this the game area was cut at the top/bottom.
     */
    private void hideSystemUi()
    {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus)
    {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @Override
    protected void onPause()
    {
        if (mUi != null) mUi.onPause();
        super.onPause();
    }

    @Override
    protected void onStart()
    {
        super.onStart();

        if (!mStarted) {
            // Check for main OBB file
            if (!mCustomGameDir && new File(OBB_MAIN_FILENAME).exists()) {
                Log.v(TAG, "Main OBB file found, starting with main OBB mount");

                // Try to mount main OBB file
                mStorageManager.mountObb(OBB_MAIN_FILENAME, null, obbListener);
            } else {
                Log.v(TAG, "Main OBB file not found, starting without main OBB mount");

                // Run from default game directory
                runSDLThread();
            }
        } else {
            // onStart: Resume SDL thread
            runSDLThread();
        }
    }

    @Override
    protected void onDestroy()
    {
        if (mUi != null) mUi.onDestroy();
        sInstance = null;
        super.onDestroy();

        // HACK: Exiting the JVM (process) since Ruby does not likes when we
        // trying to re-initialize Ruby VM in mkxp-z (JNI native library)
        // that leads to segmentation fault, even we have cleanup the Ruby VM.
        System.exit(0);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent evt)
    {
        // Back button: show the "Close game?" dialog (same as the AnyPlay app)
        if (evt.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (evt.getAction() == KeyEvent.ACTION_UP && mUi != null) {
                mUi.onBack();
            }
            return true;
        }
        return super.dispatchKeyEvent(evt);
    }

    /**
     * This method is for arguments for launching native mkxp-z.
     * 
     * @return arguments for the mkxp-z
     */
    @Override
    protected String[] getArguments()
    {
        String[] args;

        if (DEBUG) {
            // Arguments in Debug mode
            args = new String[] { "debug" };
        } else {
            // Arguments in normal mode
            args = new String[] {};
        }

        return args;
    }

    /**
     * This static method is used in native mkxp-z. (see systemImpl.cpp)
     * This method returns a string of current device locale tag. (e.g. "en_US")
     * 
     * @return string of locale tag
     */
    @SuppressWarnings("unused")
    private static String getSystemLanguage()
    {
        return Locale.getDefault().toString();
    }

    /**
     * This static method is used in native mkxp-z. (see android-binding.cpp)
     * This method returns a boolean indicating that the device has a vibrator or not.
     * 
     * @return boolean
     */
    @SuppressWarnings("unused")
    private static boolean hasVibrator()
    {
        Vibrator vib = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
        return vib.hasVibrator();
    }

    /**
     * This static method is used in native mkxp-z. (see android-binding.cpp)
     * This method makes device vibrating with given milliseconds duration.
     * 
     * @param duration milliseconds duration of vibration
     */
    @SuppressWarnings("unused")
    private static void vibrate(int duration)
    {
        Vibrator vib = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            vib.vibrate(VibrationEffect.createOneShot(duration, VibrationEffect.EFFECT_HEAVY_CLICK));
        } else {
            vib.vibrate(duration);
        }
    }

    /**
     * This static method is used in native mkxp-z. (see android-binding.cpp)
     * This method turns off the current device vibration.
     */
    @SuppressWarnings("unused")
    private static void vibrateStop()
    {
        Vibrator vib = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
        vib.cancel();
    }

    /**
     * This static method is used in native mkxp-z. (see android-binding.cpp)
     * This method returns a boolean indicating the app is in multi window mode or not.
     * (Multi-window mode supports from Android 7.0 Nougat (API 24) and higher.)
     * 
     * @param activity current MainActivity instance
     * @return boolean
     */
    @SuppressWarnings("unused")
    private static boolean inMultiWindow(Activity activity)
    {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.N && activity.isInMultiWindowMode();
    }
}
