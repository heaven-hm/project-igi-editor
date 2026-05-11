import pyautogui
import time
import pygetwindow as gw

pyautogui.FAILSAFE = False

windows = gw.getWindowsWithTitle('IGI')
if windows:
    win = windows[0]
    try:
        win.activate()
    except Exception as e:
        print("Could not activate:", e)
    
    time.sleep(1)
    pyautogui.press('f4')
    time.sleep(1)
    
    center_x = win.left + win.width // 2
    center_y = win.top + win.height // 2

    for i in range(3):
        pyautogui.moveTo(center_x, center_y)
        pyautogui.dragTo(center_x + 300, center_y, 0.5, button='left')
        time.sleep(0.2)
        pyautogui.screenshot(f'screenshot_horiz_{i}.png')
        print(f"Screenshot saved to screenshot_horiz_{i}.png")
else:
    print("Window not found")
