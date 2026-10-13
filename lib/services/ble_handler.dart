import 'dart:convert';
import 'dart:developer' as developer;
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleHandler {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandCharacteristic;
  BluetoothCharacteristic? _configCharacteristic;
  
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
  }

  Future<void> startScanning() async {
    try {
      FlutterBluePlus.scanResults.listen((results) {
        for (ScanResult result in results) {
          if (result.device.platformName == deviceName) {
            _connectToDevice(result.device);
            break;
          }
        }
      });
      
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    } catch (e) {
      developer.log('BLE scan failed: $e', name: 'BleHandler');
    }
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
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
        onDeviceConnected?.call();
        developer.log('Connected to EyeSwap ESP32', name: 'BleHandler');
      }
    } catch (e) {
      developer.log('Connection failed: $e', name: 'BleHandler');
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
  Future<bool> sendMode(String animation) async {
    final characteristic = _configCharacteristic;
    if (characteristic == null) return false;

    try {
      final packet = jsonEncode({'type': 'mode', 'animation': animation});
      await characteristic.write(utf8.encode(packet), withoutResponse: true);
      return true;
    } catch (e) {
      developer.log('Mode write failed: $e', name: 'BleHandler');
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
      await _device?.disconnect();
      _device = null;
      _commandCharacteristic = null;
      _configCharacteristic = null;
      onDeviceDisconnected?.call();
    } catch (e) {
      developer.log('Disconnect failed: $e', name: 'BleHandler');
    }
  }
}