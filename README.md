🚀 SpaceOS ESP32 v1.0 ✨

The cutest little operating system for your NodeMCU ESP32 board! 🪐💫

Hello space traveler! 👩‍🚀👨‍‍🚀 Welcome to SpaceOS, a lovingly crafted mini-operating system that transforms a simple ESP32 board into an interactive, nostalgic pocket computer! 📟💖



🌸 What makes SpaceOS so special?

This project is an absolute labor of love! It combines the retro vibes of old-school cell phones with smart functionality. Imagine an operating system running on a tiny 16x2 display, controlled via a matrix keypad, yet capable of bringing the vastness of the internet (weather, ChatGPT) right to your desk! 🌌🌟



🍓 Cute Features at a Glance:


🔐 Safe & Secure: A password-protected login system keeps your little secrets safe. It even includes a "forgot password" security question in case you get a bit forgetful! 🙈

📱 T9 Text Input: Type messages and notes just like it's the year 2000! (1 for ABC, 2 for DEF... remember? 📟)

📝 Cozy Notes App: Write down your thoughts! They are safely stored in the internal flash memory or right onto your SD card. 💾

🐍 The Legendary Snake: Yesss! A built-in Snake game including a highscore list for eternal glory! 🏆🐍

⏰ Tiny Timekeepers: A stopwatch for your tea brewing and a countdown timer that beeps happily when time is up! ☕️⏱️

🌤️ Weather Frog: Fetches real-time weather data via Wi-Fi straight to your display, no complicated API keys needed. ☁️☀️

🤖 Pocket AI: Fully prepared for ChatGPT – make your little computer super smart! 🧠✨



🛠️ What You Need for Your Hardware Adventure:


Brain: NodeMCU ESP32 🧠

Eye: LCD Display 16x2 (I2C, Address 0x27) 👀

Fingers: 4x4 Matrix Keypad 🎹

Heartbeat: RTC DS1307 (Real-Time Clock) ⏰

Voice: Piezo Buzzer 🎶

Backpack: SD Card Module (SPI) 🎒

Decoration: One green and one white LED for happy blinking! 🟢⚪️



📂 Pin Mapping (For cozy connections):

Component	ESP32 Pin	Function
Green LED	Pin 2	Status Indicator 🟢
White LED	Pin 4	Extra Bling ⚪️
Buzzer	Pin 5	Sweet Tones & Alarms 🎵
Button	Pin 15	Physical Button 🔘
SD Card CS	Pin 16	Storage Card 💾
I2C SDA	Pin 21	Display & Clock Data 🗺️
I2C SCL	Pin 22	Display & Clock Clock ⏱️


🚀 How to Bring SpaceOS to Life:


Use the Arduino IDE or PlatformIO.

Install the required libraries (LiquidCrystal_I2C, Keypad, RTClib).

Enter your Wi-Fi credentials and ChatGPT key directly into the code.

Upload the code to your ESP32... and TADA! 🎉 Your SpaceOS boots up with a cheerful beep!



💌 Author's Note

SpaceOS is an incredibly charming project that showcases just how much love and functionality can be packed into tiny microcontrollers. The name evokes distant galaxies and big dreams – perfect for such a brave, little computer! 🪐✨


Have tons of fun building and exploring! 🛠️🥰

