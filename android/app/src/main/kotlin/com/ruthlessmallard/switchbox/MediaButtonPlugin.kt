package com.ruthlessmallard.switchbox

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class MediaButtonPlugin(private val context: Context) : MethodChannel.MethodCallHandler {
    companion object {
        const val CHANNEL = "com.ruthlessmallard.switchbox/mediabutton"
        private var mediaSession: MediaSession? = null
        
        fun registerWith(engine: FlutterEngine, context: Context) {
            val channel = MethodChannel(engine.dartExecutor.binaryMessenger, CHANNEL)
            channel.setMethodCallHandler(MediaButtonPlugin(context))
            
            // Initialize media session for better media control
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                try {
                    mediaSession = MediaSession(context, "SwitchBox").apply {
                        setPlaybackState(PlaybackState.Builder()
                            .setState(PlaybackState.STATE_PLAYING, 0, 1.0f)
                            .setActions(PlaybackState.ACTION_PLAY or 
                                       PlaybackState.ACTION_PAUSE or
                                       PlaybackState.ACTION_SKIP_TO_NEXT or
                                       PlaybackState.ACTION_SKIP_TO_PREVIOUS or
                                       PlaybackState.ACTION_FAST_FORWARD or
                                       PlaybackState.ACTION_REWIND)
                            .build())
                        isActive = true
                    }
                } catch (e: Exception) {
                    android.util.Log.e("SwitchBox", "MediaSession init failed: ${e.message}")
                }
            }
        }
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        Handler(Looper.getMainLooper()).post {
            when (call.method) {
                "playPause" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
                    result.success(null)
                }
                "play" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_PLAY)
                    result.success(null)
                }
                "pause" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_PAUSE)
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
                "fastForward" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_FAST_FORWARD)
                    result.success(null)
                }
                "rewind" -> {
                    sendMediaButton(KeyEvent.KEYCODE_MEDIA_REWIND)
                    result.success(null)
                }
                "launchAudible" -> {
                    launchAudible(result)
                }
                "launchAndPlayAudible" -> {
                    launchAndPlayAudible(result)
                }
                "launchYouTubeMusic" -> {
                    launchYouTubeMusicNative(result)
                }
                "playPauseYT" -> {
                    val keyCode = call.argument<Int>("keyCode") ?: KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
                    sendMediaButtonToPackage(keyCode, "com.google.android.apps.youtube.music")
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun launchAudible(result: MethodChannel.Result) {
        try {
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage("com.audible.application")
            
            if (launchIntent != null) {
                // Add FLAG_ACTIVITY_NEW_TASK since we're starting from a non-Activity context
                launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(launchIntent)
                result.success(true)
                android.util.Log.d("SwitchBox", "Audible launched successfully")
            } else {
                // Audible not installed
                result.success(false)
                android.util.Log.w("SwitchBox", "Audible not installed")
            }
        } catch (e: Exception) {
            android.util.Log.e("SwitchBox", "Failed to launch Audible: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch Audible: ${e.message}", null)
        }
    }

    private fun killApp(packageName: String) {
        try {
            val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            activityManager.killBackgroundProcesses(packageName)
            android.util.Log.d("SwitchBox", "Killed $packageName")
        } catch (e: Exception) {
            android.util.Log.e("SwitchBox", "Failed to kill $packageName: ${e.message}")
        }
    }

    private fun launchAndPlayAudible(result: MethodChannel.Result) {
        try {
            // Kill YT Music to clear active media session
            killApp("com.google.android.apps.youtube.music")
            Thread.sleep(500)
            
            // Check if Audible is installed
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage("com.audible.application")
            
            if (launchIntent == null) {
                android.util.Log.w("SwitchBox", "Audible not installed")
                result.success("not_installed")
                return
            }
            
            // Launch Audible
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(launchIntent)
            android.util.Log.d("SwitchBox", "Audible launched, waiting for it to become foreground")
            
            // Wait 3 seconds for Audible to become foreground
            Thread {
                Thread.sleep(3000)
                
                // Send targeted media button intent directly to Audible
                sendMediaButtonToPackage(KeyEvent.KEYCODE_MEDIA_PLAY, "com.audible.application")
                
                android.util.Log.d("SwitchBox", "Targeted PLAY intent sent to Audible")
                
                Handler(Looper.getMainLooper()).post {
                    result.success("launched_and_played")
                }
            }.start()
            
        } catch (e: Exception) {
            android.util.Log.e("SwitchBox", "Failed to launch and play Audible: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch Audible: ${e.message}", null)
        }
    }

    private fun sendMediaButton(keyCode: Int) {
        var success = false
        
        // Method 1: Try dispatchMediaKeyEvent (API 19+, works with active media apps)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
                
                // Send DOWN event (returns Unit, not Boolean)
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
                
                // Small delay between events
                Thread.sleep(50)
                
                // Send UP event
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
                
                success = true
                android.util.Log.d("SwitchBox", "Media key dispatched: $keyCode")
            } catch (e: Exception) {
                android.util.Log.e("SwitchBox", "dispatchMediaKeyEvent failed: ${e.message}")
            }
        }
        
        // Method 2: Broadcast intent (works with most music apps, deprecated but reliable)
        if (!success) {
            try {
                // Send ordered broadcast so media apps can consume it
                val downIntent = Intent(Intent.ACTION_MEDIA_BUTTON)
                downIntent.putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
                context.sendOrderedBroadcast(downIntent, null)
                
                Thread.sleep(50)
                
                val upIntent = Intent(Intent.ACTION_MEDIA_BUTTON)
                upIntent.putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_UP, keyCode))
                context.sendOrderedBroadcast(upIntent, null)
                
                android.util.Log.d("SwitchBox", "Media key broadcast sent: $keyCode")
            } catch (e: Exception) {
                android.util.Log.e("SwitchBox", "Broadcast failed: ${e.message}")
            }
        }
    }

    private fun sendMediaButtonToPackage(keyCode: Int, packageName: String) {
        try {
            val downIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                setPackage(packageName)
                putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
            }
            val upIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                setPackage(packageName)
                putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_UP, keyCode))
            }
            context.sendBroadcast(downIntent)
            Thread.sleep(50)
            context.sendBroadcast(upIntent)
            android.util.Log.d("SwitchBox", "Media key sent to $packageName: $keyCode")
        } catch (e: Exception) {
            android.util.Log.e("SwitchBox", "Failed to send media key to $packageName: ${e.message}")
        }
    }

    private fun launchYouTubeMusicNative(result: MethodChannel.Result) {
        try {
            // Kill Audible to clear active media session
            killApp("com.audible.application")
            Thread.sleep(500)
            
            // Check if YouTube Music is installed
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage("com.google.android.apps.youtube.music")
            
            if (launchIntent == null) {
                android.util.Log.w("SwitchBox", "YouTube Music not installed")
                result.success("not_installed")
                return
            }
            
            // Launch YouTube Music
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(launchIntent)
            android.util.Log.d("SwitchBox", "YouTube Music launched, waiting for it to become foreground")
            
            // Wait 3 seconds for YouTube Music to become foreground
            Thread {
                Thread.sleep(3000)
                
                // Send targeted media button intent directly to YouTube Music
                sendMediaButtonToPackage(KeyEvent.KEYCODE_MEDIA_PLAY, "com.google.android.apps.youtube.music")
                
                android.util.Log.d("SwitchBox", "Targeted PLAY intent sent to YouTube Music")
                
                Handler(Looper.getMainLooper()).post {
                    result.success("launched_and_played")
                }
            }.start()
            
        } catch (e: Exception) {
            android.util.Log.e("SwitchBox", "Failed to launch YouTube Music: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch YouTube Music: ${e.message}", null)
        }
    }
}