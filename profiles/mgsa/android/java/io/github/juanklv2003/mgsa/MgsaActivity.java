package io.github.juanklv2003.mgsa;

import android.app.NativeActivity;
import android.os.Bundle;

/**
 * Minimal host for the mgsa NativeActivity. All rendering and touch handling
 * is native (jni/mgsa_jni.cpp); this class only loads libmgsa.so.
 */
public class MgsaActivity extends NativeActivity {
    static {
        System.loadLibrary("mgsa");
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
