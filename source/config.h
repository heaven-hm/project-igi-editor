#pragma once
#include <string>
#include <vector>

struct KeyBinding {
    int vkCode; // Windows virtual key code
    bool ctrl;
    bool shift;
    bool alt;
};

struct ConfigData {
    // [GamePath]
    std::string igiPath;
    int level;
    std::string editorPath;     // Path to this editor
    std::string qEditorPath;    // Path to %APPDATA%\QEditor

    // [Paths]
    std::string aiPath;
    std::string compilerPath;
    std::string filesPath;
    std::string graphsPath;

    // [Marker] - IGI 2 Style Manipulation
    char keySnapGround;
    char keySnapObject;
    char keyRotateAlpha;
    char keyRotateBeta;
    char keyRotateGamma;
    char keyResetOri;
    char keyResetPos;

    // [Marker] - Shortcuts
    int teleportToMarker; // Changed to int for VK codes
    int resetMarkerToPlayer;

    // Movement Keys
    int keyMoveForward;
    int keyMoveBackward;
    int keyMoveLeft;
    int keyMoveRight;

    // [UI] - Font and UI settings
    float fontSize;
    int fontColorR;
    int fontColorG;
    int fontColorB;

    // [Keybindings] - Configurable shortcuts
    KeyBinding keySave;
    KeyBinding keyResetLevel;
    KeyBinding keyDebug;
    KeyBinding keyQuit;
    KeyBinding keyHelp;
    KeyBinding keyResetScript;

    // NEW: Camera Controls
    KeyBinding keyEnableCamera;
    KeyBinding keyMoveCameraForward;
    KeyBinding keyMoveCameraBackward;
    KeyBinding keyAdjustCameraRadius;
    KeyBinding keyLookDown;
    KeyBinding keySnapToObject;
    KeyBinding keySnapToGround;
    KeyBinding keyClipMode;

    // NEW: Task Controls
    KeyBinding keyCreateNewTask;
    KeyBinding keyCopyTask;
    KeyBinding keyPasteTask;
    KeyBinding keyDeleteTask;
    KeyBinding keyAssignTaskID;

    // NEW: Animation Tasks
    KeyBinding keyStartRecording;
    KeyBinding keyGoToCursor;
    KeyBinding keySyncPlayback;

    // NEW: Miscellaneous
    KeyBinding keyUndo;
    KeyBinding keyRedo;
    KeyBinding keyReloadSettings;
};

class Config {
public:
    static void Init();
    static ConfigData& Get();
    static void Save();
    
private:
    static void Load();
    static void CreateDefault();
    static std::string GetConfigPath();
    
    static ConfigData data_;
};
