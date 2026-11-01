import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/ble_handler.dart';
import 'button_config_screen.dart';

/// Settings screen for configuring ESP32 display
/// Brightness offset and background color
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.bleHandler});

  /// The controller's live BLE handler. Settings must never open its own
  /// connection: the pod accepts one client, so a second one boots the first.
  final BleHandler bleHandler;

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  // Brightness offset: -50 to +50, default 0
  double _brightnessOffset = 0;
  
  // Selected colors: primary + secondary accent (single-color = both equal)
  Color _selectedColor = const Color(0xFFFFAA00); // Default amber
  Color _selectedAccent = const Color(0xFFFFFFFF); // Default white
  
  // Preset colors for common desk colors
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
  String _idleAnimation = 'static'; // firmware ids; marquee is a link indicator, not a mode

  // What a bare (unlinked) pod runs. 'marquee' = firmware default: the link
  // indicator, then the linked choice after the hold window.
  String _unlinkedAnimation = 'marquee';

  // Random Cycle rosters. Two of them: a linked pod can rotate through
  // Now Playing, a bare pod cannot, so the two sets are chosen separately.
  List<String> _cycleConnected = List.of(BleHandler.defaultCycleRoster);
  List<String> _cycleUnlinked = List.of(BleHandler.defaultCycleRoster);
  
  // Every id here is a string the firmware accepts today. Category is only for
  // layout -- the pod does not care.
  static const List<String> _idleCategories = [
    'Games',
    'Pets',
    'Living',
    'Utility',
  ];

  final List<Map<String, dynamic>> _idleAnimations = [
    // --- Games: the button does something ---
    {'id': 'knight', 'name': 'Knight Run', 'category': 'Games', 'icon': Icons.directions_run},
    {'id': 'eye', 'name': 'Quickdraw', 'category': 'Games', 'icon': Icons.adjust},
    // --- Pets: a persistent creature, one or two clicks ---
    {'id': 'pet', 'name': 'Goo Pet', 'category': 'Pets', 'icon': Icons.pets},
    {'id': 'boids', 'name': 'Fish', 'category': 'Pets', 'icon': Icons.water},
    // --- Living systems: no input, they just grow ---
    {'id': 'static', 'name': 'Game of Life', 'category': 'Living', 'icon': Icons.grain},
    {'id': 'fire', 'name': 'Fire', 'category': 'Living', 'icon': Icons.local_fire_department},
    {'id': 'crystal', 'name': 'Crystal', 'category': 'Living', 'icon': Icons.diamond},
    // --- Utility ---
    {'id': 'now_playing', 'name': 'Now Playing', 'category': 'Utility', 'icon': Icons.music_note},
    {'id': 'cycle', 'name': 'Random Cycle', 'category': 'Utility', 'icon': Icons.shuffle},
  ];

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }
  
  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      // A preference saved by an older build may name something the pod no
      // longer treats as an animation (e.g. marquee). Fall back to the pod's
      // own default instead of pushing a stale id on connect.
      final savedIdle = prefs.getString('idle_animation');
      final known = _idleAnimations.any((a) => a['id'] == savedIdle);
      _idleAnimation = known ? savedIdle! : 'static';
      final savedUnlinked = prefs.getString('unlinked_animation');
      _unlinkedAnimation = (savedUnlinked == null || savedUnlinked.isEmpty)
          ? 'marquee'
          : savedUnlinked;
      final savedConn = prefs.getStringList('cycle_connected');
      final savedUnl = prefs.getStringList('cycle_unlinked');
      _cycleConnected = (savedConn == null || savedConn.isEmpty)
          ? List.of(BleHandler.defaultCycleRoster)
          : List.of(savedConn);
      _cycleUnlinked = (savedUnl == null || savedUnl.isEmpty)
          ? List.of(BleHandler.defaultCycleRoster)
          : List.of(savedUnl);
      // _applySettings stores an int; reading it back as a double throws on
      // the type mismatch and blanked Settings on the second open.
      _brightnessOffset = (prefs.getInt('brightness_offset') ?? 0).toDouble();
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
    final sent = await widget.bleHandler.sendMode(animation);
    if (sent) {
      _setStatus('Idle animation: $animation');
    } else {
      _setStatus('Saved. Connect to EyeSwap to apply.');
    }
  }

  /// Choose what a bare pod runs. Persisted separately from the while-connected
  /// choice; the firmware treats this as persist-only while linked.
  Future<void> _saveUnlinkedAnimation(String id) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('unlinked_animation', id);
    setState(() {
      _unlinkedAnimation = id;
    });
    final sent = await widget.bleHandler.sendMode(id, link: 'unlinked');
    if (sent) {
      _setStatus('Unlinked animation: $id');
    } else {
      _setStatus('Saved. Connect to EyeSwap to apply.');
    }
  }

  /// Standard presets + a custom entry. The custom tile opens an RGB-slider /
  /// hex dialog; the picked color rides the same send path as a preset.
  static const Color _customSlotColor = Color(0xFF37474F);

  bool _isCustom(Color c) =>
      !_presetColors.any((p) => p['color'] == c);

  Future<void> _pickCustomColor({required bool accent}) async {
    final initial = accent ? _selectedAccent : _selectedColor;
    final picked = await showDialog<Color>(
      context: context,
      builder: (dialogContext) => _CustomColorDialog(initial: initial),
    );
    if (picked == null) return;
    setState(() {
      if (accent) {
        _selectedAccent = picked;
      } else {
        _selectedColor = picked;
      }
    });
    _setStatus('Custom color picked - APPLY to send it');
  }

  Widget _customColorTile({required bool accent}) {
    final current = accent ? _selectedAccent : _selectedColor;
    final isCustom = _isCustom(current);
    return GestureDetector(
      onTap: () => _pickCustomColor(accent: accent),
      child: Container(
        width: 80,
        padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 8),
        decoration: BoxDecoration(
          color: isCustom ? current.withOpacity(0.3) : const Color(0xFF1A1A1A),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: isCustom ? current : Colors.white24,
            width: isCustom ? 2 : 1,
          ),
        ),
        child: Column(
          children: [
            Container(
              width: 32,
              height: 32,
              decoration: BoxDecoration(
                color: isCustom ? current : _customSlotColor,
                shape: BoxShape.circle,
                border: Border.all(color: Colors.white24, width: 1),
              ),
              child: isCustom
                  ? const SizedBox.shrink()
                  : const Icon(Icons.colorize, size: 18, color: Colors.white70),
            ),
            const SizedBox(height: 8),
            Text(
              isCustom ? 'CUSTOM' : 'Custom',
              style: TextStyle(
                color: isCustom ? Colors.white : Colors.white54,
                fontSize: 10,
                fontWeight: isCustom ? FontWeight.bold : FontWeight.normal,
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Toggle one animation in one roster, persist both, and push to the pod.
  Future<void> _toggleCycle(String id, {required bool connected}) async {
    final target = connected ? _cycleConnected : _cycleUnlinked;
    if (target.contains(id) && target.length <= 1) {
      _setStatus('Keep at least one animation in the cycle.');
      return;
    }
    setState(() {
      if (target.contains(id)) {
        target.remove(id);
      } else {
        target.add(id);
      }
    });
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList('cycle_connected', _cycleConnected);
    await prefs.setStringList('cycle_unlinked', _cycleUnlinked);
    final sent = await widget.bleHandler.sendCycleConfig(
      connected: _cycleConnected,
      unlinked: _cycleUnlinked,
    );
    if (sent) {
      _setStatus('Random Cycle updated');
    } else {
      _setStatus('Saved. Connect to EyeSwap to apply.');
    }
  }

  Widget _cycleChips({required bool connected}) {
    final selected = connected ? _cycleConnected : _cycleUnlinked;
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: _idleAnimations
          .where((a) => a['id'] != 'cycle')
          .map((anim) {
        final id = anim['id'] as String;
        final isOn = selected.contains(id);
        return FilterChip(
          label: Text(anim['name'] as String),
          selected: isOn,
          onSelected: (_) => _toggleCycle(id, connected: connected),
          backgroundColor: const Color(0xFF0A0A0A),
          selectedColor: const Color(0xFFFFAA00),
          checkmarkColor: Colors.black,
          labelStyle: TextStyle(
            color: isOn ? Colors.black : Colors.white,
            fontSize: 12,
          ),
        );
      }).toList(),
    );
  }

  /// '#RRGGBB' uppercase, matching the firmware's parser.
  static String _rgbHex(Color c) {
    final v = '${c.red.toRadixString(16).padLeft(2, '0')}'
        '${c.green.toRadixString(16).padLeft(2, '0')}'
        '${c.blue.toRadixString(16).padLeft(2, '0')}';
    return '#${v.toUpperCase()}';
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

    final success = await widget.bleHandler.sendDisplayConfig(
      brightnessOffset: _brightnessOffset.toInt(),
      colorHex: _rgbHex(_selectedColor),
      accentHex: _rgbHex(_selectedAccent),
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
    
    final success = await widget.bleHandler.sendDisplayConfig(
      brightnessOffset: _brightnessOffset.toInt(),
      colorHex: _rgbHex(_selectedColor),
      accentHex: _rgbHex(_selectedAccent),
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
              'Choose a color to match your desk',
              style: TextStyle(color: Colors.white54, fontSize: 12),
            ),
            const SizedBox(height: 16),
            
            // Color Presets + Custom
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                ..._presetColors.map((preset) {
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
                }),
                _customColorTile(accent: false),
              ],
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
              children: [
                ..._presetColors.map((preset) {
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
                }),
                _customColorTile(accent: true),
              ],
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
                    'ANIMATIONS',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 10,
                      letterSpacing: 1,
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'What the pod runs, chosen per link state.',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'WHEN CONNECTED',
                    style: TextStyle(
                      color: Colors.white38,
                      fontSize: 10,
                      letterSpacing: 1.2,
                    ),
                  ),
                  const SizedBox(height: 6),
                  for (final cat in _idleCategories)
                    if (_idleAnimations.any((a) => a['category'] == cat)) ...[
                      Padding(
                        padding: const EdgeInsets.only(top: 10, bottom: 6),
                        child: Align(
                          alignment: Alignment.centerLeft,
                          child: Text(
                            cat.toUpperCase(),
                            style: const TextStyle(
                              color: Colors.white38,
                              fontSize: 10,
                              letterSpacing: 1.2,
                            ),
                          ),
                        ),
                      ),
                      Wrap(
                        spacing: 8,
                        runSpacing: 8,
                        children: _idleAnimations
                            .where((a) => a['category'] == cat)
                            .map((anim) {
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
                            onSelected: (_) =>
                                _saveIdleAnimation(anim['id'] as String),
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
                  const SizedBox(height: 14),
                  const Text(
                    'WHEN DISCONNECTED',
                    style: TextStyle(
                      color: Colors.white38,
                      fontSize: 10,
                      letterSpacing: 1.2,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final anim in _idleAnimations)
                        if (anim['id'] != 'cycle' && anim['id'] != 'now_playing')
                          _unlinkedChip(anim),
                      ChoiceChip(
                        label: const Text('Marquee (default)'),
                        selected: _unlinkedAnimation == 'marquee',
                        onSelected: (_) => _saveUnlinkedAnimation('marquee'),
                        backgroundColor: const Color(0xFF0A0A0A),
                        selectedColor: const Color(0xFFFFAA00),
                        labelStyle: TextStyle(
                          color: _unlinkedAnimation == 'marquee'
                              ? Colors.black
                              : Colors.white,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Runs on a bare pod (app closed or out of range). '
                    "Marquee keeps the stock 'EYESWAP' link indicator.",
                    style: TextStyle(color: Colors.white38, fontSize: 10),
                  ),
                ],
              ),
            ),
            
            const SizedBox(height: 24),

            // Random Cycle rosters
            Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'RANDOM CYCLE',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 10,
                      letterSpacing: 1,
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'What rotates, chosen per link state. Tap to include or drop an animation.',
                    style: TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'WHEN CONNECTED',
                    style: TextStyle(
                      color: Colors.white38,
                      fontSize: 10,
                      letterSpacing: 1.2,
                    ),
                  ),
                  const SizedBox(height: 6),
                  _cycleChips(connected: true),
                  const SizedBox(height: 14),
                  const Text(
                    'WHEN DISCONNECTED',
                    style: TextStyle(
                      color: Colors.white38,
                      fontSize: 10,
                      letterSpacing: 1.2,
                    ),
                  ),
                  const SizedBox(height: 6),
                  _cycleChips(connected: false),
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

  Widget _unlinkedChip(Map<String, dynamic> anim) {
    final id = anim['id'] as String;
    final isSelected = _unlinkedAnimation == id;
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
      onSelected: (_) => _saveUnlinkedAnimation(id),
      backgroundColor: const Color(0xFF0A0A0A),
      selectedColor: const Color(0xFFFFAA00),
      labelStyle: TextStyle(
        color: isSelected ? Colors.black : Colors.white,
        fontSize: 12,
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

/// RGB sliders + hex entry + live preview. Returns the picked [Color] on
/// save, null on dismiss. No packages: the pod's parser takes #RRGGBB, so the
/// hex field is the source of truth and the sliders drive it.
class _CustomColorDialog extends StatefulWidget {
  const _CustomColorDialog({required this.initial});
  final Color initial;

  @override
  State<_CustomColorDialog> createState() => _CustomColorDialogState();
}

class _CustomColorDialogState extends State<_CustomColorDialog> {
  late int _r = widget.initial.red;
  late int _g = widget.initial.green;
  late int _b = widget.initial.blue;
  late final TextEditingController _hex =
      TextEditingController(text: _hexText());
  bool _updatingHex = false;

  String _hexText() => '${_r.toRadixString(16).padLeft(2, '0')}'
      '${_g.toRadixString(16).padLeft(2, '0')}'
      '${_b.toRadixString(16).padLeft(2, '0')}';

  void _fromSliders() {
    _updatingHex = true;
    _hex.text = _hexText();
    _updatingHex = false;
  }

  void _fromHex(String v) {
    if (_updatingHex) return;
    final clean = v.replaceAll('#', '').trim();
    if (clean.length == 6 && int.tryParse(clean, radix: 16) != null) {
      final n = int.parse(clean, radix: 16);
      setState(() {
        _r = (n >> 16) & 0xFF;
        _g = (n >> 8) & 0xFF;
        _b = n & 0xFF;
      });
    }
  }

  Color get _color => Color.fromARGB(255, _r, _g, _b);

  Slider _channel(String label, int value, ValueChanged<double> onChanged) {
    return Slider(
      value: value.toDouble(),
      min: 0,
      max: 255,
      label: '$value',
      activeColor: label == 'R'
          ? const Color(0xFFE53935)
          : label == 'G'
              ? const Color(0xFF43A047)
              : const Color(0xFF1E88E5),
      onChanged: onChanged,
    );
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: const Color(0xFF1A1A1A),
      title: const Text('Custom color',
          style: TextStyle(color: Colors.white, fontSize: 16)),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 72,
            height: 72,
            decoration: BoxDecoration(
              color: _color,
              shape: BoxShape.circle,
              border: Border.all(color: Colors.white24),
            ),
          ),
          const SizedBox(height: 16),
          _channel('R', _r, (v) => setState(() { _r = v.round(); _fromSliders(); })),
          _channel('G', _g, (v) => setState(() { _g = v.round(); _fromSliders(); })),
          _channel('B', _b, (v) => setState(() { _b = v.round(); _fromSliders(); })),
          const SizedBox(height: 8),
          TextField(
            controller: _hex,
            onChanged: _fromHex,
            maxLength: 7,
            style: const TextStyle(color: Colors.white),
            decoration: const InputDecoration(
              prefixText: '#',
              prefixStyle: TextStyle(color: Colors.white54),
              hintText: 'RRGGBB',
              counterText: '',
              border: OutlineInputBorder(),
            ),
          ),
        ],
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('CANCEL'),
        ),
        ElevatedButton(
          style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFFFFAA00),
            foregroundColor: Colors.black,
          ),
          onPressed: () => Navigator.pop(context, _color),
          child: const Text('USE THIS COLOR'),
        ),
      ],
    );
  }
}