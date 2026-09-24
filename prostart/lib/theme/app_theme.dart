import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

/// ProStart mockup design system: near-black sport-tech base, one electric
/// lime accent, and "liquid glass" surfaces floating over soft colour glows.
abstract class AppColors {
  static const background = Color(0xFF07090C);

  static const accent = Color(0xFFC6FF3C);
  static const onAccent = Color(0xFF0A0C0F);

  /// Secondary brand hue, used for the photofinish mode.
  static const cyan = Color(0xFF3CD7FF);
  static const bluetooth = Color(0xFF2F7BFF);
  static const success = Color(0xFF34E08A);
  static const gold = Color(0xFFFFC94D);

  static const textPrimary = Color(0xFFF5F7FA);
  static const textSecondary = Color(0xB3FFFFFF);
  static const textFaint = Color(0x73FFFFFF);
}

abstract class AppSpacing {
  static const double xs = 4;
  static const double sm = 8;
  static const double md = 12;
  static const double lg = 16;
  static const double xl = 20;
  static const double xxl = 28;
}

abstract class AppRadius {
  static const double sm = 14;
  static const double md = 20;
  static const double lg = 28;
  static const double pill = 999;
}

abstract class AppGlass {
  static const double blur = 24;
}

abstract class AppMotion {
  static const Duration normal = Duration(milliseconds: 200);
}

/// Numbers that must not jiggle as digits change width.
const tabular = [FontFeature.tabularFigures()];

class AppTheme {
  static ThemeData get dark {
    final base = ThemeData.dark(useMaterial3: true);
    return base.copyWith(
      scaffoldBackgroundColor: AppColors.background,
      colorScheme: const ColorScheme.dark(
        surface: AppColors.background,
        primary: AppColors.accent,
        onPrimary: AppColors.onAccent,
      ),
      textTheme: base.textTheme.apply(
        bodyColor: AppColors.textPrimary,
        displayColor: AppColors.textPrimary,
      ),
      splashFactory: NoSplash.splashFactory,
      highlightColor: Colors.transparent,
      pageTransitionsTheme: const PageTransitionsTheme(
        builders: {
          TargetPlatform.iOS: CupertinoPageTransitionsBuilder(),
          TargetPlatform.android: CupertinoPageTransitionsBuilder(),
        },
      ),
    );
  }
}
