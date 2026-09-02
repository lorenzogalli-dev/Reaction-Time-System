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

/// Live accelerometer stream, notified continuously at ~50 Hz while a client
/// is subscribed. This is a *visualization-only* feed - the reaction-time
/// measurement still comes from the single-shot, high-precision
/// [prostartGoTimestampCharacteristicUuid] above.
///
/// Payload is [prostartAccelerometerPayloadBytes] bytes, all little-endian:
///
///   offset 0   uint32   t_us - the device's own `micros()` at the moment the
///                       sample was captured. Same clock as the "go" timestamp
///                       characteristic, so the two are directly comparable.
///   offset 4   float32  X in g
///   offset 8   float32  Y in g
///   offset 12  float32  Z in g
///
/// The device is the only clock in the system. The app must not timestamp
/// samples on arrival: BLE delivers them in bursts, so arrival times carry
/// +/-15-30 ms of error (see playground_IMU/README.md).
///
/// Implemented firmware side in Arduino/BLEtest/BLEtest.ino as `accelChar`,
/// which only notifies while a client is subscribed - i.e. while the live
/// data screen is open. Keep the UUID and this layout in sync with that sketch.
final Guid prostartAccelerometerCharacteristicUuid = Guid('19b10002-e8f2-537e-4f6c-d104768a1214');

/// Size of one [prostartAccelerometerCharacteristicUuid] notification.
/// Was 12 before the firmware started stamping samples with `micros()`.
const int prostartAccelerometerPayloadBytes = 16;

/// Nominal firmware notification rate for the accelerometer characteristic.
/// The sensor itself now runs at 833 Hz; the firmware decimates 1:17 for this
/// cosmetic feed and keeps the full-rate samples on-device.
const int prostartAccelerometerSampleRateHz = 50;

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

      // A BLE peripheral stops advertising while it is connected, so if the
      // OS still holds a link to the device a scan can never see it. Adopt
      // that link instead of scanning (see [_findAlreadyConnectedDevice]).
      var device = await _findAlreadyConnectedDevice();

      if (device == null) {
        onStage(BleConnectStage.scanning);
        final scanOutcome = await _scanForDevice(scanTimeout);
        if (scanOutcome.failure != null) return scanOutcome.failure!;
        device = scanOutcome.device;
        if (device == null) return _deviceNotFoundFailure;
      }

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

  /// Looks for a Prostart device the *operating system* is already connected
  /// to, and hands it back so we can adopt it instead of scanning.
  ///
  /// This matters because a connected BLE peripheral stops advertising: once
  /// the OS holds a link to the Arduino, no amount of scanning will ever find
  /// it, and the connect flow times out as "device not found" even though the
  /// board is powered on and healthy. That link routinely outlives the app -
  /// a hot restart, a crash, or a reinstall drops our Dart state but not the
  /// OS's GATT connection.
  ///
  /// `FlutterBluePlus.connectedDevices` cannot see those: it is a snapshot of
  /// connections made by *this* process. `systemDevices()` reports links held
  /// by any app, including our own from a previous run, which is what we need
  /// here. Calling `connect()` on one of them is still required (it attaches
  /// the device to our app) but returns immediately, since the radio link
  /// already exists.
  Future<BluetoothDevice?> _findAlreadyConnectedDevice() async {
    if (kIsWeb) return null;
    try {
      final devices = await FlutterBluePlus.systemDevices([prostartServiceUuid]);
      for (final candidate in devices) {
        // iOS filters systemDevices() by the service UUID for us; Android
        // ignores that argument, so fall back to matching the local name.
        // Either way discoverServices() below is the real verification.
        final looksLikeOurs = defaultTargetPlatform != TargetPlatform.android ||
            candidate.platformName == prostartDeviceLocalName;
        if (looksLikeOurs) return candidate;
      }
    } catch (_) {
      // Not fatal - fall through to a normal scan.
    }
    return null;
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
