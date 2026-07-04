import 'package:flutter/material.dart';
import '../services/esp32_ble.dart';

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
  
  // Selected color
  Color _selectedColor = const Color(0xFFFFAA00); // Default amber
  
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

  @override
  void initState() {
    super.initState();
    _bleService.initialize();
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
    
    final success = await _bleService.sendConfig(
      brightnessOffset: _brightnessOffset.toInt(),
      color: _selectedColor,
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
                    color: Colors.white24,
                    width: 2,
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
