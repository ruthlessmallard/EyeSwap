import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// First-launch scope notice. Deadpan on the surface, but the core
/// message is real: EyeSwap is a desk gadget, not a safety device and
/// not a workaround for workplace media rules. Tone borrowed from the
/// README (which assumes zero liability for your mother being
/// disappointed in you).
///
/// Sets prefs key `disclaimer_accepted` when the user accepts
/// responsibility.
class DisclaimerScreen extends StatefulWidget {
  /// When false (first-launch gate) the user cannot back out.
  final bool dismissible;

  const DisclaimerScreen({super.key, this.dismissible = false});

  @override
  State<DisclaimerScreen> createState() => _DisclaimerScreenState();
}

class _DisclaimerScreenState extends State<DisclaimerScreen> {
  static const _red = Color(0xFFD32F2F);
  static const _amber = Color(0xFFFFAA00);

  Future<void> _accept() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('disclaimer_accepted', true);
    if (!mounted) return;
    Navigator.of(context).pop(true);
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: widget.dismissible,
      child: Scaffold(
        backgroundColor: const Color(0xFF0A0A0A),
        appBar: AppBar(
          automaticallyImplyLeading: widget.dismissible,
          title: const Text(
            'EYESWAP SCOPE NOTICE',
            style: TextStyle(fontWeight: FontWeight.bold, letterSpacing: 2),
          ),
          backgroundColor: const Color(0xFF1A1A1A),
          foregroundColor: Colors.white,
        ),
        body: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(20.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: SingleChildScrollView(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Container(
                          width: double.infinity,
                          padding: const EdgeInsets.all(16),
                          decoration: BoxDecoration(
                            color: _red.withOpacity(0.15),
                            borderRadius: BorderRadius.circular(10),
                            border: Border.all(color: _red, width: 2),
                          ),
                          child: const Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Row(
                                children: [
                                  Icon(Icons.warning_amber_rounded,
                                      color: _red, size: 30),
                                  SizedBox(width: 10),
                                  Expanded(
                                    child: Text(
                                      'NOT FOR VEHICLE USE',
                                      style: TextStyle(
                                        color: _red,
                                        fontSize: 18,
                                        fontWeight: FontWeight.bold,
                                        letterSpacing: 0.5,
                                      ),
                                    ),
                                  ),
                                ],
                              ),
                              SizedBox(height: 12),
                              Text(
                                'NOT FOR USE IN DANGEROUS SITUATIONS.\n'
                                'NOT A WORKAROUND FOR SCREEN-FREE OR\n'
                                'DEVICE-PROHIBITED SITUATIONS.',
                                style: TextStyle(
                                  color: Colors.white,
                                  fontSize: 15,
                                  fontWeight: FontWeight.w700,
                                  height: 1.4,
                                ),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(height: 20),
                        const Text(
                          'EyeSwap is screen-free media control for your desk — '
                          'three tactile buttons and a small round display, with '
                          'your phone in your pocket. It is a personal project '
                          'shared as-is, not a product, not a safety device, and '
                          'not a replacement for workplace rules or common sense.',
                          style: TextStyle(color: Colors.white70, fontSize: 14, height: 1.5),
                        ),
                        const SizedBox(height: 20),
                        const Text(
                          'By using this software you accept responsibility. '
                          'The author, contributors, and this software assume '
                          'ZERO LIABILITY for:',
                          style: TextStyle(
                            color: Colors.white,
                            fontWeight: FontWeight.bold,
                            fontSize: 14,
                          ),
                        ),
                        const SizedBox(height: 8),
                        const _Bullet('Workplace media-policy violations'),
                        const _Bullet('Boss disciplinary actions'),
                        const _Bullet('Misuse of a personal device at work'),
                        const _Bullet('Insurance claim denials'),
                        const _Bullet('Arguing with Audible chapter-skip timing'),
                        const _Bullet('Your mother being disappointed in you'),
                        const SizedBox(height: 20),
                        Container(
                          width: double.infinity,
                          padding: const EdgeInsets.all(14),
                          decoration: BoxDecoration(
                            color: const Color(0xFF1A1A1A),
                            borderRadius: BorderRadius.circular(10),
                            border: Border.all(color: _amber.withOpacity(0.4)),
                          ),
                          child: const Text(
                            'Not your mother. Operate responsibly.',
                            style: TextStyle(
                              color: _amber,
                              fontSize: 15,
                              fontStyle: FontStyle.italic,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton(
                    onPressed: _accept,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: _red,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                    child: const Text(
                      'I ACCEPT RESPONSIBILITY',
                      style: TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                        letterSpacing: 1,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _Bullet extends StatelessWidget {
  final String text;
  const _Bullet(this.text);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('•  ', style: TextStyle(color: Color(0xFFD32F2F), fontSize: 14)),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(color: Colors.white70, fontSize: 13, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }
}
