# MIDI Continuous Controller (CC) 值枚举分析

> 来源文件：[AkMidiTypes.h](AkMidiTypes.h) — Audiokinetic Wwise SDK

文件 `AkMidiTypes.h` 中定义了一套完整的 MIDI CC 控制器宏常量，取值范围 0–127，遵循标准 MIDI 1.0 规范。以下按功能分组解析：

---

## 1. 粗调控制器（Coarse, 0–31）— MSB 高字节

| 编号 | 宏名 | 含义 |
|------|------|------|
| 0 | `BANK_SELECT_COARSE` | **音色库选择（粗调）** — 切换乐器音色库 |
| 1 | `MOD_WHEEL_COARSE` | **调制轮（粗调）** — 最常用的实时表现控制器，通常映射到颤音/滤波 |
| 2 | `BREATH_CTRL_COARSE` | **呼吸控制器（粗调）** — 常用于吹管乐器的动态表现 |
| 3 | `CTRL_3_COARSE` | **未定义控制器 #3** |
| 4 | `FOOT_PEDAL_COARSE` | **脚踏板控制器（粗调）** |
| 5 | `PORTAMENTO_COARSE` | **滑音时间（粗调）** — 控制音符间滑音的速度 |
| 6 | `DATA_ENTRY_COARSE` | **数据入口（粗调）** — 配合 RPN/NRPN 使用，传递参数值的 MSB |
| 7 | `VOLUME_COARSE` | **通道音量（粗调）** — 控制当前 MIDI 通道的整体音量 |
| 8 | `BALANCE_COARSE` | **声像平衡（粗调）** — 立体声左右平衡 |
| 9 | `CTRL_9_COARSE` | **未定义控制器 #9** |
| 10 | `PAN_POSITION_COARSE` | **声像位置（粗调）** — 单声道声源的左右定位 |
| 11 | `EXPRESSION_COARSE` | **表情控制器（粗调）** — 动态表现，通常为音量的子控制（0–127） |
| 12 | `EFFECT_CTRL_1_COARSE` | **效果控制 1（粗调）** |
| 13 | `EFFECT_CTRL_2_COARSE` | **效果控制 2（粗调）** |
| 14–15 | `CTRL_14~15_COARSE` | **未定义** |
| 16–19 | `GEN_SLIDER_1~4` | **通用滑块 1–4** — DAW/合成器上的可分配物理滑块 |
| 20–31 | `CTRL_20~31_COARSE` | **未定义粗调控制器** |

## 2. 微调控制器（Fine, 32–63）— LSB 低字节

| 编号 | 宏名 | 含义 |
|------|------|------|
| 32–63 | `*_FINE` 系列 | 与 0–31 号**一一对应的精细调节版本**。与粗调组合使用可实现 14-bit 高精度控制（0–16383 分辨率）。例如 `MOD_WHEEL_COARSE`(1) + `MOD_WHEEL_FINE`(33) 构成完整的调制轮 14-bit 值。 |

## 3. 开关踏板控制器（64–69）

| 编号 | 宏名 | 含义 |
|------|------|------|
| 64 | `HOLD_PEDAL` | **延音踏板（Damper/Sustain）** — 钢琴最右侧踏板，0=关/127=开 |
| 65 | `PORTAMENTO_ON_OFF` | **滑音开关** — 0=关，127=开 |
| 66 | `SUSTENUTO_PEDAL` | **持音踏板（Sostenuto）** — 仅保持按下瞬间正在发声的音符 |
| 67 | `SOFT_PEDAL` | **柔音踏板（Una Corda）** — 钢琴最左侧踏板，降低音量并柔化音色 |
| 68 | `LEGATO_PEDAL` | **连奏踏板** — 强制连奏模式 |
| 69 | `HOLD_PEDAL_2` | **延音踏板 2** — 第二延音控制器 |

## 4. 音色控制（70–79）

| 编号 | 宏名 | 含义 |
|------|------|------|
| 70 | `SOUND_VARIATION` | **音色变体** — 切换同一音色的不同变体版本 |
| 71 | `SOUND_TIMBRE` | **音色/谐波含量** — 控制滤波截止频率（类似合成器的 Filter Cutoff） |
| 72 | `SOUND_RELEASE_TIME` | **释键时间** — 音符释放后声音衰减的时长 |
| 73 | `SOUND_ATTACK_TIME` | **击键时间** — 音符起始的起音速度 |
| 74 | `SOUND_BRIGHTNESS` | **亮度** — 控制滤波/高频含量 |
| 75–79 | `SOUND_CTRL_6~10` | 音色控制 6–10（部分设备自定义映射） |

## 5. 通用按钮 / 效果深度（80–83, 91–97）

| 编号 | 宏名 | 含义 |
|------|------|------|
| 80–83 | `GENERAL_BUTTON_1~4` | **通用按钮 1–4** — 可分配的开关/触发按钮 |
| 91 | `REVERB_LEVEL` | **混响效果电平** |
| 92 | `TREMOLO_LEVEL` | **震音效果电平** |
| 93 | `CHORUS_LEVEL` | **合唱效果电平** |
| 94 | `CELESTE_LEVEL` | **钢片琴/移调效果电平** |
| 95 | `PHASER_LEVEL` | **移相效果电平** |
| 96 | `DATA_BUTTON_P1` | **数据增量（+1）按钮** |
| 97 | `DATA_BUTTON_M1` | **数据减量（-1）按钮** |

## 6. 未注册参数 / 系统命令（98–101, 120–127）

| 编号 | 宏名 | 含义 |
|------|------|------|
| 98 | `NON_REGISTER_COARSE` | **NRPN 参数号 LSB** — 配合 6/38/99/100 组成完整的 NRPN 控制序列 |
| 99 | `NON_REGISTER_FINE` | **NRPN 参数号 MSB** |
| 120 | `ALL_SOUND_OFF` | **所有声音关闭** — 立即静音所有正在发声的音符（不发送 Note Off） |
| 121 | `ALL_CONTROLLERS_OFF` | **重置所有控制器** — 将所有 CC 值重置为默认 |
| 122 | `LOCAL_KEYBOARD` | **本地键盘开关** — 断开/接通键盘与内部音源 |
| 123 | `ALL_NOTES_OFF` | **所有音符关闭** — 发送 Note Off 给所有活跃音符 |
| 124 | `OMNI_MODE_OFF` | **Omni 模式关** |
| 125 | `OMNI_MODE_ON` | **Omni 模式开** — 接收所有通道的 MIDI 消息 |
| 126 | `OMNI_MONOPHONIC_ON` | **单音模式（Mono）** |
| 127 | `OMNI_POLYPHONIC_ON` | **复音模式（Poly）** |

---

## 在 Wwise 中的应用

该文件还定义了对应的结构体 `AkMIDICC`：

```c
struct AkMIDICC {
    AkUInt8 byCc;    // 存储上述 CC 编号
    AkUInt8 byValue; // 对应的 0–127 值
};
```

Wwise 通过 `AK_MIDI_EVENT_TYPE_CONTROLLER`（`0xb0`）事件类型承载这些 CC 消息，实现游戏音频与 MIDI 控制器的实时交互——例如：

- 用 `AK_MIDI_CC_VOLUME_COARSE`(7) 实时调节通道音量
- 用 `AK_MIDI_CC_MOD_WHEEL_COARSE`(1) 驱动调制参数
- 用 `AK_MIDI_CC_EXPRESSION_COARSE`(11) 控制动态表情
- 用 `AK_MIDI_CC_HOLD_PEDAL`(64) 实现延音效果

---

## 完整枚举清单（0–127）

```
  0  AK_MIDI_CC_BANK_SELECT_COARSE       音色库选择（粗调）
  1  AK_MIDI_CC_MOD_WHEEL_COARSE         调制轮（粗调）
  2  AK_MIDI_CC_BREATH_CTRL_COARSE       呼吸控制器（粗调）
  3  AK_MIDI_CC_CTRL_3_COARSE            未定义 #3
  4  AK_MIDI_CC_FOOT_PEDAL_COARSE        脚踏板（粗调）
  5  AK_MIDI_CC_PORTAMENTO_COARSE        滑音时间（粗调）
  6  AK_MIDI_CC_DATA_ENTRY_COARSE        数据入口（粗调）
  7  AK_MIDI_CC_VOLUME_COARSE            通道音量（粗调）
  8  AK_MIDI_CC_BALANCE_COARSE           平衡（粗调）
  9  AK_MIDI_CC_CTRL_9_COARSE            未定义 #9
 10  AK_MIDI_CC_PAN_POSITION_COARSE      声像（粗调）
 11  AK_MIDI_CC_EXPRESSION_COARSE        表情（粗调）
 12  AK_MIDI_CC_EFFECT_CTRL_1_COARSE     效果1（粗调）
 13  AK_MIDI_CC_EFFECT_CTRL_2_COARSE     效果2（粗调）
 14  AK_MIDI_CC_CTRL_14_COARSE           未定义 #14
 15  AK_MIDI_CC_CTRL_15_COARSE           未定义 #15
 16  AK_MIDI_CC_GEN_SLIDER_1             通用滑块1
 17  AK_MIDI_CC_GEN_SLIDER_2             通用滑块2
 18  AK_MIDI_CC_GEN_SLIDER_3             通用滑块3
 19  AK_MIDI_CC_GEN_SLIDER_4             通用滑块4
 20  AK_MIDI_CC_CTRL_20_COARSE           未定义 #20
 21  AK_MIDI_CC_CTRL_21_COARSE           未定义 #21
 22  AK_MIDI_CC_CTRL_22_COARSE           未定义 #22
 23  AK_MIDI_CC_CTRL_23_COARSE           未定义 #23
 24  AK_MIDI_CC_CTRL_24_COARSE           未定义 #24
 25  AK_MIDI_CC_CTRL_25_COARSE           未定义 #25
 26  AK_MIDI_CC_CTRL_26_COARSE           未定义 #26
 27  AK_MIDI_CC_CTRL_27_COARSE           未定义 #27
 28  AK_MIDI_CC_CTRL_28_COARSE           未定义 #28
 29  AK_MIDI_CC_CTRL_29_COARSE           未定义 #29
 30  AK_MIDI_CC_CTRL_30_COARSE           未定义 #30
 31  AK_MIDI_CC_CTRL_31_COARSE           未定义 #31
 32  AK_MIDI_CC_BANK_SELECT_FINE         音色库选择（微调）
 33  AK_MIDI_CC_MOD_WHEEL_FINE           调制轮（微调）
 34  AK_MIDI_CC_BREATH_CTRL_FINE         呼吸控制器（微调）
 35  AK_MIDI_CC_CTRL_3_FINE              未定义 #3（微调）
 36  AK_MIDI_CC_FOOT_PEDAL_FINE          脚踏板（微调）
 37  AK_MIDI_CC_PORTAMENTO_FINE          滑音时间（微调）
 38  AK_MIDI_CC_DATA_ENTRY_FINE          数据入口（微调）
 39  AK_MIDI_CC_VOLUME_FINE              通道音量（微调）
 40  AK_MIDI_CC_BALANCE_FINE             平衡（微调）
 41  AK_MIDI_CC_CTRL_9_FINE              未定义 #9（微调）
 42  AK_MIDI_CC_PAN_POSITION_FINE        声像（微调）
 43  AK_MIDI_CC_EXPRESSION_FINE          表情（微调）
 44  AK_MIDI_CC_EFFECT_CTRL_1_FINE       效果1（微调）
 45  AK_MIDI_CC_EFFECT_CTRL_2_FINE       效果2（微调）
 46  AK_MIDI_CC_CTRL_14_FINE             未定义 #14（微调）
 47  AK_MIDI_CC_CTRL_15_FINE             未定义 #15（微调）
 52  AK_MIDI_CC_CTRL_20_FINE             未定义 #20（微调）
 53  AK_MIDI_CC_CTRL_21_FINE             未定义 #21（微调）
 54  AK_MIDI_CC_CTRL_22_FINE             未定义 #22（微调）
 55  AK_MIDI_CC_CTRL_23_FINE             未定义 #23（微调）
 56  AK_MIDI_CC_CTRL_24_FINE             未定义 #24（微调）
 57  AK_MIDI_CC_CTRL_25_FINE             未定义 #25（微调）
 58  AK_MIDI_CC_CTRL_26_FINE             未定义 #26（微调）
 59  AK_MIDI_CC_CTRL_27_FINE             未定义 #27（微调）
 60  AK_MIDI_CC_CTRL_28_FINE             未定义 #28（微调）
 61  AK_MIDI_CC_CTRL_29_FINE             未定义 #29（微调）
 62  AK_MIDI_CC_CTRL_30_FINE             未定义 #30（微调）
 63  AK_MIDI_CC_CTRL_31_FINE             未定义 #31（微调）
 64  AK_MIDI_CC_HOLD_PEDAL               延音踏板
 65  AK_MIDI_CC_PORTAMENTO_ON_OFF        滑音开关
 66  AK_MIDI_CC_SUSTENUTO_PEDAL          持音踏板
 67  AK_MIDI_CC_SOFT_PEDAL               柔音踏板
 68  AK_MIDI_CC_LEGATO_PEDAL             连奏踏板
 69  AK_MIDI_CC_HOLD_PEDAL_2             延音踏板2
 70  AK_MIDI_CC_SOUND_VARIATION          音色变体
 71  AK_MIDI_CC_SOUND_TIMBRE             音色/谐波含量
 72  AK_MIDI_CC_SOUND_RELEASE_TIME       释键时间
 73  AK_MIDI_CC_SOUND_ATTACK_TIME        击键时间
 74  AK_MIDI_CC_SOUND_BRIGHTNESS         亮度
 75  AK_MIDI_CC_SOUND_CTRL_6             音色控制6
 76  AK_MIDI_CC_SOUND_CTRL_7             音色控制7
 77  AK_MIDI_CC_SOUND_CTRL_8             音色控制8
 78  AK_MIDI_CC_SOUND_CTRL_9             音色控制9
 79  AK_MIDI_CC_SOUND_CTRL_10            音色控制10
 80  AK_MIDI_CC_GENERAL_BUTTON_1         通用按钮1
 81  AK_MIDI_CC_GENERAL_BUTTON_2         通用按钮2
 82  AK_MIDI_CC_GENERAL_BUTTON_3         通用按钮3
 83  AK_MIDI_CC_GENERAL_BUTTON_4         通用按钮4
 91  AK_MIDI_CC_REVERB_LEVEL             混响电平
 92  AK_MIDI_CC_TREMOLO_LEVEL            震音电平
 93  AK_MIDI_CC_CHORUS_LEVEL             合唱电平
 94  AK_MIDI_CC_CELESTE_LEVEL            钢片琴/移调电平
 95  AK_MIDI_CC_PHASER_LEVEL             移相电平
 96  AK_MIDI_CC_DATA_BUTTON_P1           数据+1
 97  AK_MIDI_CC_DATA_BUTTON_M1           数据-1
 98  AK_MIDI_CC_NON_REGISTER_COARSE      NRPN LSB
 99  AK_MIDI_CC_NON_REGISTER_FINE        NRPN MSB
120  AK_MIDI_CC_ALL_SOUND_OFF            所有声音关闭
121  AK_MIDI_CC_ALL_CONTROLLERS_OFF      重置所有控制器
122  AK_MIDI_CC_LOCAL_KEYBOARD           本地键盘开关
123  AK_MIDI_CC_ALL_NOTES_OFF            所有音符关闭
124  AK_MIDI_CC_OMNI_MODE_OFF            Omni关
125  AK_MIDI_CC_OMNI_MODE_ON             Omni开
126  AK_MIDI_CC_OMNI_MONOPHONIC_ON       单音模式
127  AK_MIDI_CC_OMNI_POLYPHONIC_ON       复音模式
```
