import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// First-launch safety disclaimer. Deadpan on the surface, but the core
/// message is real: this is a stationary fleet-vehicle gadget, not a
/// driving aid. Tone borrowed from the README (which assumes zero
/// liability for your mother being disappointed in you).
///
/// Sets prefs key `disclaimer_accepted` when acknowledged.
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
            'EYESWAP SAFETY NOTICE',
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
                                      'DO NOT USE WHILE OPERATING A MOTOR VEHICLE',
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
                                'DO NOT USE ON PUBLIC ROADS.\nDO NOT USE NEAR OTHER HUMANS.',
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
                          'EyeSwap is a personal media & navigation controller for '
                          'STATIONARY FLEET VEHICLES in controlled environments. It is '
                          'not a driving aid, not a substitute for attention or '
                          'situational awareness, and definitely not a replacement for '
                          'basic common sense.',
                          style: TextStyle(color: Colors.white70, fontSize: 14, height: 1.5),
                        ),
                        const SizedBox(height: 20),
                        const Text(
                          'The author, contributors, and this software assume '
                          'ZERO LIABILITY for:',
                          style: TextStyle(
                            color: Colors.white,
                            fontWeight: FontWeight.bold,
                            fontSize: 14,
                          ),
                        ),
                        const SizedBox(height: 8),
                        const _Bullet('Distracted driving incidents'),
                        const _Bullet('Fleet manager disciplinary actions'),
                        const _Bullet('Insurance claim denials'),
                        const _Bullet('Arguing with Audible chapter-skip timing'),
                        const _Bullet('Driving into a ditch mid-podcast'),
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
                      'I UNDERSTAND',
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
