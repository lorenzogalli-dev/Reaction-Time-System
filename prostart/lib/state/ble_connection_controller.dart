import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../services/ble_service.dart';

/// App-wide Bluetooth connection status, distinct from the more granular
/// [BleConnectStage] used only while a connect attempt is in flight.
enum BleAppStatus { disconnected, scanning, connecting, connected, error }

T? _firstWhereOrNull<T>(Iterable<T> items, bool Function(T item) test) {
  for (final item in items) {
    if (test(item)) return item;
  }
  return null;
}

/// Holds the Prostart BLE connection state for the whole app - which device
/// (if any) is connected, the current status, and the latest "go" timestamp
/// received from the device - so it survives tab switches and rebuilds
/// instead of living in a single screen's local state.
class BleConnectionController extends ChangeNotifier {
  final BleService _bleService = BleService();

  BleAppStatus _status = BleAppStatus.disconnected;
  BleAppStatus get status => _status;

  BleConnectStage? _stage;
  BleConnectStage? get stage => _stage;

  BluetoothDevice? _device;
  BluetoothDevice? get device => _device;

  String? _deviceName;
  String? get deviceName => _deviceName;

  BleFailureReason? _failureReason;
  BleFailureReason? get failureReason => _failureReason;

  String? _errorMessage;
  String? get errorMessage => _errorMessage;

  int? _lastGoTimestamp;
  int? get lastGoTimestamp => _lastGoTimestamp;

  bool get isConnected => _status == BleAppStatus.connected;

  StreamSubscription<BluetoothConnectionState>? _connectionStateSub;
  StreamSubscription<List<int>>? _goCharacteristicSub;

  bool _userInitiatedDisconnect = false;

  Future<BleConnectOutcome> connect({Duration scanTimeout = const Duration(seconds: 10)}) async {
    _status = BleAppStatus.scanning;
    _stage = null;
    _errorMessage = null;
    _failureReason = null;
    notifyListeners();

    final outcome = await _bleService.connectToDevice(
      onStage: (stage) {
        _stage = stage;
        _status = switch (stage) {
          BleConnectStage.requestingPermission || BleConnectStage.scanning => BleAppStatus.scanning,
          BleConnectStage.connecting || BleConnectStage.verifyingServices => BleAppStatus.connecting,
        };
        notifyListeners();
      },
      scanTimeout: scanTimeout,
    );

    switch (outcome) {
      case BleConnectSuccess(:final device, :final deviceName, :final services):
        _userInitiatedDisconnect = false;
        _device = device;
        _deviceName = deviceName;
        _status = BleAppStatus.connected;
        _errorMessage = null;
        _failureReason = null;
        notifyListeners();
        _listenToConnectionState(device);
        await _subscribeToGoCharacteristic(services);
      case BleConnectFailure(:final reason, :final message):
        _device = null;
        _deviceName = null;
        _status = BleAppStatus.error;
        _failureReason = reason;
        _errorMessage = message;
        notifyListeners();
    }

    return outcome;
  }

  Future<void> disconnect() async {
    final device = _device;
    if (device == null) return;
    _userInitiatedDisconnect = true;
    try {
      await device.disconnect();
    } catch (_) {
      // fall through - we reset local state regardless below.
    }
    _handleDisconnected(unexpected: false);
  }

  void _listenToConnectionState(BluetoothDevice device) {
    _connectionStateSub?.cancel();
    _connectionStateSub = device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected) {
        _handleDisconnected(unexpected: !_userInitiatedDisconnect);
      }
    });
  }

  Future<void> _subscribeToGoCharacteristic(List<BluetoothService> services) async {
    final service = _firstWhereOrNull(services, (s) => s.uuid == prostartServiceUuid);
    final characteristic = service == null
        ? null
        : _firstWhereOrNull(service.characteristics, (c) => c.uuid == prostartGoTimestampCharacteristicUuid);

    if (characteristic == null) {
      debugPrint('Prostart: go-timestamp characteristic not found on device');
      return;
    }

    try {
      await characteristic.setNotifyValue(true);
    } catch (e) {
      debugPrint('Prostart: failed to enable notifications on go characteristic: $e');
      return;
    }

    _goCharacteristicSub?.cancel();
    _goCharacteristicSub = characteristic.lastValueStream.listen((bytes) {
      final value = _parseGoTimestamp(bytes);
      if (value == null) return;
      _lastGoTimestamp = value;
      debugPrint('Prostart: received go timestamp: $value µs');
      notifyListeners();
    });
  }

  /// Parses a little-endian unsigned integer of whatever width
  /// flutter_blue_plus reported for the characteristic's value.
  int? _parseGoTimestamp(List<int> bytes) {
    if (bytes.isEmpty) return null;
    final data = ByteData.sublistView(Uint8List.fromList(bytes));
    if (bytes.length >= 8) return data.getUint64(0, Endian.little);
    if (bytes.length >= 4) return data.getUint32(0, Endian.little);
    if (bytes.length >= 2) return data.getUint16(0, Endian.little);
    return data.getUint8(0);
  }

  void _handleDisconnected({required bool unexpected}) {
    // Both an explicit disconnect() call and the connectionState listener
    // it triggers can reach here for the same event - make it a no-op the
    // second time.
    if (_device == null) return;

    _connectionStateSub?.cancel();
    _connectionStateSub = null;
    _goCharacteristicSub?.cancel();
    _goCharacteristicSub = null;
    _device = null;
    _deviceName = null;
    _stage = null;
    _lastGoTimestamp = null;
    if (unexpected) {
      _status = BleAppStatus.error;
      _failureReason = BleFailureReason.connectionFailed;
      _errorMessage = 'Connection lost — device disconnected unexpectedly';
    } else {
      _status = BleAppStatus.disconnected;
      _failureReason = null;
      _errorMessage = null;
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _connectionStateSub?.cancel();
    _goCharacteristicSub?.cancel();
    _bleService.dispose();
    super.dispose();
  }
}
