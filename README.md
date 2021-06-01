# Download [here](https://github.com/Soreepeong/XivAlexander/releases)
Use [**Releases**](https://github.com/Soreepeong/XivAlexander/releases) section on the sidebar. Don't download using the green Code button.

# XivAlexander
| Connection | Image |
| --- | --- |
| Korea to NA DC<br />VPN only | ![Before GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/before.gif) |
| Korea to NA DC<br />XivAlexander enabled | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/after.gif) | 
| Korea to Korean DC<br />Direct connection | ![After GIF](https://github.com/Soreepeong/XivAlexander/raw/main/Graphics/ref.gif) | 

Use [XivMitmLatencyMitigator](https://github.com/Soreepeong/XivMitmLatencyMitigator) instead if you're playing on non-Windows system(Linux using WINE, Mac, or PlayStation(PS4)), or want to apply this solution to a custom VPN server running on Linux operating system. You can use both solutions at the same time, in which case, this(XivAlexander) will take precedence.

# English
This add-on will make double weaving always possible. No more low DPS output just because you are on high latency network. (Actions with long delay to begin with - like High Jump - don't count.)

## Usage
Run the game if it isn't already running, run `XivAlexanderLoader.exe`, and follow the instructions. Try running as Administrator if it fails to load.

**As it uses DLL injection, your anti-virus software might flag this add-on.**
You might have to add both `XivAlexander.dll` and `XivAlexanderLoader.exe` to exclusion list.
Refer to your anti-virus software manual for instructions.

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

### First time setup
1. Open `XivAlexander.dll.json` with a text editor which should have been created after a successful load for the first time.
2. Modify opcode values. As of Patch 5.55 for the international release, opcodes are as following:
   ```json
   {
       "ActorCast": "0x0228",
       "ActorControl": "0x030f",
       "ActorControlSelf": "0x0200",
       "AddStatusEffect": "0x0093",
       "RequestUseAction": "0x0396",
       "RequestUseAction2": "0x0205",
       "SkillResultResponse01": "0x0128",
       "SkillResultResponse08": "0x0295",
       "SkillResultResponse16": "0x025e",
       "SkillResultResponse24": "0x0299",
       "SkillResultResponse32": "0x00a7"
   }
   ```
3. Find the XivAlexander icon on your shell notification area (tray area), and click on *Reload Configuration*.

### How to find Opcodes
1. Turn on *Use IPC Type Finder* menu from notification area icon, and open Log window.
2. Cast Cure-like casted actions to self, and figure out `RequestUseAction`, `SkillResultResponse01`, `ActorCast`, and `ActorControlSelf`(this includes cooldown information.) 
3. Use ground targeted action (Asylum, Shukuchi, Sacred Soil, Soten, Earthly Star) to figure out `RequestUseAction2`.
4. Cancel cast by moving around, and figure out `ActorControl`.
5. Use AoE action without any target around, and figure out `SkillResultResponse08`.
6. Enter Sastasha without level synchronization, pull more than 9 adds, and use an AoE action to figure out `SkillResultResponse16`.
7. Do the same but pull more than 17 adds to figure out `SkillResultResponse24`.
8. Leave and re-enter Sastasha, and do the same but pull more than 25 adds to figure out `SkillResultResponse32`.

### Description on options
* `High Latency Mitigation`: The purpose of this program.
  * `Enable`: Turn this off when you're looking for updated opcodes.
  * `Use Delay Detection`: When checked, the program will try to detect how much time does the server spend to process your action requests.
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

**Because of how it works, this addon is only a step away from flat out cheating. Changing parameters below the limit means you're effectively claiming that your latency is below zero, which is just impossible. Do NOT modify numeric parameters outside of opcodes, or you ARE cheating. You have been warned.**

## Third-party Libraries
* https://github.com/TsudaKageyu/minhook
* https://github.com/madler/zlib
* https://github.com/mirror/scintilla
* https://github.com/nlohmann/json

## License
Apache License 2.0

# 한국어
높은 핑에서 글쿨 밀리지 않고 오프글쿨을 두개 쓰세요. 핑이 높다고 딜이 떨어지는 일이 있어서야 되겠습니까? (하이점프 같은 애초에 긴 스킬은 해당사항 없습니다)

## 사용 방법
게임이 이미 켜져 있지 않다면 게임을 켜고, `XivAlexanderLoader.exe`를 관리자 권한으로 실행하세요.

**DLL 인젝션을 이용하므로 안티바이러스 프로그램에서 악성코드로 잡을 수 있습니다. 예외로 추가해 주세요.**

### 실행 인자
사용 방법: XivAlexanderLoader [options] targets

#### 위치 인자
* targets: XivAlexander를 불러들일 프로세스 ID, 또는 문자열 지정 시 해당 문자열로 끝나는 경로를 가진 프로세스에 불러들임

#### 선택적 인자
* -h (--help): 도움말 메시지 출력
* -a (--action): 찾은 프로세스별로 할 작업 선택 (가능한 값: ask, load, unload)
* -q (--quiet): 오류 메시지 출력하지 않음

### 처음 사용할 때 세팅
1. `XivAlexanderLoader.exe`가 있는 폴더에 생긴 `XivAlexander.dll.json`를 텍스트 편집기 (메모장 등)으로 열어 주세요.
2. 옵코드를 적당히 수정해 주세요. 현재 한썹 5.4 HotFix에서는 다음과 같습니다. 
   ```json
   {
        "ActorCast": "0x00eb",
        "ActorControl": "0x01cf",
        "ActorControlSelf": "0x0307",
        "AddStatusEffect": "0x01f2",
        "RequestUseAction": "0x0398",
        "RequestUseAction2": "0x02b0",
        "SkillResultResponse01": "0x00b5",
        "SkillResultResponse08": "0x0235",
        "SkillResultResponse16": "0x0131",
        "SkillResultResponse24": "0x03c0",
        "SkillResultResponse32": "0x0361"
   }
   ```
3. 시스템 알림 영역(트레이)에 있는 아이콘을 오른쪽 클릭한 후 *Reload Configuration*을 누르세요.

### 옵코드 찾는 방법
1. 메뉴에서 *Use IPC Type Finder*를 켜고, 로그 창을 엽니다.
2. 자기 자신에게 케알을 시전해 `RequestUseAction`, `SkillResultResponse01`, `ActorCast`, `ActorControlSelf`(재사용 대기시간 정보 포함)를 알아냅니다.
3. 케알 시전 중 움직여서 취소해서 `ActorControl`을 알아냅니다.
4. 위치 지정 스킬을 이용해서 (축지법 등) `RequestUseAction2`을 알아냅니다.
5. 대상이 안 닿는 광역기를 시전해서 `SkillResultResponse08`을 알아냅니다.
6. 사스타샤를 조율 해제하고 들어가서 9쫄까지 몰아서 광역기를 쓴 후 `SkillResultResponse16`을 알아냅니다.
7. 17쫄까지 몰아서 광역기를 쓴 후 `SkillResultResponse24`을 알아냅니다.
8. 사스타샤를 다시 들어가서 25쫄까지 몰아서 광역기를 쓴 후 `SkillResultResponse32`을 알아냅니다.

### 작동 원리
스킬 사용 후 기본으로 0.5초동안 다른 스킬을 사용할 수 없습니다.
서버에서 스킬 다시 사용할 수 있을 때까지의 시간을 응답하면 그 때부터 다시 지정된 시간 동안 스킬을 사용할 수 없게 됩니다.
응답받은 대기시간을 핑과 상관없이 이용하기 때문에, 그 대기시간에서 서버에서 응답하기까지 걸린 시간을 빼서 이용합니다.

**작동 원리 때문에 부정행위 직전까지 가는 애드온입니다. 핑이 아무리 낮아도 0초 이상이지, 핑이 음수일 수는 없는 것처럼, 여러 수치를 지정한 값보다 낮게 바꾸면 확실한 부정행위입니다. 분명히 경고하였으니, 지정된 값 이하로 쓰지 마십시오.**
