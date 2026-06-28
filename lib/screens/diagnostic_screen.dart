import 'package:flutter/material.dart';
import '../services/diagnostic_service.dart';

class DiagnosticScreen extends StatefulWidget {
  const DiagnosticScreen({super.key});

  @override
  State<DiagnosticScreen> createState() => _DiagnosticScreenState();
}

class _DiagnosticScreenState extends State<DiagnosticScreen> {
  final DiagnosticService _diagnosticService = DiagnosticService();
  final ScrollController _scrollController = ScrollController();
  bool _isRecording = false;
  List<String> _logs = [];

  @override
  void initState() {
    super.initState();
    _diagnosticService.setOnLogUpdate(_onLogUpdate);
    _loadLogs();
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  void _onLogUpdate(List<String> logs) {
    setState(() {
      _logs = logs;
    });
    // Auto-scroll to bottom
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollController.hasClients) {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 200),
          curve: Curves.easeOut,
        );
      }
    });
  }

  Future<void> _loadLogs() async {
    final logs = await _diagnosticService.getLog();
    setState(() {
      _logs = logs;
    });
  }

  Future<void> _toggleRecording() async {
    if (_isRecording) {
      await _diagnosticService.stopRecording();
    } else {
      await _diagnosticService.startRecording();
    }
    setState(() {
      _isRecording = !_isRecording;
    });
  }

  Future<void> _clearLogs() async {
    await _diagnosticService.clearLog();
    setState(() {
      _logs = [];
    });
  }

  Future<void> _shareLogs() async {
    await _diagnosticService.shareLog();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A0A),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1A1A1A),
        title: const Text(
          'Diagnostic Mode',
          style: TextStyle(
            color: Colors.white,
            fontFamily: 'monospace',
            fontWeight: FontWeight.bold,
          ),
        ),
        iconTheme: const IconThemeData(color: Colors.white),
        actions: [
          IconButton(
            icon: const Icon(Icons.share, color: Colors.white),
            onPressed: _logs.isEmpty ? null : _shareLogs,
            tooltip: 'Export/Share Log',
          ),
          IconButton(
            icon: const Icon(Icons.delete_outline, color: Colors.white),
            onPressed: _logs.isEmpty ? null : _clearLogs,
            tooltip: 'Clear Log',
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: Column(
        children: [
          // Recording status bar
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            color: const Color(0xFF1A1A1A),
            child: Row(
              children: [
                Container(
                  width: 12,
                  height: 12,
                  decoration: BoxDecoration(
                    color: _isRecording ? Colors.red : Colors.grey,
                    shape: BoxShape.circle,
                  ),
                ),
                const SizedBox(width: 12),
                Text(
                  _isRecording ? 'RECORDING' : 'IDLE',
                  style: TextStyle(
                    color: _isRecording ? Colors.red : Colors.grey,
                    fontFamily: 'monospace',
                    fontWeight: FontWeight.bold,
                    fontSize: 14,
                  ),
                ),
                const Spacer(),
                Text(
                  '${_logs.length} events captured',
                  style: const TextStyle(
                    color: Colors.white70,
                    fontFamily: 'monospace',
                    fontSize: 12,
                  ),
                ),
              ],
            ),
          ),
          // Log view
          Expanded(
            child: Container(
              margin: const EdgeInsets.all(16),
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: const Color(0xFF111111),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: const Color(0xFF333333)),
              ),
              child: _logs.isEmpty
                  ? Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(
                            Icons.radio_button_checked,
                            size: 48,
                            color: Colors.white.withOpacity(0.3),
                          ),
                          const SizedBox(height: 16),
                          Text(
                            'No events recorded yet',
                            style: TextStyle(
                              color: Colors.white.withOpacity(0.5),
                              fontFamily: 'monospace',
                              fontSize: 14,
                            ),
                          ),
                          const SizedBox(height: 8),
                          Text(
                            'Press the record button to start',
                            style: TextStyle(
                              color: Colors.white.withOpacity(0.3),
                              fontFamily: 'monospace',
                              fontSize: 12,
                            ),
                          ),
                        ],
                      ),
                    )
                  : ListView.builder(
                      controller: _scrollController,
                      itemCount: _logs.length,
                      itemBuilder: (context, index) {
                        final log = _logs[index];
                        Color logColor = Colors.white70;
                        if (log.contains('MEDIA_BUTTON')) {
                          logColor = const Color(0xFF00FF88); // Green for media buttons
                        } else if (log.contains('SESSION_CHANGED')) {
                          logColor = const Color(0xFF88CCFF); // Blue for session changes
                        } else if (log.contains('ERROR')) {
                          logColor = Colors.red; // Red for errors
                        }
                        return Padding(
                          padding: const EdgeInsets.symmetric(vertical: 2),
                          child: Text(
                            log,
                            style: TextStyle(
                              color: logColor,
                              fontFamily: 'monospace',
                              fontSize: 12,
                            ),
                          ),
                        );
                      },
                    ),
            ),
          ),
          // Record button
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: SizedBox(
              width: double.infinity,
              height: 56,
              child: ElevatedButton(
                onPressed: _toggleRecording,
                style: ElevatedButton.styleFrom(
                  backgroundColor: _isRecording
                      ? const Color(0xFFB71C1C)
                      : const Color(0xFFD32F2F),
                  foregroundColor: Colors.white,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(8),
                  ),
                ),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(
                      _isRecording ? Icons.stop : Icons.fiber_manual_record,
                      color: Colors.white,
                    ),
                    const SizedBox(width: 12),
                    Text(
                      _isRecording ? 'STOP RECORDING' : 'START RECORDING',
                      style: const TextStyle(
                        fontFamily: 'monospace',
                        fontWeight: FontWeight.bold,
                        fontSize: 16,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
          // Instructions
          Padding(
            padding: const EdgeInsets.only(left: 16, right: 16, bottom: 24),
            child: Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'How to use:',
                    style: TextStyle(
                      color: Colors.white,
                      fontFamily: 'monospace',
                      fontWeight: FontWeight.bold,
                      fontSize: 12,
                    ),
                  ),
                  SizedBox(height: 8),
                  Text(
                    '1. Start recording\n2. Use your media apps normally (play/pause, switch apps)\n3. Stop recording to review\n4. Share the log for debugging',
                    style: TextStyle(
                      color: Colors.white70,
                      fontFamily: 'monospace',
                      fontSize: 11,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
