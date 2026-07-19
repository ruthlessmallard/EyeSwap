import 'dart:convert';
import 'dart:developer' as developer;
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleHandler {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandCharacteristic;
  
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
      
      List<BluetoothService> services = await device.discoverServices();
      for (BluetoothService service in services) {
        if (service.uuid.toString().toLowerCase() == serviceUuid) {
          for (BluetoothCharacteristic char in service.characteristics) {
            if (char.uuid.toString().toLowerCase() == buttonCharUuid) {
              _commandCharacteristic = char;
              await char.setNotifyValue(true);
              char.value.listen(_handleCommand);
              onDeviceConnected?.call();
              developer.log('Connected to EyeSwap ESP32', name: 'BleHandler');
              return;
            }
          }
        }
      }
    } catch (e) {
      developer.log('Connection failed: $e', name: 'BleHandler');
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
      onDeviceDisconnected?.call();
    } catch (e) {
      developer.log('Disconnect failed: $e', name: 'BleHandler');
    }
  }
}