package io.github.juanklv2003.mgsa.touch;

import android.app.NativeActivity;
import android.os.Bundle;

/**
 * Host for the native touch game. Rendering and input are implemented in C++;
 * this class only loads libmgsa_touch.so.
 */
public class MgsaActivity extends NativeActivity {
    static {
        System.loadLibrary("mgsa_touch");
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
