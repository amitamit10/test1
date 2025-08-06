import pyautogui

# הזזת העכבר למיקום
pyautogui.moveTo(100, 100)

# קליק שמאלי
pyautogui.click()

# גרירה
pyautogui.dragTo(500, 500, duration=1)

# קבלת מיקום העכבר
x, y = pyautogui.position()
print(f"Mouse at: {x}, {y}")
