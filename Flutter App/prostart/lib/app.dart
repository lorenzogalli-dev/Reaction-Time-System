import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'screens/onboarding_screen.dart';
import 'state/ble_connection_controller.dart';
import 'theme/app_theme.dart';

class ProstartApp extends StatelessWidget {
  const ProstartApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => BleConnectionController(),
      child: MaterialApp(
        title: 'Prostart',
        debugShowCheckedModeBanner: false,
        theme: AppTheme.dark,
        themeMode: ThemeMode.dark,
        home: const OnboardingScreen(),
      ),
    );
  }
}
