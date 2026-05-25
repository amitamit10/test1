# Ion HT Infotainment — Spotify Integration

Connects the Ion HT vehicle dashboard to Spotify via the Web API.

## Setup

```bash
pip install -r requirements.txt
```

Set environment variables:

```bash
export SPOTIFY_CLIENT_ID=<your_client_id>
export SPOTIFY_CLIENT_SECRET=<your_client_secret>
export SPOTIFY_REDIRECT_URI=http://localhost:8888/callback   # optional
```

Get credentials from [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).

## Run

```bash
python ion_ht_spotify.py
```

## Commands

| Command | Action |
|---|---|
| `now` | Show currently playing track |
| `play` | Toggle play / pause |
| `play <number>` | Play a search result by number |
| `next` | Skip to next track |
| `prev` | Go to previous track |
| `vol <0-100>` | Set volume |
| `search <query>` | Search for tracks |
| `quit` | Exit |
