import 'dart:async';
import 'dart:io';
import 'dart:ui' show Rect;

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:path_provider/path_provider.dart';
import 'package:share_plus/share_plus.dart';

import '../models/accel_sample.dart';
import '../models/recording_session.dart';
import 'ble_connection_controller.dart';

enum LiveStreamStatus {
  /// Enabling notifications on the characteristic.
  starting,

  /// Samples are flowing.
  streaming,

  /// Connected, but this firmware doesn't expose the accelerometer feed.
  unavailable,

  /// The device went away while we were watching it.
  disconnected,
}

/// Drives the live accelerometer screen: keeps a short rolling window for the
/// chart, and optionally buffers every sample into a [RecordingSession].
///
/// Scoped to the live data screen (created/disposed with it) rather than to
/// the app, so notifications are only enabled while the user is actually
/// looking at the stream.
class LiveDataController extends ChangeNotifier {
  LiveDataController(this._connection);

  final BleConnectionController _connection;

  /// How much history the scrolling chart shows.
  static const Duration chartWindow = Duration(seconds: 5);

  /// Hard cap on a single recording (~10 min at 50 Hz) so a forgotten
  /// recording can't grow without bound.
  static const int maxRecordedSamples = 30000;

  LiveStreamStatus _status = LiveStreamStatus.starting;
  LiveStreamStatus get status => _status;

  String? _errorMessage;
  String? get errorMessage => _errorMessage;

  /// Bumped on every incoming sample. The chart and the numeric readout
  /// listen to this instead of the controller itself, so 50 Hz of data
  /// doesn't rebuild the whole screen.
  final ValueNotifier<AccelSample?> latest = ValueNotifier(null);

  final List<AccelSample> _window = [];
  List<AccelSample> get windowSamples => _window;

  final Stopwatch _clock = Stopwatch();

  /// Microseconds since the stream opened - the chart's "now" edge.
  int get elapsedMicros => _clock.elapsedMicroseconds;

  bool _isRecording = false;
  bool get isRecording => _isRecording;

  List<AccelSample> _recordingBuffer = [];
  DateTime? _recordingStartedAt;
  int _recordingStartMicros = 0;

  /// Elapsed time of the in-flight recording, ticked once a second so the
  /// timer label doesn't rebuild at the sample rate.
  final ValueNotifier<Duration> recordingElapsed = ValueNotifier(Duration.zero);
  Timer? _recordingTimer;

  RecordingSession? _lastSession;
  RecordingSession? get lastSession => _lastSession;

  bool _isExporting = false;
  bool get isExporting => _isExporting;

  StreamSubscription<List<int>>? _valueSub;
  BluetoothCharacteristic? _characteristic;

  Future<void> start() async {
    _connection.addListener(_onConnectionChanged);

    final characteristic = _connection.accelerometerCharacteristic;
    if (characteristic == null) {
      _status = LiveStreamStatus.unavailable;
      _errorMessage = 'This device firmware does not stream accelerometer data yet.';
      notifyListeners();
      return;
    }

    _characteristic = characteristic;
    try {
      await characteristic.setNotifyValue(true);
    } catch (e) {
      _status = LiveStreamStatus.unavailable;
      _errorMessage = 'Could not start the accelerometer stream — try reconnecting the device.';
      debugPrint('Prostart: failed to enable accelerometer notifications: $e');
      notifyListeners();
      return;
    }

    _clock.start();
    _valueSub = characteristic.lastValueStream.listen(_onValue);
    _status = LiveStreamStatus.streaming;
    notifyListeners();
  }

  void _onConnectionChanged() {
    if (_connection.isConnected) return;
    if (_status == LiveStreamStatus.disconnected) return;
    if (_isRecording) stopRecording();
    _clock.stop();
    _valueSub?.cancel();
    _valueSub = null;
    _characteristic = null;
    _status = LiveStreamStatus.disconnected;
    _errorMessage = 'Device disconnected.';
    notifyListeners();
  }

  void _onValue(List<int> bytes) {
    final sample = _parseSample(bytes, _clock.elapsedMicroseconds);
    if (sample == null) return;

    _window.add(sample);
    final cutoff = sample.elapsedMicros - chartWindow.inMicroseconds;
    // Samples arrive in order, so trimming from the front is enough.
    var drop = 0;
    while (drop < _window.length && _window[drop].elapsedMicros < cutoff) {
      drop++;
    }
    if (drop > 0) _window.removeRange(0, drop);

    if (_isRecording && _recordingBuffer.length < maxRecordedSamples) {
      _recordingBuffer.add(sample);
    }

    latest.value = sample;
  }

  /// Firmware payload: three little-endian float32 values (X, Y, Z) in g.
  AccelSample? _parseSample(List<int> bytes, int elapsedMicros) {
    if (bytes.length < 12) return null;
    final data = ByteData.sublistView(Uint8List.fromList(bytes));
    return AccelSample(
      elapsedMicros: elapsedMicros,
      x: data.getFloat32(0, Endian.little),
      y: data.getFloat32(4, Endian.little),
      z: data.getFloat32(8, Endian.little),
    );
  }

  void toggleRecording() => _isRecording ? stopRecording() : startRecording();

  void startRecording() {
    if (_isRecording || _status != LiveStreamStatus.streaming) return;
    _isRecording = true;
    _lastSession = null;
    _recordingBuffer = [];
    _recordingStartedAt = DateTime.now();
    _recordingStartMicros = _clock.elapsedMicroseconds;
    recordingElapsed.value = Duration.zero;
    _recordingTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      recordingElapsed.value =
          Duration(microseconds: _clock.elapsedMicroseconds - _recordingStartMicros);
    });
    notifyListeners();
  }

  void stopRecording() {
    if (!_isRecording) return;
    _isRecording = false;
    _recordingTimer?.cancel();
    _recordingTimer = null;
    _lastSession = RecordingSession(
      startedAt: _recordingStartedAt ?? DateTime.now(),
      samples: List.unmodifiable(_recordingBuffer),
    );
    _recordingBuffer = [];
    notifyListeners();
  }

  void dismissLastSession() {
    if (_lastSession == null) return;
    _lastSession = null;
    notifyListeners();
  }

  /// Writes the last recording to a temp CSV and hands it to the platform
  /// share sheet. Returns an error message on failure, null on success.
  Future<String?> exportLastSession({Rect? shareOrigin}) async {
    final session = _lastSession;
    if (session == null || session.isEmpty || _isExporting) return null;

    _isExporting = true;
    notifyListeners();
    try {
      final dir = await getTemporaryDirectory();
      final file = File('${dir.path}/${session.fileName}');
      await file.writeAsString(session.toCsv());
      await SharePlus.instance.share(
        ShareParams(
          files: [XFile(file.path, mimeType: 'text/csv')],
          fileNameOverrides: [session.fileName],
          subject: 'Prostart IMU recording',
          sharePositionOrigin: shareOrigin,
        ),
      );
      return null;
    } catch (e) {
      debugPrint('Prostart: CSV export failed: $e');
      return 'Could not export the recording.';
    } finally {
      _isExporting = false;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _connection.removeListener(_onConnectionChanged);
    _recordingTimer?.cancel();
    _valueSub?.cancel();
    // Best effort - the device may already be gone.
    _characteristic?.setNotifyValue(false).catchError((_) => false);
    latest.dispose();
    recordingElapsed.dispose();
    super.dispose();
  }
}
