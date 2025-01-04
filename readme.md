# Shake to Find Cursor

A Windows utility that helps you locate your cursor by enlarging it when you shake your mouse. When you can't find your cursor on screen, just shake your mouse and the cursor will temporarily become larger.

## Features

- Two tracking modes:
  - Hook mode: Uses Windows hook to track mouse movement
  - Polling mode: Uses timer to track mouse movement
- System tray integration
- Temporary cursor enlargement
- Shake pattern recognition
- Administrator privileges required (for system cursor modification)

## Usage

### Command Line Arguments

- `--hook`: Use hook mode for mouse tracking (default is polling mode)

Example:
```
ShakeToFindCursor.exe --hook
```

### Finding Your Cursor

1. When you lose track of your cursor, shake your mouse rapidly
2. The cursor will temporarily enlarge for better visibility
3. After 0.5 seconds, the cursor will return to its normal size

### System Tray

- Right-click the tray icon to access the menu
- Select "Exit" to close the application

## Auto-start Setup

To have the application launch at Windows startup:
1. Right-click the tray icon and click "Enable Auto-start."
2. A scheduled task will be created to start the application automatically.

To remove auto-start:
1. Right-click the tray icon again.
2. Click "Disable Auto-start" to remove the scheduled task.

## System Requirements

- Windows 7 or later
- Administrator privileges
- No additional dependencies required

## Building

1. Clone the repository
2. Create a "build" folder  
3. Run "cmake .." inside that folder  
4. Build the project using your chosen compiler

## Configuration

The following parameters can be adjusted in `CursorConfig` class:

- `kScaleFactor`: Cursor enlargement factor (default: 3.0)
- `kEnlargeDurationMs`: Duration of cursor enlargement (default: 500ms)
- `kHistorySize`: Number of movements to track for shake detection (default: 10)
- `kMinDirectionChanges`: Minimum direction changes to trigger enlargement (default: 5)
- `kMinMovementSpeed`: Minimum speed to consider as shaking (default: 800 pixels/second)
- `kMaxTimeWindow`: Time window for shake detection (default: 500ms)

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Inspired by macOS's shake-to-find-cursor feature