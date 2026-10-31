import 'dart:async';
import 'dart:convert';
import 'dart:developer' as developer;
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleHandler {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandCharacteristic;
  BluetoothCharacteristic? _configCharacteristic;

  // Scan/connection bookkeeping so the link recovers on its own after the
  // pod reboots (the app used to scan exactly once, then sit on "SCANNING"
  // forever until a manual force-stop).
  bool _scanListenerAttached = false;
  bool _connecting = false;
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothConnectionState>? _connStateSub;

  // Five-minute hunt. The app used to scan once and then sit on "SCANNING"
  // forever until a force-stop; now it keeps re-scanning and, if the pod never
  // turns up, asks the UI to shut the app down so the radio and the wake locks
  // stop costing battery.
  static const Duration searchWindow = Duration(minutes: 5);
  static const Duration _searchTickPeriod = Duration(seconds: 5);
  DateTime? _searchStartedAt;
  Timer? _searchTick;
  Timer? _clockTick;
  bool _gaveUp = false;
  VoidCallback? onSearchExpired;
  
  // Callbacks - wire to button handlers
  VoidCallback? onButton1Press;
  VoidCallback? onButton1LongPress;
  VoidCallback? onButton2Press;
  VoidCallback? onButton2LongPress;
  VoidCallback? onButton3Press;
  VoidCallback? onButton3LongPress;
  VoidCallback? onDeviceConnected;
  VoidCallback? onDeviceDisconnected;

  static const String deviceName = 'EyeSwap';
  static const String serviceUuid = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
  static const String buttonCharUuid = 'a1e60244-960d-4d06-aca2-a2fc604f09be';
  static const String configCharUuid = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';

  Future<void> initialize() async {
    try {
      await FlutterBluePlus.turnOn();
    } catch (e) {
      developer.log('BLE init failed: $e', name: 'BleHandler');
    }
    beginSearch();
  }

  /// Start (or restart) the five-minute hunt for the pod.
  void beginSearch() {
    _searchStartedAt = DateTime.now();
    _gaveUp = false;
    _searchTick?.cancel();
    _searchTick = Timer.periodic(_searchTickPeriod, (_) => _searchTickBody());
    startScanning();
  }

  void _searchTickBody() {
    if (_device != null || _connecting) return;   // linked, or mid-handshake

    final started = _searchStartedAt;
    if (started != null &&
        DateTime.now().difference(started) >= searchWindow) {
      _searchTick?.cancel();
      _searchTick = null;
      if (!_gaveUp) {
        _gaveUp = true;
        developer.log('No pod in five minutes - stopping the app',
            name: 'BleHandler');
        onSearchExpired?.call();
      }
      return;
    }
    startScanning();
  }

  /// A live link resets the clock, so a blip never kills the session.
  void _searchSucceeded() {
    _searchStartedAt = null;
    _gaveUp = false;
    _searchTick?.cancel();
    _searchTick = null;
  }

  Future<void> startScanning() async {
    try {
      // Attach the result listener once; re-entrant scans must not stack
      // duplicate listeners or every hit would spawn another connect.
      if (!_scanListenerAttached) {
        _scanSub = FlutterBluePlus.scanResults.listen((results) {
          for (ScanResult result in results) {
            if (result.device.platformName == deviceName &&
                _device == null &&
                !_connecting) {
              _connectToDevice(result.device);
              break;
            }
          }
        });
        _scanListenerAttached = true;
      }

      if (FlutterBluePlus.isScanningNow) return;
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    } catch (e) {
      developer.log('BLE scan failed: $e', name: 'BleHandler');
    }
  }

  /// Rescan a few seconds after a drop, unless we already reconnected.
  void _scheduleRescan() {
    Future.delayed(const Duration(seconds: 3), () {
      if (_device == null && !_connecting) startScanning();
    });
  }

  /// Watch the link so a pod reboot triggers an automatic reconnect.
  void _watchConnectionState(BluetoothDevice device) {
    _connStateSub?.cancel();
    _connStateSub = device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected) {
        _device = null;
        _commandCharacteristic = null;
        _configCharacteristic = null;
        _clockTick?.cancel();
        _clockTick = null;
        onDeviceDisconnected?.call();
        developer.log('EyeSwap disconnected - auto-rescanning', name: 'BleHandler');
        beginSearch();
      }
    });
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    _connecting = true;
    try {
      await FlutterBluePlus.stopScan();
      await device.connect();
      _device = device;

      // Larger MTU so now-playing metadata fits in a single write.
      try {
        await device.requestMtu(512);
      } catch (e) {
        developer.log('MTU request failed: $e', name: 'BleHandler');
      }

      List<BluetoothService> services = await device.discoverServices();
      for (BluetoothService service in services) {
        if (service.uuid.toString().toLowerCase() == serviceUuid) {
          for (BluetoothCharacteristic char in service.characteristics) {
            final uuid = char.uuid.toString().toLowerCase();
            if (uuid == buttonCharUuid) {
              _commandCharacteristic = char;
              await char.setNotifyValue(true);
              char.value.listen(_handleCommand);
            } else if (uuid == configCharUuid) {
              _configCharacteristic = char;
            }
          }
        }
      }

      if (_commandCharacteristic != null) {
        _watchConnectionState(device);
        _searchSucceeded();
        // The pod has no RTC: hand it the wall clock now, then keep it honest.
        unawaited(sendClock());
        _clockTick?.cancel();
        _clockTick = Timer.periodic(const Duration(minutes: 15), (_) {
          unawaited(sendClock());
        });
        onDeviceConnected?.call();
        developer.log('Connected to EyeSwap ESP32', name: 'BleHandler');
      } else {
        _scheduleRescan();
      }
    } catch (e) {
      developer.log('Connection failed: $e', name: 'BleHandler');
      _scheduleRescan();
    } finally {
      _connecting = false;
    }
  }

  /// Push the phone's wall clock (HH:MM, 24h). The pod has no RTC, so this is
  /// the only way the Now Playing card can show a time.
  Future<bool> sendClock([DateTime? now]) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    final t = now ?? DateTime.now();
    final hh = t.hour.toString().padLeft(2, '0');
    final mm = t.minute.toString().padLeft(2, '0');
    try {
      final packet = jsonEncode({'type': 'time', 'time': '$hh:$mm'});
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Clock write failed: $e', name: 'BleHandler');
      return false;
    }
  }

  /// Push now-playing metadata to the ESP32 (config characteristic, JSON).
  Future<bool> sendNowPlaying(Map<String, dynamic> nowPlaying) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    // Keep the payload within a comfortable BLE write size.
    String clamp(Object? value, int max) {
      final s = (value ?? '').toString();
      return s.length <= max ? s : s.substring(0, max);
    }

    final packet = jsonEncode({
      'type': 'now_playing',
      'title': clamp(nowPlaying['title'], 60),
      'artist': clamp(nowPlaying['artist'], 40),
      'state': clamp(nowPlaying['state'], 16),
      'source': clamp(nowPlaying['source'], 40),
    });

    try {
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Now-playing write failed: $e', name: 'BleHandler');
      return false;
    }
  }

  /// Push idle-animation mode to the ESP32 (config characteristic, JSON).
  /// [resync] marks a re-push of stored settings on connect (not a user pick),
  /// so the pod does not drop out of Random Cycle because we reminded it.
  Future<bool> sendMode(String animation, {bool resync = false}) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    try {
      final packet = jsonEncode({
        'type': 'mode',
        'animation': animation,
        if (resync) 'resync': true,
      });
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Mode write failed: $e', name: 'BleHandler');
      return false;
    }
  }

  /// The firmware's built-in Random Cycle roster. Used when the user has never
  /// chosen one, and as the reference list in Settings.
  static const List<String> defaultCycleRoster = [
    'static', 'boids', 'fire', 'knight', 'pet', 'crystal',
  ];

  /// Push the Random Cycle rosters: what rotates while a phone is linked, and
  /// what rotates on an unlinked pod. Both lists must be non-empty; the pod
  /// refuses an empty roster and keeps its previous one.
  Future<bool> sendCycleConfig({
    required List<String> connected,
    required List<String> unlinked,
  }) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    if (connected.isEmpty || unlinked.isEmpty) return false;
    try {
      final packet = jsonEncode({
        'type': 'cycle',
        'connected': connected,
        'unlinked': unlinked,
      });
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Cycle write failed: $e', name: 'BleHandler');
      return false;
    }
  }

  /// Push display config (brightness offset + color) to the ESP32.
  Future<bool> sendDisplayConfig({
    required int brightnessOffset,
    required String colorHex,
    String? accentHex,
  }) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    final brightness = brightnessOffset.clamp(-50, 50);
    try {
      final packet = jsonEncode({
        'type': 'config',
        'brightness_offset': brightness,
        'bg_color': colorHex.toUpperCase(),
        if (accentHex != null) 'accent_color': accentHex.toUpperCase(),
      });
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Config write failed: $e', name: 'BleHandler');
      return false;
    }
  }

  void _handleCommand(List<int> data) {
    if (data.isEmpty) return;
    
    String jsonString = String.fromCharCodes(data).trim();
    developer.log('BLE JSON: $jsonString', name: 'BleHandler');
    
    try {
      // Parse JSON from firmware: {"type":"button","button":1,"action":"tap"}
      final json = jsonDecode(jsonString);
      if (json['type'] == 'button') {
        final int buttonNum = json['button'];
        final String action = json['action'];
        
        // Route to appropriate handler
        switch (buttonNum) {
          case 1:
            if (action == 'tap') {
              developer.log('Calling onButton1Press', name: 'BleHandler');
              onButton1Press?.call();
            } else if (action == 'long') {
              developer.log('Calling onButton1LongPress', name: 'BleHandler');
              onButton1LongPress?.call();
            }
            break;
          case 2:
            if (action == 'tap') {
              onButton2Press?.call();
            } else if (action == 'long') {
              onButton2LongPress?.call();
            }
            break;
          case 3:
            if (action == 'tap') {
              onButton3Press?.call();
            } else if (action == 'long') {
              onButton3LongPress?.call();
            }
            break;
        }
      }
    } catch (e) {
      developer.log('JSON parse error: $e', name: 'BleHandler');
    }
  }

  Future<void> disconnect() async {
    try {
      await _connStateSub?.cancel();
      _connStateSub = null;
      await _device?.disconnect();
    } catch (e) {
      developer.log('Disconnect failed: $e', name: 'BleHandler');
    }
    _device = null;
    _commandCharacteristic = null;
    _configCharacteristic = null;
    onDeviceDisconnected?.call();
  }
}