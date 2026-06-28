import 'dart:developer' as developer;
import 'package:flutter/services.dart';
import 'package:android_intent_plus/android_intent.dart';
import 'voice_assistant.dart';

class MediaController {
  static const MethodChannel _channel = MethodChannel('com.ruthlessmallard.switchbox/mediabutton');
  
  // Package names for target apps
  static const String youtubeMusicPackage = 'com.google.android.apps.youtube.music';
  static const String audiblePackage = 'com.audible.application';
  static const String googleMapsPackage = 'com.google.android.apps.maps';

  /// Launch YouTube Music and start playing
  Future<void> launchYouTubeMusic() async {
    developer.log('Launching YouTube Music via native platform channel', name: 'SwitchBox');

    try {
      // Use native platform channel to launch YT Music and simulate headset play
      final result = await _channel.invokeMethod('launchYouTubeMusic');

      if (result == 'launched_and_played') {
        developer.log('YouTube Music launched and play sent', name: 'SwitchBox');
      } else if (result == 'not_installed') {
        developer.log('YouTube Music not installed', name: 'SwitchBox');
      } else {
        developer.log('YouTube Music launch returned: $result', name: 'SwitchBox');
      }
    } catch (e) {
      developer.log('Error launching YT Music: $e', name: 'SwitchBox');
    }
  }

  /// Launch Audible and resume last book using MediaBrowser connection
  /// Falls back to launchAndPlayAudible if MediaBrowser fails
  Future<bool> launchAudible({int delayMs = 4500}) async {
    developer.log('Launching Audible via MediaBrowser connection', name: 'SwitchBox');

    try {
      // Try new MediaBrowser connection method first (proper Android media framework)
      final result = await _channel.invokeMethod('connectAndPlayAudible');
      
      if (result == 'connected_played') {
        developer.log('Audible connected via MediaBrowser, play sent', name: 'SwitchBox');
        return true;
      } else if (result == 'fallback_launched') {
        developer.log('MediaBrowser failed, fell back to launchAndPlayAudible', name: 'SwitchBox');
        return true;
      } else if (result == 'failed') {
        developer.log('Audible not installed', name: 'SwitchBox');
        return false;
      } else {
        developer.log('Audible launch returned: $result', name: 'SwitchBox');
        return false;
      }
    } catch (e) {
      developer.log('Audible MediaBrowser connection failed: $e', name: 'SwitchBox');
      return false;
    }
  }

  /// Send play/pause toggle
  Future<void> playPause() async {
    developer.log('Play/Pause', name: 'SwitchBox');
    try {
      await _channel.invokeMethod('playPause');
    } catch (e) {
      developer.log('Error sending playPause: $e', name: 'SwitchBox');
    }
  }

  /// Send next track media command
  Future<void> nextTrack() async {
    developer.log('Next track', name: 'SwitchBox');
    try {
      await _channel.invokeMethod('next');
    } catch (e) {
      developer.log('Error sending next: $e', name: 'SwitchBox');
    }
  }

  /// Send previous track media command
  Future<void> previousTrack() async {
    developer.log('Previous track', name: 'SwitchBox');
    try {
      await _channel.invokeMethod('previous');
    } catch (e) {
      developer.log('Error sending previous: $e', name: 'SwitchBox');
    }
  }

  /// Skip forward 30 seconds (Audible)
  Future<void> skipForward30() async {
    developer.log('Skip +30s', name: 'SwitchBox');
    try {
      await _channel.invokeMethod('fastForward');
    } catch (e) {
      developer.log('Error skipping forward: $e', name: 'SwitchBox');
    }
  }

  /// Skip backward 30 seconds (Audible)
  Future<void> skipBackward30() async {
    developer.log('Skip -30s', name: 'SwitchBox');
    try {
      await _channel.invokeMethod('rewind');
    } catch (e) {
      developer.log('Error skipping backward: $e', name: 'SwitchBox');
    }
  }

  /// Accept incoming call on speakerphone
  Future<void> acceptCall() async {
    developer.log('Accepting call', name: 'SwitchBox');
    
    final intent = AndroidIntent(
      action: 'android.intent.action.ANSWER',
    );
    
    try {
      await intent.launch();
    } catch (e) {
      developer.log('Error accepting call: $e', name: 'SwitchBox');
    }
  }

  /// Deny incoming call
  Future<void> denyCall() async {
    developer.log('Denying call', name: 'SwitchBox');
    
    final intent = AndroidIntent(
      action: 'android.intent.action.CALL_BUTTON',
    );
    
    try {
      await intent.launch();
    } catch (e) {
      developer.log('Error denying call: $e', name: 'SwitchBox');
    }
  }

  /// Navigate to home location using voice assistant
  Future<void> navigateHome() async {
    developer.log('Navigating home via voice assistant', name: 'SwitchBox');
    await VoiceAssistant.navigateHome();
  }

  /// Find nearby diesel stations using voice assistant
  Future<void> findDiesel() async {
    developer.log('Finding diesel via voice assistant', name: 'SwitchBox');
    await VoiceAssistant.findDieselStation();
  }
}