package com.ruthlessmallard.switchbox

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MediaButtonPlugin(private val context: Context) : MethodChannel.MethodCallHandler {
    companion object {
        const val CHANNEL = "com.ruthlessmallard.switchbox/mediabutton"
        private const val AUDIBLE_PACKAGE = "com.audible.application"
        private const val YOUTUBE_MUSIC_PACKAGE = "com.google.android.apps.youtube.music"
        private const val TAG = "SwitchBox"
        
        fun registerWith(engine: FlutterEngine, context: Context) {
            val channel = MethodChannel(engine.dartExecutor.binaryMessenger, CHANNEL)
            channel.setMethodCallHandler(MediaButtonPlugin(context))
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
                    launchYouTubeMusic(result)
                }
                "sendMediaPlay" -> {
                    _sendMediaPlay(result)
                }
                "playPauseYT" -> {
                    playPauseYouTubeMusic(result)
                }
                "startRecording" -> {
                    MediaDiagnosticRecorder.getInstance(context).startRecording()
                    result.success(true)
                }
                "stopRecording" -> {
                    MediaDiagnosticRecorder.getInstance(context).stopRecording()
                    result.success(MediaDiagnosticRecorder.getInstance(context).getLog())
                }
                "getLog" -> {
                    result.success(MediaDiagnosticRecorder.getInstance(context).getLog())
                }
                "clearLog" -> {
                    MediaDiagnosticRecorder.getInstance(context).clearLog()
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
    }

    /**
     * Get active media sessions via MediaSessionManager.
     * Requires NotificationListenerService to be connected and user-granted.
     */
    private fun getActiveMediaSessions(): List<MediaController> {
        return try {
            val mediaSessionManager = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
            val componentName = ComponentName(context, SwitchBoxNotificationListener::class.java)
            mediaSessionManager.getActiveSessions(componentName)
        } catch (e: SecurityException) {
            android.util.Log.e(TAG, "Notification listener access not granted: ${e.message}")
            emptyList()
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to get active sessions: ${e.message}")
            emptyList()
        }
    }

    /**
     * Pause YouTube Music specifically, using its MediaController.
     * Prevents audio focus fights when launching Audible.
     */
    private fun pauseYouTubeMusic() {
        val sessions = getActiveMediaSessions()
        val ytmController = sessions.firstOrNull { it.packageName == YOUTUBE_MUSIC_PACKAGE }
        
        if (ytmController != null) {
            try {
                ytmController.transportControls.pause()
                android.util.Log.d(TAG, "YouTube Music paused via MediaController")
            } catch (e: Exception) {
                android.util.Log.e(TAG, "Failed to pause YouTube Music: ${e.message}")
            }
        } else {
            android.util.Log.d(TAG, "YouTube Music session not found, may already be paused")
        }
    }

    /**
     * Play Audible using its specific MediaController with retry logic.
     * Audible takes 500-1500ms after launch to register its MediaSession.
     */
    private fun playAudibleWithRetry(attempt: Int = 0, maxAttempts: Int = 8) {
        val sessions = getActiveMediaSessions()
        val audibleController = sessions.firstOrNull { it.packageName == AUDIBLE_PACKAGE }
        
        if (audibleController != null) {
            try {
                audibleController.transportControls.play()
                android.util.Log.d(TAG, "Audible playback started via MediaController")
                return
            } catch (e: Exception) {
                android.util.Log.e(TAG, "Failed to play Audible: ${e.message}")
            }
        }
        
        if (attempt < maxAttempts) {
            // Exponential backoff: 400ms, 600ms, 800ms, etc.
            val delay = 400 + (attempt * 200)
            android.util.Log.d(TAG, "Audible session not ready, retrying in ${delay}ms (attempt ${attempt + 1}/$maxAttempts)")
            
            Handler(Looper.getMainLooper()).postDelayed({
                playAudibleWithRetry(attempt + 1, maxAttempts)
            }, delay.toLong())
        } else {
            android.util.Log.w(TAG, "Audible session never appeared after $maxAttempts attempts")
            // Fallback to global media key as last resort
            sendMediaButton(KeyEvent.KEYCODE_MEDIA_PLAY)
        }
    }

    /**
     * Play or pause YouTube Music using its specific MediaController.
     * Falls back to broadcast if MediaSessionManager isn't available.
     */
    private fun playPauseYouTubeMusic(result: MethodChannel.Result) {
        val sessions = getActiveMediaSessions()
        val ytmController = sessions.firstOrNull { it.packageName == YOUTUBE_MUSIC_PACKAGE }
        
        if (ytmController != null) {
            try {
                // Check current state and toggle
                val state = ytmController.playbackState?.state
                if (state == PlaybackState.STATE_PLAYING) {
                    ytmController.transportControls.pause()
                    android.util.Log.d(TAG, "YouTube Music paused via MediaController")
                } else {
                    ytmController.transportControls.play()
                    android.util.Log.d(TAG, "YouTube Music playing via MediaController")
                }
                result.success(true)
            } catch (e: Exception) {
                android.util.Log.e(TAG, "Failed to control YouTube Music: ${e.message}")
                // Fallback to broadcast
                sendMediaButtonToPackage(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, YOUTUBE_MUSIC_PACKAGE)
                result.success(false)
            }
        } else {
            android.util.Log.d(TAG, "YouTube Music session not found, using broadcast fallback")
            sendMediaButtonToPackage(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, YOUTUBE_MUSIC_PACKAGE)
            result.success(false)
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

    private fun launchAndPlayAudible(result: MethodChannel.Result) {
        try {
            val packageManager = context.packageManager
            val launchIntent = packageManager.getLaunchIntentForPackage(AUDIBLE_PACKAGE)
            
            if (launchIntent == null) {
                android.util.Log.w(TAG, "Audible not installed")
                result.success("not_installed")
                return
            }
            
            // Step 1: Pause YouTube Music if it's playing (prevents audio focus fights)
            pauseYouTubeMusic()
            
            // Step 2: Launch Audible
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(launchIntent)
            android.util.Log.d(TAG, "Audible launched, starting session discovery")
            
            // Step 3: Start retry loop to find Audible's MediaSession and play
            // Wait initial 500ms then begin retry cycle
            Handler(Looper.getMainLooper()).postDelayed({
                playAudibleWithRetry()
            }, 500)
            
            result.success("launched_and_playing")
            
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to launch and play Audible: ${e.message}")
            result.error("LAUNCH_FAILED", "Failed to launch Audible: ${e.message}", null)
        }
    }

    private fun sendMediaButton(keyCode: Int) {
        var success = false
        
        // Method 1: Try dispatchMediaKeyEvent (API 19+, works with active media apps)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
                Thread.sleep(50)
                audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
                success = true
                android.util.Log.d(TAG, "Media key dispatched: $keyCode")
            } catch (e: Exception) {
                android.util.Log.e(TAG, "dispatchMediaKeyEvent failed: ${e.message}")
            }
        }
        
        // Method 2: Broadcast intent (deprecated but reliable fallback)
        if (!success) {
            try {
                val downIntent = Intent(Intent.ACTION_MEDIA_BUTTON)
                downIntent.putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
                context.sendOrderedBroadcast(downIntent, null)
                
                Thread.sleep(50)
                
                val upIntent = Intent(Intent.ACTION_MEDIA_BUTTON)
                upIntent.putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_UP, keyCode))
                context.sendOrderedBroadcast(upIntent, null)
                
                android.util.Log.d(TAG, "Media key broadcast sent: $keyCode")
            } catch (e: Exception) {
                android.util.Log.e(TAG, "Broadcast failed: ${e.message}")
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
            android.util.Log.d(TAG, "Media key sent to $packageName: $keyCode")
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to send media key to $packageName: ${e.message}")
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

    private fun _sendMediaPlay(result: MethodChannel.Result) {
        try {
            sendMediaButton(KeyEvent.KEYCODE_MEDIA_PLAY)
            result.success(true)
        } catch (e: Exception) {
            android.util.Log.e(TAG, "Failed to send media play: ${e.message}")
            result.error("SEND_FAILED", "Failed to send media play: ${e.message}", null)
        }
    }
}

/**
 * MediaDiagnosticRecorder - Singleton for recording media button events via BroadcastReceiver
 * for diagnostic purposes.
 */
class MediaDiagnosticRecorder private constructor(private val context: Context) {
    
    private val logList = mutableListOf<String>()
    private var isRecording = false
    private val maxLogSize = 100
    private var mediaButtonReceiver: android.content.BroadcastReceiver? = null
    private val dateFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    
    companion object {
        @Volatile
        private var instance: MediaDiagnosticRecorder? = null
        
        fun getInstance(context: Context): MediaDiagnosticRecorder {
            return instance ?: synchronized(this) {
                instance ?: MediaDiagnosticRecorder(context.applicationContext).also {
                    instance = it
                }
            }
        }
    }
    
    fun startRecording() {
        if (isRecording) return
        
        isRecording = true
        logList.clear()
        addLog("Recording started")
        
        mediaButtonReceiver = object : android.content.BroadcastReceiver() {
            override fun onReceive(ctx: Context?, intent: android.content.Intent?) {
                if (!isRecording) return
                
                intent?.let { 
                    val action = it.action
                    if (action == android.content.Intent.ACTION_MEDIA_BUTTON) {
                        val keyEvent = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            it.getParcelableExtra(android.content.Intent.EXTRA_KEY_EVENT, KeyEvent::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            it.getParcelableExtra(android.content.Intent.EXTRA_KEY_EVENT)
                        }
                        
                        val keyCode = keyEvent?.keyCode?.toString() ?: "none"
                        val pkg = it.`package` ?: "unknown"
                        
                        addLog("MEDIA_BUTTON from $pkg: $keyCode")
                    }
                }
            }
        }
        
        try {
            val filter = android.content.IntentFilter(android.content.Intent.ACTION_MEDIA_BUTTON).apply {
                priority = 999
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.registerReceiver(mediaButtonReceiver, filter, Context.RECEIVER_EXPORTED)
            } else {
                context.registerReceiver(mediaButtonReceiver, filter)
            }
            addLog("Receiver registered for ACTION_MEDIA_BUTTON with priority 999")
        } catch (e: Exception) {
            addLog("ERROR: Failed to register receiver: ${e.message}")
        }
    }
    
    fun stopRecording(): List<String> {
        isRecording = false
        
        mediaButtonReceiver?.let {
            try {
                context.unregisterReceiver(it)
            } catch (e: Exception) {
                // Receiver may not be registered
            }
            mediaButtonReceiver = null
        }
        
        addLog("Recording stopped")
        return getLog()
    }
    
    fun getLog(): List<String> {
        return logList.toList()
    }
    
    fun clearLog() {
        logList.clear()
    }
    
    private fun addLog(message: String) {
        val timestamp = dateFormat.format(Date())
        val logEntry = "[$timestamp] $message"
        logList.add(logEntry)
        
        while (logList.size > maxLogSize) {
            logList.removeAt(0)
        }
    }
}
