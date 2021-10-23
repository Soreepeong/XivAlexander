import ctypes
import dataclasses
import datetime
import os.path
import pickle
import re
import struct
import typing

import sqexdata


@dataclasses.dataclass
class ActorStatusEffect:
    effect_id: int = 0
    param: int = 0
    expiry: float = 0.
    source_actor_id: int = 0

    def get_effect_name(self):
        global status
        try:
            return status[self.effect_id][0]
        except KeyError:
            return f"Unknown({self.effect_id})"

    def get_effect_description(self):
        global status
        try:
            return status[self.effect_id][1]
        except KeyError:
            return f"Unknown({self.effect_id})"

    def get_action_description_of_same_name_with_effect(self):
        global actiontransient, action
        for row_id in action.keys():
            if action[row_id] == self.get_effect_name():
                return actiontransient[row_id]
        return "?"


@dataclasses.dataclass
class Actor:
    id: int
    hp: typing.Optional[int] = None
    max_hp: typing.Optional[int] = None
    mp: typing.Optional[int] = None
    max_mp: typing.Optional[int] = None
    owner_id: typing.Optional[int] = None
    name: typing.Optional[str] = None
    bnpcname_id: typing.Optional[int] = None
    job: typing.Optional[int] = None
    level: typing.Optional[int] = None
    synced_level: typing.Optional[int] = None
    shield_ratio: typing.Optional[float] = None
    status_effects: typing.List[ActorStatusEffect] = dataclasses.field(default_factory=list)


class XivMessage(ctypes.LittleEndianStructure):
    SEGMENT_TYPE_IPC = 3

    _fields_ = (
        ("length", ctypes.c_uint32),
        ("source_actor", ctypes.c_uint32),
        ("current_actor", ctypes.c_uint32),
        ("segment_type", ctypes.c_uint16),
        ("unknown", ctypes.c_uint16),
    )


class XivStatusEffect(ctypes.LittleEndianStructure):
    _fields_ = (
        ("effect_id", ctypes.c_uint16),
        ("param", ctypes.c_uint16),
        ("duration", ctypes.c_float),
        ("source_actor_id", ctypes.c_uint32),
    )
    effect_id: int
    param: int
    duration: float
    source_actor_id: int


class XivActionEffect(ctypes.LittleEndianStructure):
    _fields_ = (
        ("effect_type", ctypes.c_uint8),
        ("param0", ctypes.c_uint8),
        ("param1", ctypes.c_uint8),
        ("param2", ctypes.c_uint8),
        ("extended_value_highest_byte", ctypes.c_uint8),
        ("flags", ctypes.c_uint8),
        ("value", ctypes.c_uint16),
    )
    effect_type: int
    param0: int
    param1: int
    param2: int
    extended_value_highest_byte: int
    flags: int
    value: int


class XivPositionVector(ctypes.LittleEndianStructure):
    _fields_ = (
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
    )
    x: float
    y: float
    z: float


class AlmostStructureBase:
    _data: bytearray
    _offset: int

    @classmethod
    def from_buffer(cls, data: typing.Union[bytearray], offset: int = 0):
        self = cls()
        self._data = data
        self._offset = offset
        return self

    def _uint32_at(self, offset: int) -> int:
        begin_offset = self._offset + offset
        return struct.unpack("<I", self._data[begin_offset:begin_offset + 4])[0]

    def _uint16_at(self, offset: int) -> int:
        begin_offset = self._offset + offset
        return struct.unpack("<H", self._data[begin_offset:begin_offset + 2])[0]

    def _float_at(self, offset: int) -> float:
        begin_offset = self._offset + offset
        return struct.unpack("<f", self._data[begin_offset:begin_offset + 4])[0]


class XivIpcOnPlayerSpawn(AlmostStructureBase):
    @property
    def name(self) -> str:
        begin_offset = self._offset + 0x0230
        return self._data[begin_offset:self._data.find(0, begin_offset)].decode("utf-8")

    @property
    def owner_id(self) -> int:
        return self._uint32_at(0x0054)

    @property
    def max_hp(self) -> int:
        return self._uint32_at(0x005c)

    @property
    def hp(self) -> int:
        return self._uint32_at(0x0060)

    @property
    def max_mp(self) -> int:
        return self._uint16_at(0x006a)

    @property
    def mp(self) -> int:
        return self._uint16_at(0x006c)

    @property
    def bnpcname_id(self) -> int:
        return self._uint32_at(0x0044)

    @property
    def job(self) -> int:
        return self._data[self._offset + 0x0082]

    @property
    def level(self) -> int:
        return self._data[self._offset + 0x0081]

    @property
    def status_effects(self) -> typing.List[XivStatusEffect]:
        offset = self._offset + 0x0094
        return [XivStatusEffect.from_buffer(self._data, offset + i * ctypes.sizeof(XivStatusEffect))
                for i in range(30)]

    @property
    def position_vector(self) -> XivPositionVector:
        return XivPositionVector.from_buffer(self._offset + 0x01fc)


class XivIpcModelEquip(AlmostStructureBase):
    @property
    def job(self) -> int:
        return self._data[self._offset + 0x0011]

    @property
    def level(self) -> int:
        return self._data[self._offset + 0x0012]


class XivIpcActionEffect(AlmostStructureBase):
    @property
    def action_id(self) -> int:
        return self._uint32_at(0x0008)

    @property
    def global_sequence_id(self):
        return self._uint32_at(0x000c)

    @property
    def animation_lock_duration(self) -> float:
        return self._float_at(0x0010)

    @property
    def effect_count(self) -> int:
        return self._data[self._offset + 0x0021]

    @property
    def action_effects(self) -> typing.List[typing.List[XivActionEffect]]:
        offset = self._offset + 0x002a
        if self.effect_count == 1:
            res = [XivActionEffect.from_buffer(self._data, offset + ctypes.sizeof(XivActionEffect) * i)
                   for i in range(8 * 1)]
        elif self.effect_count <= 8:
            res = [XivActionEffect.from_buffer(self._data, offset + ctypes.sizeof(XivActionEffect) * i)
                   for i in range(8 * 8)]
        elif self.effect_count <= 16:
            res = [XivActionEffect.from_buffer(self._data, offset + ctypes.sizeof(XivActionEffect) * i)
                   for i in range(8 * 16)]
        elif self.effect_count <= 24:
            res = [XivActionEffect.from_buffer(self._data, offset + ctypes.sizeof(XivActionEffect) * i)
                   for i in range(8 * 24)]
        elif self.effect_count <= 32:
            res = [XivActionEffect.from_buffer(self._data, offset + ctypes.sizeof(XivActionEffect) * i)
                   for i in range(8 * 32)]
        else:
            raise RuntimeError(f"effect_count above {self.effect_count} is unsupported")
        return [res[i:i + 8] for i in range(0, len(res), 8)]

    @property
    def targets(self) -> typing.List[int]:
        if self.effect_count == 1:
            offset = 0x002a + ctypes.sizeof(XivActionEffect) * 8 * 1 + 6
            return [self._uint32_at(offset)]
        elif self.effect_count <= 8:
            offset = 0x002a + ctypes.sizeof(XivActionEffect) * 8 * 8 + 6
            return [self._uint32_at(offset + 2 * 4 * i)
                    for i in range(8)]
        elif self.effect_count <= 16:
            offset = 0x002a + ctypes.sizeof(XivActionEffect) * 8 * 16 + 6
            return [self._uint32_at(offset + 2 * 4 * i)
                    for i in range(16)]
        elif self.effect_count <= 24:
            offset = 0x002a + ctypes.sizeof(XivActionEffect) * 8 * 24 + 6
            return [self._uint32_at(offset + 2 * 4 * i)
                    for i in range(24)]
        elif self.effect_count <= 32:
            offset = 0x002a + ctypes.sizeof(XivActionEffect) * 8 * 32 + 6
            return [self._uint32_at(offset + 2 * 4 * i)
                    for i in range(32)]
        else:
            raise RuntimeError(f"effect_count above {self.effect_count} is unsupported")


class XivIpcUpdateHpMpTp(AlmostStructureBase):
    @property
    def hp(self):
        return self._uint32_at(0x0000)

    @property
    def mp(self):
        return self._uint16_at(0x0004)


class XivIpcActorStats(AlmostStructureBase):
    @property
    def max_hp(self) -> int:
        return self._uint32_at(0x18)

    @property
    def max_mp(self) -> int:
        return self._uint32_at(0x1c)

    @property
    def attack_power(self) -> int:
        return self._uint32_at(0x34)

    @property
    def critical_hit(self) -> int:
        return self._uint32_at(0x48)

    @property
    def magic_attack_potency(self) -> int:
        return self._uint32_at(0x4c)

    @property
    def magic_heal_potency(self) -> int:
        return self._uint32_at(0x50)

    @property
    def skill_speed(self) -> int:
        return self._uint32_at(0x5c)

    @property
    def spell_speed(self) -> int:
        return self._uint32_at(0x60)


class XivIpcActorControl(AlmostStructureBase):
    CATEGORY_JOB_CHANGE = 0x0005
    CATEGORY_DEATH = 0x0006
    CATEGORY_CANCEL_CAST = 0x000f
    CATEGORY_EFFECT_OVER_TIME = 0x0017

    @property
    def category(self) -> int:
        return self._uint16_at(0x00)

    @property
    def padding1(self) -> int:  # not actually a padding
        return self._uint16_at(0x02)

    @property
    def param1(self) -> int:
        return self._uint32_at(0x04)

    @property
    def param2(self) -> int:
        return self._uint32_at(0x08)

    @property
    def param3(self) -> int:
        return self._uint32_at(0x0c)

    @property
    def param4(self) -> int:
        return self._uint32_at(0x10)

    @property
    def padding2(self) -> int:
        return self._uint32_at(0x14)


class XivStatusEffectEntryModificationInfo(ctypes.LittleEndianStructure):
    _fields_ = (
        ("index", ctypes.c_uint8),
        ("unknown_3", ctypes.c_uint8),  # "unknown1"
        ("effect_id", ctypes.c_uint16),
        ("param", ctypes.c_uint16),  # "unknown2"
        ("unknown_4", ctypes.c_uint16),  # "unknown3"
        ("duration", ctypes.c_float),
        ("source_actor_id", ctypes.c_uint32)
    )
    index: int
    effect_id: int
    param: int
    duration: float
    source_actor_id: int


class XivIpcActionEffectResult(AlmostStructureBase):
    @property
    def global_sequence_id(self):
        return self._uint32_at(0x00)

    @property
    def actor_id(self):  # don't use?
        return self._uint32_at(0x04)

    @property
    def hp(self):
        return self._uint32_at(0x08)

    @property
    def max_hp(self):
        return self._uint32_at(0x0c)

    @property
    def mp(self):
        return self._uint16_at(0x10)

    @property
    def job(self):
        return self._data[self._offset + 0x13]

    @property
    def shield_percentage(self):
        return self._data[self._offset + 0x14]

    @property
    def entry_count(self):
        return self._data[self._offset + 0x15]

    @property
    def entries(self) -> typing.List[XivStatusEffectEntryModificationInfo]:
        offset = self._offset + 0x18
        return [
            XivStatusEffectEntryModificationInfo.from_buffer(
                self._data, offset + ctypes.sizeof(XivStatusEffectEntryModificationInfo) * i
            ) for i in range(self.entry_count)]


class XivIpcStatusEffectList(AlmostStructureBase):
    _info_offset: int
    _has_more_effect_list: bool

    @classmethod
    def from_buffer(cls, data: typing.Union[bytearray], offset: int = 0,
                    is_type_2: bool = False, is_boss: bool = False):
        self = cls()
        self._data = data
        self._offset = offset
        self._info_offset = 4 if is_type_2 else (30 * ctypes.sizeof(XivStatusEffect) if is_boss else 0)
        self._has_more_effect_list = is_boss
        return self

    @property
    def job(self):
        return self._data[self._offset + self._info_offset + 0x0000]

    @property
    def effective_level(self):
        return self._data[self._offset + self._info_offset + 0x0001]

    @property
    def level(self):
        return self._data[self._offset + self._info_offset + 0x0002]

    @property
    def synced_level(self):
        return self._data[self._offset + self._info_offset + 0x0003]

    @property
    def hp(self):
        return self._uint32_at(self._info_offset + 0x0004)

    @property
    def max_hp(self):
        return self._uint32_at(self._info_offset + 0x0008)

    @property
    def mp(self):
        return self._uint16_at(self._info_offset + 0x000c)

    @property
    def max_mp(self):
        return self._uint16_at(self._info_offset + 0x000e)

    @property
    def shield_percentage(self):
        return self._data[self._offset + self._info_offset + 0x0010]

    @property
    def status_effects(self) -> typing.List[XivStatusEffect]:
        offset = self._offset + self._info_offset + 0x0014
        result = [XivStatusEffect.from_buffer(self._data, offset + i * ctypes.sizeof(XivStatusEffect))
                  for i in range(30)]
        if self._has_more_effect_list:
            offset = self._offset
            result += [XivStatusEffect.from_buffer(self._data, offset + i * ctypes.sizeof(XivStatusEffect))
                       for i in range(30)]
        return result


class XivIpc(ctypes.LittleEndianStructure):
    IPC_TYPE1_INTERESTED = 0x0014
    IPC_TYPE2_ACTION_EFFECTS = [0x03ca, 0x03c4, 0x00fa, 0x0339, 0x023c]
    IPC_TYPE2_ACTION_EFFECT_RESULT = 0x0387
    IPC_TYPE2_ACTOR_CONTROL = 0x00b0
    IPC_TYPE2_ACTOR_CONTROL_SELF = 0x02b6
    IPC_TYPE2_ACTOR_CONTROL_TARGET = 0x03c5
    IPC_TYPE2_UPDATE_HPMPTP = 0x01a7
    IPC_TYPE2_INITIALIZE_CURRENT_PLAYER = 0x01d5
    IPC_TYPE2_PLAYER_SPAWN = 0x01d8
    IPC_TYPE2_PET_SPAWN = 0x00d2
    IPC_TYPE2_MODEL_EQUIP = 0x03a2
    IPC_TYPE2_ACTOR_STATS = 0x0295
    IPC_TYPE2_STATUS_EFFECT_LIST = 0x0074
    IPC_TYPE2_STATUS_EFFECT_LIST_2 = 0x02aa
    IPC_TYPE2_STATUS_EFFECT_LIST_BOSS = 0x0223

    _fields_ = (
        ("type1", ctypes.c_uint16),
        ("type2", ctypes.c_uint16),
        ("unknown_1", ctypes.c_uint16),
        ("server_id", ctypes.c_uint16),
        ("epoch", ctypes.c_uint32),
        ("unknown_2", ctypes.c_uint32),
    )


class DpsParser:
    _message: typing.Optional[XivMessage] = None
    _timestamp: typing.Optional[datetime.datetime] = None
    _source_actor: typing.Optional[Actor] = None

    def __init__(self):
        self._actors: typing.Dict[int, Actor] = {}
        self._unrealized_action_effects: typing.Dict[int, typing.Tuple[
            Actor, XivIpcActionEffect, typing.Dict[int, typing.List[XivActionEffect]]
        ]] = {}
        self._over_time_potency: typing.Dict[int, int] = {}

    def get_actor(self, actor_id: int):
        if actor_id not in self._actors:
            self._actors[actor_id] = Actor(actor_id)
        return self._actors[actor_id]

    def feed(self, timestamp: datetime.datetime, is_recv: bool, data: bytearray):
        self._timestamp = timestamp
        self._message = XivMessage.from_buffer(data, 0)
        self._source_actor = self.get_actor(self._message.source_actor)
        offset = ctypes.sizeof(self._message)
        if self._message.segment_type != XivMessage.SEGMENT_TYPE_IPC:
            return

        ipc = XivIpc.from_buffer(data, ctypes.sizeof(self._message))
        offset += ctypes.sizeof(ipc)
        if is_recv:
            if ipc.type2 in (XivIpc.IPC_TYPE2_PLAYER_SPAWN, XivIpc.IPC_TYPE2_PET_SPAWN):
                self._on_spawn(XivIpcOnPlayerSpawn.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_MODEL_EQUIP:
                self._on_model_equip(XivIpcModelEquip.from_buffer(data, offset))
            elif ipc.type2 in XivIpc.IPC_TYPE2_ACTION_EFFECTS:
                self._on_action_effects(XivIpcActionEffect.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_UPDATE_HPMPTP:
                self._on_update_hpmptp(XivIpcUpdateHpMpTp.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_ACTOR_STATS:
                self._on_actor_stats(XivIpcActorStats.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_ACTOR_CONTROL:
                self._on_actor_control(XivIpcActorControl.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_ACTION_EFFECT_RESULT:
                self._on_action_effect_result(XivIpcActionEffectResult.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_STATUS_EFFECT_LIST:
                self._on_status_effect_list(XivIpcStatusEffectList.from_buffer(data, offset))
            elif ipc.type2 == XivIpc.IPC_TYPE2_STATUS_EFFECT_LIST_2:
                self._on_status_effect_list(XivIpcStatusEffectList.from_buffer(data, offset, is_type_2=True))
            elif ipc.type2 == XivIpc.IPC_TYPE2_STATUS_EFFECT_LIST_BOSS:
                self._on_status_effect_list(XivIpcStatusEffectList.from_buffer(data, offset, is_boss=True))

    def _register_amount(self, source: Actor, target: Actor, amount: int, is_over_time: bool, action_or_buff_id: int,
                         is_crit: bool, is_dh: bool):
        global action, status
        name = f"{status[action_or_buff_id][0]}(*)" if is_over_time else action[action_or_buff_id]
        print(f"{source.name} -> {target.name} (tick={is_over_time} action={action_or_buff_id}): "
              f"{name} "
              f"{amount:+}{'!' if is_crit else ''}{'!!' if is_dh else ''} => {target.hp / target.max_hp * 100:.02f}%")

    def _on_update_hpmptp(self, data: XivIpcUpdateHpMpTp):
        self._source_actor.hp = data.hp
        self._source_actor.mp = data.mp

    def _on_spawn(self, data: XivIpcOnPlayerSpawn):
        self._source_actor.name = data.name
        self._source_actor.owner_id = data.owner_id
        self._source_actor.bnpcname_id = data.bnpcname_id
        self._source_actor.level = data.level
        self._source_actor.job = data.job
        self._source_actor.max_hp = data.max_hp
        self._source_actor.max_mp = data.max_mp
        self._source_actor.hp = data.hp
        self._source_actor.mp = data.mp
        for i, effect in enumerate(data.status_effects):
            while len(self._source_actor.status_effects) <= i:
                self._source_actor.status_effects.append(ActorStatusEffect())
            t = self._source_actor.status_effects[i]
            t.effect_id = effect.effect_id
            t.param = effect.param
            t.expiry = self._timestamp + datetime.timedelta(seconds=effect.duration) if effect.duration > 0 else 0.
            t.source_actor_id = effect.source_actor_id

    def _on_model_equip(self, model: XivIpcModelEquip):
        self._source_actor.job = model.job
        self._source_actor.level = model.level

    def _on_action_effects(self, data: XivIpcActionEffect):
        effects = dict(zip(data.targets, data.action_effects))
        effects = {k: v for k, v in effects.items() if any(v2.effect_type for v2 in v)}
        if not effects:
            return
        self._unrealized_action_effects[data.global_sequence_id] = (
            self._source_actor, data, effects
        )

    def _on_actor_stats(self, data: XivIpcActorStats):
        self._source_actor.max_hp = data.max_hp
        self._source_actor.max_mp = data.max_mp

    def _on_actor_control(self, data: XivIpcActorControl):
        if data.category == XivIpcActorControl.CATEGORY_JOB_CHANGE:
            self._source_actor.job = data.param1
        elif data.category == XivIpcActorControl.CATEGORY_DEATH:
            source = self.get_actor(data.param2)
            target = self._source_actor
            print(f"{source.name} -> {target.name}: Killing")
            pass
        elif data.category == XivIpcActorControl.CATEGORY_CANCEL_CAST:
            pass
        elif data.category == XivIpcActorControl.CATEGORY_EFFECT_OVER_TIME:
            buff_id = data.param1  # can be zero
            effect_type = data.param2  # 3 = damage, 4 = heal
            amount = data.param3
            source_actor_id = data.param4
            if effect_type == 3:  # damage
                pass
            elif effect_type == 4:  # heal
                pass
            else:
                return

            self._source_actor.hp = max(0, min(self._source_actor.max_hp,
                                               self._source_actor.hp + amount * (-1 if effect_type == 3 else 1)))
            if buff_id:
                self._register_amount(
                    source=self.get_actor(source_actor_id),
                    target=self._source_actor,
                    amount=amount * (-1 if effect_type == 3 else 1),
                    is_over_time=True,
                    action_or_buff_id=buff_id,
                    is_crit=False,
                    is_dh=False,
                )
            else:
                potency_per_actor = {}
                for effect in self._source_actor.status_effects:
                    if not effect.effect_id:
                        continue
                    potency = self._over_time_potency.get(effect.effect_id, None)
                    if potency is None:
                        effect_description = effect.get_effect_description()
                        action_description = (effect.get_action_description_of_same_name_with_effect()
                                              .replace("\r", " ").replace("\x02", ""))
                        test = effect_description.lower()
                        try:
                            if "damage over time" in test:
                                potency = self._over_time_potency[effect.effect_id] = -int(
                                    re.search("Potency: ([0-9]+)", action_description).group(1))
                            elif "regenerating hp over time" in test:
                                potency = self._over_time_potency[effect.effect_id] = int(
                                    re.search("Potency: ([0-9]+)", action_description).group(1))
                            else:
                                potency = self._over_time_potency[effect.effect_id] = 0
                        except AttributeError:
                            potency = self._over_time_potency[effect.effect_id] = 0
                    if potency == 0:
                        continue
                    if effect_type == 3 and potency > 0:
                        continue
                    if effect_type == 4 and potency < 0:
                        continue

                    if effect.source_actor_id not in potency_per_actor:
                        potency_per_actor[effect.source_actor_id, effect.effect_id] = abs(potency)
                    else:
                        potency_per_actor[effect.source_actor_id, effect.effect_id] += abs(potency)

                if not potency_per_actor:
                    potency_per_actor = {(0xE0000000, 0): 1}
                maxval = sum(potency_per_actor.values())
                sumval = 0
                for i, ((actor_id, effect_id), val) in enumerate(potency_per_actor.items()):
                    self._register_amount(
                        source=self.get_actor(actor_id),
                        target=self._source_actor,
                        amount=(
                                (int(amount * val / maxval) if i < len(potency_per_actor) - 1 else amount - sumval)
                                * (-1 if effect_type == 3 else 1)),
                        is_over_time=True,
                        action_or_buff_id=effect_id,
                        is_crit=False,
                        is_dh=False,
                    )
                    sumval += int(amount * val / maxval)

    def _on_action_effect_result(self, data: XivIpcActionEffectResult):
        effect_target_actor = self._source_actor
        # effect_target_actor = self.get_actor(data.actor_id)
        effect_target_actor.hp = data.hp
        effect_target_actor.max_hp = data.max_hp
        effect_target_actor.mp = data.mp
        effect_target_actor.shield_ratio = data.shield_percentage / 100.
        for effect in data.entries:
            while len(effect_target_actor.status_effects) <= effect.index:
                effect_target_actor.status_effects.append(ActorStatusEffect())
            t = effect_target_actor.status_effects[effect.index]
            t.effect_id = effect.effect_id
            t.param = effect.param
            t.expiry = self._timestamp + datetime.timedelta(seconds=effect.duration) if effect.duration > 0 else 0.
            t.source_actor_id = effect.source_actor_id
            source_actor = self.get_actor(effect.source_actor_id)

        try:
            effect_source_actor, effect_summary, targets = self._unrealized_action_effects[data.global_sequence_id]
        except KeyError:
            # log maybe?
            return

        try:
            targeted_effects = targets.pop(effect_target_actor.id)
        except KeyError:
            return
        finally:
            if not targets:
                self._unrealized_action_effects.pop(data.global_sequence_id, None)

        for effect in targeted_effects:
            absorbed = bool(effect.flags & 0x04)
            extended = bool(effect.flags & 0x40)
            effect_on_source = bool(effect.flags & 0x80)
            reflected = bool(effect.flags & 0xa0)

            effect_target_actor = effect_source_actor if effect_on_source else self._source_actor

            value = effect.value
            if extended:
                value = effect.extended_value_highest_byte << 16 | value

            if effect.effect_type == 3:  # damage
                crit = bool(effect.param0 & 1)
                dh = bool(effect.param0 & 2)

            elif effect.effect_type == 4:  # heal
                crit = bool(effect.param1 & 1)
                dh = False

            else:
                continue

            self._register_amount(
                source=effect_source_actor,
                target=effect_target_actor,
                amount=value * (-1 if effect.effect_type == 3 else 1),
                is_over_time=False,
                is_crit=crit,
                is_dh=dh,
                action_or_buff_id=effect_summary.action_id,
            )

    def _on_status_effect_list(self, data: XivIpcStatusEffectList):
        self._source_actor.hp = data.hp
        self._source_actor.max_hp = data.max_hp
        self._source_actor.mp = data.mp
        self._source_actor.max_mp = data.max_mp
        self._source_actor.shield_ratio = data.shield_percentage / 100.
        self._source_actor.job = data.job
        self._source_actor.level = data.level
        self._source_actor.synced_level = data.synced_level
        for i, effect in enumerate(data.status_effects):
            while len(self._source_actor.status_effects) <= i:
                self._source_actor.status_effects.append(ActorStatusEffect())
            t = self._source_actor.status_effects[i]
            t.effect_id = effect.effect_id
            t.param = effect.param
            t.expiry = self._timestamp + datetime.timedelta(seconds=effect.duration) if effect.duration > 0 else 0.
            t.source_actor_id = effect.source_actor_id


def main():
    global status, action, actiontransient
    cached_file = "parsedump_cached.tmp"
    if os.path.exists(cached_file):
        try:
            with open(cached_file, "rb") as fp:
                status, action, actiontransient = pickle.load(fp)
        except pickle.PickleError:
            os.unlink(cached_file)
    if not os.path.exists(cached_file):
        with sqexdata.SqpackReader(
                r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv", "0a0000"
        ) as reader:
            status = sqexdata.ExhHeader.read_data_from(reader, "Status").data
            action = sqexdata.ExhHeader.read_data_from(reader, "Action").data
            actiontransient = sqexdata.ExhHeader.read_data_from(reader, "ActionTransient").data
            status = {k: (
                sqexdata.SqEscapedString(v[0]).str(), sqexdata.SqEscapedString(v[1]).str()
            ) for k, v in status[2].items()}
            action = {k: sqexdata.SqEscapedString(v[0]).str() for k, v in action[2].items()}
            actiontransient = {k: sqexdata.SqEscapedString(v[0]).str() for k, v in actiontransient[2].items()}
        with open(cached_file, "wb") as fp:
            pickle.dump((status, action, actiontransient), fp)

    parser = DpsParser()

    p = r"C:\Users\SP\AppData\Roaming\XivAlexander\Dump\Dump_20211023_111519_579_238.log"
    with open(p) as fp:
        for line in fp:
            ts = datetime.datetime(int(line[0:4], 10), int(line[5:7], 10), int(line[8:10], 10),
                                   int(line[11:13], 10), int(line[14:16], 10), int(line[17:19], 10),
                                   int(line[20:23], 10) * 1000)
            is_recv = line[24] == '>'
            data = bytearray.fromhex(line[25:])
            parser.feed(ts, is_recv, data)
    return 0


if __name__ == "__main__":
    exit(main())
