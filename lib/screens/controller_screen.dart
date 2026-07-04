import 'package:flutter/material.dart';
import '../widgets/round_display.dart';
import '../widgets/chunky_button.dart';
import '../services/media_controller.dart';
import '../services/esp32_ble.dart';
import 'settings_screen.dart';

class ControllerScreen extends StatefulWidget {
  const ControllerScreen({super.key});

  @override
  State<ControllerScreen> createState() => _ControllerScreenState();
}

class _ControllerScreenState extends State<ControllerScreen> {
  final MediaController _mediaController = MediaController();
  final ESP32BLEService _bleService = ESP32BLEService();
  String _displayText = 'EYESWAP';
  String _subText = 'READY...';
  bool _isScrolling = false;
  bool _isBLEConnected = false;
  bool _isBLEAvailable = false;

  @override
  void initState() {
    super.initState();
    _initBLE();
  }

  Future<void> _initBLE() async {
    try {
      // Listen for connection state
      _bleService.connectionStream.listen((connected) {
        setState(() {
          _isBLEConnected = connected;
          if (connected) {
            _subText = 'COM-CONNECTED';
          } else if (_isBLEAvailable) {
            _subText = 'SCANNING...';
          } else {
            _subText = 'BLE NOT AVAILABLE';
          }
        });
      });

      // Initialize BLE with button callbacks
      await _bleService.initialize(
        onButtonEvent: _handleBLEButtonEvent,
      );
      
      setState(() {
        _isBLEAvailable = true;
        _subText = 'SCANNING...';
      });
    } catch (e) {
      print('BLE initialization failed: $e');
      setState(() {
        _isBLEAvailable = false;
        _subText = 'BLE UNAVAILABLE';
      });
    }
  }

  void _handleBLEButtonEvent(int button, String action) {
    switch (button) {
      case 1:
        if (action == 'tap') {
          _handleButton1Press();
        } else if (action == 'long') {
          _handleButton1LongPress();
        }
        break;
      case 2:
        if (action == 'tap') {
          _handleButton2Press();
        } else if (action == 'long') {
          _handleButton2LongPress();
        }
        break;
      case 3:
        if (action == 'tap') {
          _handleButton3Press();
        } else if (action == 'long') {
          _handleButton3LongPress();
        }
        break;
    }
  }

  @override
  void dispose() {
    _bleService.dispose();
    super.dispose();
  }

  void _updateDisplay(String mainText, String subText, {bool scroll = false}) {
    setState(() {
      _displayText = mainText;
      _subText = subText;
      _isScrolling = scroll;
    });
  }

  void _handleButton1Press() {
    _updateDisplay('YOUTUBE MUSIC', 'Launching...', scroll: true);
    _mediaController.launchYouTubeMusic();
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) {
        _updateDisplay('YOUTUBE MUSIC', 'PLAYING', scroll: false);
      }
    });
  }

  void _handleButton1LongPress() {
    _updateDisplay('MUSIC NEXT TRACK', 'Skipping...', scroll: true);
    // Note: Need to add nextTrack method to MediaController
    Future.delayed(const Duration(seconds: 2), () {
      if (mounted) {
        _updateDisplay('YOUTUBE MUSIC', 'PLAYING', scroll: false);
      }
    });
  }

  void _handleButton2Press() {
    _updateDisplay('AUDIBLE', 'Launching...', scroll: true);
    _mediaController.launchAudible();
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) {
        _updateDisplay('AUDIBLE', 'PLAYING', scroll: false);
      }
    });
  }

  void _handleButton2LongPress() {
    _updateDisplay('AUDIBLE REW', 'Rewinding...', scroll: true);
    // Note: Need to add rewind method to MediaController
    Future.delayed(const Duration(seconds: 2), () {
      if (mounted) {
        _updateDisplay('AUDIBLE', 'PLAYING', scroll: false);
      }
    });
  }

  void _handleButton3Press() {
    _updateDisplay('CALL DENIED', 'Sending SMS...', scroll: true);
    // Note: Need to add call handling to MediaController
    Future.delayed(const Duration(seconds: 2), () {
      if (mounted) {
        _updateDisplay('EYESWAP', _isBLEConnected ? 'COM-CONNECTED' : 'BLE UNAVAILABLE', scroll: false);
      }
    });
  }

  void _handleButton3LongPress() {
    _updateDisplay('CALL ACCEPTED', 'Connecting...', scroll: true);
    // Note: Need to add call handling to MediaController
    Future.delayed(const Duration(seconds: 2), () {
      if (mounted) {
        _updateDisplay('EYESWAP', _isBLEConnected ? 'COM-CONNECTED' : 'BLE UNAVAILABLE', scroll: false);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: OrientationBuilder(
        builder: (context, orientation) {
          return Row(
            children: [
              // Left column: 3 buttons
              Expanded(
                flex: 1,
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    ChunkyButton(
                      label: 'MEDIA A',
                      color: const Color(0xFFD32F2F),
                      onPressed: _handleButton1Press,
                      onLongPress: _handleButton1LongPress,
                    ),
                    const SizedBox(height: 20),
                    ChunkyButton(
                      label: 'MEDIA B',
                      color: const Color(0xFFD32F2F),
                      onPressed: _handleButton2Press,
                      onLongPress: _handleButton2LongPress,
                    ),
                    const SizedBox(height: 20),
                    ChunkyButton(
                      label: 'COMM',
                      color: const Color(0xFFD32F2F),
                      onPressed: _handleButton3Press,
                      onLongPress: _handleButton3LongPress,
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 20),
              // Right column: Display
              Expanded(
                flex: 2,
                child: RoundDisplay(
                  mainText: _displayText,
                  subText: _subText,
                  isScrolling: _isScrolling,
                ),
              ),
            ],
          );
        },
      ),
      floatingActionButton: FloatingActionButton(
        mini: true,
        onPressed: () {
          Navigator.push(
            context,
            MaterialPageRoute(builder: (context) => const SettingsScreen()),
          );
        },
        child: const Icon(Icons.settings),
      ),
    );
  }
}