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

  static const String deviceName = 'EyeSwap-ESP32';
  static const String serviceUuid = '12345678-1234-1234-1234-123456789abc';
  static const String commandCharUuid = '87654321-4321-4321-4321-cba987654321';

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
            if (char.uuid.toString().toLowerCase() == commandCharUuid) {
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
    
    String command = String.fromCharCodes(data).trim();
    developer.log('BLE command: $command', name: 'BleHandler');
    
    // Map ESP32 commands to button handlers
    switch (command) {
      case 'BTN1':
        onButton1Press?.call();
        break;
      case 'BTN1_LONG':
        onButton1LongPress?.call();
        break;
      case 'BTN2':
        onButton2Press?.call();
        break;
      case 'BTN2_LONG':
        onButton2LongPress?.call();
        break;
      case 'BTN3':
        onButton3Press?.call();
        break;
      case 'BTN3_LONG':
        onButton3LongPress?.call();
        break;
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