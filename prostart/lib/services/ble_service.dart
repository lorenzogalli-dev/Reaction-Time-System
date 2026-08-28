import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

/// Identifiers advertised by the Prostart (BlockStart) Arduino firmware.
/// These must match the firmware exactly for the app to recognize a device.
const String prostartDeviceLocalName = 'BlockStartDevice';
final Guid prostartServiceUuid = Guid('19b10000-e8f2-537e-4f6c-d104768a1214');

/// BLEUnsignedLongCharacteristic on the firmware side: a little-endian
/// unsigned integer timestamp (in microseconds) of the last "go" signal.
final Guid prostartGoTimestampCharacteristicUuid = Guid('19b10001-e8f2-537e-4f6c-d104768a1214');

/// Stages of the BLE connect flow, surfaced to the UI so it can show the
/// right animation/copy at each step.
enum BleConnectStage {
  requestingPermission,
  scanning,
  connecting,
  verifyingServices,
}

enum BleFailureReason {
  permissionDenied,
  bluetoothOff,
  deviceNotFound,
  connectionFailed,
  unsupported,
}

sealed class BleConnectOutcome {
  const BleConnectOutcome();
}

class BleConnectSuccess extends BleConnectOutcome {
  final BluetoothDevice device;
  final String deviceName;
  final List<BluetoothService> services;
  BleConnectSuccess({required this.device, required this.deviceName, required this.services});
}

class BleConnectFailure extends BleConnectOutcome {
  final BleFailureReason reason;
  final String message;
  const BleConnectFailure({required this.reason, required this.message});
}

/// Handles real BLE scanning/connection against a Prostart device using
/// flutter_blue_plus. Kept separate from UI so the connect flow can be
/// reused or extended (e.g. reconnect, live data) later.
class BleService {
  StreamSubscription<List<ScanResult>>? _scanResultsSub;
  StreamSubscription<BluetoothAdapterState>? _adapterSub;

  static const _permissionDeniedFailure = BleConnectFailure(
    reason: BleFailureReason.permissionDenied,
    message: 'Bluetooth permission is required — enable it in Settings',
  );

  static const _bluetoothOffFailure = BleConnectFailure(
    reason: BleFailureReason.bluetoothOff,
    message: 'Bluetooth is off — please enable it',
  );

  static const _deviceNotFoundFailure = BleConnectFailure(
    reason: BleFailureReason.deviceNotFound,
    message: "No Prostart device found — make sure it's powered on and nearby, then try again",
  );

  static const _connectionFailedFailure = BleConnectFailure(
    reason: BleFailureReason.connectionFailed,
    message: 'Connection failed — try moving closer to the device or restarting it',
  );

  Future<BleConnectOutcome> connectToDevice({
    required void Function(BleConnectStage stage) onStage,
    Duration scanTimeout = const Duration(seconds: 10),
  }) async {
    try {
      // Android needs an explicit runtime permission request before scanning.
      // iOS has no equivalent pre-check here: permission_handler's Bluetooth
      // status is unreliable on iOS (it's modeled on Android's runtime
      // permission flow). On iOS we instead call startScan() directly below
      // and let CoreBluetooth show its native system prompt on first use
      // (driven by NSBluetoothAlwaysUsageDescription in Info.plist), then
      // read the resulting state from FlutterBluePlus.adapterState.
      if (!kIsWeb && defaultTargetPlatform == TargetPlatform.android) {
        onStage(BleConnectStage.requestingPermission);
        final permissionFailure = await _ensureAndroidPermissions();
        if (permissionFailure != null) return permissionFailure;
      }

      if (!kIsWeb) {
        final supported = await FlutterBluePlus.isSupported;
        if (!supported) {
          return const BleConnectFailure(
            reason: BleFailureReason.unsupported,
            message: 'Bluetooth is not supported on this device',
          );
        }
      }

      // A device from a previous session (hot restart, app relaunch after a
      // crash, etc.) may still be alive at the OS level even though our
      // in-memory state has no record of it. Clear those out first so a
      // fresh scan/connect doesn't collide with a stale GATT connection.
      await _disconnectStaleConnections();

      onStage(BleConnectStage.scanning);
      final scanOutcome = await _scanForDevice(scanTimeout);
      if (scanOutcome.failure != null) return scanOutcome.failure!;
      final device = scanOutcome.device;
      if (device == null) return _deviceNotFoundFailure;

      onStage(BleConnectStage.connecting);
      try {
        await device.connect(timeout: const Duration(seconds: 10));
      } catch (_) {
        return _connectionFailedFailure;
      }

      onStage(BleConnectStage.verifyingServices);
      List<BluetoothService> services;
      try {
        services = await device.discoverServices();
        final hasExpectedService = services.any((s) => s.uuid == prostartServiceUuid);
        if (!hasExpectedService) {
          await device.disconnect();
          return _connectionFailedFailure;
        }
      } catch (_) {
        await device.disconnect();
        return _connectionFailedFailure;
      }

      final name = device.platformName.isNotEmpty ? device.platformName : prostartDeviceLocalName;
      return BleConnectSuccess(device: device, deviceName: name, services: services);
    } catch (_) {
      return _connectionFailedFailure;
    }
  }

  /// Best-effort cleanup of any device our app already holds a live GATT
  /// connection to (per `FlutterBluePlus.connectedDevices`) that matches the
  /// Prostart service. Failures here are swallowed - a fresh scan/connect
  /// attempt right after will surface any real problem.
  Future<void> _disconnectStaleConnections() async {
    if (kIsWeb) return;
    for (final staleDevice in FlutterBluePlus.connectedDevices) {
      try {
        final looksLikeOurs = staleDevice.platformName == prostartDeviceLocalName;
        if (!looksLikeOurs) {
          final services = await staleDevice.discoverServices();
          if (!services.any((s) => s.uuid == prostartServiceUuid)) continue;
        }
        await staleDevice.disconnect();
      } catch (_) {
        // not our device, or already gone - ignore.
      }
    }
  }

  Future<BleConnectFailure?> _ensureAndroidPermissions() async {
    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
    ].request();
    final allGranted = statuses.values.every((s) => s.isGranted);
    return allGranted ? null : _permissionDeniedFailure;
  }

  /// Starts scanning directly and races it against the adapter-state stream,
  /// so a denied/disabled Bluetooth state - surfaced natively by CoreBluetooth
  /// on iOS once the user responds to its system prompt, or the Android
  /// Bluetooth toggle - aborts the scan with a specific reason instead of
  /// just running out the clock as "device not found".
  Future<_ScanOutcome> _scanForDevice(Duration timeout) async {
    final completer = Completer<_ScanOutcome>();

    void complete(_ScanOutcome outcome) {
      if (!completer.isCompleted) completer.complete(outcome);
    }

    _adapterSub = FlutterBluePlus.adapterState.listen((state) {
      switch (state) {
        case BluetoothAdapterState.unauthorized:
          complete(_ScanOutcome.failure(_permissionDeniedFailure));
        case BluetoothAdapterState.off:
        case BluetoothAdapterState.unavailable:
          complete(_ScanOutcome.failure(_bluetoothOffFailure));
        default:
          break;
      }
    });

    _scanResultsSub = FlutterBluePlus.onScanResults.listen((results) {
      if (results.isNotEmpty) {
        complete(_ScanOutcome.found(results.first.device));
      }
    }, onError: (_) {
      complete(_ScanOutcome.found(null));
    });

    try {
      await FlutterBluePlus.startScan(
        withServices: [prostartServiceUuid],
        withNames: [prostartDeviceLocalName],
        timeout: timeout,
      );
    } catch (_) {
      complete(_ScanOutcome.found(null));
    }

    final outcome = await completer.future.timeout(
      timeout + const Duration(seconds: 1),
      onTimeout: () => _ScanOutcome.found(null),
    );

    await _stopScan();
    return outcome;
  }

  Future<void> _stopScan() async {
    await _scanResultsSub?.cancel();
    _scanResultsSub = null;
    await _adapterSub?.cancel();
    _adapterSub = null;
    if (FlutterBluePlus.isScanningNow) {
      await FlutterBluePlus.stopScan();
    }
  }

  void dispose() {
    _scanResultsSub?.cancel();
    _adapterSub?.cancel();
  }
}

class _ScanOutcome {
  final BluetoothDevice? device;
  final BleConnectFailure? failure;
  _ScanOutcome.found(this.device) : failure = null;
  _ScanOutcome.failure(this.failure) : device = null;
}
