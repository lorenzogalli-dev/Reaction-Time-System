import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import '../widgets/floating_nav_bar.dart';
import 'home_screen.dart';
import 'placeholder_screen.dart';

/// App shell: hosts the 4 top-level tabs behind a floating bottom nav bar.
/// Only Home is functional in this MVP; the rest are placeholders.
class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  static const _navItems = [
    NavItemData(icon: Icons.home_rounded, label: 'Home'),
    NavItemData(icon: Icons.bar_chart_rounded, label: 'Stats'),
    NavItemData(icon: Icons.settings_rounded, label: 'Settings'),
    NavItemData(icon: Icons.person_rounded, label: 'Profile'),
  ];

  static const _screens = [
    HomeScreen(),
    PlaceholderScreen(title: 'Stats', icon: Icons.bar_chart_rounded),
    PlaceholderScreen(title: 'Settings', icon: Icons.settings_rounded),
    PlaceholderScreen(title: 'Profile', icon: Icons.person_rounded),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.background,
      extendBody: true,
      body: Stack(
        children: [
          IndexedStack(index: _index, children: _screens),
          Positioned(
            left: AppSpacing.lg,
            right: AppSpacing.lg,
            bottom: AppSpacing.lg,
            child: FloatingNavBar(
              items: _navItems,
              currentIndex: _index,
              onTap: (i) => setState(() => _index = i),
            ),
          ),
        ],
      ),
    );
  }
}
