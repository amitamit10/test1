import pyautogui
import time

while True:
    x, y = pyautogui.position()
    print(f"X: {x} Y: {y}", end="\r")
    time.sleep(0.1)
