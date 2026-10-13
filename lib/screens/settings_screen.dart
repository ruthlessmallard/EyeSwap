import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/esp32_ble.dart';
import 'button_config_screen.dart';

/// Settings screen for configuring ESP32 display
/// Brightness offset and background color
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final ESP32BLEService _bleService = ESP32BLEService();
  
  // Brightness offset: -50 to +50, default 0
  double _brightnessOffset = 0;
  
  // Selected colors: primary + secondary accent (single-color = both equal)
  Color _selectedColor = const Color(0xFFFFAA00); // Default amber
  Color _selectedAccent = const Color(0xFFFFFFFF); // Default white
  
  // Preset colors for common dash colors
  final List<Map<String, dynamic>> _presetColors = [
    {'name': 'Amber', 'color': const Color(0xFFFFAA00), 'hex': 'FFAA00'},
    {'name': 'Blue', 'color': const Color(0xFF0066FF), 'hex': '0066FF'},
    {'name': 'Red', 'color': const Color(0xFFCC0000), 'hex': 'CC0000'},
    {'name': 'Green', 'color': const Color(0xFF00AA00), 'hex': '00AA00'},
    {'name': 'White', 'color': const Color(0xFFFFFFFF), 'hex': 'FFFFFF'},
    {'name': 'Black', 'color': const Color(0xFF000000), 'hex': '000000'},
  ];
  
  bool _isSending = false;
  String _statusMessage = '';
  
  // Idle animation mode
  String _idleAnimation = 'marquee'; // marquee, static, none
  
  final List<Map<String, dynamic>> _idleAnimations = [
    {'id': 'marquee', 'name': 'Text Marquee', 'icon': Icons.text_fields},
    {'id': 'static', 'name': 'Game of Life', 'icon': Icons.grain},
    {'id': 'now_playing', 'name': 'Now Playing', 'icon': Icons.music_note},
    {'id': 'boids', 'name': 'Boids', 'icon': Icons.flight},
    {'id': 'ant', 'name': "Langton's Ant", 'icon': Icons.grid_on},
    {'id': 'fire', 'name': 'Fire', 'icon': Icons.local_fire_department},
    {'id': 'pipes', 'name': 'Pipes', 'icon': Icons.plumbing},
    {'id': 'radar', 'name': 'Radar Sweep', 'icon': Icons.radar},
    {'id': 'starfield', 'name': 'Starfield', 'icon': Icons.star},
    {'id': 'oscilloscope', 'name': 'Oscilloscope', 'icon': Icons.show_chart},
    {'id': 'pong', 'name': 'Pong', 'icon': Icons.sports_esports},
    {'id': 'eq', 'name': 'Spectrum (EQ)', 'icon': Icons.graphic_eq},
    {'id': 'cycle', 'name': 'Random Cycle', 'icon': Icons.shuffle},
  ];

  @override
  void initState() {
    super.initState();
    _bleService.initialize();
    _loadSettings();
  }
  
  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _idleAnimation = prefs.getString('idle_animation') ?? 'marquee';
      _brightnessOffset = prefs.getDouble('brightness_offset') ?? 0;
      final hex = prefs.getString('bg_color') ?? 'FFAA00';
      _selectedColor = Color(int.parse('FF$hex', radix: 16));
      final ahex = prefs.getString('accent_color') ?? 'FFFFFF';
      _selectedAccent = Color(int.parse('FF$ahex', radix: 16));
    });
  }
  
  Future<void> _saveIdleAnimation(String animation) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('idle_animation', animation);
    setState(() {
      _idleAnimation = animation;
    });

    // Push the mode to the hardware. If it isn't connected yet, the choice is
    // still saved and applied on the next connect.
    final sent = await _bleService.sendIdleAnimation(animation);
    if (sent) {
      _setStatus('Idle animation: $animation');
    } else {
      _setStatus('Saved. Connect to EyeSwap to apply.');
    }
  }

  @override
  void dispose() {
    _bleService.dispose();
    super.dispose();
  }

  void _setStatus(String message) {
    setState(() {
      _statusMessage = message;
    });
    // Clear status after 3 seconds
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted) {
        setState(() {
          _statusMessage = '';
        });
      }
    });
  }

  Future<void> _applySettings() async {
    setState(() {
      _isSending = true;
    });
    
    // Remember the last applied settings.
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt('brightness_offset', _brightnessOffset.toInt());
    await prefs.setString(
      'bg_color',
      '${_selectedColor.red.toRadixString(16).padLeft(2, '0')}'
      '${_selectedColor.green.toRadixString(16).padLeft(2, '0')}'
      '${_selectedColor.blue.toRadixString(16).padLeft(2, '0')}',
    );
    await prefs.setString(
      'accent_color',
      '${_selectedAccent.red.toRadixString(16).padLeft(2, '0')}'
      '${_selectedAccent.green.toRadixString(16).padLeft(2, '0')}'
      '${_selectedAccent.blue.toRadixString(16).padLeft(2, '0')}',
    );

    final success = await _bleService.sendConfig(
      brightnessOffset: _brightnessOffset.toInt(),
      color: _selectedColor,
      accent: _selectedAccent,
    );
    
    setState(() {
      _isSending = false;
    });
    
    if (success) {
      _setStatus('Settings applied to EyeSwap');
    } else {
      _setStatus('Failed to send. Check BLE connection to EyeSwap device.');
    }
  }

  Future<void> _testSettings() async {
    setState(() {
      _isSending = true;
    });
    
    final success = await _bleService.sendTestFlash(
      _selectedColor,
      _brightnessOffset.toInt(),
    );
    
    setState(() {
      _isSending = false;
    });
    
    if (success) {
      _setStatus('Test flash sent to EyeSwap');
    } else {
      _setStatus('Failed to send. Check BLE connection to EyeSwap device.');
    }
  }

  String _getBrightnessLabel() {
    final offset = _brightnessOffset.toInt();
    if (offset == 0) return 'Default';
    if (offset > 0) return '+$offset%';
    return '$offset%';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A0A),
      appBar: AppBar(
        title: const Text(
          'EYESWAP SETTINGS',
          style: TextStyle(
            fontWeight: FontWeight.bold,
            letterSpacing: 2,
          ),
        ),
        backgroundColor: const Color(0xFF1A1A1A),
        foregroundColor: Colors.white,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Header
            const Text(
              'DISPLAY CONFIGURATION',
              style: TextStyle(
                color: Colors.white70,
                fontSize: 12,
                letterSpacing: 3,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 24),
            
            // Brightness Section
            _buildSectionTitle('Brightness Offset'),
            const SizedBox(height: 8),
            const Text(
              'Adjust display brightness relative to default',
              style: TextStyle(color: Colors.white54, fontSize: 12),
            ),
            const SizedBox(height: 16),
            
            // Brightness Slider
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(12),
              ),
              child: Column(
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      const Text('-50%', style: TextStyle(color: Colors.white54)),
                      Text(
                        _getBrightnessLabel(),
                        style: const TextStyle(
                          color: Color(0xFFFFAA00),
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const Text('+50%', style: TextStyle(color: Colors.white54)),
                    ],
                  ),
                  Slider(
                    value: _brightnessOffset,
                    min: -50,
                    max: 50,
                    divisions: 20,
                    activeColor: const Color(0xFFFFAA00),
                    inactiveColor: Colors.white24,
                    onChanged: (value) {
                      setState(() {
                        _brightnessOffset = value;
                      });
                    },
                  ),
                ],
              ),
            ),
            
            const SizedBox(height: 32),
            
            // Color Section
            _buildSectionTitle('Background Color'),
            const SizedBox(height: 8),
            const Text(
              'Choose a color to match your dash',
              style: TextStyle(color: Colors.white54, fontSize: 12),
            ),
            const SizedBox(height: 16),
            
            // Color Presets
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: _presetColors.map((preset) {
                final isSelected = _selectedColor == preset['color'];
                return _buildColorButton(
                  name: preset['name'] as String,
                  color: preset['color'] as Color,
                  isSelected: isSelected,
                  onTap: () {
                    setState(() {
                      _selectedColor = preset['color'] as Color;
                    });
                  },
                );
              }).toList(),
            ),
            
            const SizedBox(height: 32),
            
            // Accent (secondary) color
            _buildSectionTitle('Accent Color'),
            const SizedBox(height: 8),
            const Text(
              'Second color the animations use (fire core, boid leaders, radar '
              'sweep, pong paddles...). Pick the same as the primary for a '
              'single-color look.',
              style: TextStyle(color: Colors.white54, fontSize: 12),
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: _presetColors.map((preset) {
                final isSelected = _selectedAccent == preset['color'];
                return _buildColorButton(
                  name: preset['name'] as String,
                  color: preset['color'] as Color,
                  isSelected: isSelected,
                  onTap: () {
                    setState(() {
                      _selectedAccent = preset['color'] as Color;
                    });
                  },
                );
              }).toList(),
            ),
            const SizedBox(height: 8),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton.icon(
                onPressed: () {
                  setState(() => _selectedAccent = _selectedColor);
                },
                icon: const Icon(Icons.link, size: 18),
                label: const Text('Match primary'),
                style: TextButton.styleFrom(
                    foregroundColor: const Color(0xFFFFAA00)),
              ),
            ),
            
            const SizedBox(height: 32),
            
            // Preview
            _buildSectionTitle('Preview'),
            const SizedBox(height: 16),
            Center(
              child: Container(
                width: 120,
                height: 120,
                decoration: BoxDecoration(
                  color: _selectedColor,
                  shape: BoxShape.circle,
                  border: Border.all(
                    color: _selectedAccent,
                    width: 3,
                  ),
                  boxShadow: [
                    BoxShadow(
                      color: _selectedColor.withOpacity(0.3),
                      blurRadius: 20,
                      spreadRadius: 5,
                    ),
                  ],
                ),
                child: Center(
                  child: Text(
                    '${_brightnessOffset.toInt().abs()}%',
                    style: TextStyle(
                      color: _selectedColor == const Color(0xFFFFFFFF) 
                          ? Colors.black 
                          : Colors.white,
                      fontWeight: FontWeight.bold,
                      fontSize: 16,
                    ),
                  ),
                ),
              ),
            ),
            
            const SizedBox(height: 32),
            
            // Status Message
            if (_statusMessage.isNotEmpty)
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: _statusMessage.contains('Failed') 
                      ? Colors.red.withOpacity(0.2) 
                      : Colors.green.withOpacity(0.2),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(
                    color: _statusMessage.contains('Failed') 
                        ? Colors.red 
                        : Colors.green,
                  ),
                ),
                child: Text(
                  _statusMessage,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    color: _statusMessage.contains('Failed') 
                        ? Colors.red 
                        : Colors.green,
                  ),
                ),
              ),
            
            const SizedBox(height: 24),
            
            // Action Buttons
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isSending ? null : _testSettings,
                    icon: _isSending 
                        ? const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              valueColor: AlwaysStoppedAnimation(Colors.black),
                            ),
                          )
                        : const Icon(Icons.flash_on),
                    label: const Text('TEST'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.white24,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  flex: 2,
                  child: ElevatedButton.icon(
                    onPressed: _isSending ? null : _applySettings,
                    icon: _isSending 
                        ? const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              valueColor: AlwaysStoppedAnimation(Colors.black),
                            ),
                          )
                        : const Icon(Icons.check),
                    label: const Text('APPLY TO EYESWAP'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFFFFAA00),
                      foregroundColor: Colors.black,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                  ),
                ),
              ],
            ),
            
            const SizedBox(height: 24),

            // Button Config Navigation
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(
                  color: const Color(0xFFFFAA00).withOpacity(0.3),
                  width: 1,
                ),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'BUTTON CONFIGURATION',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 10,
                      letterSpacing: 1,
                    ),
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'Remap button functions without hardware changes',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                  const SizedBox(height: 12),
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton.icon(
                      onPressed: () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => const ButtonConfigScreen(),
                          ),
                        );
                      },
                      icon: const Icon(Icons.gamepad, color: Colors.black),
                      label: const Text(
                        'CONFIGURE BUTTONS',
                        style: TextStyle(fontWeight: FontWeight.bold),
                      ),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFFFFAA00),
                        foregroundColor: Colors.black,
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(8),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
            
            const SizedBox(height: 24),

            // Idle Animation Selection
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'IDLE ANIMATION',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 10,
                      letterSpacing: 1,
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Display mode when no media playing',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                  const SizedBox(height: 12),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: _idleAnimations.map((anim) {
                      final isSelected = _idleAnimation == anim['id'];
                      return ChoiceChip(
                        label: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(
                              anim['icon'] as IconData,
                              size: 16,
                              color: isSelected ? Colors.black : Colors.white54,
                            ),
                            const SizedBox(width: 6),
                            Text(anim['name'] as String),
                          ],
                        ),
                        selected: isSelected,
                        onSelected: (_) => _saveIdleAnimation(anim['id'] as String),
                        backgroundColor: const Color(0xFF0A0A0A),
                        selectedColor: const Color(0xFFFFAA00),
                        labelStyle: TextStyle(
                          color: isSelected ? Colors.black : Colors.white,
                          fontSize: 12,
                        ),
                      );
                    }).toList(),
                  ),
                ],
              ),
            ),
            
            const SizedBox(height: 24),
            
            // Connection Info
            Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'CONNECTION INFO',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 10,
                      letterSpacing: 1,
                    ),
                  ),
                  SizedBox(height: 8),
                  Text(
                    'BLE Device: EyeSwap',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                  Text(
                    'Auto-connects when in range',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSectionTitle(String title) {
    return Text(
      title.toUpperCase(),
      style: const TextStyle(
        color: Colors.white,
        fontSize: 14,
        fontWeight: FontWeight.bold,
        letterSpacing: 1,
      ),
    );
  }

  Widget _buildColorButton({
    required String name,
    required Color color,
    required bool isSelected,
    required VoidCallback onTap,
  }) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 80,
        padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 8),
        decoration: BoxDecoration(
          color: isSelected ? color.withOpacity(0.3) : const Color(0xFF1A1A1A),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: isSelected ? color : Colors.white24,
            width: isSelected ? 2 : 1,
          ),
        ),
        child: Column(
          children: [
            Container(
              width: 32,
              height: 32,
              decoration: BoxDecoration(
                color: color,
                shape: BoxShape.circle,
                border: Border.all(
                  color: Colors.white24,
                  width: 1,
                ),
              ),
            ),
            const SizedBox(height: 8),
            Text(
              name,
              style: TextStyle(
                color: isSelected ? Colors.white : Colors.white54,
                fontSize: 10,
                fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
