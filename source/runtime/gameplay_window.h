// gameplay_window.h - Windows gameplay-window lifecycle behind a narrow host seam
#pragma once

namespace igi {

struct GameplayWindowCallbacks {
    void (*display)() = nullptr;
    void (*reshape)(int width, int height) = nullptr;
    void (*mouse)(int button, int state, int x, int y) = nullptr;
    void (*mouse_wheel)(int wheel, int direction, int x, int y) = nullptr;
    void (*motion)(int x, int y) = nullptr;
    void (*passive_motion)(int x, int y) = nullptr;
    void (*special)(int key, int x, int y) = nullptr;
    void (*special_up)(int key, int x, int y) = nullptr;
    void (*keyboard)(unsigned char key, int x, int y) = nullptr;
    void (*keyboard_up)(unsigned char key, int x, int y) = nullptr;
    void (*close)() = nullptr;
};

class GameplayWindowHost {
public:
    GameplayWindowHost() = default;
    ~GameplayWindowHost();

    GameplayWindowHost(const GameplayWindowHost&) = delete;
    GameplayWindowHost& operator=(const GameplayWindowHost&) = delete;

    bool Create(
        int editor_window_id,
        int width,
        int height,
        const GameplayWindowCallbacks& callbacks);
    void Destroy();
    void NotifyClosed();

    void Show();
    void Hide();
    void Focus();
    void FocusEditor();
    void MakeCurrent() const;

    bool IsCreated() const { return gameplay_window_id_ != 0; }
    bool IsCurrent() const;
    int GetWindowId() const { return gameplay_window_id_; }

private:
    int editor_window_id_ = 0;
    int gameplay_window_id_ = 0;
};

} // namespace igi
