"""
Ion HT Infotainment System - Spotify Integration
Connects the Ion HT vehicle dashboard to Spotify Web API.
"""

import os
import time
import spotipy
from spotipy.oauth2 import SpotifyOAuth


SCOPES = " ".join([
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "playlist-read-private",
    "playlist-modify-private",
    "user-library-read",
])


def get_client() -> spotipy.Spotify:
    auth = SpotifyOAuth(
        client_id=os.environ["SPOTIFY_CLIENT_ID"],
        client_secret=os.environ["SPOTIFY_CLIENT_SECRET"],
        redirect_uri=os.environ.get("SPOTIFY_REDIRECT_URI", "http://localhost:8888/callback"),
        scope=SCOPES,
    )
    return spotipy.Spotify(auth_manager=auth)


# ---------- playback helpers ----------

def now_playing(sp: spotipy.Spotify) -> dict | None:
    """Return current track info or None if nothing is playing."""
    state = sp.current_playback()
    if not state or not state.get("item"):
        return None
    item = state["item"]
    return {
        "title": item["name"],
        "artist": ", ".join(a["name"] for a in item["artists"]),
        "album": item["album"]["name"],
        "duration_ms": item["duration_ms"],
        "progress_ms": state["progress_ms"],
        "is_playing": state["is_playing"],
        "volume": state["device"]["volume_percent"],
        "uri": item["uri"],
    }


def format_ms(ms: int) -> str:
    s = ms // 1000
    return f"{s // 60}:{s % 60:02d}"


def display_now_playing(sp: spotipy.Spotify) -> None:
    info = now_playing(sp)
    if not info:
        print("[Ion HT] Nothing is playing on Spotify.")
        return
    status = "▶" if info["is_playing"] else "⏸"
    progress = format_ms(info["progress_ms"])
    duration = format_ms(info["duration_ms"])
    print(f"\n{'─' * 50}")
    print(f"  {status}  {info['title']}")
    print(f"     Artist : {info['artist']}")
    print(f"     Album  : {info['album']}")
    print(f"     Time   : {progress} / {duration}")
    print(f"     Volume : {info['volume']}%")
    print(f"{'─' * 50}\n")


# ---------- controls ----------

def play_pause(sp: spotipy.Spotify) -> None:
    state = sp.current_playback()
    if state and state["is_playing"]:
        sp.pause_playback()
        print("[Ion HT] Paused.")
    else:
        sp.start_playback()
        print("[Ion HT] Playing.")


def next_track(sp: spotipy.Spotify) -> None:
    sp.next_track()
    time.sleep(0.5)
    display_now_playing(sp)


def previous_track(sp: spotipy.Spotify) -> None:
    sp.previous_track()
    time.sleep(0.5)
    display_now_playing(sp)


def set_volume(sp: spotipy.Spotify, level: int) -> None:
    level = max(0, min(100, level))
    sp.volume(level)
    print(f"[Ion HT] Volume set to {level}%.")


# ---------- search ----------

def search(sp: spotipy.Spotify, query: str, limit: int = 5) -> list[dict]:
    results = sp.search(q=query, type="track", limit=limit)
    tracks = []
    for i, item in enumerate(results["tracks"]["items"], 1):
        tracks.append({
            "index": i,
            "title": item["name"],
            "artist": ", ".join(a["name"] for a in item["artists"]),
            "album": item["album"]["name"],
            "uri": item["uri"],
        })
    return tracks


def display_search_results(tracks: list[dict]) -> None:
    if not tracks:
        print("[Ion HT] No results found.")
        return
    print(f"\n{'─' * 50}")
    for t in tracks:
        print(f"  {t['index']}. {t['title']} — {t['artist']}")
    print(f"{'─' * 50}\n")


def play_track(sp: spotipy.Spotify, uri: str) -> None:
    sp.start_playback(uris=[uri])
    time.sleep(0.5)
    display_now_playing(sp)


# ---------- playlist ----------

def create_playlist(sp: spotipy.Spotify, name: str, track_uris: list[str]) -> str:
    user_id = sp.current_user()["id"]
    playlist = sp.user_playlist_create(user_id, name, public=False)
    sp.playlist_add_items(playlist["id"], track_uris)
    print(f"[Ion HT] Playlist '{name}' created with {len(track_uris)} tracks.")
    return playlist["id"]


# ---------- Ion HT dashboard loop ----------

def dashboard(sp: spotipy.Spotify) -> None:
    print("\n" + "=" * 50)
    print("   Ion HT Infotainment — Spotify")
    print("=" * 50)
    print("Commands: now | play | next | prev | vol <0-100>")
    print("          search <query> | quit")
    print("=" * 50 + "\n")

    display_now_playing(sp)
    last_search: list[dict] = []

    while True:
        try:
            raw = input("Ion HT> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n[Ion HT] Goodbye.")
            break

        if not raw:
            continue

        parts = raw.split(maxsplit=1)
        cmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if cmd in ("quit", "exit", "q"):
            print("[Ion HT] Goodbye.")
            break
        elif cmd == "now":
            display_now_playing(sp)
        elif cmd == "play":
            if arg.startswith("spotify:track:") or arg.startswith("spotify:"):
                play_track(sp, arg)
            elif arg.isdigit() and last_search:
                idx = int(arg) - 1
                if 0 <= idx < len(last_search):
                    play_track(sp, last_search[idx]["uri"])
                else:
                    print("[Ion HT] Invalid track number.")
            else:
                play_pause(sp)
        elif cmd == "next":
            next_track(sp)
        elif cmd == "prev":
            previous_track(sp)
        elif cmd == "vol":
            if arg.isdigit():
                set_volume(sp, int(arg))
            else:
                print("[Ion HT] Usage: vol <0-100>")
        elif cmd == "search":
            if not arg:
                print("[Ion HT] Usage: search <query>")
            else:
                last_search = search(sp, arg)
                display_search_results(last_search)
                print("  Tip: type 'play <number>' to play a result.")
        else:
            print(f"[Ion HT] Unknown command: '{cmd}'")


# ---------- entry point ----------

if __name__ == "__main__":
    try:
        sp = get_client()
        dashboard(sp)
    except KeyboardInterrupt:
        print("\n[Ion HT] Session ended.")
