import 'dart:async';
import 'package:flutter/services.dart';
import 'package:share_plus/share_plus.dart';

class DiagnosticService {
  static const MethodChannel _channel =
      MethodChannel('com.ruthlessmallard.switchbox/mediabutton');

  Timer? _logPollingTimer;
  List<String> _cachedLogs = [];
  Function(List<String>)? _onLogUpdate;

  void setOnLogUpdate(Function(List<String>) callback) {
    _onLogUpdate = callback;
  }

  /// Start recording media events
  Future<bool> startRecording() async {
    try {
      final result = await _channel.invokeMethod('startRecording');
      _startLogPolling();
      return result == true;
    } catch (e) {
      print('Failed to start recording: $e');
      return false;
    }
  }

  /// Stop recording and return logs
  Future<List<String>> stopRecording() async {
    _stopLogPolling();
    try {
      final result = await _channel.invokeMethod('stopRecording');;
      if (result != null && result is List) {
        _cachedLogs = result.cast<String>();
        return _cachedLogs;
      }
    } catch (e) {
      print('Failed to stop recording: $e');
    }
    return _cachedLogs;
  }

  /// Get current log without stopping
  Future<List<String>> getLog() async {
    try {
      final result = await _channel.invokeMethod('getLog');
      if (result != null && result is List) {
        _cachedLogs = result.cast<String>();
      }
    } catch (e) {
      print('Failed to get log: $e');
    }
    return _cachedLogs;
  }

  /// Clear the log
  Future<void> clearLog() async {
    try {
      await _channel.invokeMethod('clearLog');
      _cachedLogs = [];
    } catch (e) {
      print('Failed to clear log: $e');
    }
  }

  /// Share the log via system share sheet
  Future<void> shareLog() async {
    if (_cachedLogs.isEmpty) return;
    
    final logText = _cachedLogs.join('\n');
    final timestamp = DateTime.now().toIso8601String();
    
    await Share.share(
      logText,
      subject: 'SwitchBox Diagnostic Log $timestamp',
    );
  }

  void _startLogPolling() {
    _stopLogPolling();
    _logPollingTimer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      getLog().then((logs) {
        _onLogUpdate?.call(logs);
      });
    });
  }

  void _stopLogPolling() {
    _logPollingTimer?.cancel();
    _logPollingTimer = null;
  }
}
