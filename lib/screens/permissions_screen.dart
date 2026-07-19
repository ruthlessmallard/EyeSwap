import 'package:flutter/material.dart';
import '../services/media_controller.dart';

class PermissionsScreen extends StatefulWidget {
  const PermissionsScreen({super.key});

  @override
  State<PermissionsScreen> createState() => _PermissionsScreenState();
}

class _PermissionsScreenState extends State<PermissionsScreen> {
  final MediaController _mediaController = MediaController();
  bool _notificationAccess = false;
  bool _accessibilityService = false;
  bool _isChecking = false;

  @override
  void initState() {
    super.initState();
    _checkPermissions();
  }

  Future<void> _checkPermissions() async {
    setState(() => _isChecking = true);
    
    final notificationEnabled = await _mediaController.isNotificationAccessEnabled();
    final accessibilityEnabled = await _mediaController.isAccessibilityServiceEnabled();
    
    setState(() {
      _notificationAccess = notificationEnabled;
      _accessibilityService = accessibilityEnabled;
      _isChecking = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final bool allPermissionsGranted = _notificationAccess && _accessibilityService;
    
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A0A),
      appBar: AppBar(
        title: const Text('EyeSwap Setup'),
        backgroundColor: Colors.transparent,
        elevation: 0,
      ),
      body: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Required Permissions',
              style: TextStyle(
                fontSize: 28,
                fontWeight: FontWeight.bold,
                color: Colors.white,
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              'EyeSwap needs these permissions for hands-free vehicle operation:',
              style: TextStyle(
                fontSize: 16,
                color: Colors.white70,
              ),
            ),
            const SizedBox(height: 32),
            
            // Notification Access Card
            _PermissionCard(
              title: 'Notification Access',
              description: 'Required to control media playback from other apps',
              isEnabled: _notificationAccess,
              onTap: () async {
                await _mediaController.openNotificationSettings();
                // Check again after user returns
                Future.delayed(const Duration(milliseconds: 500), _checkPermissions);
              },
            ),
            
            const SizedBox(height: 16),
            
            // Accessibility Service Card  
            _PermissionCard(
              title: 'Accessibility Service',
              description: 'Auto-taps YouTube Music offline popups for hands-free use',
              isEnabled: _accessibilityService,
              onTap: () async {
                await _mediaController.openAccessibilitySettings();
                // Check again after user returns
                Future.delayed(const Duration(milliseconds: 500), _checkPermissions);
              },
            ),
            
            const Spacer(),
            
            // Status and Continue Button
            if (_isChecking)
              const Center(
                child: CircularProgressIndicator(color: Color(0xFFD32F2F)),
              )
            else if (allPermissionsGranted)
              Column(
                children: [
                  const Row(
                    children: [
                      Icon(Icons.check_circle, color: Colors.green, size: 24),
                      SizedBox(width: 8),
                      Text(
                        'All permissions granted!',
                        style: TextStyle(
                          color: Colors.green,
                          fontSize: 16,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton(
                      onPressed: () {
                        Navigator.of(context).pop();
                      },
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFFD32F2F),
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 16),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(8),
                        ),
                      ),
                      child: const Text(
                        'Continue to EyeSwap',
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
                      ),
                    ),
                  ),
                ],
              )
            else
              Column(
                children: [
                  const Row(
                    children: [
                      Icon(Icons.warning, color: Colors.orange, size: 24),
                      SizedBox(width: 8),
                      Text(
                        'Setup required for full functionality',
                        style: TextStyle(
                          color: Colors.orange,
                          fontSize: 16,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton(
                      onPressed: _checkPermissions,
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFF424242),
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 16),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(8),
                        ),
                      ),
                      child: const Text(
                        'Refresh Status',
                        style: TextStyle(fontSize: 16),
                      ),
                    ),
                  ),
                ],
              ),
          ],
        ),
      ),
    );
  }
}

class _PermissionCard extends StatelessWidget {
  final String title;
  final String description;
  final bool isEnabled;
  final VoidCallback onTap;

  const _PermissionCard({
    required this.title,
    required this.description,
    required this.isEnabled,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      color: const Color(0xFF1A1A1A),
      child: InkWell(
        onTap: isEnabled ? null : onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Icon(
                isEnabled ? Icons.check_circle : Icons.settings,
                color: isEnabled ? Colors.green : const Color(0xFFD32F2F),
                size: 32,
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.w600,
                        color: Colors.white,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      description,
                      style: const TextStyle(
                        fontSize: 14,
                        color: Colors.white70,
                      ),
                    ),
                  ],
                ),
              ),
              Icon(
                isEnabled ? Icons.check : Icons.arrow_forward_ios,
                color: isEnabled ? Colors.green : Colors.white54,
                size: 20,
              ),
            ],
          ),
        ),
      ),
    );
  }
}