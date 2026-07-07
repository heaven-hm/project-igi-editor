#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct KeyBinding {
    int vkCode; // Windows virtual key code
    bool ctrl;
    bool shift;
    bool alt;
};

struct ConfigData {
    // [GamePath]
    int level;
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
    KeyBinding keyToggleGame;
    KeyBinding keySaveState;
    KeyBinding keyToggleSaveStateOnExit;
    KeyBinding keyToggleMagicObj;

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

    // NEW: Logging
    bool enableLogging;
    bool debugLogging;

    // [Renderer]
    bool enableLOD; // Portal/attachment distance culling for buildings
    bool enableLightmaps; // Apply calculated "Calculate Lightmap" textures during render (Escape menu checkbox)
    bool enableFog; // Terrain/object atmospheric fog — toggle in Terrain Settings panel
    int  fogIntensity; // Fog intensity 0-200% (default 10 = 10%)
    bool musicEnabled; // Escape-menu Music checkbox preference — persists across level loads/game launches

    // NEW: Advanced QED Settings
    int consoleAutoActivate;
    int searchType;
    bool invertMouse;
    bool displayTaskNote;
    bool allowDynamicSwitching;
    bool saveConfigOnExit;
    bool auto_save_enabled;
    int auto_save_interval_seconds;
    bool runEvent;
    bool cameraLock;
    bool enableBackup;
    bool useEditorFont;  // TRUE: render HUD with editor/qed/editor.fnt; FALSE: GLUT system font
    int  systemFontSize; // GLUT system font point size: 10, 12 (default), or 18
    std::string findTaskName;
    std::string findTaskNote;
    std::string findTaskID;
    std::string findTaskText;
    std::string taskFileName;
    std::string objectFilePath;
    int interpolation;
    float renderZNear;
    int graphNodeSize;

    // Camera State
    float cameraOriX, cameraOriY, cameraOriZ;
    float cameraRadiusX, cameraRadiusY;
    float cameraPosX, cameraPosY, cameraPosZ;
    float cameraMatX, cameraMatY, cameraMatZ;

    // Full event bindings map (all ~120 events loaded from config)
    std::unordered_map<std::string, KeyBinding> eventBindings_;

    // Per-level background music override: level number -> filename (just the
    // name, e.g. "music_wind.wav") to look up under that level's sounds/ dir.
    // Levels with no entry fall back to "game_music.wav".
    std::unordered_map<int, std::string> levelMusicFiles;
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
    static std::string GetKeybindingsPath();
    
    static ConfigData data_;
};
