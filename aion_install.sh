#!/bin/bash
# AION HT - Spotify APK installer via ADB
# Usage: ./aion_install.sh [path/to/Spotify.apk]

set -e

APK="${1:-Spotify.apk}"
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[+]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[-]${NC} $1"; exit 1; }

# 1. Check ADB installed
if ! command -v adb &>/dev/null; then
    error "ADB לא מותקן. הרץ: sudo apt install adb"
fi

# 2. Check APK exists
if [[ ! -f "$APK" ]]; then
    error "לא נמצא קובץ APK: $APK"
fi

info "מחפש מכשירים מחוברים..."
adb kill-server &>/dev/null
adb start-server &>/dev/null

# Wait for device
TIMEOUT=15
COUNT=0
while ! adb devices | grep -q "device$"; do
    sleep 1
    COUNT=$((COUNT+1))
    if [[ $COUNT -ge $TIMEOUT ]]; then
        error "לא נמצא מכשיר. ודא שה-USB מחובר לרכב והרכב דולק."
    fi
    echo -ne "\r  ממתין לחיבור... ${COUNT}s"
done
echo ""

DEVICE=$(adb devices | grep "device$" | awk '{print $1}')
info "מחובר ל: $DEVICE"

# 3. Print Android version info
info "מידע על המערכת:"
adb shell getprop ro.product.model 2>/dev/null | xargs echo "  דגם :"
adb shell getprop ro.build.version.release 2>/dev/null | xargs echo "  אנדרואד:"
adb shell getprop ro.product.manufacturer 2>/dev/null | xargs echo "  יצרן :"

# 4. Allow unknown sources (best effort - may not work on all builds)
info "מנסה לאפשר התקנה ממקורות לא ידועים..."
adb shell settings put global install_non_market_apps 1 2>/dev/null || warn "לא הצליח — נמשיך בכל מקרה"

# 5. Install APK
info "מתקין $APK ..."
adb install -r -d "$APK" && info "✓ Spotify הותקנה בהצלחה!" || {
    warn "התקנה רגילה נכשלה, מנסה עם גישה מלאה..."
    adb install -r -d -g "$APK" || error "ההתקנה נכשלה. ראה הוראות ידניות למטה."
}

# 6. Try to launch
info "מנסה להפעיל את Spotify..."
adb shell monkey -p com.spotify.music 1 2>/dev/null || \
adb shell monkey -p com.spotify.musix.automotive 1 2>/dev/null || \
warn "לא הצליח להפעיל אוטומטית — פתח ידנית מהמסך."

echo ""
info "סיום! חפש את Spotify במסך הרכב."
