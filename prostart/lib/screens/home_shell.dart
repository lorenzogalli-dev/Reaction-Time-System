import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import '../widgets/app_bottom_nav_bar.dart';
import '../widgets/glass.dart';
import 'home_screen.dart';
import 'reaction_result_screen.dart';

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  static const _placeholders = {
    1: (Icons.history_rounded, 'History'),
    2: (Icons.insights_rounded, 'Stats'),
    3: (Icons.person_rounded, 'Profile'),
  };

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      extendBody: true,
      bottomNavigationBar: AppBottomNavBar(
        activeIndex: _index,
        onTap: (i) => setState(() => _index = i),
        onTapAction: () =>
            Navigator.of(context).push(ReactionResultScreen.route()),
      ),
      body: GlowBackground(
        child: _index == 0 ? const HomeScreen() : _placeholder(_index),
      ),
    );
  }

  Widget _placeholder(int index) {
    final (icon, label) = _placeholders[index]!;
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 48, color: AppColors.textFaint),
          const SizedBox(height: AppSpacing.md),
          Text(
            '$label — coming soon',
            style: const TextStyle(color: AppColors.textSecondary),
          ),
        ],
      ),
    );
  }
}
