import 'dart:developer' as developer;
import 'package:flutter/services.dart';
import 'package:android_intent_plus/android_intent.dart';
import 'package:url_launcher/url_launcher.dart';

class MediaController {
  static const MethodChannel _channel = MethodChannel('com.ruthlessmallard.eyeswap/mediabutton');

  /// Launch YouTube Music and start playing (legacy method - uses native intent)
  Future<void> launchYouTubeMusic() async {
    developer.log('Launching YouTube Music', name: 'EyeSwap');
    try {
      await _channel.invokeMethod('launchYouTubeMusic');
      await Future.delayed(const Duration(milliseconds: 4000));
      await _channel.invokeMethod('playPauseYT');
    } catch (e) {
      developer.log('Error launching YT Music: $e', name: 'EyeSwap');
    }
  }

  /// Launch YouTube Music with smart online/offline detection
  Future<bool> launchYouTubeMusicDownloads() async {
    developer.log('Launching YouTube Music (smart routing)', name: 'EyeSwap');
    try {
      final result = await _channel.invokeMethod('launchYouTubeMusicSmart');
      return result == 'launched_and_playing' || result == 'launched_and_played' || result == 'launched_offline';
    } catch (e) {
      developer.log('Error launching YTM smart: $e', name: 'EyeSwap');
      return false;
    }
  }
  
  /// Check if device has internet connectivity
  Future<bool> hasInternetConnection() async {
    try {
      final result = await _channel.invokeMethod('hasInternetConnection');
      return result == true;
    } catch (e) {
      developer.log('Error checking internet: $e', name: 'EyeSwap');
      return false;
    }
  }
  
  /// Check if notification access is enabled
  Future<bool> isNotificationAccessEnabled() async {
    try {
      final result = await _channel.invokeMethod('isNotificationAccessEnabled');
      return result == true;
    } catch (e) {
      developer.log('Error checking notification access: $e', name: 'EyeSwap');
      return false;
    }
  }
  
  /// Check if accessibility service is enabled
  Future<bool> isAccessibilityServiceEnabled() async {
    try {
      final result = await _channel.invokeMethod('isAccessibilityServiceEnabled');
      return result == true;
    } catch (e) {
      developer.log('Error checking accessibility service: $e', name: 'EyeSwap');
      return false;
    }
  }
  
  /// Open notification access settings
  Future<void> openNotificationSettings() async {
    try {
      await _channel.invokeMethod('openNotificationSettings');
    } catch (e) {
      developer.log('Error opening notification settings: $e', name: 'EyeSwap');
    }
  }
  
  /// Open accessibility settings
  Future<void> openAccessibilitySettings() async {
    try {
      await _channel.invokeMethod('openAccessibilitySettings');
    } catch (e) {
      developer.log('Error opening accessibility settings: $e', name: 'EyeSwap');
    }
  }

  /// Launch YouTube Music to a specific playlist
  Future<bool> launchYouTubeMusicPlaylist(String playlistId) async {
    developer.log('Launching YouTube Music playlist: $playlistId', name: 'EyeSwap');
    try {
      final uri = Uri.parse('https://music.youtube.com/playlist?list=$playlistId');
      final launched = await launchUrl(
        uri,
        mode: LaunchMode.externalApplication,
      );
      
      if (launched) {
        await Future.delayed(const Duration(milliseconds: 2500));
        await playPause();
      }
      
      return launched;
    } catch (e) {
      developer.log('Error launching YT Music playlist: $e', name: 'EyeSwap');
      return false;
    }
  }

  /// Launch Audible and resume playback
  Future<bool> launchAudible({int delayMs = 4500}) async {
    developer.log('Launching Audible', name: 'EyeSwap');
    try {
      final result = await _channel.invokeMethod('launchAndPlayAudible');
      return result == 'launched_and_playing' || result == 'launched_and_played';
    } catch (e) {
      developer.log('Error launching Audible: $e', name: 'EyeSwap');
      return false;
    }
  }

  /// Send play/pause toggle
  Future<void> playPause() async {
    developer.log('Play/Pause', name: 'EyeSwap');
    try {
      await _channel.invokeMethod('playPause');
    } catch (e) {
      developer.log('Error sending playPause: $e', name: 'EyeSwap');
    }
  }

  /// Skip backward 30 seconds (Audible)
  Future<void> skipBackward30() async {
    developer.log('Skip -30s', name: 'EyeSwap');
    try {
      await _channel.invokeMethod('rewind');
    } catch (e) {
      developer.log('Error skipping backward: $e', name: 'EyeSwap');
    }
  }

  /// Accept incoming call
  Future<void> acceptCall() async {
    developer.log('Accepting call', name: 'EyeSwap');
    try {
      await _channel.invokeMethod('acceptCall');
    } catch (e) {
      developer.log('Error accepting call: $e', name: 'EyeSwap');
    }
  }

  /// Activate Gemini voice assistant
  Future<void> activateGemini() async {
    developer.log('Activating Gemini', name: 'EyeSwap');
    try {
      await _channel.invokeMethod('activateGemini');
    } catch (e) {
      developer.log('Error activating Gemini: $e', name: 'EyeSwap');
    }
  }
}