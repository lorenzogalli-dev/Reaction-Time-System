import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:prostart/app.dart';

void main() {
  testWidgets('Onboarding screen shows app name and Get Started button', (WidgetTester tester) async {
    await tester.pumpWidget(const ProstartApp());
    // The runner animation loops forever once the intro finishes, so pump a
    // bounded duration instead of pumpAndSettle (which would never settle).
    await tester.pump(const Duration(seconds: 2));

    expect(find.text('Prostart'), findsOneWidget);
    expect(find.widgetWithText(ElevatedButton, 'Get Started'), findsOneWidget);
  });
}
