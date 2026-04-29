import os
import time
import requests
import anthropic

TELEGRAM_TOKEN = "8419768951:AAGCWbKMcq0IWmXtRQQw3UmzOnfEsgJsX5k"
TELEGRAM_API = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}"
ANTHROPIC_API_KEY = os.environ.get("ANTHROPIC_API_KEY", "")

client = anthropic.Anthropic(api_key=ANTHROPIC_API_KEY)

# שמירת היסטוריית שיחה לכל משתמש
conversation_histories = {}

SYSTEM_PROMPT = """אתה Claude, עוזר AI חכם ומועיל של Anthropic.
אתה מדבר בעברית אלא אם המשתמש כותב בשפה אחרת.
היה קצר, ישיר וידידותי."""


def send_message(chat_id: int, text: str) -> None:
    requests.post(f"{TELEGRAM_API}/sendMessage", json={
        "chat_id": chat_id,
        "text": text,
        "parse_mode": "Markdown"
    })


def send_typing(chat_id: int) -> None:
    requests.post(f"{TELEGRAM_API}/sendChatAction", json={
        "chat_id": chat_id,
        "action": "typing"
    })


def get_claude_response(chat_id: int, user_message: str) -> str:
    if chat_id not in conversation_histories:
        conversation_histories[chat_id] = []

    conversation_histories[chat_id].append({
        "role": "user",
        "content": user_message
    })

    # שמירה על מקסימום 20 הודעות אחרונות
    if len(conversation_histories[chat_id]) > 20:
        conversation_histories[chat_id] = conversation_histories[chat_id][-20:]

    response = client.messages.create(
        model="claude-opus-4-7",
        max_tokens=1024,
        system=SYSTEM_PROMPT,
        messages=conversation_histories[chat_id]
    )

    assistant_reply = response.content[0].text

    conversation_histories[chat_id].append({
        "role": "assistant",
        "content": assistant_reply
    })

    return assistant_reply


def handle_update(update: dict) -> None:
    message = update.get("message")
    if not message:
        return

    chat_id = message["chat"]["id"]
    text = message.get("text", "")

    if not text:
        return

    if text == "/start":
        send_message(chat_id, "שלום! 👋 אני Claude, עוזר AI של Anthropic.\nשלח לי כל שאלה ואענה לך!")
        return

    if text == "/clear":
        conversation_histories.pop(chat_id, None)
        send_message(chat_id, "✅ היסטוריית השיחה נמחקה. נתחיל מחדש!")
        return

    send_typing(chat_id)

    try:
        reply = get_claude_response(chat_id, text)
        send_message(chat_id, reply)
    except Exception as e:
        send_message(chat_id, f"❌ שגיאה: {str(e)}")


def run_bot() -> None:
    print("🤖 הבוט פעיל! שלח הודעה בטלגרם...")
    offset = 0

    while True:
        try:
            response = requests.get(f"{TELEGRAM_API}/getUpdates", params={
                "offset": offset,
                "timeout": 30
            }, timeout=35)

            updates = response.json().get("result", [])

            for update in updates:
                offset = update["update_id"] + 1
                handle_update(update)

        except requests.exceptions.Timeout:
            continue
        except Exception as e:
            print(f"שגיאה: {e}")
            time.sleep(5)


if __name__ == "__main__":
    if not ANTHROPIC_API_KEY:
        print("❌ חסר ANTHROPIC_API_KEY - הגדר את משתנה הסביבה ונסה שוב")
        print("   export ANTHROPIC_API_KEY='your-key-here'")
        exit(1)
    run_bot()
