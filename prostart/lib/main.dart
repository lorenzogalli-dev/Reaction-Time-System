import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'screens/home_shell.dart';
import 'screens/photofinish_result_screen.dart';
import 'screens/reaction_result_screen.dart';
import 'screens/splash_screen.dart';
import 'theme/app_theme.dart';

void main() {
  SystemChrome.setSystemUIOverlayStyle(SystemUiOverlayStyle.light);
  runApp(const ProStartApp());
}

/// Opens straight onto one page, for screenshots:
/// `flutter run --dart-define=PAGE=home|reaction|photofinish`.
const _startPage = String.fromEnvironment('PAGE');

class ProStartApp extends StatelessWidget {
  const ProStartApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ProStart',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.dark,
      home: switch (_startPage) {
        'home' => const HomeShell(),
        'reaction' => const ReactionResultScreen(),
        'photofinish' => const PhotofinishResultScreen(),
        _ => const SplashScreen(),
      },
    );
  }
}
