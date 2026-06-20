#!/bin/bash
# This script would run on a phone with ADB to find Audible's activities
# Since we can't run on phone, let's document what the user should do:

echo "Run this on a computer with your phone connected via ADB:"
echo ""
echo "adb shell pm dump com.audible.application | grep -A 2 'main'"
echo ""
echo "Or use this to see ALL activities:"
echo "adb shell pm list packages -f | grep audible"
echo ""
echo "Then look for the launcher activity in the output."
echo "It might be something like:"
echo "  com.audible.application/.main.MainActivity"
echo "  com.audible.application/.activities.HomeActivity"
echo "  com.audible.application/.ui.LauncherActivity"
