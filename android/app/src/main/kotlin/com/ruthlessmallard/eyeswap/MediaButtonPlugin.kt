package com.ruthlessmallard.eyeswap

import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class MediaButtonPlugin(private val context: Context) : MethodChannel.MethodCallHandler {
    companion object {
        const val CHANNEL = "com.ruthlessmallard.switchbox/mediabutton"
        private const val AUDIBLE_PACKAGE = "com.audible.application"
        private const val YOUTUBE_MUSIC_PACKAGE = "com.google.android.apps.youtube.music"
        private const val TAG = "EyeSwap"
        
        fun registerWith(engine: FlutterEngine, context: Context) {
            val channel = MethodChannel(engine.dartExecutor.binaryMessenger, CHANNEL)
            channel.setMethodCallHandler(MediaButtonPlugin(context))
        }
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        Handler(Looper.getMainLooper()).post {
            when (call.method) {
                "launchYouTubeMusic" -> {
                    launchYouTubeMusic(result)
                }
                "launchAudible" -> {
                    launchAudible(result)
                }
                "playPause" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
                    result.success(null)
                }
                "next" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_NEXT)
                    result.success(null)
                }
                "previous" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_PREVIOUS)
                    result.success(null)
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
    }

    private fun launchYouTubeMusic(result: MethodChannel.Result) {
        try {
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage(YOUTUBE_MUSIC_PACKAGE)
            
            if (launchIntent != null) {
                launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(launchIntent)
                result.success(true)
                android.util.Log.d(TAG, "YouTube Music launched successfully")
            } else {
                result.success(false)
                android.util.Log.w(TAG, "YouTube Music not installed")
            }
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to launch YouTube Music: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch YouTube Music: ${e.message}", null)
        }
    }

    private fun launchAudible(result: MethodChannel.Result) {
        try {
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage(AUDIBLE_PACKAGE)
            
            if (launchIntent != null) {
                launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(launchIntent)
                result.success(true)
                android.util.Log.d(TAG, "Audible launched successfully")
            } else {
                result.success(false)
                android.util.Log.w(TAG, "Audible not installed")
            }
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to launch Audible: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch Audible: ${e.message}", null)
        }
    }

    private fun sendMediaButton(keyCode: Int) {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val keyEvent = KeyEvent(KeyEvent.ACTION_DOWN, keyCode)
        audioManager.dispatchMediaKeyEvent(keyEvent)
        
        val keyUpEvent = KeyEvent(KeyEvent.ACTION_UP, keyCode)
        audioManager.dispatchMediaKeyEvent(keyUpEvent)
        
        android.util.Log.d(TAG, "Sent media key: $keyCode")
    }
}