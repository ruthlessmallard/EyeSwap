package com.ruthlessmallard.eyeswap

import android.accessibilityservice.AccessibilityService
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo

class EyeSwapAccessibilityService : AccessibilityService() {

    // YTM's offline popup button sequence (airplane mode)
    // 1. "go to downloads" -> navigate to downloads screen
    // 2. "shuffle all" -> begin offline playback
    private val OFFLINE_BUTTON_TEXTS = listOf(
        "go to downloads",
        "Go to downloads",
        "GO TO DOWNLOADS",
        "shuffle all",
        "Shuffle all",
        "SHUFFLE ALL",
        "play downloads", // fallback
        "Play downloads"  // fallback
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
        // Debug: log all visible text nodes to help troubleshoot
        logVisibleTextNodes(node)
        
        for (buttonText in OFFLINE_BUTTON_TEXTS) {
            val matches = node.findAccessibilityNodeInfosByText(buttonText)
            for (match in matches) {
                if (match.isClickable) {
                    android.util.Log.d("EyeSwap", "Auto-tapping YTM button: '$buttonText'")
                    match.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    return
                }
                // Button text sometimes lives in a child of the clickable parent
                val parent = match.parent
                if (parent != null && parent.isClickable) {
                    android.util.Log.d("EyeSwap", "Auto-tapping YTM button parent: '$buttonText'")
                    parent.performAction(AccessibilityNodeInfo.ACTION_CLICK)
                    return
                }
            }
        }
    }
    
    private fun logVisibleTextNodes(node: AccessibilityNodeInfo, depth: Int = 0) {
        if (depth > 3) return // Limit recursion depth
        
        val text = node.text?.toString()
        if (!text.isNullOrBlank()) {
            android.util.Log.d("EyeSwap", "Found text node (depth $depth): '$text' clickable=${node.isClickable}")
        }
        
        for (i in 0 until node.childCount) {
            val child = node.getChild(i)
            if (child != null) {
                logVisibleTextNodes(child, depth + 1)
                child.recycle()
            }
        }
    }

    override fun onInterrupt() {}
}