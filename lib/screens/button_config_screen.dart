import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Button configuration screen
/// Map each button (1-3) short and long press to functions
class ButtonConfigScreen extends StatefulWidget {
  const ButtonConfigScreen({super.key});

  @override
  State<ButtonConfigScreen> createState() => _ButtonConfigScreenState();
}

class _ButtonConfigScreenState extends State<ButtonConfigScreen> {
  final List<Map<String, dynamic>> _availableFunctions = [
    {'id': 'yt_music', 'name': 'YouTube Music', 'icon': Icons.music_note},
    {'id': 'yt_downloads', 'name': 'YT Music Downloads', 'icon': Icons.download},
    {'id': 'audible', 'name': 'Audible', 'icon': Icons.menu_book},
    {'id': 'skip_back', 'name': 'Skip Back 30s', 'icon': Icons.replay_30},
    {'id': 'play_pause', 'name': 'Play/Pause', 'icon': Icons.play_arrow},
    {'id': 'accept_call', 'name': 'Accept Call', 'icon': Icons.call},
    {'id': 'gemini', 'name': 'Gemini Voice', 'icon': Icons.mic},
    {'id': 'none', 'name': 'None', 'icon': Icons.block},
  ];

  // Default mappings
  Map<String, String> _buttonMappings = {
    'btn1_short': 'yt_downloads',
    'btn1_long': 'play_pause',
    'btn2_short': 'audible',
    'btn2_long': 'skip_back',
    'btn3_short': 'accept_call',
    'btn3_long': 'gemini',
  };

  bool _isLoading = true;

  @override
  void initState() {
    super.initState();
    _loadMappings();
  }

  Future<void> _loadMappings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _buttonMappings['btn1_short'] = prefs.getString('btn1_short') ?? 'yt_downloads';
      _buttonMappings['btn1_long'] = prefs.getString('btn1_long') ?? 'play_pause';
      _buttonMappings['btn2_short'] = prefs.getString('btn2_short') ?? 'audible';
      _buttonMappings['btn2_long'] = prefs.getString('btn2_long') ?? 'skip_back';
      _buttonMappings['btn3_short'] = prefs.getString('btn3_short') ?? 'accept_call';
      _buttonMappings['btn3_long'] = prefs.getString('btn3_long') ?? 'gemini';
      _isLoading = false;
    });
  }

  Future<void> _saveMapping(String key, String value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(key, value);
    setState(() {
      _buttonMappings[key] = value;
    });
  }

  String _getFunctionName(String id) {
    final func = _availableFunctions.firstWhere(
      (f) => f['id'] == id,
      orElse: () => {'name': 'Unknown'},
    );
    return func['name'] as String;
  }

  IconData _getFunctionIcon(String id) {
    final func = _availableFunctions.firstWhere(
      (f) => f['id'] == id,
      orElse: () => {'icon': Icons.help},
    );
    return func['icon'] as IconData;
  }

  void _showFunctionPicker(String buttonKey, String currentValue) {
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xFF1A1A1A),
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
      ),
      builder: (context) {
        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                margin: const EdgeInsets.only(top: 8),
                width: 40,
                height: 4,
                decoration: BoxDecoration(
                  color: Colors.white24,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
              const Padding(
                padding: EdgeInsets.all(16),
                child: Text(
                  'SELECT FUNCTION',
                  style: TextStyle(
                    color: Colors.white70,
                    fontSize: 12,
                    letterSpacing: 2,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
              ..._availableFunctions.map((func) {
                final isSelected = func['id'] == currentValue;
                return ListTile(
                  leading: Icon(
                    func['icon'] as IconData,
                    color: isSelected ? const Color(0xFFFFAA00) : Colors.white54,
                  ),
                  title: Text(
                    func['name'] as String,
                    style: TextStyle(
                      color: isSelected ? const Color(0xFFFFAA00) : Colors.white,
                      fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
                    ),
                  ),
                  trailing: isSelected
                      ? const Icon(Icons.check, color: Color(0xFFFFAA00))
                      : null,
                  onTap: () {
                    _saveMapping(buttonKey, func['id'] as String);
                    Navigator.pop(context);
                  },
                );
              }),
              const SizedBox(height: 16),
            ],
          ),
        );
      },
    );
  }

  Widget _buildButtonConfig(String title, String buttonKey, String description) {
    final currentValue = _buttonMappings[buttonKey] ?? 'none';
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(12),
      ),
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        leading: Container(
          width: 48,
          height: 48,
          decoration: BoxDecoration(
            color: const Color(0xFF0A0A0A),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Icon(
            _getFunctionIcon(currentValue),
            color: const Color(0xFFFFAA00),
            size: 24,
          ),
        ),
        title: Text(
          title,
          style: const TextStyle(
            color: Colors.white,
            fontWeight: FontWeight.bold,
            fontSize: 14,
          ),
        ),
        subtitle: Text(
          description,
          style: const TextStyle(
            color: Colors.white54,
            fontSize: 12,
          ),
        ),
        trailing: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xFFFFAA00).withOpacity(0.2),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: const Color(0xFFFFAA00),
              width: 1,
            ),
          ),
          child: Text(
            _getFunctionName(currentValue),
            style: const TextStyle(
              color: Color(0xFFFFAA00),
              fontSize: 12,
              fontWeight: FontWeight.bold,
            ),
          ),
        ),
        onTap: () => _showFunctionPicker(buttonKey, currentValue),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(
        backgroundColor: Color(0xFF0A0A0A),
        body: Center(
          child: CircularProgressIndicator(
            color: Color(0xFFFFAA00),
          ),
        ),
      );
    }

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A0A),
      appBar: AppBar(
        title: const Text(
          'BUTTON CONFIG',
          style: TextStyle(
            fontWeight: FontWeight.bold,
            letterSpacing: 2,
          ),
        ),
        backgroundColor: const Color(0xFF1A1A1A),
        foregroundColor: Colors.white,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Header
            const Text(
              'BUTTON MAPPING',
              style: TextStyle(
                color: Colors.white70,
                fontSize: 12,
                letterSpacing: 3,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              'Tap any button to change its function',
              style: TextStyle(
                color: Colors.white54,
                fontSize: 12,
              ),
            ),
            const SizedBox(height: 24),

            // Button 1
            _buildSectionTitle('Button 1 (Red)'),
            const SizedBox(height: 12),
            _buildButtonConfig(
              'Short Press',
              'btn1_short',
              'Quick tap action',
            ),
            _buildButtonConfig(
              'Long Press',
              'btn1_long',
              'Hold for 500ms',
            ),
            const SizedBox(height: 24),

            // Button 2
            _buildSectionTitle('Button 2 (Red)'),
            const SizedBox(height: 12),
            _buildButtonConfig(
              'Short Press',
              'btn2_short',
              'Quick tap action',
            ),
            _buildButtonConfig(
              'Long Press',
              'btn2_long',
              'Hold for 500ms',
            ),
            const SizedBox(height: 24),

            // Button 3
            _buildSectionTitle('Button 3 (Gray)'),
            const SizedBox(height: 12),
            _buildButtonConfig(
              'Short Press',
              'btn3_short',
              'Quick tap action',
            ),
            _buildButtonConfig(
              'Long Press',
              'btn3_long',
              'Hold for 500ms',
            ),
            const SizedBox(height: 24),

            // Reset button
            Center(
              child: TextButton.icon(
                onPressed: () async {
                  final prefs = await SharedPreferences.getInstance();
                  await prefs.clear();
                  await _loadMappings();
                  if (mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text('Reset to defaults'),
                        backgroundColor: Color(0xFFFFAA00),
                      ),
                    );
                  }
                },
                icon: const Icon(Icons.restore, color: Colors.white54),
                label: const Text(
                  'RESET TO DEFAULTS',
                  style: TextStyle(color: Colors.white54),
                ),
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
}
