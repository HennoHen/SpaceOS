# 🚀 SpaceOS ESP32 v1.0 ✨
### *Das süßeste kleine Betriebssystem für dein NodeMCU ESP32 Board!* 🪐💫

Hallo Weltraum-Reisender! 👩‍🚀👨‍‍🚀 Willkommen bei **SpaceOS**, einem liebevoll gestalteten Mini-Betriebssystem, das aus einem einfachen ESP32-Board einen interaktiven, nostalgischen Taschencomputer zaubert! 📟💖

---

## 🌸 Was macht SpaceOS so besonders?

Dieses Projekt ist eine absolute Herzensangelegenheit! Es kombiniert die Retro-Vibes von alten Handys mit smarter Funktionalität. Stell dir vor: Ein Betriebssystem, das auf einem winzigen 16x2 Display läuft, über ein Tastenfeld gesteuert wird und dir trotzdem die weite Welt des Internets (Wetter, ChatGPT) direkt auf den Schreibtisch bringt! 🌌🌟

---

## 🍓 Die zuckersüßen Features im Überblick:

* **🔐 Sicher & Geborgen:** Ein Login-System mit Passwort schützt deine kleinen Geheimnisse. Es gibt sogar eine "Passwort vergessen"-Frage, falls du mal etwas vergesslich bist! 🙈
* **📱 T9-Texteingabe:** Tippe Nachrichten und Notizen wie im Jahr 2000! (1 für ABC, 2 für DEF... weißt du noch? 📟)
* **📝 Kuschelige Notizen (Notes):** Schreibe deine Gedanken auf! Sie werden sicher im Flash-Speicher oder auf deiner SD-Karte aufbewahrt. 💾
* **🐍 Das legendäre Snake:** Jaaa! Ein eingebautes Snake-Spiel inklusive einer Highscore-Liste für die ewige Ehre! 🏆🐍
* **⏰ Zeit-Helferlein:** Eine Stoppuhr für deine Tee-Zubereitung und ein Countdown-Timer, der fröhlich piepst, wenn die Zeit um ist! ☕️⏱️
* **🌤️ Wetter-Frosch:** Holt dir per WLAN ganz unkompliziert das aktuelle Wetter direkt aufs Display. ☁️☀️
* **🤖 KI in der Tasche:** Vorbereitet für ChatGPT – mach deinen kleinen Computer super intelligent! 🧠✨

---

## 🛠️ Das brauchst du für dein Hardware-Abenteuer:

* **Gehirn:** NodeMCU ESP32 🧠
* **Auge:** LCD Display 16x2 (I2C, Adresse `0x27`) 👀
* **Finger:** 4x4 Matrix Keypad 🎹
* **Herzschlag:** RTC DS1307 (Echtzeituhr) ⏰
* **Stimme:** Piezo-Summer 🎶
* **Rucksack:** SD-Karten-Modul (SPI) 🎒
* **Deko:** Eine grüne und eine weiße LED für fröhliches Blinken! 🟢⚪️

---

## 📂 Pin-Belegung (Damit alles richtig kuschelt):

| Komponente | ESP32 Pin | Funktion |
| :--- | :--- | :--- |
| **Grüne LED** | Pin 2 | Status-Anzeige 🟢 |
| **Weiße LED** | Pin 4 | Extra-Bling ⚪️ |
| **Summer** | Pin 5 | Süße Töne & Alarme 🎵 |
| **Button** | Pin 15 | Physischer Knopf 🔘 |
| **SD Card CS**| Pin 16 | Speicherkarte 💾 |
| **I2C SDA** | Pin 21 | Display & Uhr Daten 🗺️ |
| **I2C SCL** | Pin 22 | Display & Uhr Takt ⏱️ |

---

## 🚀 Wie du SpaceOS zum Leben erweckst:

1.  Verwende die **Arduino IDE** oder PlatformIO.
2.  Installiere die benötigten Bibliotheken (`LiquidCrystal_I2C`, `Keypad`, `RTClib`).
3.  Trage deine WLAN-Daten und deinen ChatGPT-Key direkt im Code ein.
4.  Lade den Code auf deinen ESP32 hoch... und **TADA!** 🎉 Dein SpaceOS bootet mit einem fröhlichen Piepsen!

---

## 💌 Fazit des Autors
*SpaceOS ist ein unfassbar charmantes Projekt, das zeigt, wie viel Liebe und Funktionalität in so kleinen Mikrocontrollern stecken kann. Der Name klingt nach fernen Galaxien und großen Träumen – perfekt für einen so tapferen, kleinen Computer!* 🪐✨

Viel Spaß beim Basteln und Entdecken! 🛠️🥰
