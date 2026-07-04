import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// BLE communication service for ESP32 configuration
/// Scans for "EyeSwap" device and manages GATT communication
class ESP32BLEService {
  // Device name and UUIDs (from esp32 branch firmware)
  static const String deviceName = 'EyeSwap';
  static const String serviceUuid = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
  static const String configCharUuid = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
  static const String buttonCharUuid = 'a1e60244-960d-4d06-aca2-a2fc604f09be';

  BluetoothDevice? _device;
  BluetoothCharacteristic? _configChar;
  BluetoothCharacteristic? _buttonChar;
  StreamSubscription<BluetoothConnectionState>? _connectionSub;
  StreamSubscription<List<int>>? _buttonSub;

  bool _isConnected = false;
  bool _isScanning = false;
  // Button event callback: (buttonNumber, action)
  // action: "tap", "long"
  void Function(int button, String action)? _onButtonEvent;

  // Connection state broadcast
  final StreamController<bool> _connectionController = StreamController<bool>.broadcast();
  Stream<bool> get connectionStream => _connectionController.stream;
  bool get isConnected => _isConnected;

  /// Initialize BLE and set up listeners
  Future<void> initialize({
    void Function(int button, String action)? onButtonEvent,
  }) async {
    _onButtonEvent = onButtonEvent;

    // Check if BLE is supported and enabled
    final adapterState = await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      throw Exception('Bluetooth is not enabled. Please turn on Bluetooth.');
    }

    await _startScan();
  }

  /// Start scanning for EyeSwap device
  Future<void> _startScan() async {
    if (_isScanning) return;
    _isScanning = true;

    // Listen for scan results
    FlutterBluePlus.scanResults.listen((results) async {
      for (ScanResult result in results) {
        if (result.device.platformName == deviceName) {
          await FlutterBluePlus.stopScan();
          _isScanning = false;
          await _connectToDevice(result.device);
          return;
        }
      }
    });

    // Start scanning
    await FlutterBluePlus.startScan(
      withServices: [], // Scan all devices, filter by name
      timeout: const Duration(seconds: 30),
    );
  }

  /// Connect to discovered device and discover services
  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      _device = device;

      // Listen for connection state changes
      _connectionSub = device.connectionState.listen((state) async {
        if (state == BluetoothConnectionState.connected) {
          _isConnected = true;
          _connectionController.add(true);
          await _discoverServices();
        } else if (state == BluetoothConnectionState.disconnected) {
          _isConnected = false;
          _connectionController.add(false);
          // Auto-reconnect after delay
          Future.delayed(const Duration(seconds: 3), () {
            if (!_isConnected) _startScan();
          });
        }
      });

      await device.connect(autoConnect: false);
    } catch (e) {
      _isConnected = false;
      _connectionController.add(false);
      // Retry scan
      Future.delayed(const Duration(seconds: 3), _startScan);
    }
  }

  /// Discover services and characteristics
  Future<void> _discoverServices() async {
    if (_device == null) return;

    try {
      List<BluetoothService> services = await _device!.discoverServices();

      for (var service in services) {
        if (service.uuid.toString() == serviceUuid) {
          for (var char in service.characteristics) {
            if (char.uuid.toString() == configCharUuid) {
              _configChar = char;
            } else if (char.uuid.toString() == buttonCharUuid) {
              _buttonChar = char;
              await _subscribeToButton(char);
            }
          }
        }
      }
    } catch (e) {
      // Service discovery failed
    }
  }

  /// Subscribe to button notifications
  Future<void> _subscribeToButton(BluetoothCharacteristic char) async {
    try {
      await char.setNotifyValue(true);
      _buttonSub = char.onValueReceived.listen((value) {
        _parseButtonData(value);
      });
    } catch (e) {
      // Failed to subscribe
    }
  }

  /// Parse button notification data (JSON: {"type":"button","button":1,"action":"tap"})
  void _parseButtonData(List<int> value) {
    try {
      final jsonStr = utf8.decode(value);
      final json = jsonDecode(jsonStr);

      if (json['type'] == 'button') {
        final button = json['button'] as int?;
        final action = json['action'] as String?;
        if (button != null && action != null) {
          _onButtonEvent?.call(button, action);
        }
      }
    } catch (e) {
      // Failed to parse
    }
  }

  /// Send brightness offset to ESP32
  /// [offset] -50 to +50, where 0 is default
  Future<bool> sendBrightnessOffset(int offset) async {
    if (!_isConnected || _configChar == null) return false;

    offset = offset.clamp(-50, 50);

    final packet = jsonEncode({
      'type': 'config',
      'brightness_offset': offset,
    });

    return _sendConfigPacket(packet);
  }

  /// Send background color to ESP32 (JSON: {"type":"config","bg_color":"#RRGGBB"})
  Future<bool> sendColor(Color color) async {
    if (!_isConnected || _configChar == null) return false;

    final hexColor = '#${color.red.toRadixString(16).padLeft(2, '0')}'
        '${color.green.toRadixString(16).padLeft(2, '0')}'
        '${color.blue.toRadixString(16).padLeft(2, '0')}';

    final packet = jsonEncode({
      'type': 'config',
      'bg_color': hexColor.toUpperCase(),
    });

    return _sendConfigPacket(packet);
  }

  /// Send both brightness offset and color in one packet
  Future<bool> sendConfig({required int brightnessOffset, required Color color}) async {
    if (!_isConnected || _configChar == null) return false;

    brightnessOffset = brightnessOffset.clamp(-50, 50);

    final hexColor = '#${color.red.toRadixString(16).padLeft(2, '0')}'
        '${color.green.toRadixString(16).padLeft(2, '0')}'
        '${color.blue.toRadixString(16).padLeft(2, '0')}';

    final packet = jsonEncode({
      'type': 'config',
      'brightness_offset': brightnessOffset,
      'bg_color': hexColor.toUpperCase(),
    });

    return _sendConfigPacket(packet);
  }

  /// Send a test flash command
  Future<bool> sendTestFlash(Color color, int brightnessOffset) async {
    return sendConfig(brightnessOffset: brightnessOffset, color: color);
  }

  /// Internal method to send config packet via BLE
  Future<bool> _sendConfigPacket(String data) async {
    if (_configChar == null) return false;

    try {
      final bytes = utf8.encode(data);
      await _configChar!.write(bytes, withoutResponse: true);
      return true;
    } catch (e) {
      return false;
    }
  }

  /// Manually disconnect and clean up
  Future<void> disconnect() async {
    await _buttonSub?.cancel();
    await _connectionSub?.cancel();
    await _device?.disconnect();
    _device = null;
    _configChar = null;
    _buttonChar = null;
    _isConnected = false;
  }

  /// Dispose service
  void dispose() {
    disconnect();
    _connectionController.close();
  }
}
