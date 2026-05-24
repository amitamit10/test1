# דיאגרמות מחלקות — Remikob2026tamar
### משחק רמיקוב — C# Windows Forms

> **מאגר:** [`tamaravami-hub/Remikob2026tamar`](https://github.com/tamaravami-hub/Remikob2026tamar)

---

## דיאגרמת מחלקות מלאה

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "primaryColor":        "#1B2A4A",
    "primaryTextColor":    "#FFFFFF",
    "primaryBorderColor":  "#4A90D9",
    "lineColor":           "#7FB3D3",
    "edgeLabelBackground": "#1B2A4A",
    "fontFamily":          "Segoe UI, Helvetica, sans-serif",
    "fontSize":            "14px"
  }
}}%%
classDiagram
    direction TB

    %% ── מסכים (Forms) ──────────────────────────────────────────
    class FormOpening {
        <<מסך פתיחה>>
        +btnTraining_Click()
        +btnComputer_Click()
        +btnRegular_Click()
    }

    class FormTraining {
        <<מצב אימון>>
        -Player player
        -Board  board
        +StartTraining()
        +CheckMove() bool
        +ShowHint()
    }

    class FormComputer {
        <<מצב מחשב>>
        -Player   humanPlayer
        -Computer computer
        -Board    board
        +StartGame()
        +NextTurn()
        +CheckWinner() Player
    }

    class FormRegular {
        <<מצב רגיל>>
        -List~Player~ players
        -Board board
        +StartGame()
        +NextTurn()
        +CheckWinner() Player
    }

    %% ── ישויות משחק ─────────────────────────────────────────────
    class Player {
        <<שחקן>>
        -string name
        -List~Tile~ hand
        +DrawTile(Tile) void
        +PlayTile(Tile) bool
        +GetHandSize() int
    }

    class Computer {
        <<מחשב / בוט>>
        +CalculateBestMove() List~Tile~
        +AutoPlay() void
    }

    class Board {
        <<לוח המשחק>>
        -List~Tile~ pool
        -List~List~Tile~~ sets
        +DrawFromPool() Tile
        +PlaceSet(List~Tile~) bool
        +IsValidSet(List~Tile~) bool
        +IsGameOver() bool
    }

    class Tile {
        <<אריח>>
        -int    number
        -string color
        -bool   isJoker
        +ToString() string
    }

    %% ── קשרים בין מסכים ────────────────────────────────────────
    FormOpening  ..>  FormTraining  : פותח
    FormOpening  ..>  FormComputer  : פותח
    FormOpening  ..>  FormRegular   : פותח

    %% ── קשרים: מסכים ← ישויות ──────────────────────────────────
    FormTraining  -->  Player  : משתמשת ב
    FormTraining  -->  Board   : משתמשת ב
    FormComputer  -->  Player  : שחקן אנושי
    FormComputer  -->  Computer: שחקן מחשב
    FormComputer  -->  Board   : משתמשת ב
    FormRegular   -->  Player  : משתמשת ב
    FormRegular   -->  Board   : משתמשת ב

    %% ── קשרים בין ישויות ────────────────────────────────────────
    Computer  --|>  Player        : יורשת מ
    Player    "1" o-- "0..*" Tile : יד השחקן
    Board     "1" *-- "0..*" Tile : מאגר האריחים

    %% ── צבעים לפי סוג ──────────────────────────────────────────
    classDef screen   fill:#0B3D91,stroke:#4A90D9,stroke-width:2px,color:#FFFFFF,font-weight:bold
    classDef entity   fill:#145A32,stroke:#27AE60,stroke-width:2px,color:#FFFFFF,font-weight:bold
    classDef computer fill:#7D3C00,stroke:#E67E22,stroke-width:2px,color:#FFFFFF,font-weight:bold
    classDef tile     fill:#4A235A,stroke:#9B59B6,stroke-width:2px,color:#FFFFFF

    class FormOpening:::screen
    class FormTraining:::screen
    class FormComputer:::screen
    class FormRegular:::screen
    class Player:::entity
    class Board:::entity
    class Computer:::computer
    class Tile:::tile
```

---

## זרימת ניווט בין המסכים

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "primaryColor":       "#1B2A4A",
    "primaryTextColor":   "#FFFFFF",
    "primaryBorderColor": "#4A90D9",
    "lineColor":          "#4A90D9",
    "fontFamily":         "Segoe UI, Helvetica, sans-serif"
  }
}}%%
flowchart TD
    A([🚀 הפעלת התוכנית])
    B[["🏠 מסך פתיחה\nFormOpening"]]
    C[["🎓 מצב אימון\nFormTraining"]]
    D[["🤖 מצב מחשב\nFormComputer"]]
    E[["👥 מצב רגיל\nFormRegular"]]
    F([🏆 סיום משחק])

    A --> B
    B -->|לחיצה על אימון| C
    B -->|לחיצה על מחשב| D
    B -->|לחיצה על רגיל | E
    C -->|חזרה| B
    D -->|יש מנצח| F
    E -->|יש מנצח| F
    F -->|משחק חדש| B

    style A fill:#0B3D91,stroke:#4A90D9,color:#fff
    style B fill:#0B3D91,stroke:#4A90D9,color:#fff,font-weight:bold
    style C fill:#145A32,stroke:#27AE60,color:#fff
    style D fill:#7D3C00,stroke:#E67E22,color:#fff
    style E fill:#145A32,stroke:#27AE60,color:#fff
    style F fill:#4A235A,stroke:#9B59B6,color:#fff
```

---

## הירארכיית ירושה

```mermaid
%%{init: {
  "theme": "base",
  "themeVariables": {
    "primaryColor":       "#1B2A4A",
    "primaryTextColor":   "#FFFFFF",
    "primaryBorderColor": "#4A90D9",
    "lineColor":          "#27AE60",
    "fontFamily":         "Segoe UI, Helvetica, sans-serif"
  }
}}%%
classDiagram
    direction BT

    class Form {
        <<System.Windows.Forms>>
    }
    class FormOpening {
        <<מסך פתיחה>>
    }
    class FormTraining {
        <<מצב אימון>>
    }
    class FormComputer {
        <<מצב מחשב>>
    }
    class FormRegular {
        <<מצב רגיל>>
    }
    class Player {
        <<שחקן>>
    }
    class Computer {
        <<בוט>>
    }

    FormOpening  --|> Form
    FormTraining --|> Form
    FormComputer --|> Form
    FormRegular  --|> Form
    Computer     --|> Player

    classDef screen   fill:#0B3D91,stroke:#4A90D9,stroke-width:2px,color:#fff
    classDef base     fill:#2C3E50,stroke:#7F8C8D,stroke-width:1px,color:#BDC3C7,font-style:italic
    classDef entity   fill:#145A32,stroke:#27AE60,stroke-width:2px,color:#fff
    classDef computer fill:#7D3C00,stroke:#E67E22,stroke-width:2px,color:#fff

    class Form:::base
    class FormOpening:::screen
    class FormTraining:::screen
    class FormComputer:::screen
    class FormRegular:::screen
    class Player:::entity
    class Computer:::computer
```

---

## סיכום קשרים

| מחלקה | קשר | מחלקה שניה | סוג |
|:---:|:---:|:---:|:---:|
| `FormOpening` | פותחת | `FormTraining` | תלות |
| `FormOpening` | פותחת | `FormComputer` | תלות |
| `FormOpening` | פותחת | `FormRegular` | תלות |
| `FormTraining` | משתמשת ב | `Player`, `Board` | אסוציאציה |
| `FormComputer` | משתמשת ב | `Player`, `Computer`, `Board` | אסוציאציה |
| `FormRegular` | משתמשת ב | `Player` (רשימה), `Board` | אסוציאציה |
| `Computer` | יורשת מ | `Player` | ירושה |
| `Player` | מכיל | `Tile` (יד) | אגרגציה |
| `Board` | מכיל | `Tile` (מאגר) | קומפוזיציה |

---

## מפתח צבעים

| צבע | משמעות |
|:---:|---|
| 🔵 כחול כהה | מסכים / Forms |
| 🟢 ירוק | ישויות משחק (שחקן, לוח) |
| 🟠 כתום | מחשב / בוט |
| 🟣 סגול | אריחים |
