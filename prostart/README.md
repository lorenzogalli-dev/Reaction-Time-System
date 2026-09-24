# ProStart app

The Flutter app for ProStart. It covers the two product modes: **reaction time**
off the starting block and **photofinish** time from the camera.

**Status: UI only.** Every number and image on screen comes from
`lib/mock_data.dart`. The app has no Bluetooth and does no timing yet. The BLE
layer to port is in `../Old Flutter App/prostart/lib/services/`.

## Run

```bash
flutter pub get
flutter run
```

To open one page directly, for screenshots:

```bash
flutter run --dart-define=PAGE=home         # also: reaction, photofinish
```

Without the define the app starts on the splash screen.

## Layout

```
lib/
├── main.dart                         # app entry, PAGE switch
├── mock_data.dart                    # all the fake data shown on screen
├── theme/app_theme.dart              # colors, spacing, dark theme
├── screens/
│   ├── splash_screen.dart
│   ├── home_shell.dart               # bottom nav; History/Stats/Profile are placeholders
│   ├── home_screen.dart              # device card, the two mode cards, week stats
│   ├── reaction_result_screen.dart
│   └── photofinish_result_screen.dart
└── widgets/                          # glass cards, bottom nav bar, result widgets
assets/images/                        # start trace, finish frame, avatar
Prostart Media/                       # screenshots and page renders for the presentation
```
