# Download [here](https://github.com/Soreepeong/XivAlexander/releases)
Use [**Releases**](https://github.com/Soreepeong/XivAlexander/releases) section on the sidebar.
Don't download using the green Code button.

Use [XivMitmLatencyMitigator](https://github.com/Soreepeong/XivMitmLatencyMitigator) instead if you're not on Mac, PS4,
or PS5, or want to apply this solution to a custom VPN server running on Linux operating system.

# XivAlexander
| Connection | Image |
| --- | --- |
| Korea to NA DC<br />VPN only | ![Before GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/before.gif) |
| Korea to NA DC<br />XivAlexander enabled | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/after.gif) | 
| Korea to Korean DC<br />Direct connection | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/ref.gif) | 

This add-on will make double (or triple, depending on your skill/spell speed) weaving always possible.
No more low DPS output just because you are on high latency network.

## Usage
Run the game if it isn't already running, run `XivAlexanderLoader.exe`, and follow the instructions.
Try running as Administrator if it fails to load.

**As it uses DLL injection, your anti-virus software might flag this add-on.**
You might have to add both `XivAlexander.dll` and `XivAlexanderLoader.exe` to exclusion list.
Refer to your anti-virus software manual for instructions.

**[Find opcodes](https://github.com/Soreepeong/XivAlexander/wiki/How-to-find-opcodes)** after game updates.
This add-on will break whenever ACT FFXIV Plugin breaks.

### Command line arguments
Usage: XivAlexanderLoader [options] targets

#### Positional arguments
* targets: list of target process ID or path suffix.

#### Optional arguments
* -h (--help): show this help message and exit
* -a (--action): specifies default action for each process (possible values: ask, load, unload)
* -q (--quiet): disables error messages

#### Example:
* `XivAlexanderLoader.exe -a load 16844`: Loads XivAlexander into process ID 16844.
* `XivAlexanderLoader.exe -q -a unload ffxiv_dx11.exe`: Unloads XivAlexander from every process with its path ending with `ffxiv_dx11.exe`, and suppress all error messages.

### Description on options
* `High Latency Mitigation`: The purpose of this program.
  * `Enable`: Turn this off when you're looking for updated opcodes.
  * `Use Delay Detection`: When checked, the program will try to detect how much time does the server spend to process your action requests.
    * When unchecked, the program will assume the server always take 75ms to process your action requests.
  * Turn both `Use Delay Detection` and `Use Latency Correction` off if...
    * ...your ping is above 200ms.
    * ...your VPN gives a fake ping of 0ms (&lt;1ms).
* `Reduce Packet Delay`: When checked, following additional socket options are set or changed. 
  * `TCP_NODELAY`: If enabled, sends packets as soon as possible instead of waiting for ACK or large packet.
  * `SIO_TCP_SET_ACK_FREQUENCY`: Controls ACK delay every x ACK. Default delay is 40 or 200ms. Default value is 2 ACKs, set to 1 to disable ACK delay.
* `Use IPC Type Finder` and `Use All IPC Message Logger`: When checked, the program will dump some network traffic and print all opcodes, to assist finding out the opcodes after patch.
* `Use Effect Appliation Delay Logger`: When enabled, the program will log whenever an action actually takes effect. Used to measure how long does it take for an action to actually take effect. 
   
## How it works
After you use an action, the game will apply 500ms animation lock. 
When server responds to action use request, it will contain animation lock duration information, and the game client will re-apply the animation lock with the duration given from the server.
The game does not take latency into account, so higher your latency is, longer the net effect of animation lock will be. (up to 500ms + &lt;action animation lock duration&gt;).
This add-on will subtract the time taken for a response from the server for the particular request from animation lock duration.

In case when it takes more than 500ms to receive a response, the client will let you use two actions consecutively, which will occasionally trigger server-side sanity check to reject the third action, or even delay the usage of the third action.
This add-on will prevent that from happening by forcing animation lock times for both actions combined.

Example:
1. [0.0s] Action A Request
2. [0.5s] Action B Request
3. [0.6s] Action A Response (1.0s lock)
4. [1.1s] Action B Response (1.0s lock)
* By default, the client will let you input at 2.1s.
* This addon will force the next input to be accepted at 2.0s.

**Because of how it works, this addon is only a step away from flat out cheating.
Changing temporal constants in code below the limit means you're effectively claiming that your latency is below zero,
which is just impossible. Do NOT modify temporal constants in code, or you ARE cheating. You have been warned.**

## Third-party Libraries
This project uses [vcpkg](https://github.com/microsoft/vcpkg) to import dependencies.

* https://github.com/TsudaKageyu/minhook
* https://github.com/madler/zlib
* https://github.com/mirror/scintilla
* https://github.com/nlohmann/json

## License
Apache License 2.0
