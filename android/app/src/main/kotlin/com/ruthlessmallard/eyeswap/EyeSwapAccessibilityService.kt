package com.ruthlessmallard.eyeswap

import android.accessibilityservice.AccessibilityService
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo

class EyeSwapAccessibilityService : AccessibilityService() {

    // YTM's offline popup uses several possible button labels
    // depending on app version — cast a wide net
    private val OFFLINE_PLAY_TRIGGERS = listOf(
        "play downloads",
        "downloads", 
        "play offline",
        "offline music",
        "listen offline",
        "Play downloads",
        "Downloads",
        "Play offline",
        "Offline music", 
        "Listen offline"
    )

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        event ?: return
        if (event.packageName != "com.google.android.apps.youtube.music") return

        val root = rootInActiveWindow ?: return

        // Walk the tree looking for a tappable node matching known offline popup text
        findAndTapOfflineButton(root)
        root.recycle()
    }

    private fun findAndTapOfflineButton(node: AccessibilityNodeInfo) {
        for (trigger in OFFLINE_PLAY_TRIGGERS) {
            val matches = node.findAccessibilityNodeInfosByText(trigger)
            for (match in matches) {
                if (match.isClickable) {
                    android.util.Log.d("EyeSwap", "Auto-tapping YTM offline button: $trigger")
                    match.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    return
                }
                // Button text sometimes lives in a child of the clickable parent
                val parent = match.parent
                if (parent != null && parent.isClickable) {
                    android.util.Log.d("EyeSwap", "Auto-tapping YTM offline button parent: $trigger")
                    parent.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    return
                }
            }
        }
    }

    override fun onInterrupt() {}
}