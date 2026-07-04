package com.ruthlessmallard.eyeswap

import android.os.Bundle
import android.view.KeyEvent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine

class MainActivity: FlutterActivity() {
    
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        // Note: Removed S-Pen plugin registration to fix crashes
        // Note: MediaButtonPlugin may need updating for package rename
    }
    
    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        // Basic media key handling
        when (keyCode) {
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
            KeyEvent.KEYCODE_MEDIA_NEXT,
            KeyEvent.KEYCODE_MEDIA_PREVIOUS -> {
                // Handle media keys if needed
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }
}