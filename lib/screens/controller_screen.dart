import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../widgets/round_display.dart';
import '../widgets/chunky_button.dart';
import '../services/media_controller.dart';
import '../services/ble_handler.dart';
import 'settings_screen.dart';

class ControllerScreen extends StatefulWidget {
  const ControllerScreen({super.key});

  @override
  State<ControllerScreen> createState() => _ControllerScreenState();
}

class _ControllerScreenState extends State<ControllerScreen> {
  final MediaController _mediaController = MediaController();
  final BleHandler _bleHandler = BleHandler();
  String _displayText = 'EYESWAP';
  String _subText = 'SCANNING';
  bool _isScrolling = false;

  @override
  void initState() {
    super.initState();
    _initializeBle();
  }

  @override
  void dispose() {
    _bleHandler.disconnect();
    super.dispose();
  }

  void _initializeBle() async {
    await _bleHandler.initialize();
    _bleHandler.onDeviceConnected = () {
      _updateDisplay('EYESWAP', 'CONNECTED');
    };
    _bleHandler.onDeviceDisconnected = () {
      _updateDisplay('EYESWAP', 'SCANNING');
    };
    // Wire BLE button events to touch button handlers
    _bleHandler.onButton1Press = _handleButton1Press;
    _bleHandler.onButton1LongPress = _handleButton1LongPress;
    _bleHandler.onButton2Press = _handleButton2Press;
    _bleHandler.onButton2LongPress = _handleButton2LongPress;
    _bleHandler.onButton3Press = _handleButton3Press;
    _bleHandler.onButton3LongPress = _handleButton3LongPress;
    
    _bleHandler.startScanning();
  }

  void _updateDisplay(String mainText, String subText, {bool scroll = false}) {
    setState(() {
      _displayText = mainText;
      _subText = subText;
      _isScrolling = scroll;
    });
  }

  // Load button mapping from SharedPreferences
  Future<String> _getButtonFunction(String key) async {
    final prefs = await SharedPreferences.getInstance();
    final defaults = {
      'btn1_short': 'yt_downloads',
      'btn1_long': 'play_pause',
      'btn2_short': 'audible',
      'btn2_long': 'skip_back',
      'btn3_short': 'accept_call',
      'btn3_long': 'gemini',
    };
    return prefs.getString(key) ?? defaults[key] ?? 'none';
  }

  // Execute function by ID
  Future<void> _executeFunction(String functionId) async {
    print('[DEBUG] Executing function: $functionId');
    switch (functionId) {
      case 'yt_music':
        _updateDisplay('YOUTUBE', 'MUSIC', scroll: true);
        await _mediaController.launchYouTubeMusic();
        break;
      case 'yt_downloads':
        _updateDisplay('YT MUSIC', 'DOWNLOADS', scroll: true);
        await _mediaController.launchYouTubeMusicDownloads();
        break;
      case 'audible':
        _updateDisplay('AUDIBLE', 'BOOK', scroll: true);
        await _mediaController.launchAudible();
        break;
      case 'skip_back':
        _updateDisplay('SKIP', '-30 SEC', scroll: true);
        await _mediaController.skipBackward30();
        break;
      case 'play_pause':
        _updateDisplay('PLAY/PAUSE', 'GLOBAL', scroll: true);
        await _mediaController.playPause();
        break;
      case 'accept_call':
        _updateDisplay('CALL', 'ACCEPTED', scroll: true);
        await _mediaController.acceptCall();
        break;
      case 'gemini':
        _updateDisplay('GEMINI', 'LISTENING', scroll: true);
        await _mediaController.activateGemini();
        break;
      case 'none':
      default:
        // Do nothing
        break;
    }
  }

  // Button handlers - now dynamic
  void _handleButton1Press() async {
    print('[DEBUG] Button 1 pressed!');
    final func = await _getButtonFunction('btn1_short');
    print('[DEBUG] Button 1 function: $func');
    await _executeFunction(func);
  }

  void _handleButton1LongPress() async {
    final func = await _getButtonFunction('btn1_long');
    await _executeFunction(func);
  }

  void _handleButton2Press() async {
    final func = await _getButtonFunction('btn2_short');
    await _executeFunction(func);
  }

  void _handleButton2LongPress() async {
    final func = await _getButtonFunction('btn2_long');
    await _executeFunction(func);
  }

  void _handleButton3Press() async {
    final func = await _getButtonFunction('btn3_short');
    await _executeFunction(func);
  }

  void _handleButton3LongPress() async {
    final func = await _getButtonFunction('btn3_long');
    await _executeFunction(func);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [
              Color(0xFF0A0A0A),
              Color(0xFF1A1A1A),
              Color(0xFF0A0A0A),
            ],
          ),
        ),
        child: SafeArea(
          child: Stack(
            children: [
              // Settings button - top right
              Positioned(
                top: 8,
                right: 8,
                child: IconButton(
                  icon: const Icon(
                    Icons.settings,
                    color: Colors.white54,
                    size: 28,
                  ),
                  onPressed: () {
                    Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (context) => const SettingsScreen(),
                      ),
                    );
                  },
                  tooltip: 'Settings',
                ),
              ),
              // Main content
              Padding(
                padding: const EdgeInsets.all(24.0),
                child: Row(
                  children: [
                    // Round Display (Left side)
                    Expanded(
                      flex: 2,
                      child: Center(
                        child: RoundDisplay(
                          mainText: _displayText,
                          subText: _subText,
                          isScrolling: _isScrolling,
                        ),
                      ),
                    ),
                    const SizedBox(width: 32),
                    // 3-Button Cluster (Right side)
                    Expanded(
                      flex: 3,
                      child: Center(
                        child: Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            ChunkyButton(
                              label: '1',
                              color: const Color(0xFFD32F2F),
                              onPressed: _handleButton1Press,
                              onLongPress: _handleButton1LongPress,
                            ),
                            const SizedBox(width: 20),
                            ChunkyButton(
                              label: '2',
                              color: const Color(0xFFD32F2F),
                              onPressed: _handleButton2Press,
                              onLongPress: _handleButton2LongPress,
                            ),
                            const SizedBox(width: 20),
                            ChunkyButton(
                              label: '3',
                              color: const Color(0xFF424242),
                              onPressed: _handleButton3Press,
                              onLongPress: _handleButton3LongPress,
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}