package com.ruthlessmallard.switchbox

import android.annotation.TargetApi
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
                    val delayMs = call.argument<Int>("delayMs") ?: 4500
                    launchAndPlayAudible(result, delayMs)
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

    @TargetApi(26)
    private fun launchAndPlayAudible(result: MethodChannel.Result, delayMs: Int) {
        // Early return for API < 26 - activePlaybackConfigurations requires API 26
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            android.util.Log.w("SwitchBox", "launchAndPlayAudible requires API 26+, current: ${Build.VERSION.SDK_INT}")
            result.success("api_too_low")
            return
        }
        
        try {
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage("com.audible.application")
            
            if (launchIntent == null) {
                android.util.Log.w("SwitchBox", "Audible not installed")
                result.success("failed")
                return
            }
            
            // Add FLAG_ACTIVITY_NEW_TASK since we're starting from a non-Activity context
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(launchIntent)
            android.util.Log.d("SwitchBox", "Audible launched, polling for active media session")
            
            // Use a background thread for polling to avoid blocking the main thread
            Thread {
                var audibleActive = false
                var attempts = 0
                val maxAttempts = 20 // 10 seconds total (20 * 500ms)
                
                // Poll for Audible to become the active media session
                while (attempts < maxAttempts && !audibleActive) {
                    try {
                        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
                        val sessions = audioManager.activePlaybackConfigurations
                        
                        for (config in sessions) {
                            if (config.packageName == "com.audible.application") {
                                audibleActive = true
                                android.util.Log.d("SwitchBox", "Audible detected as active media session after ${attempts * 500}ms")
                                break
                            }
                        }
                    } catch (e: Exception) {
                        android.util.Log.e("SwitchBox", "Error checking active playback: ${e.message}")
                    }
                    
                    if (!audibleActive) {
                        Thread.sleep(500)
                        attempts++
                    }
                }
                
                val statusString = if (audibleActive) "audible_active_played" else "timeout_played"
                
                if (!audibleActive) {
                    android.util.Log.w("SwitchBox", "Timeout waiting for Audible session, sending play anyway")
                }
                
                // Now send play command
                val playKeyCode = KeyEvent.KEYCODE_MEDIA_PLAY
                
                // Method 1: Try targeted broadcast to Audible
                try {
                    val downIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                        setPackage("com.audible.application")
                        putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, playKeyCode))
                    }
                    val upIntent = Intent(Intent.ACTION_MEDIA_BUTTON).apply {
                        setPackage("com.audible.application")
                        putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_UP, playKeyCode))
                    }
                    
                    context.sendBroadcast(downIntent)
                    Thread.sleep(50)
                    context.sendBroadcast(upIntent)
                    
                    android.util.Log.d("SwitchBox", "Targeted play broadcast sent to Audible")
                } catch (e: Exception) {
                    android.util.Log.e("SwitchBox", "Targeted broadcast failed: ${e.message}")
                }
                
                // Small additional delay then try non-targeted
                Thread.sleep(200)
                
                // Method 2: Use dispatchMediaKeyEvent
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                    try {
                        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
                        
                        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, playKeyCode))
                        Thread.sleep(50)
                        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, playKeyCode))
                        
                        android.util.Log.d("SwitchBox", "dispatchMediaKeyEvent PLAY sent")
                    } catch (e: Exception) {
                        android.util.Log.e("SwitchBox", "dispatchMediaKeyEvent failed: ${e.message}")
                    }
                }
                
                // Return to main thread for result
                Handler(Looper.getMainLooper()).post {
                    result.success(statusString)
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
}