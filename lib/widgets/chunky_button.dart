import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'dart:async';

class ChunkyButton extends StatefulWidget {
  final String label;
  final Color color;
  final VoidCallback onPressed;
  final VoidCallback? onLongPress;

  const ChunkyButton({
    super.key,
    required this.label,
    required this.color,
    required this.onPressed,
    this.onLongPress,
  });

  @override
  State<ChunkyButton> createState() => _ChunkyButtonState();
}

class _ChunkyButtonState extends State<ChunkyButton> {
  bool _isPressed = false;
  Timer? _longPressTimer;
  bool _longPressTriggered = false;

  void _onTapDown(TapDownDetails details) {
    setState(() => _isPressed = true);
    _longPressTriggered = false;
    
    if (widget.onLongPress != null) {
      _longPressTimer = Timer(const Duration(milliseconds: 750), () {
        _longPressTriggered = true;
        widget.onLongPress?.call();
        // Haptic feedback
        HapticFeedback.heavyImpact();
      });
    }
  }

  void _onTapUp(TapUpDetails details) {
    setState(() => _isPressed = false);
    _longPressTimer?.cancel();
    
    if (!_longPressTriggered) {
      widget.onPressed();
      HapticFeedback.mediumImpact();
    }
  }

  void _onTapCancel() {
    setState(() => _isPressed = false);
    _longPressTimer?.cancel();
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTapDown: _onTapDown,
      onTapUp: _onTapUp,
      onTapCancel: _onTapCancel,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 50),
        width: 80,
        height: 80,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: _isPressed
                ? [
                    widget.color.withOpacity(0.6),
                    widget.color.withOpacity(0.4),
                  ]
                : [
                    widget.color.withOpacity(1.0),
                    widget.color.withOpacity(0.7),
                  ],
          ),
          boxShadow: _isPressed
              ? [
                  BoxShadow(
                    color: widget.color.withOpacity(0.3),
                    blurRadius: 8,
                    offset: const Offset(2, 2),
                  ),
                ]
              : [
                  BoxShadow(
                    color: Colors.black.withOpacity(0.5),
                    blurRadius: 4,
                    offset: const Offset(4, 4),
                  ),
                  BoxShadow(
                    color: widget.color.withOpacity(0.3),
                    blurRadius: 8,
                    offset: const Offset(-2, -2),
                  ),
                ],
          border: Border.all(
            color: _isPressed ? widget.color.withOpacity(0.8) : const Color(0xFF2A2A2A),
            width: 2,
          ),
        ),
        child: Center(
          child: Text(
            widget.label,
            style: TextStyle(
              fontSize: 28,
              fontWeight: FontWeight.bold,
              color: _isPressed ? Colors.white.withOpacity(0.8) : Colors.white,
            ),
          ),
        ),
      ),
    );
  }
}