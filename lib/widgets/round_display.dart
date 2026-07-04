import 'package:flutter/material.dart';
import 'dart:async';

class RoundDisplay extends StatefulWidget {
  final String mainText;
  final String subText;
  final bool isScrolling;

  const RoundDisplay({
    super.key,
    required this.mainText,
    required this.subText,
    this.isScrolling = false,
  });

  @override
  State<RoundDisplay> createState() => _RoundDisplayState();
}

class _RoundDisplayState extends State<RoundDisplay> {
  double _scrollOffset = 0;
  Timer? _scrollTimer;

  @override
  void initState() {
    super.initState();
    if (widget.isScrolling) {
      _startScrolling();
    }
  }

  @override
  void didUpdateWidget(RoundDisplay oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.isScrolling && !oldWidget.isScrolling) {
      _startScrolling();
    } else if (!widget.isScrolling && oldWidget.isScrolling) {
      _stopScrolling();
    }
  }

  void _startScrolling() {
    _scrollTimer?.cancel();
    _scrollOffset = 0;
    _scrollTimer = Timer.periodic(const Duration(milliseconds: 50), (timer) {
      setState(() {
        _scrollOffset -= 2;
      });
    });
  }

  void _stopScrolling() {
    _scrollTimer?.cancel();
    setState(() {
      _scrollOffset = 0;
    });
  }

  @override
  void dispose() {
    _scrollTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 240,
      height: 240,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: const Color(0xFF0A0A0A),
        border: Border.all(
          color: const Color(0xFF2A2A2A),
          width: 4,
        ),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.5),
            blurRadius: 20,
            spreadRadius: 5,
          ),
          BoxShadow(
            color: const Color(0xFFD32F2F).withOpacity(0.1),
            blurRadius: 30,
            spreadRadius: 10,
          ),
        ],
      ),
      child: ClipOval(
        child: Container(
          decoration: BoxDecoration(
            gradient: RadialGradient(
              center: Alignment.center,
              radius: 0.8,
              colors: [
                const Color(0xFF1A1A1A),
                const Color(0xFF0A0A0A),
              ],
            ),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Container(
                width: 200,
                height: 100,
                decoration: BoxDecoration(
                  shape: BoxShape.rectangle,
                  borderRadius: BorderRadius.circular(8),
                  color: const Color(0xFFFFB300).withOpacity(0.05),
                ),
                child: Center(
                  child: widget.isScrolling
                      ? Transform.translate(
                          offset: Offset(_scrollOffset, 0),
                          child: Text(
                            widget.mainText,
                            style: const TextStyle(
                              fontSize: 32,
                              fontWeight: FontWeight.bold,
                              color: Color(0xFFFFB300),
                              letterSpacing: 2,
                            ),
                          ),
                        )
                      : Text(
                          widget.mainText,
                          style: const TextStyle(
                            fontSize: 32,
                            fontWeight: FontWeight.bold,
                            color: Color(0xFFFFB300),
                            letterSpacing: 2,
                          ),
                        ),
                ),
              ),
              const SizedBox(height: 8),
              Text(
                widget.subText,
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w500,
                  color: const Color(0xFFFFB300).withOpacity(0.7),
                  letterSpacing: 4,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}