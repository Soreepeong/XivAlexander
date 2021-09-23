# Download [here](https://github.com/Soreepeong/XivAlexander/releases)
Use [**Releases**](https://github.com/Soreepeong/XivAlexander/releases) section on the sidebar.
Don't download using the green Code button.

# XivAlexander
Latency mitigation and a bunch of modding tools mixed into one giant mishmash of a program.

## Mitigate lags
| Connection | Image |
| --- | --- |
| Korea to NA DC<br />VPN only | ![Before GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/before.gif) |
| Korea to NA DC<br />XivAlexander enabled | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/after.gif) | 
| Korea to Korean DC<br />Direct connection | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/ref.gif) | 

This add-on will make double (or triple, depending on your skill/spell speed) weaving always possible.
No more low DPS output just because you are on high latency network. **[Read how does it work.](https://github.com/Soreepeong/XivAlexander/wiki/Interface:-Main-Menu)**

Use [XivMitmLatencyMitigator](https://github.com/Soreepeong/XivMitmLatencyMitigator) instead if you're not on Mac, PS4,
or PS5, or want to apply this solution to a custom VPN server running on Linux operating system.

## Apply mods easily
![Example mod menu](https://user-images.githubusercontent.com/3614868/134066429-67f7d590-6ab3-4f82-a9d9-33321ff3ff53.png)
Easily import (drag and drop TTMP2 file onto control window!) and use TexTools ModPack, and configure if a ModPack requires it.
**[Read how to apply TexTools ModPacks.](https://github.com/Soreepeong/XivAlexander/wiki/Modding)**

### Feature for language learners
![English text with Japanese text](https://user-images.githubusercontent.com/3614868/133910444-e44f6ac0-649b-4e8e-bcf8-5bb2c597d0ad.png)
Learn Japanese while playing Final Fantasy XIV. Or the other way - learn English if you're a native Japanese speaker.
Or, quickly figure out what is an item called in other languages, or how would autotranslate feature show a phrase in a
different language.
**[Read how to do this.](https://github.com/Soreepeong/XivAlexander/wiki/EXDF-Transformation-Rules)**

### Use alternative fonts
![Data Center Selection screen in Comic Sans](https://user-images.githubusercontent.com/3614868/132991000-e65f4803-c6e2-4318-a412-cdc8343fb615.png)
Play Comic Fantasy XIV.
**[Read how to replace fonts.](https://github.com/Soreepeong/XivAlexander/wiki/Set-up-font-replacement)**

Also, this can be used to use characters that are not natively supported by the game (Use Chinese or Korean characters in International client.)
Except for right-to-left languages, which the game probably doesn't support at all.

## Quickstart
It does not matter whichever you run - `XivAlexanderLoader32.exe` or `XivAlexanderLoader64.exe`.

* Portable 1: Run the game if it isn't already running, run `XivAlexanderLoader32/64.exe`, and follow the instructions.
* Portable 2: Run `XivAlexanderLoader32/64.exe`, and log in using the official launcher.
* Installation: Autoload XivAlexander when you launch the game in whatever way. Compatible with Reshade and stuff.
  **[Read how to install and uninstall.](https://github.com/Soreepeong/XivAlexander/wiki/Installation)**

## Building
* This project uses [vcpkg](https://github.com/microsoft/vcpkg) to import dependencies.
* Make `Certificate.pfx` and `CertificatePassword.txt` in `Build` directory.

## License
Apache License 2.0
