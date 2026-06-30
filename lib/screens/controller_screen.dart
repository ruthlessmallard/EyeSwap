import 'package:flutter/material.dart';
import '../widgets/round_display.dart';
import '../widgets/chunky_button.dart';
import '../services/media_controller.dart';
import '../services/spen_handler.dart';
import 'diagnostic_screen.dart';
import 'settings_screen.dart';

class ControllerScreen extends StatefulWidget {
  const ControllerScreen({super.key});

  @override
  State<ControllerScreen> createState() => _ControllerScreenState();
}

class _ControllerScreenState extends State<ControllerScreen> {
  final MediaController _mediaController = MediaController();
  final SPenHandler _spenHandler = SPenHandler();
  String _displayText = 'SWITCHBOX';
  String _subText = 'MINE READY';
  bool _isScrolling = false;

  @override
  void initState() {
    super.initState();
    _spenHandler.startListening(() {
      // S Pen button acts as Button 1 (YouTube)
      _handleButton1Press();
    });
  }

  @override
  void dispose() {
    _spenHandler.stopListening();
    super.dispose();
  }

  void _updateDisplay(String mainText, String subText, {bool scroll = false}) {
    setState(() {
      _displayText = mainText;
      _subText = subText;
      _isScrolling = scroll;
    });
  }

  // Button 1: YouTube Music (tap) / Global Play/Pause (long)
  void _handleButton1Press() {
    _updateDisplay('YOUTUBE', 'MUSIC', scroll: true);
    _mediaController.launchYouTubeMusic();
  }

  void _handleButton1LongPress() {
    _updateDisplay('PLAY/PAUSE', 'GLOBAL', scroll: true);
    _mediaController.playPause();
  }

  // Button 2: Audible (tap) / Skip Back 30s (long)
  void _handleButton2Press() {
    _updateDisplay('AUDIBLE', 'BOOK', scroll: true);
    _mediaController.launchAudible();
  }

  void _handleButton2LongPress() {
    _updateDisplay('SKIP', '-30 SEC', scroll: true);
    _mediaController.skipBackward30();
  }

  // Button 3: Reject call + SMS (tap) / Gemini voice (long)
  void _handleButton3Press() {
    _updateDisplay('CALL', 'REJECTED', scroll: true);
    _mediaController.rejectCallWithSms();
  }

  void _handleButton3LongPress() {
    _updateDisplay('GEMINI', 'LISTENING', scroll: true);
    _mediaController.activateGemini();
  }

  void _openSettings() {
    Navigator.push(
      context,
      MaterialPageRoute(builder: (context) => const SettingsScreen()),
    );
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
              // Settings button - top right corner
              Positioned(
                top: 8,
                right: 8,
                child: IconButton(
                  icon: const Icon(Icons.settings, color: Colors.white54, size: 20),
                  onPressed: _openSettings,
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
